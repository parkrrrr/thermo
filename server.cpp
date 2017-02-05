#include <iostream>
#include <iomanip>
#include <sstream>
#include <list>

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <sqlite3.h>
#include <SQLiteCpp/SQLiteCpp.h>

#include "structs.h"

/* These are the default values for settings specified in the Settings table in the database */
static const char *default_device = "/dev/ttyUSB0";
static const int default_baudrate = 2400; 
static const int default_pv_margin = 5; // degrees either side of SV (used by Ramp and AFAP to determine whether SV has been reached, used by Ramp as temp step)
static const int default_firingLogInterval = 5; // seconds between logs while firing
static const int default_idleLogInterval = 60; // seconds between logs while idle

/* These are the names that are written into the Firings table in the database */
static const char *commandNames[] = {"     ","AFAP ","Hold ","Pause","Ramp "};

// Convert a boost ptime to a time_t
// to_time_t isn't defined until boost 1.58. Raspbian Jessie is still on 1.55.
// code from https://stackoverflow.com/a/4462309
time_t to_time_t(boost::posix_time::ptime t)
{
    using namespace boost::posix_time;
    ptime epoch(boost::gregorian::date(1970,1,1));
    time_duration::sec_type x = (t - epoch).total_seconds();
    return time_t(x);
}

// convert ptime to ISO-standard time string
std::string to_iso_time(boost::posix_time::ptime t) {
    boost::posix_time::time_facet *facet = new boost::posix_time::time_facet("%Y-%m-%d %H:%M:%S%F%Q");
    std::stringstream stream;
    stream.imbue(std::locale(stream.getloc(), facet));
    stream << t;
    return stream.str();
}

// Get a boost ptime that represents the current time in UTC.
boost::posix_time::ptime Now( void ) {
    return boost::posix_time::second_clock::universal_time();
}

/* This class is used to extract settings from the database */
class Settings {
    SQLite::Database &database;          // < reference to an open database
public:
    Settings( SQLite::Database &database_ ) : database(database_) {}    
    
    /* Extract a string-type setting with the specified name. If it doesn't exist, return defVal */
    std::string operator()( std::string name, const char *defVal ) {
        const char *result;
        SQLite::Statement query(database, "SELECT value FROM settings WHERE name=?");
        query.bind(1, name );
        if ( query.executeStep()) {
            result = query.getColumn(0);
        }
        else {
            result = defVal;
        }
        return result;
    }
    
    /* Extract an int-type setting with the specified name. If it doesn't exist or is empty, return defVal */
    int operator()( std::string name, const int defVal ) {
        int result;
        std::string str = operator()( name, "" );
        if ( str.length()) {
            std::stringstream stream(str);
            stream >> result;
        }
        else {
            result = defVal;
        }
        return result;
    }
};
    
/* This class represents a segment in the currently running program. Note that some members are only meaningful for the currently-executing segment. */

class Segment {
public:
    SegmentType type;     // < The type of the segment
    int targetSV;         // < The target SV for this segment (SV for AFAP, Pause, or Hold; ending SV for Ramp)
    int rampTime;         // < The duration of a ramp or a hold segment

    boost::posix_time::ptime startTime;   // < The wall clock time when this segment started (current segment only)
    int startTemp;                        // < The PV when this segment started (current segment only)

    
    Segment(void) { 
        // By default, a segment puts the kiln in a "safe" state: paused with an SV of 0 degrees
        type = SegmentType::Pause;
        targetSV = 0;
        rampTime = 0;
        startTime = Now();
        startTemp = 0;
    }
};

/* A list of segments is a program. This is a program. */
typedef std::list<Segment> SegmentQueue;

/* This class encapsulates all of the behavior of the program. 
   Why a class instead of global variables out the (ahem)? 
   Because we can force the initialization order of class members. Note in particular that the settings member takes a 
   reference to the database member, so it MUST be declared after database */

