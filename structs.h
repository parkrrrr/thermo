#pragma once
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>

struct Message {
    static const int QUIT = 1;
    static const int CANCEL = 2;
    static const int SET = 3;
    static const int START = 4;
    static const int PAUSE = 5;
    static const int RESUME = 6;
    
    int messageID;
    int param1;
    int param2;
};

struct Shared {
    int sv;    
    int pv;
    int segTimeElapsed;
    int progTimeElapsed;
    int firingID;
    int stepID;    
};

class SegmentType {
public:
    static const int AFAP = 1;
    static const int Hold = 2;
    static const int Pause = 3;
    static const int Ramp = 4;
    
    int value; 
    
    SegmentType(void) {value = Pause;}
    SegmentType &operator =(int newValue ) {value = newValue;return *this;}
    operator int() {return value;}
};

class SharedMemory {
    boost::interprocess::shared_memory_object *sharedMemory;
    boost::interprocess::mapped_region *mappedRegion;
    void *memory;
public:    
    SharedMemory( bool create = false ) {
        using boost::interprocess;
        sharedMemory = new shared_memory_object( create ? create_only : open_only, "thermo_shared_memory", read_write );
        if ( create ) sharedMemory->truncate(1000);
        mappedRegion = new mapped_region( *sharedMemory, read_write);
        memory = mappedRegion->get_address();
    }
    
    ~SharedMemory(void) {
        memory = nullptr;
        delete mappedRegion;
        delete sharedMemory;
    }
    
    void *operator void *(void) {return memory;}
};
        
class MessageQueue {
    boost::interprocess::message_queue messageQueue;
public:
    MessageQueue( bool create = false ) 
        : messageQueue( create ? boost::interprocess::create_only : boost::interprocess::open_only, "thermo_message_queue", 100, sizeof(Message))
    {
    }
    
    void Send( messageID, param1, param2 ) {
        Message message;

        message.messageID = messageID;
        message.param1 = param1;
        message.param2 = param2;
 
        messageQueue.send( &message, sizeof(Message), 0);
    }
};
   