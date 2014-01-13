//heartbeat LED
#include "hbLED.h"

hbLED::hbLED(int pin, unsigned long msShort, unsigned long msLong) {
    _pin = pin;
    _msOn = _msShort = msShort;
    _msOff = _msLong = msLong;
    pinMode(_pin, OUTPUT);
}

void hbLED::update(void)
{
    unsigned long ms = millis();
    if (ms - _lastChange >= (_state ? _msOn : _msOff)) {
        _lastChange = ms;
        digitalWrite(_pin, _state = !_state);
    }
}

void hbLED::type(hbTypes_t type)
{
    switch (type) {
        case HB_FAST:
            _msOn = _msShort;
            _msOff = _msShort;
            break;
        case HB_SHORT:
            _msOn = _msShort;
            _msOff = _msLong;
            break;
        case HB_LONG:
            _msOn = _msLong;
            _msOff = _msShort;
            break;
    }
}