class Processor {
    SQLite::Database database;            // < The SQLite database that holds programs and settings and logs and so on
    Settings settings;                    // < Accessor class for database-defined settings
 
    boost::asio::io_service ioService;    // < Boost management class for serial and timer operations
    boost::asio::serial_port serialPort;  // < The serial port that talks to the controller
    boost::asio::streambuf readBuffer;    // < The buffer into which reads from that serial port happen
    boost::asio::deadline_timer timer;    // < The timer that forms the beating heart of the server: it sends periodic reads and determines whether state changes are needed
    SharedMemory sharedMemory;            // < The shared memory block that communicates state to all of the clients
    Shared *shared;                       // < A structure that points into the above memory
    MessageQueue messageQueue;            // < The message queue with which clients tell the server what to do
  
    bool abort;                           // < If true, timers are not set and reads are not sent. This causes ioService to run out of work and terminate the Run loop.
    
    Segment segment;                      // < The currently-executing program segment. When idle, this is a Pause segment with an SV of 0.
    SegmentQueue segmentQueue;            // < The remainder of the currently-executing program, if any.
    
    /* Commands to the controller are queued so it only gets and processes one at a time. The next one isn't sent until the current one is processed.
       NOTE: This means the controller MUST be set to echo commands! */
    std::list<std::string> pendingCommands;    // < Commands waiting to be sent to the controller
    bool commandSent;                          // < true if there is a command outstanding at the controller
    boost::posix_time::ptime lastSentCommand;  // < the time of the last sent command. Used to time out if the controller drops a command.

    int pv_margin;                        // < The PV margin: used by ramp and AFAP to determine when the segment is done, used by ramp to determine temp step
    int firingLogInterval;                // < The time in seconds between log entries while firing
    int idleLogInterval;                  // < The time in seconds between log entires while idle
    
public:

    // Return the time elapsed during the current segment, in seconds
    int ElapsedTime( void ) {
        return (Now() - segment.startTime).total_seconds();
    }
    
    // Send the next pending command, if any, to the controller
    void DequeueCommand( void ) {
        if ( pendingCommands.empty()) { 
            commandSent = false;
        }
        else {
            lastSentCommand = Now();
            commandSent = true;
            std::string command = pendingCommands.front();
            pendingCommands.pop_front(); 
            boost::asio::write( serialPort, boost::asio::buffer(command));            
        } 
    }

    // Queue a command, and send it (or a command ahead of it) if the previous command timed out or there is no previous command.
    void SendCommand( std::string command ) {
        bool send = false;
        if ( !commandSent ) {
            send = true;
        }
        pendingCommands.push_back( command );
        if ( !send && (Now() - lastSentCommand).total_seconds() > 2 ) {
            send = true;
        }
        if ( send ) {
            DequeueCommand();
        } 
    }

    // Start an async read. Because the timer is constantly writing "read status" commands, there'll always be something along sooner or later, so we 
    // almost always have an outstanding async read.
    void StartPendingRead( void ) {
        if ( !abort ) boost::asio::async_read_until( serialPort, readBuffer, "\r\n", boost::bind(&Processor::ReadHandler, this) );    
    }
    
    // Start the timer
    void StartTimer( void ) {
        if ( !abort ) {
            timer.expires_from_now(boost::posix_time::seconds(1));
            timer.async_wait( boost::bind(&Processor::TimerHandler,this));        
        }    
    }
  
    // Set the segment start values to the current values (UTC wall clock and temperature)
    void ResetSegmentStart(void) {
        segment.startTime = Now();
        segment.startTemp = shared->pv;
    }

    // Set the SV of the controller to the specified value.
    // Note: The nvram parameter determines whether the SV will survive a controller reset.
    //       This depends on an undocumented and unsupported feature of the Micromega CN77000 controller that
    //       allows a setpoint value to be "put" rather than "written" into register 1. Omega technical support
    //       denies that this should work, and disclaims any responsibility when it doesn't. I'm doing it anyway.
    void SetSV( int temp, bool nvram ) {
        std::stringstream stream;
        char command = nvram ? 'W' : 'P';
        stream << "*" << command << "011" << std::setw(5) << std::setfill('0') << std::hex << std::setiosflags(std::ios::uppercase) << temp << "\r\n";
        SendCommand( stream.str());  
        shared->sv = temp;    
    }
    
