#ifndef _NTPCLASS_H
#define _NTPCLASS_H

#include <Arduino.h>
#include <Ethernet.h>               //http://arduino.cc/en/Reference/Ethernet
#include <EthernetUdp.h>
#include <SPI.h>                    //http://arduino.cc/en/Reference/SPI
#include <Time.h>                   //http://www.arduino.cc/playground/Code/Time
#include <Streaming.h>              //http://arduiniana.org/libraries/streaming/
#include <Dns.h>

//const IPAddress NTP_SERVER(132, 163, 4, 101);    //time-a.timefreq.bldrdoc.gov
//const IPAddress NTP_SERVER(24, 56, 178, 140);    //wwv.nist.gov
//const IPAddress NTP_SERVER(129, 6, 15, 30);    //time-c.nist.gov
const char NTP_SERVER_NAME[] = "time.nist.gov";
const int NTP_SERVER_PORT = 123;
const unsigned int UDP_PORT = 8888;
const int NTP_BUF_SIZE = 48;     //NTP time stamp is in the first 48 bytes of the message
const unsigned long SEVENTY_YEARS = 2208988800;  //offset between Unix time and NTP time, number of seconds between 1900 and 1970
const time_t SYNC_INTERVAL = 120;       //sync interval in seconds
const time_t SYNC_TIMEOUT = 60;         //seconds
const time_t T2_SYNC_INTERVAL = 10;     //seconds
const uint8_t MAX_TIMEOUTS = 3;        //if this many timeouts occur consecutively, reset the MCU

//a union between an unsigned long and a 4-byte array
union ul_byte_t {
    unsigned long ul;
    byte b[4];
};

enum syncType_t { TYPE_NONE, TYPE_APPROX, TYPE_PRECISE };
enum syncStatus_t { STATUS_NONE, STATUS_WAITING, STATUS_RECD };

class ntpClass {
    public:
//        ntpClass(void);
        void begin(void);
        boolean run(void);
        int dnsLookup(const char* hostname, IPAddress& addr);
        time_t utc;
        syncType_t lastSyncType;
        syncStatus_t syncStatus;
        
    private:
        IPAddress ntpServerIP;
        unsigned long _ms0;           //millis() value when last request sent
        unsigned long _timeout;       //how long to wait for an ntp response, milliseconds
        unsigned long _syncDelay;     //for SET_PRECISE
        unsigned long _delayStart;    //for SET_PRECISE
        time_t _n2;
        time_t _u0;
        time_t _u3;
        time_t _nextSyncTime;
        long _offset;                 //difference between our clock and the NTP packet, milliseconds
        long _rtd;                    //round trip delay, milliseconds
        uint8_t _buf[NTP_BUF_SIZE];   //buffer for transmitting and receiving NTP packets
        void xmit(void);
        void recv(void);
        void copyBuf(byte *dest, unsigned long source);
        unsigned long getBuf(byte *source);
        unsigned long ntpTime(time_t unixTime);
        time_t unixTime(unsigned long ntpTime);
        long ntpMS(int tIdx);
        time_t calcNextSync(time_t currentTime);
};

extern ntpClass NTP;

/* HELPER FUNCTIONS */
void printDateTime(time_t t);
void printTime(time_t t);
void printDate(time_t t);
void printI00(int val, char delim);
void dumpBuffer(char *tag, byte *buf, int nBytes);
time_t nextMinute();

#endif

