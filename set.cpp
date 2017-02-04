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
    MessageQueue queue;
    queue.Send(Message::SET, atol(argv[1]), 0 );
   
    return 0;
    }