    // Go to the next segment in the current program, if any
    void NextSegment(void) {
        WriteFiringRecord();
        shared->progTimeElapsed += shared->segTimeElapsed;
        shared->segTimeElapsed = 0;
        
        if ( !segmentQueue.empty()) {
            segment = segmentQueue.front();
            segmentQueue.pop_front();
        }
        else {
            CancelMessageHandler(false);            
        }
               
        // AFAP and some Pause segments can change the target SV. Hold segments do not, and ramp segments don't until later.
        if ( segment.type == SegmentType::AFAP ) {
            SetSV(segment.targetSV, false );
        }
        if ( segment.type == SegmentType::Pause && segment.targetSV ) {
            SetSV(segment.targetSV, false );
        }
        if (shared->firingID) {
            ++shared->stepID;
        } 
        ResetSegmentStart();
    }
    
    // Check whether the current PV has reached its target, or whether the current segment has timed out or needs its SV adjusted.
    void CheckTimeAndPV(void) {        

        // An AFAP or Ramp segment ends if it reaches its target SV (plus or minus the PV margin)
        if ( segment.type == SegmentType::AFAP || segment.type == SegmentType::Ramp ) {
            if ( labs(shared->pv - segment.targetSV) <= pv_margin ) {
                NextSegment();
            }            
        }

        // A ramp segment updates the SV whenever the computed SV for this point in the ramp changes by more than the PV margin
        // The computed SV is based on the starting SV, starting time, and ramp slope.
        if ( segment.type == SegmentType::Ramp ) {
            // if ramp SV changed
            int usedTime = ElapsedTime();
            int newSV = (segment.targetSV - segment.startTemp) * usedTime / segment.rampTime + segment.startTemp;
            if ( labs(newSV - shared->sv) >= pv_margin ) {
                SetSV( newSV, false );
            }
        }

        // A Hold segment ends when the specified time is reached
        if ( segment.type == SegmentType::Hold ) {
            if ( ElapsedTime() >= segment.rampTime ) {
                NextSegment();
            }
        }
    }

    // Retrieve and repost messages from the queue until the queue is empty 
    void MessageLoop(void) {

        Message message;
        boost::interprocess::message_queue::size_type receivedSize = 0;
        unsigned int priority = 0;
        
        while (messageQueue.TryReceive( &message, sizeof(Message), receivedSize, priority )) {
            if ( receivedSize == sizeof(Message)) {
                // Post message handlers to the ASIO queue. This gets them handled eventually, when there's not other work to be done (such as completed timers or async reads)
                switch ( message.messageID ) {
                    case Message::QUIT: {
                        abort = true;
                        break;
                    }                
                    case Message::CANCEL: {
                        ioService.post( boost::bind(&Processor::CancelMessageHandler, this, true));
                        break;
                    }
                    case Message::SET: {
                        ioService.post( boost::bind(&Processor::SetMessageHandler, this, message.param1));
                        break;
                    }
                    case Message::START: {
                        ioService.post( boost::bind(&Processor::StartMessageHandler, this, message.param1, message.param2));
                        break;
                    }
                    case Message::PAUSE: {
                        ioService.post( boost::bind(&Processor::PauseMessageHandler, this));
                        break;
                    }
                    case Message::RESUME: {
                        ioService.post( boost::bind(&Processor::ResumeMessageHandler, this));
                        break;
                    }
                    default: 
                        break;
                }
            }    
        }        
    }
    
