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

static const char *default_device = "/dev/ttyUSB0";
static const int default_baudrate = 2400; 
static const int default_pv_margin = 5; // degrees Fahrenheit either side of SV
static const int default_firingLogInterval = 5; // seconds between logs while firing
static const int default_idleLogInterval = 60; // seconds between logs while idle

static const char *commandNames[] = {"     ","AFAP ","Hold ","Pause","Ramp "};

// to_time_t isn't defined until boost 1.58. Raspbian Jessie is still on 1.55.
// code from https://stackoverflow.com/a/4462309
time_t to_time_t(boost::posix_time::ptime t)
{
    using namespace boost::posix_time;
    ptime epoch(boost::gregorian::date(1970,1,1));
    time_duration::sec_type x = (t - epoch).total_seconds();
    return time_t(x);
}

// convert to ISO-standard time string
std::string to_iso_time(boost::posix_time::ptime t) {
    boost::posix_time::time_facet *facet = new boost::posix_time::time_facet("%Y-%m-%d %H:%M:%S%F%Q");
    std::stringstream stream;
    stream.imbue(std::locale(stream.getloc(), facet));
    stream << t;
    return stream.str();
}

boost::posix_time::ptime Now( void ) {
    return boost::posix_time::second_clock::universal_time();
}
    
class Settings {
    SQLite::Database &database;
public:
    Settings( SQLite::Database &database_ ) : database(database_) {}    
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
    

class Segment {
public:
    SegmentType type;
    int targetSV;
    int rampTime;   

    boost::posix_time::ptime startTime; 
    int startTemp;

    
    Segment(void) {
        type = SegmentType::Pause;
        targetSV = 0;
        rampTime = 0;
        startTime = Now();
        startTemp = 0;
    }
};

typedef std::list<Segment> SegmentQueue;

class Processor {
    SQLite::Database database;
    Settings settings;
 
    boost::asio::io_service ioService;
    boost::asio::serial_port serialPort;
    boost::asio::streambuf readBuffer;
    boost::asio::deadline_timer timer;
    SharedMemory sharedMemory;
    MessageQueue messageQueue;
    Shared *shared;   
  
    bool abort;
    
    Segment segment;
    SegmentQueue segmentQueue;
    
    std::list<std::string> pendingCommands;
    bool commandSent;
    boost::posix_time::ptime lastSentCommand;

    int pv_margin;
    int firingLogInterval;
    int idleLogInterval;
    
public:

    int ElapsedTime( void ) {
        return (Now() - segment.startTime).total_seconds();
    }
    
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

    // send or queue a command
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

    void StartPendingRead( void ) {
        if ( !abort ) boost::asio::async_read_until( serialPort, readBuffer, "\r\n", boost::bind(&Processor::ReadHandler, this) );    
    }
    
    void StartTimer( void ) {
        if ( !abort ) {
            timer.expires_from_now(boost::posix_time::seconds(1));
            timer.async_wait( boost::bind(&Processor::TimerHandler,this));        
        }    
    }
  
    void ResetSegmentStart(void) {
        segment.startTime = Now();
        segment.startTemp = shared->pv;
    }
    
    void SetSV( int temp, bool nvram ) {
        std::stringstream stream;
        char command = nvram ? 'W' : 'P';
        stream << "*" << command << "011" << std::setw(5) << std::setfill('0') << std::hex << std::setiosflags(std::ios::uppercase) << temp << "\r\n";
        SendCommand( stream.str());  
        shared->sv = temp;    
    }
    
    // next segment
    void NextSegment(void) {
        WriteFiringRecord();
        shared->progTimeElapsed += shared->segTimeElapsed;
        shared->segTimeElapsed = 0;
        
        ResetSegmentStart();
        if ( !segmentQueue.empty()) {
            segment = segmentQueue.front();
            segmentQueue.pop_front();
        }
        else {
            CancelMessageHandler();            
        }
        if ( segment.type == SegmentType::AFAP ) {
            SetSV(segment.targetSV, false );
        }
        if ( segment.type == SegmentType::Pause && segment.targetSV ) {
            SetSV(segment.targetSV, false );
        }
        if (shared->firingID) {
            ++shared->stepID;
        } 
    }
    
    // check time and PV
    void CheckTimeAndPV(void) {        

        if ( segment.type == SegmentType::AFAP || segment.type == SegmentType::Ramp ) {
            // if target SV reached
            if ( labs(shared->pv - segment.targetSV) <= pv_margin ) {
                NextSegment();
            }            
        }

        if ( segment.type == SegmentType::Ramp ) {
            // if ramp SV changed
            int usedTime = ElapsedTime();
            int newSV = (segment.targetSV - segment.startTemp) * usedTime / segment.rampTime + segment.startTemp;
            if ( labs(newSV - shared->sv) > pv_margin ) {
                SetSV( newSV, false );
            }
        }

        if ( segment.type == SegmentType::Hold ) {
            // if hold time exceeded
            if ( ElapsedTime() >= segment.rampTime ) {
                NextSegment();
            }
        }
    }

