#pragma once

#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>

/*
A Message is used to command the server to do something. 
*/

struct Message {
    static const int QUIT = 1;        // < shut down and exit
    static const int CANCEL = 2;      // < stop the current program, including temporary programs created by SET
    static const int SET = 3;         // < create and run a temporary program that goes AFAP to the temperature given by param1, then pauses, then goes AFAP to 0.
    static const int START = 4;       // < start the program whose ID is given by param 1 at the step given in param2.
    static const int PAUSE = 5;       // < pause the current program (inserts a PAUSE step at the current point in the current program)
    static const int RESUME = 6;      // < resume the current program (terminates a pause step, whether from a message or from the program)
    
    int messageID;       // < one of the message IDs from above
    int param1;          // < first message parameter (see above)
    int param2;          // < second message parameter (see above)
};

/*
This class encapsulates the various types of segments. It's basically the same as an enum in a namespace with a default, for now.
*/

class SegmentType {
public:
    static const int AFAP = 1;     // < As Fast As Possible: set the SV to the specified value and wait until it's reached
    static const int Hold = 2;     // < Hold: stay at the current SV (not, note, the SV in the Programs table - that's ignored) for a specified amount of time
    static const int Pause = 3;    // < Pause: stay at the current SV until a Resume or Cancel message is received
    static const int Ramp = 4;     // < Ramp: move the SV toward the specified temperature at the specified rate
    
    int value;     // < the value of this SegmentType instance
    
    SegmentType(void) {value = Pause;} 
    SegmentType &operator =(int newValue ) {value = newValue;return *this;}  
    operator int() {return value;}
};

/*
This is the structure of the shared memory segment, used by the server to communicate state to clients
*/

struct Shared {
    int sv;                  // < current setpoint value (changes when a ramp is running)
    int pv;                  // < current process value
    int segTimeElapsed;      // < time elapsed in the current segment
    int progTimeElapsed;     // < time elapsed before the current segment (DOES NOT include current segment time)
    int segTimePlanned;      // < time the current segment "should" take - zero if unknown
    SegmentType segmentType; // < type of the current segment
    int firingID;            // < ID of the currently running firing in the database (or 0 if no current firing)
    int stepID;              // < step in the current running firing in the Firings table; inserted pauses may cause this number to vary from those in Programs.
};

/*
This class encapsulates the shared memory functionality used by the server to communicate state to clients. 
*/

class SharedMemory {
    boost::interprocess::shared_memory_object *sharedMemory;
    boost::interprocess::mapped_region *mappedRegion;
    void *memory;
public:    
    SharedMemory( bool create = false ) {
        using namespace boost::interprocess;
        if ( create ) {
            // remove any existing object, then create a new one with room for 1000 bytes. (that's way more than sizeof(Shared) but smaller than a page)
            shared_memory_object::remove( "thermo_shared_memory");
            permissions perms;
            perms.set_unrestricted(); 
            sharedMemory = new shared_memory_object( create_only, "thermo_shared_memory", read_write, perms );
            sharedMemory->truncate(sizeof(Shared));
        }
        else {
            // open the shared memory if it exists. Probably crashes if it doesn't.
            sharedMemory = new shared_memory_object( open_only, "thermo_shared_memory", read_write );
        }
        mappedRegion = new mapped_region( *sharedMemory, read_write);
        memory = mappedRegion->get_address();
    }
    
    ~SharedMemory(void) {
        delete mappedRegion;
        delete sharedMemory;
    }
    
    operator Shared *(void) {return static_cast<Shared *>(memory);}
};
        
/* 
This class manages the message queue used by clients to send commands to the server
*/

class MessageQueue {
    boost::interprocess::message_queue *messageQueue;
public:
    MessageQueue( bool create = false ) 
    {
        using namespace boost::interprocess;
        if ( create ) {
            // remove any existing queue, then create a new one with room for 100 Messages.
            boost::interprocess::message_queue::remove("thermo_message_queue");
            permissions perms;
            perms.set_unrestricted(); 
            messageQueue = new message_queue( create_only, "thermo_message_queue", 100, sizeof(Message),perms);
        }
        else {
            // open the message queue if it exists. Probably crashes if it doesn't.
            messageQueue = new message_queue( open_only, "thermo_message_queue");
        }
    }
   
    ~MessageQueue(void ) {
        delete messageQueue;
    }
 
    // Send a message to the server with the given ID and params
    void Send( int messageID, int param1, int param2 ) {
        Message message;

        message.messageID = messageID;
        message.param1 = param1;
        message.param2 = param2;
 
        messageQueue->send( &message, sizeof(Message), 0);
    }
    
    // Receive a message from the queue, if there is one
    bool TryReceive( void *message, boost::interprocess::message_queue::size_type msgSize, boost::interprocess::message_queue::size_type &receivedSize, unsigned int priority) {
        return messageQueue->try_receive( message, msgSize, receivedSize, priority );
    }
};
   
