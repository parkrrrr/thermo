#include <iostream>

#include <jsoncpp/json/json.h>
#include <cgicc/Cgicc.h>
#include <cgicc/HTTPContentHeader.h>
#include <SQLiteCpp/SQLiteCpp.h>

#include "structs.h"

int main(int argc, char **argv) {
    SQLite::Database database("thermo.db");

    database.setBusyTimeout(1000);

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
        SQLite::Statement query( database, "UPDATE ProgramInfo SET deleted = 1 WHERE programID = ?");
        query.bind(1, programID );
        query.exec();
    }

    Json::FastWriter writer;
    std::cout << writer.write(root);

    return 0;
}
