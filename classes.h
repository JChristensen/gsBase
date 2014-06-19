#include <util/Atomic.h>
#include <Arduino.h>
#include <Time.h>
#include <Streaming.h>    //http://arduiniana.org/libraries/streaming/

void timeStamp(Print& p, time_t t);

volatile bool _pulse;             //ISR pulse flag
volatile int _count;              //ISR pulse count
const time_t WARMUP_TIME = 5;     //seconds to turn on the GC before the sample time to let it settle in a bit

ISR(INT0_vect)
{
    _pulse = true;
    ++_count;
}

class geiger
{
    public:
        void begin(int sampleInterval, uint8_t powerPin, time_t utc);
        bool run(int* count, time_t utc);
        bool pulse(void);
        int count(void);

    private:
        int _sampleInterval;
        time_t _nextSampleTime;
        bool _continuous;            //sample continuously
        uint8_t _powerPin;
};

//sample interval in minutes, current utc
void geiger::begin(int sampleInterval, uint8_t powerPin, time_t utc)
{
    EICRA |= _BV(ISC01);                 //INT0 on falling edge
    EIFR |= _BV(INTF0);                  //ensure interrupt flag is cleared (setting ISC bits can set it)
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        _count = 0;
        _pulse = false;
    }
    _powerPin = powerPin;
    digitalWrite(_powerPin, LOW);
    pinMode(_powerPin, OUTPUT);
    if (sampleInterval < 1 || sampleInterval > 60) sampleInterval = 10;
    _sampleInterval = 60 * sampleInterval;                              //convert to seconds
    _continuous = (_sampleInterval == 60);
}

enum gmStates_t { gmINIT, gmWAIT, gmWARMUP, gmCOLLECT, gmCONTINUOUS } gmState;

//populate the user count if a minute has elapsed
bool geiger::run(int* count, time_t utc)
{
    bool ret = false;

    switch (gmState) {
        case gmINIT:
        gmState = gmWAIT;
        timeStamp(Serial, utc);
        Serial << F("GM wait") << endl;
        _nextSampleTime = utc + _sampleInterval - utc % _sampleInterval;    //next "neat" time
        if (_nextSampleTime - utc < WARMUP_TIME) _nextSampleTime += _sampleInterval;    //make sure not too soon
        break;

        case gmWAIT:
        if (utc >= _nextSampleTime - WARMUP_TIME) {
            gmState = gmWARMUP;
            digitalWrite(_powerPin, HIGH);
            timeStamp(Serial, utc);
            Serial << F("GM warmup, power on") << endl;
        }
        break;

        case gmWARMUP:
        if (utc >= _nextSampleTime) {
            gmState = _continuous ? gmCONTINUOUS : gmCOLLECT;
            timeStamp(Serial, utc);
            Serial << F("GM collect") << endl;
            ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
                _count = 0;
                _pulse = false;
            }
            EIFR |= _BV(INTF0);                  //ensure interrupt flag is cleared
            EIMSK |= _BV(INT0);                  //enable the interrupt
        }
        break;

        case gmCOLLECT:
        if (utc >= _nextSampleTime + 60) {
            EIMSK &= ~_BV(INT0);                 //disable the interrupt
            ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
                *count = _count;
                _count = 0;
                _pulse = false;
            }
            _nextSampleTime += _sampleInterval;
            timeStamp(Serial, utc);
            Serial << F("GM wait") << endl;
            gmState = gmWAIT;
            digitalWrite(_powerPin, LOW);
            ret = true;
        }
        break;

        case gmCONTINUOUS:
        if (utc >= _nextSampleTime + 60) {
            ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
                *count = _count;
                _count = 0;
                _pulse = false;
            }
            _nextSampleTime += _sampleInterval;
            ret = true;
        }
        break;
    }
    return ret;
}

//return true if a pulse was detected since last call
bool geiger::pulse(void)
{
    bool p;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (p = _pulse) _pulse = false;
    }
    return p;
}

geiger GEIGER;

class oneShotLED
{
    public:
        void begin(uint8_t pin, unsigned long duration);
        void run(void);
        void on(void);

    private:
        uint8_t _pin;
        unsigned long _dur;
        unsigned long _msOn;
};

void oneShotLED::begin(uint8_t pin, unsigned long duration)
{
    _pin = pin;
    _dur = duration;
    pinMode(_pin, OUTPUT);
}

void oneShotLED::run(void)
{
    if (millis() - _msOn >= _dur) digitalWrite(_pin, LOW);
}

void oneShotLED::on(void)
{
    _msOn = millis();
    digitalWrite(_pin, HIGH);
}

