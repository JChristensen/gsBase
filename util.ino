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

//calculate the next time where seconds = 0
time_t nextMinute()
{
    tmElements_t tm;

    breakTime(NTP.now(), tm);
    tm.Second = 0;
    return makeTime(tm) + 60;
}

//given an epoch time (or a time span), returns a character string
//to the caller's buffer in the form ddddd.hh:mm.
//max string length is 12 characters including the null terminator.
void timeSpan(char* buf, time_t span)
{
    sprintf( buf, "%lu.%02i:%02i", span / SECS_PER_DAY, hour(span), minute(span) );
}

