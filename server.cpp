#include <iostream>
#include <iomanip>
#include <sstream>
#include <list>

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <sqlite3.h>
#include <SQLiteCpp/SQLiteCpp.h>

#include "structs.h"

static const std::string device = "/dev/ttyUSB0";
static const int pv_margin = 5; // degrees Fahrenheit either side of SV

static const char *commandNames[] = {"     ","AFAP ","Hold ","Pause","Ramp "};

class Segment {
public:
    SegmentType type;
    int targetSV;
    int rampTime;
    
    Segment(void) {
        type = SegmentType::Pause;
        targetSV = 0;
        rampTime = 0;
    }
};

typedef std::list<Segment> SegmentQueue;

class Processor {
public:
    boost::asio::io_service ioService;
    boost::asio::serial_port serialPort;
    boost::asio::streambuf readBuffer;
    boost::asio::deadline_timer timer;
    boost::interprocess::shared_memory_object *sharedMemory;
    boost::interprocess::mapped_region *mappedRegion;
    boost::interprocess::message_queue *messageQueue;
    Shared *shared;
   
    SQLite::Database database;
 
    bool abort;
    
    Segment segment;
    SegmentQueue segmentQueue;
    
    boost::posix_time::ptime segmentStartTime; 
    int segmentStartTemp;

    std::list<std::string> pendingCommands;
    bool commandSent;
    boost::posix_time::ptime lastSentCommand;

    void DequeueCommand( void ) {
        if ( pendingCommands.empty()) { 
            commandSent = false;
        }
        else {
            lastSentCommand = boost::posix_time::second_clock::universal_time();
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
        boost::posix_time::ptime now = boost::posix_time::second_clock::universal_time();
        if ( !send && (now - lastSentCommand).total_seconds() > 2 ) {
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
        segmentStartTime = boost::posix_time::second_clock::universal_time();
        segmentStartTemp = shared->pv;
    }
    
    int ElapsedTime( void ) {
        return (boost::posix_time::second_clock::universal_time() - segmentStartTime).total_seconds();
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
            int newSV = (segment.targetSV - segmentStartTemp) * usedTime / segment.rampTime + segmentStartTemp;
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
        
        while (messageQueue->try_receive( &message, sizeof(Message), receivedSize, priority )) {
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
                    std::cout << "  " << 
                        commandNames[segment.type] << " | " <<
                        "PV " << shared->pv << " | " << 
                        "SV " << shared->sv << " / " << 
                                 segment.targetSV << " | " << 
                        "Time " << boost::posix_time::to_simple_string(boost::posix_time::seconds(shared->segTimeElapsed))
                                << " / " <<
                                   boost::posix_time::to_simple_string(boost::posix_time::seconds(segment.rampTime)) << 
                        "         \r";
                    std::flush(std::cout);    
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

    void WriteFiringRecord( void ) {
        if ( shared->firingID && shared->stepID ) {
            SQLite::Statement statement(database,
               "INSERT INTO \"Firings\" VALUES (?,?,?,?,?)");
            statement.bind(1, shared->firingID);
            statement.bind(2, shared->stepID );
            statement.bind(3, commandNames[segment.type] );
            statement.bind(4, shared->pv );
            statement.bind(5, shared->segTimeElapsed); 
            statement.exec();
        } 
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

    // create a firing record for this firing
    void CreateFiring(int programID) {
        boost::posix_time::ptime now = boost::posix_time::second_clock::universal_time();
        boost::posix_time::time_facet *facet = new boost::posix_time::time_facet("%Y-%m-%d %H:%M:%S%F%Q");
        std::stringstream stream;
        stream.imbue(std::locale(stream.getloc(), facet));
        stream << now;
 
        SQLite::Statement statement(database, 
            "INSERT INTO FiringInfo VALUES( NULL, ?, ?, NULL)");
        statement.bind(1, programID > 0 ? programID : NULL );
        statement.bind(2, stream.str());
        statement.exec();
        shared->firingID = database.getLastInsertRowid(); 
        shared->stepID = 0;
    } 

    // start msg handler
    void StartMessageHandler( int programID, int segmentID ) {
        CancelMessageHandler();
       
        SQLite::Statement query( database, "SELECT instruction, temperature, param FROM Programs WHERE programID = ? AND step >= ?");
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
            newseg.rampTime = newseg.rampTime * (shared->sv - segmentStartTemp) / (newseg.targetSV-segmentStartTemp);
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
    ioService(),
    serialPort(ioService, device),
    readBuffer(),
    timer(ioService),
    database("thermo.db",SQLITE_OPEN_READWRITE)

    {
        // create shared mem
        boost::interprocess::shared_memory_object::remove( "thermo_shared_memory");
        sharedMemory = new boost::interprocess::shared_memory_object( boost::interprocess::create_only, "thermo_shared_memory", boost::interprocess::read_write );
        sharedMemory->truncate(1000);
        mappedRegion = new boost::interprocess::mapped_region( *sharedMemory, boost::interprocess::read_write);
        shared = static_cast<Shared *>(mappedRegion->get_address());
        memset(shared, 0, sizeof(Shared));
        
        shared->pv = -1;
        
        // create msg queue
        boost::interprocess::message_queue::remove("thermo_message_queue");
        messageQueue = new boost::interprocess::message_queue( boost::interprocess::create_only, "thermo_message_queue", 100, sizeof(Message));
        
        abort = false;
        commandSent = false;
 
        // set up serial port
        serialPort.set_option(boost::asio::serial_port_base::baud_rate(2400));
        serialPort.set_option(boost::asio::serial_port_base::stop_bits());
        serialPort.set_option(boost::asio::serial_port_base::parity());
        serialPort.set_option(boost::asio::serial_port_base::character_size(8));
        serialPort.set_option(boost::asio::serial_port_base::flow_control());
   
        database.setBusyTimeout(1000); 
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
