#include <iostream>
#include <boost/interprocess/ipc/message_queue.hpp>

#include "structs.h"

// main
int main( int argc, char **argv )
{
    if ( argc < 2 ) {
        std::cout << "Error: must provide program ID" << std::endl;
        return 1;
    }

    MessageQueue queue;
    
    queue.Send( Message::START, atoi(argv[1]), 1 );
    return 0;
    }