    // Write a log entry to the database if needed. Display current parameters on the terminal.
    void WriteLog( void ) {
        static int lastFiringTick=-1;
        static int lastIdleTick=-1; 

        std::cout << "  " << 
            commandNames[segment.type] << " | " <<
            "PV " << 
              shared->pv << " | " << 
            "SV " << 
              shared->sv << " / " << 
              segment.targetSV << " | " << 
            "Time " << 
              boost::posix_time::to_simple_string(boost::posix_time::seconds(shared->segTimeElapsed)) << " / " <<
              boost::posix_time::to_simple_string(boost::posix_time::seconds(segment.rampTime)) <<
"   " << to_iso_time(segment.startTime) << "  " << segment.startTemp << "  " <<
            "         \r";
        std::flush(std::cout);    

        if (shared->firingID) {
            lastIdleTick=-1;
            int firingTick = (shared->segTimeElapsed+shared->progTimeElapsed)/firingLogInterval;
            if ( firingTick == lastFiringTick ) return;
            lastFiringTick = firingTick;
        }
        else {
            lastFiringTick=-1;
            int idleTick = shared->segTimeElapsed / idleLogInterval;
            if ( idleTick == lastIdleTick ) return;
            lastIdleTick = idleTick;
        } 
        SQLite::Statement statement(database,
            "INSERT INTO Log VALUES (?,?,?,?,?,?,?)");
        statement.bind(1,to_iso_time(Now()));
        statement.bind(2,static_cast<int>(to_time_t(Now())));
        statement.bind(3,shared->firingID);
        statement.bind(4,shared->stepID);
        statement.bind(5,shared->pv);
        statement.bind(6,shared->progTimeElapsed+shared->segTimeElapsed);
        statement.bind(7,shared->segTimeElapsed);
        statement.exec(); 
    }

    // When we start a firing, create a firing ID for it and set its start time. If it's a stored program, update the program's stats to 
    // reflect this firing.
    void CreateFiring(int programID) {
        std::string isoTime( to_iso_time(Now()));
        SQLite::Statement statement(database, "INSERT INTO FiringInfo VALUES( NULL, ?, ?, NULL)");
        statement.bind(1, programID > 0 ? programID : NULL );
        statement.bind(2, isoTime );
        statement.exec();
                
        shared->firingID = database.getLastInsertRowid();         
        shared->stepID = 0;
        
        if ( programID ) {
            SQLite::Statement update( database, "UPDATE ProgramInfo SET lastExecTime=?, execCount=execCount+1 WHERE ProgramID = ?" );                                      
            update.bind(1, isoTime );
            update.bind(2, programID );
            update.exec();
        }
    }  

    // As each segment completes, we want to write the information about how that segment actually ran into the database, so that
    // we can replicate odd situations later if necessary. This includes inserted pauses, but does not currently include external 
    // events such as the kiln lid being opened during a firing. Those events will show up in the temperature log, though.
    void WriteFiringRecord( void ) {
        if ( shared->firingID && shared->stepID ) {
            SQLite::Statement statement(database,
               "INSERT INTO \"Firings\" VALUES (?,?,?,?,?,?,?)");
            statement.bind(1, shared->firingID);
            statement.bind(2, shared->stepID );
            statement.bind(3, commandNames[segment.type] );
            statement.bind(4, shared->pv );
            statement.bind(5, shared->segTimeElapsed);
            statement.bind(6, static_cast<int>(to_time_t(segment.startTime)));
            statement.bind(7, static_cast<int>(to_time_t(Now()))); 
            statement.exec();
        } 
    }
    
public:
    // The timer handler executes once per second (see timeout value in StartTimer, above)
    // It starts a read of the PV and moves any waiting messages from the IPC queue to the ASIO queue,
    // then starts another timer.
    void TimerHandler( void ) {
        // Start read PV
        SendCommand("*V01\r\n");
        MessageLoop();
        StartTimer();
    }

