#include <iostream>

#include <jsoncpp/json/json.h>
#include <cgicc/HTTPContentHeader.h>

#include "structs.h"

int main(int argc, char **argv) {
    SharedMemory sharedMemory;
    Shared *shared = sharedMemory;

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
