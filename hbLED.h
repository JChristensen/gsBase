//heartbeat LED
#ifndef _HBLED_H
#define _HBLED_H

# include <Arduino.h>

enum hbTypes_t {HB_FAST, HB_SHORT, HB_LONG};

class hbLED
{
    private:
        int _pin;
        boolean _state;
        unsigned long _lastChange;
        unsigned long _msOn;
        unsigned long _msOff;
        unsigned long _msShort;
        unsigned long _msLong;
    
    public:
        hbLED(int pin, unsigned long msShort, unsigned long msLong);
        void update(void);
        void type(hbTypes_t type);
};
#endif


