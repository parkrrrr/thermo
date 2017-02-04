#include <iostream>

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>

#include "structs.h"

class Monitor {
public:
    boost::asio::io_service ioService;
    boost::asio::deadline_timer timer;
    SharedMemory sharedMemory;
    Shared *shared;
    
    void StartTimer( void ) {
            timer.expires_from_now(boost::posix_time::seconds(1));
            timer.async_wait( boost::bind(&Monitor::TimerHandler,this));        
    }

    // timer handler
    void TimerHandler( void ) {
        std::cout << "  " << shared->pv << "  \r";
        std::flush(std::cout);    
        StartTimer();
    }

    // constructor
    Monitor( void ) :
    ioService(),
    timer(ioService)
    {
        shared = static_cast<Shared *>(sharedMemory);
    }
    
    // monitor processing 
    void Run(void)
    {
        StartTimer();    
        ioService.run();
    }
};

// main
int main( int argc, char **argv )
{
    Monitor instance;
    instance.Run();
}
