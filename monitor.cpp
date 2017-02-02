#include <iostream>

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>

#include "structs.h"

class Monitor {
public:
	boost::asio::io_service ioService;
	boost::asio::deadline_timer timer;
	boost::interprocess::shared_memory_object *sharedMemory;
	boost::interprocess::mapped_region *mappedRegion;
	Shared *shared;
	
	void StartTimer( void ) {
			timer.expires_from_now(boost::posix_time::seconds(1));
			timer.async_wait( boost::bind(&Monitor::TimerHandler,this));		
	}

	// constructor
	Monitor( void ) :
	ioService(),
	timer(ioService)
	{
		//   create shared mem
		sharedMemory = new boost::interprocess::shared_memory_object( boost::interprocess::open_only, "thermo_shared_memory", boost::interprocess::read_write );
		mappedRegion = new boost::interprocess::mapped_region( *sharedMemory, boost::interprocess::read_write);
		shared = static_cast<Shared *>(mappedRegion->get_address());
	}
	
	// timer handler
	void TimerHandler( void ) {
	        std::cout << "  " << shared->pv << "  \r";
		std::flush(std::cout);	
		//   reset timer
		StartTimer();
	}

	// monitor thread
	void Run(void)
	{
		//   set timer
		StartTimer();	
		//   io_service::run
		ioService.run();
		
	}
};

// main
int main( int argc, char **argv )
{
	//   start asio thread
	Monitor instance;

	instance.Run();
}
