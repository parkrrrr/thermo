#include <iostream>
#include <boost/interprocess/ipc/message_queue.hpp>

#include "structs.h"

// main
int main( int argc, char **argv )
{
    if ( argc < 2 ) {
        std::cout << "Error: must provide value" << std::endl;
        return 1;
    }

    boost::interprocess::message_queue messageQueue( boost::interprocess::open_only, "thermo_message_queue");
    
    Message message;
    
    message.messageID = Message::SET;
    message.param1 = atoi(argv[1]);
    
    messageQueue.send( &message, sizeof(Message), 0);
    
    return 0;
    }
