//TO DO: Is timeStamp() a redundant function? -- yes, removed.

// geiger and oneShotLED classes

#include <util/atomic.h>
#include <Arduino.h>
#include <Time.h>
#include <Streaming.h>    //http://arduiniana.org/libraries/streaming/

extern const char* tzUTC;
extern gsXBee XB;

volatile bool _pulse;             //ISR pulse flag
volatile int _count;              //ISR pulse count

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
        bool pulse();
        void setInterval(int sampleInterval);

    private:
        bool _started;
        int _sampleInterval;
        time_t _nextSampleTime;
        bool _continuous;            //sample continuously
        uint8_t _powerPin;
};

enum gmStates_t { gmINIT, gmWAIT, gmWARMUP, gmCOLLECT, gmCONTINUOUS } gmState;

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
    setInterval(sampleInterval);
    _started = true;
}

void geiger::setInterval(int sampleInterval)
{
    if (sampleInterval < 1 || sampleInterval > 60) sampleInterval = 10;
    _sampleInterval = 60 * sampleInterval;                              //convert to seconds
    _continuous = (_sampleInterval == 60);
    gmState = gmINIT;
    Serial << F("G-M interval = ") << _sampleInterval << endl;
}

//populate the user count if a minute has elapsed
bool geiger::run(int* count, time_t utc)
{
    bool ret = false;

    if (_started) {
        switch (gmState) {
        case gmINIT:
            gmState = gmWAIT;
            digitalWrite(_powerPin, LOW);   //if interval is changed while collect is in progress, it gets aborted, so ensure the power is turned off here
            _nextSampleTime = utc + _sampleInterval - utc % _sampleInterval;    //next "neat" time
            if (_nextSampleTime - utc < XB.txWarmup) _nextSampleTime += _sampleInterval;    //make sure not too soon
            printDateTime(utc, tzUTC, false);
            Serial << F(" G-M wait, next sample: ");
            printDateTime(_nextSampleTime, tzUTC);
            break;

        case gmWAIT:
            if (utc >= _nextSampleTime - XB.txWarmup) {
                gmState = gmWARMUP;
                digitalWrite(_powerPin, HIGH);
                printDateTime(utc, tzUTC, false);
                Serial << F(" G-M warmup, power on\n");
            }
            break;

        case gmWARMUP:
            if (utc >= _nextSampleTime) {
                gmState = _continuous ? gmCONTINUOUS : gmCOLLECT;
                printDateTime(utc, tzUTC, false);
                Serial << F(" G-M collect\n");
                ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
                {
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
                printDateTime(utc, tzUTC, false);
                Serial << F(" G-M wait, next sample: ");
                printDateTime(_nextSampleTime, tzUTC);
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
                printDateTime(utc, tzUTC, false);
                Serial << F(" G-M continuous, next sample: ");
                printDateTime(_nextSampleTime, tzUTC);
                ret = true;
            }
            break;
        }
    }
    return ret;
}

//return true if a pulse was detected since last call
bool geiger::pulse()
{
    bool p;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if ( (p = _pulse) ) _pulse = false;
    }
    return p;
}

geiger GEIGER;

class oneShotLED
{
    public:
        void begin(uint8_t pin, unsigned long duration);
        void run();
        void on();

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

void oneShotLED::run()
{
    if (millis() - _msOn >= _dur) digitalWrite(_pin, LOW);
}

void oneShotLED::on()
{
    _msOn = millis();
    digitalWrite(_pin, HIGH);
}
