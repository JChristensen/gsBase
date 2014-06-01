//GroveStreams Class
#ifndef _GROVESTREAMS_H
#define _GROVESTREAMS_H

#include <avr/wdt.h>
#include <Arduino.h>
#include <Ethernet.h>               //http://arduino.cc/en/Reference/Ethernet
#include <NTP.h>
#include <MemoryFree.h>             //http://playground.arduino.cc/Code/AvailableMemory

const int serverPort = 80;
extern EthernetClient client;
const unsigned long RECEIVE_TIMEOUT = 10000;    //wait 10 sec for packet to be received from server

class GroveStreams
{
    public:
        GroveStreams(char* serverName, char* PROGMEM orgID, char* PROGMEM apiKey, char* PROGMEM compID, char* PROGMEM compName, int ledPin = -1);
        void begin(void);
        int send(char* data);
        int run(void);
        
        IPAddress serverIP;

        //data to be posted
        unsigned int seq;                   //post sequence number
        unsigned long connTime;             //time to connect to server in milliseconds
        unsigned long respTime;             //response time in milliseconds
        unsigned long discTime;             //time to disconnect from server in milliseconds
        unsigned int success;               //number of successful posts
        unsigned int fail;                  //number of Ethernet connection failures
        unsigned int timeout;               //number of Ethernet timeouts
        unsigned int freeMem;               //bytes of free SRAM
        
    private:
        int _xmit(void);
        char* _serverName;
        char* PROGMEM _orgID;
        char* PROGMEM _apiKey;
        char* PROGMEM _compID;        //component ID
        char* PROGMEM _compName;      //component name
 
        char* _data;
        unsigned long _msConnect;
        unsigned long _msConnected;
        unsigned long _msPutComplete;
        unsigned long _msLastPacket;
        unsigned long _msDisconnecting;
        unsigned long _msDisconnected;
        
        int _ledPin;

};

#endif


