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

    int command = 0;
    int param1 = 0;
    int param2 = 0;

    std::stringstream queryStream( queryString );
    do {
        std::string pair;
        std::getline(queryStream, pair, '&');
        std::stringstream pairStream( pair );
        std::string name;
        std::string value;
        std::getline(pairStream, name, '=');
        if ( name == "cmd" ) {
            pairStream >> command;
        }
        else if ( name == "p1" ) {
            pairStream >> param1;
        }
        else if ( name == "p2" ) {
            pairStream >> param2;
        }
    } while (!queryStream.eof());

    MessageQueue queue;
    
    queue.Send( command, param1, param2 );

    cgicc::HTTPContentHeader header("application/json");
    header.render(std::cout);

    Json::Value root;

    Json::FastWriter writer;
    std::cout << writer.write(root);

    return 0;
}
