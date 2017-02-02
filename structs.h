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

