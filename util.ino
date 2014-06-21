//print date and time
void printDateTime(Print& p, time_t t)
{
    printDate(p, t);
    p << ' ';
    printTime(p, t);
}

//print time
void printTime(Print& p, time_t t)
{
    printI00(p, hour(t), ':');
    printI00(p, minute(t), ':');
    printI00(p, second(t), ' ');
}

//print date
void printDate(Print& p, time_t t)
{
    printI00(p, day(t), 0);
    p << monthShortStr(month(t)) << _DEC(year(t));
}

//print day & date
void printDayDate(Print &p, time_t t)
{
    p << dayShortStr(weekday(t)) << ' ';
    printI00(p, day(t), ' ');
    p << monthShortStr(month(t)) << ' ' << _DEC(year(t));
}

//Print an integer in "00" format (with leading zero),
//followed by a delimiter character.
//Input value assumed to be between 0 and 99.
void printI00(Print& p, int val, char delim)
{
    if (val < 10) p << '0';
    p << _DEC(val);
    if (delim > 0) p << delim;
    return;
}

void timeStamp(Print& p, time_t t)
{
    printTime(p, t);
    printI00(p, day(t), 0);
    Serial << monthShortStr(month(t)) << year(t) << ' ';
}

//get 6-byte MAC address from 24AA02E48 EEPROM (quick-and-dirty version)
void getMAC(uint8_t* mac)
{
    const int EEPROM_ADDR = 0x50;
    const uint8_t UID_ADDR = 0xFA;

    Wire.beginTransmission(EEPROM_ADDR);
    Wire.write(UID_ADDR);
    Wire.endTransmission();

    Wire.requestFrom(EEPROM_ADDR, 6);
    for (uint8_t i = 0; i < 6; i++) {
        *(mac + i) = Wire.read();
    }
}

//calculate the next time where seconds = 0
time_t nextMinute()
{
    tmElements_t tm;

    breakTime(NTP.now(), tm);
    tm.Second = 0;
    return makeTime(tm) + 60;
}