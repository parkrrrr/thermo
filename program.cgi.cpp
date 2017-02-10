#include <iostream>

#include <jsoncpp/json/json.h>
#include <cgicc/Cgicc.h>
#include <cgicc/HTTPContentHeader.h>
#include <SQLiteCpp/SQLiteCpp.h>

#include "structs.h"


int main(int argc, char **argv) {
    SQLite::Database database("thermo.db");

    cgicc::Cgicc cgi;
    const cgicc::CgiEnvironment &environment( cgi.getEnvironment());
    std::string queryString = environment.getQueryString();
    size_t eq = queryString.find('=');

    int programID = 0;
    if ( eq != std::string::npos ) {
        std::stringstream stream(queryString.substr(eq+1)); 
        stream >> programID; 
    }

    cgicc::HTTPContentHeader header("application/json");
    header.render(std::cout);

    Json::Value root;

    { 
        SQLite::Statement query( database, "SELECT name FROM ProgramInfo WHERE programID = ?");
        query.bind(1, programID );
        if ( query.executeStep()) {
            const char *name = query.getColumn(0);
            root["name"] = name;
        }
    }

    {
        SQLite::Statement query( database, "SELECT instruction, temperature, param FROM Programs WHERE programID=? ORDER BY step");
        query.bind(1, programID );
        while ( query.executeStep()) {
            const char *ins = query.getColumn(0);
            int temperature = query.getColumn(1);
            int param = query.getColumn(2);
            Json::Value step;
            step["instruction"] = ins;
            step["temperature"] = temperature; 
            step["param"] = param;
            root["steps"].append(step);
        }
    }

    Json::FastWriter writer;
    std::cout << writer.write(root);

    return 0;
}
