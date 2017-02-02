#include <iostream>
#include <boost/interprocess/ipc/message_queue.hpp>

#include "structs.h"

// main
int main( int argc, char **argv )
{
    boost::interprocess::message_queue messageQueue( boost::interprocess::open_only, "thermo_message_queue");
    
    Message message;
    
    message.messageID = Message::RESUME;
    
    messageQueue.send( &message, sizeof(Message), 0);
    
    return 0;
    }
