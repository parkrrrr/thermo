#include <iostream>

#include <jsoncpp/json/json.h>
#include <cgicc/HTTPContentHeader.h>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <SQLiteCpp/SQLiteCpp.h>

#include "structs.h"

std::string to_string( int sec ) {
    using namespace boost::posix_time;
    std::string result = to_simple_string( seconds(sec));
    if ( result == "00:00:00" ) result = "--:--:--";
    return result;
}

int main(int argc, char **argv) {
    SharedMemory sharedMemory;
    Shared *shared = sharedMemory;

    SQLite::Database database("thermo.db");

    int lastTime = 0;
    {
        SQLite::Statement query(database, "SELECT MAX(timet) FROM log");
        if ( query.executeStep()) {
            lastTime = query.getColumn(0);
        }
    }

    const char *programName = "Idle";
    if ( shared->firingID ) {
        SQLite::Statement nameQuery(database, 
            "SELECT programInfo.name FROM firingInfo INNER JOIN programInfo ON firingInfo.programID=programInfo.programID WHERE firingID=?");
        nameQuery.bind(1,shared->firingID);

        if ( nameQuery.executeStep()) {
            programName = nameQuery.getColumn(0);
        }
        else {
            programName = "Setpoint";
        }
    }
	
    cgicc::HTTPContentHeader header("application/json");
    header.render(std::cout);

    Json::Value root;

    root["name"] = Json::Value(programName);
    root["pv"] = Json::Value(shared->pv);
    root["sv"] = Json::Value(shared->sv);
    root["segment"] = Json::Value(shared->stepID);
    root["elapsedTime"] = Json::Value(to_string(shared->segTimeElapsed));
    root["plannedTime"] = Json::Value(to_string(shared->segTimePlanned));
    root["lastTime"] = Json::Value(lastTime);
    Json::FastWriter writer;
    std::cout << writer.write(root);

    return 0;
}
