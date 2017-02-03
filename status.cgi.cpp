#include <iostream>

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>

#include <jsoncpp/json/json.h>
#include <cgicc/HTTPContentHeader.h>

#include "structs.h"

int main(int argc, char **argv) {
    boost::interprocess::shared_memory_object *sharedMemory;
    boost::interprocess::mapped_region *mappedRegion;
    Shared *shared;
    
    //   create shared mem
    sharedMemory = new boost::interprocess::shared_memory_object( boost::interprocess::open_only, "thermo_shared_memory", boost::interprocess::read_write );
    mappedRegion = new boost::interprocess::mapped_region( *sharedMemory, boost::interprocess::read_write);
    shared = static_cast<Shared *>(mappedRegion->get_address());

    cgicc::HTTPContentHeader header("application/json");
    header.render(std::cout);

    Json::Value root;

    // root["name"] = Json::Value(programName);
    root["pv"] = Json::Value(shared->pv);
    root["sv"] = Json::Value(shared->sv);
    root["segment"] = Json::Value(shared->stepID);

    Json::FastWriter writer;
    std::cout << writer.write(root);

    return 0;
}
