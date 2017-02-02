#include <iostream>

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/date_time/posix_time/conversion.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <sqlite3.h>
#include <SQLiteCpp/SQLiteCpp.h>

#include "structs.h"

static const int firingInterval = 5;
static const int nonfiringInterval = 60;

// to_time_t isn't defined until boost 1.58. Raspbian Jessie is still on 1.55.
// code from https://stackoverflow.com/a/4462309
time_t to_time_t(boost::posix_time::ptime t)
{
    using namespace boost::posix_time;
    ptime epoch(boost::gregorian::date(1970,1,1));
    time_duration::sec_type x = (t - epoch).total_seconds();
    return time_t(x);
}

class Logger {
public:
    boost::asio::io_service ioService;
    boost::asio::deadline_timer timer;
    boost::interprocess::shared_memory_object *sharedMemory;
    boost::interprocess::mapped_region *mappedRegion;
    Shared *shared;
    SQLite::Database database;

    int ticks;
 
    void StartTimer( void ) {
            timer.expires_from_now(boost::posix_time::seconds(1));
            timer.async_wait( boost::bind(&Logger::TimerHandler,this));        
    }

    // timer handler
    void TimerHandler( void ) {
        ++ticks;
        bool write = false;
        if ( shared->firingID ) {
            if ( ticks % firingInterval == 0 ) {
                write = true;
            }
        }
        else {
            if ( ticks % nonfiringInterval == 0 ) {
                write = true;
            }
        }
        if ( write ) {
            SQLite::Statement statement( database, "INSERT INTO TemperatureLog VALUES(?,?,?)");
            statement.bind(1,static_cast<int>(to_time_t(boost::posix_time::second_clock::universal_time())));
            statement.bind(2,shared->firingID);
            statement.bind(3,shared->pv);
            statement.exec(); 
        } 
        std::cout << "  " << shared->pv << (write?" write":"      ") << "  \r";
        std::flush(std::cout);    
        StartTimer();
    }

    // constructor
    Logger( void ) :
    ioService(),
    database("thermo.db",SQLITE_OPEN_READWRITE),
    timer(ioService)
    {
        //   create shared mem
        sharedMemory = new boost::interprocess::shared_memory_object( boost::interprocess::open_only, "thermo_shared_memory", boost::interprocess::read_write );
        mappedRegion = new boost::interprocess::mapped_region( *sharedMemory, boost::interprocess::read_write);
        shared = static_cast<Shared *>(mappedRegion->get_address());
        ticks = 0;
    }
    
    // logger processing 
    void Run(void)
    {
        StartTimer();    
        ioService.run();
    }
};

// main
int main( int argc, char **argv )
{
    Logger instance;
    instance.Run();
}
