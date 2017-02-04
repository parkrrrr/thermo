#include <iostream>
#include <boost/interprocess/ipc/message_queue.hpp>

#include "structs.h"

// main
int main( int argc, char **argv )
{
    MessageQueue queue;
    queue.Send(Message::QUIT, 0, 0 );
    return 0;
}
