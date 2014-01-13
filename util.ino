//print date and time to Serial
void printDateTime(time_t t)
{
    printTime(t);
    printDate(t);
    Serial << endl;
}

//print time to Serial
void printTime(time_t t)
{
    printI00(hour(t), ':');
    printI00(minute(t), ':');
    printI00(second(t), ' ');
}

//print date to Serial
void printDate(time_t t)
{
    Serial << dayShortStr(weekday(t)) << ' ';
    printI00(day(t), ' ');
    Serial << monthShortStr(month(t)) << ' ' << _DEC(year(t));
}

//Print an integer in "00" format (with leading zero),
//followed by a delimiter character to Serial.
//Input value assumed to be between 0 and 99.
void printI00(int val, char delim)
{
    if (val < 10) Serial << '0';
    Serial << _DEC(val) << delim;
    return;
}

void dumpBuffer(char *tag, byte *buf, int nBytes)
{
    int nRows, ix;
    byte bufByte;

    nRows = nBytes / 8;
    if (nBytes % 8 != 0) nRows++;
    Serial << endl << tag;

    for (int r=0; r<nRows; r++) {
        Serial << endl << "0x";
        if (r * 8 < 16) Serial << '0';
        Serial << _HEX(r * 8);
        for (int b=0; b<8; b++) {
            ix = r * 8 + b;
            if (ix >= nBytes) break;
            Serial << ' ';
            bufByte = buf[ix];
            if (bufByte < 16) Serial << '0';
            Serial << _HEX(bufByte);
        }
    }
    Serial << endl;
}

//calculate the next time where seconds = 0
time_t nextMinute()
{
    tmElements_t tm;
    
    breakTime(now(), tm);
    tm.Second = 0;
    return makeTime(tm) + 60;   
}

