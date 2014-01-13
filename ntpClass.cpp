#include <avr/wdt.h>
#include "ntpClass.h"

EthernetUDP udp;
ntpClass NTP;

//ntpClass::ntpClass(void)
//{
//}

//initialize
void ntpClass::begin(void)
{
    int ret = dnsLookup(NTP_SERVER_NAME, ntpServerIP);
    if (ret == 1) {
        Serial << millis() << F(" NTP Server IP=") << ntpServerIP << endl;
    }
    else {
        Serial << millis() << F(" NTP DNS fail, ret=") << ret << endl;
        wdt_enable(WDTO_4S);
        while (1);
    }
    udp.begin(UDP_PORT);
    _timeout = T2_SYNC_INTERVAL * 1000UL;
}

enum ntpState_t { NTP_WAIT_SEND, NTP_WAIT_RECV, NTP_SET_PRECISE } NTP_STATE;

//run the NTP state machine, returns true after an NTP packet was received and the time was set.
boolean ntpClass::run(void)
{
    boolean ret = false;
    static uint8_t timeoutCount;
    
    utc = now();    
    recv();        //process incoming udp traffic (ntp packets)
    
    switch (NTP_STATE) {

    case NTP_WAIT_SEND:
        if (lastSyncType == TYPE_NONE || now() >= _nextSyncTime) {
            _ms0 = millis();
            xmit();
            NTP_STATE = NTP_WAIT_RECV;
        }
        break;

    case NTP_WAIT_RECV:
        if (syncStatus == STATUS_RECD) {
            timeoutCount = 0;
            if (lastSyncType == TYPE_APPROX) {
                _nextSyncTime = _n2 + T2_SYNC_INTERVAL;
                Serial << millis() << F(" NTP approx sync") << endl;
//                dumpBuffer("NTP Recv", _buf, NTP_BUF_SIZE);
                NTP_STATE = NTP_WAIT_SEND;
                ret = true;
            }
            else if (lastSyncType == TYPE_PRECISE) {
                NTP_STATE = NTP_SET_PRECISE;
            }
        }
        else if (millis() - _ms0 >= _timeout) {
            NTP_STATE = NTP_WAIT_SEND;
            Serial << millis() << F(" Timeout ") << _timeout << endl;
            if (++timeoutCount >= MAX_TIMEOUTS) {
                Serial << millis() << F(" Too many timeouts") << endl;
                wdt_enable(WDTO_4S);
                while (1);
            }
        }
        break;

    case NTP_SET_PRECISE:
        if (millis() - _delayStart >= _syncDelay) {
            setTime(_u3);
            utc = now();
            _nextSyncTime = calcNextSync(utc);
//            _nextSyncTime += SYNC_INTERVAL;
            ret = true;
            NTP_STATE = NTP_WAIT_SEND;
            Serial << millis() << F(" NTP precise sync: offset=") << _offset << F(" rtd=") << _rtd << endl;
//            dumpBuffer("NTP Recv", _buf, NTP_BUF_SIZE);
        }
        break;
    }
    return ret;
}

//send request for NTP time
void ntpClass::xmit(void)
{
    syncStatus = STATUS_WAITING;
    
    //sinc system time is only kept to 1-second precision by the time library, we cannot know the current
    //time more precisely than that. this code will set the clock back by some amount less than
    //one second. this in turn allows us to calculate offset to 1ms precision.
    _ms0 = millis();            //millis() value when we sent the sync request
    _u0 = utc = now();          //sets the clock back by some amount less than a second
    setTime(_u0);

    //initialize the request
    memset(_buf, 0, NTP_BUF_SIZE);       //set all bytes in the buffer to 0
    _buf[0] = 0b11100011;                //LI, Version, Mode
    _buf[1] = 0;                         //Stratum, or type of clock
    _buf[2] = 6;                         //Polling Interval
    _buf[3] = 0xEC;                      //Peer Clock Precision
    //eight bytes of zero for Root Delay & Root Dispersion
    _buf[12] = 49;                       //not sure what these four bytes are
    _buf[13] = 0x4E;
    _buf[14] = 49;
    _buf[15] = 52;
    copyBuf(_buf+40, ntpTime(_u0));     //transmit timestamp

    udp.beginPacket(ntpServerIP, NTP_SERVER_PORT);    //ask for the time
    udp.write(_buf, NTP_BUF_SIZE);
    udp.endPacket();
    Serial << millis() << F(" NTP request ");
    printDateTime(utc);
//    dumpBuffer("Sent", _buf, NTP_BUF_SIZE);	//show the buffer as sent
}