    // The read handler executes when a line is received from the controller
    void ReadHandler( void ) {
        // get line
        std::istream inStream( &readBuffer );
        std::string line;
        std::getline(inStream, line);

        std::stringstream result(line);
        
        char command;
        result >> command;
        
        switch( command ) {
            // V is the result sent from a PV query (*V01, sent by the timer handler)
            // The format is configured as V01 xxxxx where xxxxx is the current temperature.
            // Note that this is configurable in the controller, but the controller must be 
            // set this way at this time.
            case 'V': {
                // if V01 [PV]
                int reg;                
                int temperature;
                result >> reg >> temperature;
                if (reg == 1 ) {       // reg shouldn't be anything else but 1, because we never ask for any other value, but just in case...
                    // store in shared mem
                    shared->pv = temperature;
                    shared->segTimeElapsed = ElapsedTime();
                    WriteLog();
                }
                break;
            }
            // Other expected results are W01 or P01, from writes or puts, or Z02, from the initial reset. We only send 
            // one command at a time, so at the moment we just assume that if we got a response, it was to the command
            // we just sent. Don't configure the controller to send continuous status updates, eh?
            default: {
                break;
            }
        }
        
        // After handling the read, check whether we need to do anything with it, start looking for another reply, and send another command if there is one waiting
        CheckTimeAndPV();    
        StartPendingRead();
        DequeueCommand();
    }

    // Handle the CANCEL message. This is also called by other functionality within the server: START and SET cancel the current program, as does running off the end of the program
    // in NextSegment
    void CancelMessageHandler( bool write=true ) {
        if ( write ) {
            WriteFiringRecord();
        }
        
        // erase remainder of program
        segmentQueue.clear();
        
        // reset firing information
        shared->firingID = shared->stepID = 0;
        shared->segTimeElapsed = shared->progTimeElapsed = 0;
 
        // set SV to 0 degrees
        std::stringstream stream;
        SetSV( 0, true );    // Note that we set the SV in nvram. 0 degrees is a "safe" SV to survive reset, and we really want it to stick in case that Omega guy was right.
        
        segment.type = SegmentType::Pause;
        segment.targetSV = 0;
        segment.rampTime = 0;
        ResetSegmentStart();
    }

    // Handle the START message, starting the given program at the given step
    void StartMessageHandler( int programID, int segmentID ) {
        CancelMessageHandler();

        // read the part of the program starting at the given segment ID
        SQLite::Statement query( database, "SELECT instruction, temperature, param FROM Programs WHERE programID = ? AND step >= ? ORDER BY step");
        query.bind(1, programID); 
        query.bind(2, segmentID); 

        int previousTemp = shared->pv;
        
        while ( query.executeStep()) {
            const char *instruction = query.getColumn(0);
            int temp = query.getColumn(1);
            int param = query.getColumn(2);
            Segment newseg;
            newseg.rampTime = 0;

            switch (tolower(instruction[0])) { 
            case 'h': // hold - param is seconds to hold
                newseg.type = SegmentType::Hold;
                newseg.rampTime = param;
		temp = previousTemp;
                break;
            case 'a': // afap
                newseg.type = SegmentType::AFAP;
                break;
            case 'p': // pause
                newseg.type = SegmentType::Pause;
                temp = previousTemp;
                break;
            case 'r': // ramp - param is degrees/hour
                newseg.type = SegmentType::Ramp;
                newseg.rampTime = 3600 * labs(temp-previousTemp) / param;
            default:
                break; 
            }
            newseg.targetSV = temp;
            segmentQueue.push_back(newseg);
            previousTemp = temp;
        }
        // We're starting a firing, so record it in the database and start running the first segment in the queue.
        CreateFiring(programID);
        NextSegment(); 
    }