    // message loop
    void MessageLoop(void) {
        // get msg
        Message message;
        boost::interprocess::message_queue::size_type receivedSize = 0;
        unsigned int priority = 0;
        
        while (messageQueue->TryReceive( &message, sizeof(Message), receivedSize, priority )) {
            if ( receivedSize == sizeof(Message)) {
                // post msg handler to asio queue
                switch ( message.messageID ) {
                    case Message::QUIT: {
                        abort = true;
                        break;
                    }                
                    case Message::CANCEL: {
                        ioService.post( boost::bind(&Processor::CancelMessageHandler, this));
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
    
    // write log
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

    // create a firing record for this firing
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
    // timer handler
    void TimerHandler( void ) {
        // Start read PV
        SendCommand("*V01\r\n");
        MessageLoop();
        StartTimer();
    }

    // read handler
    void ReadHandler( void ) {
        // get line
        std::istream inStream( &readBuffer );
        std::string line;
        std::getline(inStream, line);

        std::stringstream result(line);
        
        char command;
        result >> command;
        
        switch( command ) {
            case 'V': {
                // if V01 [PV]
                int reg;                
                int temperature;
                result >> reg >> temperature;
                if (reg == 1 ) {
                    // store in shared mem
                    shared->pv = temperature;
                    shared->segTimeElapsed = ElapsedTime();
                    WriteLog();
                }
                break;
            }
            default: {
                break;
            }
        }
        
        CheckTimeAndPV();    
        StartPendingRead();
        DequeueCommand();
    }

    // cancel msg handler
    void CancelMessageHandler( void ) {
        WriteFiringRecord();
        ResetSegmentStart();
        
        // erase remainder of program
        segmentQueue.clear();
        
        // reset firing information
        shared->firingID = shared->stepID = 0;
        shared->segTimeElapsed = shared->progTimeElapsed = 0;
 
        // set SV to 0 degrees
        std::stringstream stream;
        SetSV( 0, true );
        
        segment.type = SegmentType::Pause;
        segment.targetSV = 0;
        segment.rampTime = 0;
    }

    // start msg handler
    void StartMessageHandler( int programID, int segmentID ) {
        CancelMessageHandler();
       
        SQLite::Statement query( database, "SELECT instruction, temperature, param FROM Programs WHERE programID = ? AND step >= ? ORDER BY step");
        query.bind(1, programID); 
        query.bind(2, segmentID); 

        int previousTemp = shared->pv;
        
        while ( query.executeStep()) {
            const char *instruction = query.getColumn(0);
            int temp = query.getColumn(1);
            int param = query.getColumn(2);
            Segment newseg;
            newseg.targetSV = temp;
            newseg.rampTime = 0;

            switch (tolower(instruction[0])) { 
            case 'h': // hold - param is seconds to hold
                newseg.type = SegmentType::Hold;
                newseg.rampTime = param;
                break;
            case 'a': // afap
                newseg.type = SegmentType::AFAP;
                break;
            case 'p': // pause
                newseg.type = SegmentType::Pause;
                break;
            case 'r': // ramp - param is degrees/hour
                newseg.type = SegmentType::Ramp;
                newseg.rampTime = 3600 * labs(temp-previousTemp) / param;
            default:
                break; 
            }
            segmentQueue.push_back(newseg);
            previousTemp = temp;
        }
        CreateFiring(programID);
        NextSegment(); 
    }

    // pause msg handler
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

    // resume msg handler
    void ResumeMessageHandler( void ) {
        NextSegment();
    }

    // set msg handler
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
        memset(shared, 0, sizeof(Shared));
        
        shared->pv = -1;
        
        abort = false;
        commandSent = false;
 
        // set up serial port
        serialPort.set_option(boost::asio::serial_port_base::baud_rate(settings("baudrate",default_baudrate)));
        serialPort.set_option(boost::asio::serial_port_base::stop_bits());
        serialPort.set_option(boost::asio::serial_port_base::parity());
        serialPort.set_option(boost::asio::serial_port_base::character_size(8));
        serialPort.set_option(boost::asio::serial_port_base::flow_control());
   
        database.setBusyTimeout(1000); 
                              
        pv_margin = settings("pvmargin", default_pv_margin );
        firingLogInterval = settings("firinglog", default_firingLogInterval);
        idleLogInterval = settings("idlelog", default_idleLogInterval);                            
    }
    
    // asio processing
    void Run(void)
    {  
        StartPendingRead();
        SendCommand("*Z02\r\n");
        StartTimer();
        CancelMessageHandler(); 
	ioService.run();
	CancelMessageHandler();
    }    
};

// main
int main( int argc, char **argv )
{
    // start asio thread
    Processor threadInstance;
    threadInstance.Run();
}