//process responses from the NTP server
void ntpClass::recv(void)
{
    unsigned long ms3, msDelta;
    unsigned long ntpT2;                //t2 from the ntp server
    long t0, t1, t2, t3;

    //check for response from the NTP server
    if ( udp.parsePacket() == NTP_BUF_SIZE ) {
        udp.read(_buf, NTP_BUF_SIZE);	//read packet into buffer
        //after the first packet received, use a longer timeout
        if (lastSyncType == TYPE_NONE) _timeout = SYNC_TIMEOUT * 1000UL;
        syncStatus = STATUS_RECD;
        ms3 = millis();		        		//determine t3: use millis() to compensate for only having 1 sec resolution with time_t
        msDelta = ms3 - _ms0;			
        ntpT2 = getBuf(_buf+40);       //get the t2 the ntp server sent
        if (_buf[44] & 0x80 != 0) {ntpT2++;}    //round up if 1/2 sec or more
        _n2 = unixTime(ntpT2);                          //convert it to unix time
        if ((_n2 > utc ? _n2 - utc : utc - _n2) > 4*3600) {      //how far off are we?
            //if more than 4 hours off, our algorithm can't properly compute an offset,
            //so we'll just set our clock to t2 from the ntp server, and schedule another sync soon.
            setTime(_n2);
            utc = now();
            lastSyncType = TYPE_APPROX;
        }
        else {
            t0 = ntpMS(0);        //origin timestamp
            t1 = ntpMS(1);        //receive timestamp
            t2 = ntpMS(2);        //transmit timestamp
            t3 = t0 + msDelta;
            _offset = ((t1 - t0) + (t2 - t3)) / 2;    //calculate how far off we are
            _rtd = (t3 - t0) - (t2 - t1);             //calculate round trip delay just for grins
            _u3 = _u0 + _offset / 1000;
            if (_offset >= 0) {
                _u3++;
                _syncDelay = 1000 - (_offset % 1000);
            }
            else {
                _syncDelay = -_offset;
            }
            _delayStart = millis();
            lastSyncType = TYPE_PRECISE;
        }
    }
}

//copies 4 bytes to the designated offset in the buffer
void ntpClass::copyBuf(byte *dest, unsigned long source)
{
    ul_byte_t s;
   
    s.ul = source;
    dest[0] = s.b[3];
    dest[1] = s.b[2];
    dest[2] = s.b[1];
    dest[3] = s.b[0];
}

//gets 4 bytes from the buffer starting at the designated offset
unsigned long ntpClass::getBuf(byte *source)
{
    ul_byte_t retValue;

    retValue.b[3] = source[0];
    retValue.b[2] = source[1];
    retValue.b[1] = source[2];
    retValue.b[0] = source[3];
    return retValue.ul;
}

//convert a time_t value to NTP time (integer portion only, 32 bits)
unsigned long ntpClass::ntpTime(time_t unixTime)
{
    return unixTime + SEVENTY_YEARS;
}

//convert an NTP time value (integer portion only, 32 bits) to unix time
time_t ntpClass::unixTime(unsigned long ntpTime)
{
    return ntpTime - SEVENTY_YEARS;
}

//from a 64-bit ntp timestamp, returns a long integer that consists of the least
//significant 15 bits of the integer portion plus the most significant 16 bits
//of the fractional portion. while this allows calculation of the offset to millisecond
//precision, it trades off the ability to calculate large offsets. since we only
//expect large offsets when initially synchronizing, the initial sync is done by
//using only the server's transmit timestamp (t2) as an approximation (usually within a second)
//and then scheduling another sync after a short time for a more precise result.
long ntpClass::ntpMS(int tIdx)
{
    int i;
    long retval;
    unsigned long intPart, fracPart;
    
    i = 24 + tIdx * 8;
    intPart = getBuf(_buf+i) & 0x00007FFF;
    retval = intPart * 1000;    //convert to mS
    fracPart = getBuf(_buf+i+4) & 0xFFFF0000;
    fracPart = fracPart >> 16;
    fracPart = fracPart * 1000;
    fracPart = fracPart >> 16;
    retval = retval + fracPart;
    return retval;
}

// Calculate next NTP sync time to be at a fixed minute and second.
// Normally this will be an hour from the last sync, except for
// the next sync after the startup sync.  Once calculated, if the
// next sync time is closer than 5 min, add an hour.
time_t ntpClass::calcNextSync(time_t currentTime)
{
    time_t tSync;
    tmElements_t tmSync;
    
    breakTime(currentTime, tmSync);
    tmSync.Minute = 57;
    tmSync.Second = 0;
    tSync = makeTime(tmSync);
    if (tSync < currentTime + 300) tSync += 3600;
    Serial << millis() << F(" Next Sync ");
    printDateTime(tSync);
    return tSync;
}

int ntpClass::dnsLookup(const char* hostname, IPAddress& addr)
{
    int ret = 0;
    DNSClient dns;

    dns.begin(Ethernet.dnsServerIP());
    ret = dns.getHostByName(hostname, addr);
    return ret;
}
