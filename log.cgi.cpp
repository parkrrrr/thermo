#include <iostream>

#include <jsoncpp/json/json.h>
#include <cgicc/Cgicc.h>
#include <cgicc/HTTPContentHeader.h>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <SQLiteCpp/SQLiteCpp.h>

#include "structs.h"

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

// Get a boost ptime that represents the current time in UTC.
boost::posix_time::ptime Now( void ) {
    return boost::posix_time::second_clock::universal_time();
}

int main(int argc, char **argv) {
    SQLite::Database database("thermo.db");

    cgicc::Cgicc cgi;
    const cgicc::CgiEnvironment &environment( cgi.getEnvironment());
    std::string queryString = environment.getQueryString();
    size_t eq = queryString.find('=');

    int seconds = 0;
    if ( eq != std::string::npos ) {
        std::stringstream stream(queryString.substr(eq+1)); 
        stream >> seconds; 
    }

    long long endtime = to_time_t(Now());
    long long starttime = endtime - seconds;

    cgicc::HTTPContentHeader header("application/json");
    header.render(std::cout);

    Json::Value root;

    root["endTime"] = endtime;
    root["startTime"] = starttime;

    int firstLog = 0;
    {
        SQLite::Statement query( database, "SELECT MAX(TimeT) FROM log WHERE TimeT <= ?" ); 
        query.bind(1, starttime );
        if ( query.executeStep()) {
            firstLog = query.getColumn(0);
        }
    }

    {
        SQLite::Statement query( database, "SELECT TimeT, Temperature FROM log WHERE TimeT >= ?");
        query.bind(1, firstLog );
        while ( query.executeStep()) {
            int timet = query.getColumn(0);
            int temp = query.getColumn(1);
            Json::Value entry;
            entry.append(timet);
            entry.append(temp);
            root["temps"].append(entry);
        }
    } 

    {
        SQLite::Statement query( database, "SELECT starttimet, endtimet FROM firings WHERE starttimet > ? OR endtimet > ?");
        query.bind(1, starttime );
        query.bind(2, starttime );
        while ( query.executeStep()) {
            int start = query.getColumn(0);
            int end = query.getColumn(1);
            {
                std::stringstream stream;
                stream << start;
                root["segments"][stream.str()] = 1;
            }            
            {
                std::stringstream stream;
                stream << end;
                root["segments"][stream.str()] = 1;
            }            
        }
    } 
    Json::FastWriter writer;
    std::cout << writer.write(root);

    return 0;
}