    // Handle the PAUSE message. This inserts a Pause segment in the current program, pushing a copy of the currently-running segment
    // back onto the front of the program so it can be resumed later. If the current segment is a ramp or a hold, it must be adjusted
    // to reflect just the part of the original ramp or hold that remains.
    void PauseMessageHandler( void ) {
        // can't pause a pause
        if ( segment.type == SegmentType::Pause ) {
            return;
        }

        Segment newseg(segment);
 
        // insert remainder of current segment after current segment
        if ( newseg.type == SegmentType::Ramp ) {
            // adjust ramp time by comparing SV to target SV and scaling
            newseg.rampTime = newseg.rampTime * (shared->sv - segment.startTemp) / (newseg.targetSV-segment.startTemp);
        }
        if ( newseg.type == SegmentType::Hold) {
            // adjust hold time by subtracting time held so far
            newseg.rampTime -= ElapsedTime();
        }
        segmentQueue.push_front(newseg);

        // change current segment to pause at current PV
        newseg.type = SegmentType::Pause;
        newseg.targetSV = shared->pv;
        newseg.rampTime = 0;
        segmentQueue.push_front(newseg);
        NextSegment();
    } 

    // Handle the RESUME message. This just skips to the next segment. Notably, it doesn't really care if the current segment is a Pause.
    void ResumeMessageHandler( void ) {
        NextSegment();
    }

    // Handle the SET message. This creates and runs an unnamed program that does AFAP <temperature>, PAUSE, AFAP 0
    void SetMessageHandler( int temperature ) {
        Segment newseg;

        // clear current program
        CancelMessageHandler( );        
        
        // AFAP to temp
        newseg.type = SegmentType::AFAP;
        newseg.targetSV = temperature;
        newseg.rampTime = 0;
        segmentQueue.push_back(newseg);
        
        // PAUSE
        newseg.type = SegmentType::Pause;
        segmentQueue.push_back(newseg);
        
        // AFAP to 0
        newseg.type = SegmentType::AFAP;
        newseg.targetSV = 0;
        segmentQueue.push_back(newseg);

        // run new program
        CreateFiring(-1);
        NextSegment();
    }

    // constructor
    Processor( void ) :
    database("thermo.db",SQLITE_OPEN_READWRITE),
    settings(database),
    ioService(),
    readBuffer(),
    timer(ioService),
    serialPort(ioService, settings("port",default_device)),
    sharedMemory(true),
    messageQueue(true),
    shared(sharedMemory)   
    {
        // initialize the shared memory data
        memset(shared, 0, sizeof(Shared));
        
        shared->pv = -1;
        
        // initialize the state of various flags
        abort = false;
        commandSent = false;
 
        // set up the serial port
        serialPort.set_option(boost::asio::serial_port_base::baud_rate(settings("baudrate",default_baudrate)));
        serialPort.set_option(boost::asio::serial_port_base::stop_bits());
        serialPort.set_option(boost::asio::serial_port_base::parity());
        serialPort.set_option(boost::asio::serial_port_base::character_size(8));
        serialPort.set_option(boost::asio::serial_port_base::flow_control());
   
        // If a write to the database fails due to a lock, try again until a second elapses
        database.setBusyTimeout(1000); 

        // Read server settings from the database
        pv_margin = settings("pvmargin", default_pv_margin );
        firingLogInterval = settings("firinglog", default_firingLogInterval);
        idleLogInterval = settings("idlelog", default_idleLogInterval);                            
    }
    
    // The main program loop
    void Run(void)
    {  
        // Reset the controller. (The reset will reply with Z02 when it completes; that's why we need a pending read.)
        StartPendingRead();
        SendCommand("*Z02\r\n");

        // Start the first timer
        StartTimer();
        
        // Terminate the current program. There won't be one, but this will set the rest of the state to idle
        CancelMessageHandler(); 
        
        // Loop, executing handlers until there are no more events to handle. There will always be pending timers, so this won't end until we
        // stop setting them.
    	ioService.run();
        
        // Terminate the current program again, just in case whoever asked us to quit didn't already do so.
	    CancelMessageHandler();
    }    
};

// main: create an instance of the processor and run it.
int main( int argc, char **argv )
{
    Processor instance;
    instance.Run();
    return 0;
}
