#include <iostream>

#include <jsoncpp/json/json.h>
#include <cgicc/HTTPContentHeader.h>
#include <SQLiteCpp/SQLiteCpp.h>

#include "structs.h"

int main(int argc, char **argv) {
    SQLite::Database database("thermo.db");

    cgicc::HTTPContentHeader header("application/json");
    header.render(std::cout);

    Json::Value root;

    { 
        SQLite::Statement query( database, "SELECT ProgramID, Name FROM ProgramInfo WHERE deleted=0");
        while ( query.executeStep()) {
            Json::Value pair;
            int id = query.getColumn(0);
            pair["id"] = id;
            const char *name = query.getColumn(1);
            pair["name"] = name;

            root["programs"].append(pair);
        }
    }

    Json::FastWriter writer;
    std::cout << writer.write(root);

    return 0;
}
