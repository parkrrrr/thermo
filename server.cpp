#include <iostream>
#include <sstream>
#include <list>

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>

#include "structs.h"

static const std::string device = "/dev/ttyUSB0";
static const int pv_margin = 5; // degrees Fahrenheit either side of SV

class Direction {
public:
	static const int Up = 1;
	static const int Down = -1;

	int value; 
	
	Direction(void) {value = Down;}
	Direction &operator =(int newValue ) {value = newValue;return *this;}
	operator int() {return value;}
};

class Segment {
public:
	SegmentType type;
	int targetSV;
	int rampTime;
	Direction direction;
	
	Segment(void) {
		type = SegmentType::Pause;
		targetSV = 0;
		rampTime = 0;
		direction = Direction::Down;
	}
};

typedef std::list<Segment> SegmentQueue;

class AsioThread {
public:
	boost::asio::io_service ioService;
	boost::asio::serial_port serialPort;
	boost::asio::streambuf readBuffer;
	boost::asio::deadline_timer timer;
	boost::interprocess::shared_memory_object *sharedMemory;
	boost::interprocess::mapped_region *mappedRegion;
	boost::interprocess::message_queue *messageQueue;
	Shared *shared;
	
	bool abort;
	
	Segment segment;
	SegmentQueue segmentQueue;
	
	boost::posix_time::ptime segmentStartTime; 

	// start pending read
	void StartPendingRead( void ) {
		if ( !abort ) boost::asio::async_read_until( serialPort, readBuffer, "\r\n", boost::bind(&AsioThread::ReadHandler, this) );	
	}
	
	void StartTimer( void ) {
		if ( !abort ) {
			timer.expires_from_now(boost::posix_time::seconds(1));
			timer.async_wait( boost::bind(&AsioThread::TimerHandler,this));		
		}	
	}

	// constructor
	AsioThread( void ) :
	ioService(),
	serialPort(ioService, device),
	readBuffer(),
	timer(ioService)
	{
		//   create shared mem
		boost::interprocess::shared_memory_object::remove( "thermo_shared_memory");
		sharedMemory = new boost::interprocess::shared_memory_object( boost::interprocess::create_only, "thermo_shared_memory", boost::interprocess::read_write );
		sharedMemory->truncate(1000);
		mappedRegion = new boost::interprocess::mapped_region( *sharedMemory, boost::interprocess::read_write);
		shared = static_cast<Shared *>(mappedRegion->get_address());
		memset(shared, 0, sizeof(Shared));
		
		//   create msg queue
		boost::interprocess::message_queue::remove("thermo_message_queue");
		messageQueue = new boost::interprocess::message_queue( boost::interprocess::create_only, "thermo_message_queue", 100, sizeof(Message));
		
		abort = false;
		
		SetMessageHandler( 0 );
	}
	
	void ResetSegmentTime(void) {
		segmentStartTime = boost::posix_time::second_clock::universal_time();
	}
	
	// next segment
	void NextSegment(void) {
		WriteFiringRecord();
		ResetSegmentTime();
// TODO
	//   move to next segment
	}
	
	// check time and PV
	void CheckTimeAndPV(void) {		
		//   if afap || ramp
		if ( segment.type == SegmentType::AFAP || segment.type == SegmentType::Ramp ) {
// TODO			
			//     if target PV reached
			NextSegment();	
		}
		//   if ramp
		if ( segment.type == SegmentType::Ramp ) {
// TODO			
			//     if current SV changed
			//       write *P011xxxxx [set SV] to serial
		}
		//   if hold
		if ( segment.type == SegmentType::Hold ) {
// TODO			
			//     if time exceeded
			NextSegment();
		}
	}

	// message loop
	void MessageLoop(void) {
		//     get msg
		Message message;
		boost::interprocess::message_queue::size_type receivedSize = 0;
		unsigned int priority = 0;
		
		while (messageQueue->try_receive( &message, sizeof(Message), receivedSize, priority )) {
			if ( receivedSize == sizeof(Message)) {
				//     post msg handler to asio queue
				switch ( message.messageID ) {
					case Message::QUIT: {
						abort = true;
						break;
					}				
					case Message::CANCEL: {
						ioService.post( boost::bind(&AsioThread::CancelMessageHandler, this));
						break;
					}
					case Message::SET: {
						ioService.post( boost::bind(&AsioThread::SetMessageHandler, this, message.param1));
						break;
					}
					case Message::START: {
						ioService.post( boost::bind(&AsioThread::StartMessageHandler, this, message.param1, message.param2));
						break;
					}
					case Message::PAUSE: {
						ioService.post( boost::bind(&AsioThread::PauseMessageHandler, this));
						break;
					}
					case Message::RESUME: {
						ioService.post( boost::bind(&AsioThread::ResumeMessageHandler, this));
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
		//   write *V01 [read PV] to serial
		boost::asio::write( serialPort, boost::asio::buffer("*V01\r\n"));
		
		// handle messages
		MessageLoop();
		
		//   reset timer
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
				//   if V01 [PV]
				int reg;				
				int temperature;
				result >> reg >> temperature;
				if (reg == 1 ) {
					//     store in shared mem
					shared->pv = temperature;
					std::cout << "  " << temperature << "  \r";
					std::flush(std::cout);	
				}
				break;
			}
			case 'W': {
				break;
			}
			default: {
				break;
			}
		}
		
		//   check time and PV
		CheckTimeAndPV();	
		
		//   start pending read
		StartPendingRead();
	}

	// asio thread
	void Run(void)
	{
		//   open serial port
		serialPort.set_option(boost::asio::serial_port_base::baud_rate(2400));
		serialPort.set_option(boost::asio::serial_port_base::stop_bits());
		serialPort.set_option(boost::asio::serial_port_base::parity());
		serialPort.set_option(boost::asio::serial_port_base::character_size(8));
		serialPort.set_option(boost::asio::serial_port_base::flow_control());
		
		//   start pending read
		StartPendingRead();
		
		//   set timer
		TimerHandler();
		
		//   io_service::run
		ioService.run();
		
		SetMessageHandler(0);
	}

	void WriteFiringRecord( void ) {
		// TODO:
	}
	
	// cancel msg handler
	void CancelMessageHandler( void ) {
		//   write firing record for current segment
		WriteFiringRecord();
		// TODO:
		//   reset segment time
		//   reset firing information
		//   set SV to 0 degrees
	}

	// start msg handler
	void StartMessageHandler( int programID, int segmentID ) {
		//   cancel msg handler
		CancelMessageHandler();
		
		// TODO:
		//   load program
		//   skip to specified segment
		//   check PV; can we skip more initial segments? [e.g. AFAP 1000, RAMP 1100 400dph, HOLD 2h, temp is currently 1100. Two segments can be skipped.]
		//   If current start segment is ramp, modify timeout as needed
	}

	// pause msg handler
	void PauseMessageHandler( void ) {
		// TODO:
		//   insert remainder of current segment after current segment
		//   insert hold-for-resume segment after current segment
		//   modify current segment to end immediately
	} 

	// resume msg handler
	void ResumeMessageHandler( void ) {
		//   next segment
		NextSegment();
	}

	// set msg handler
	void SetMessageHandler( int temperature ) {
		//   cancel msg handler
		CancelMessageHandler( );
		// TODO:
		//   set program to AFAP <temp>, PAUSE, AFAP 0
		//   run program
	}
};

// main
int main( int argc, char **argv )
{
	//   start asio thread
	AsioThread threadInstance;

	threadInstance.Run();
}
