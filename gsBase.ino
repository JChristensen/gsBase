#include <utility/w5100.h>
#include <Ethernet.h>               //http://arduino.cc/en/Reference/Ethernet
#include <MCP980X.h>
#include <MCP79412RTC.h>            //http://github.com/JChristensen/MCP79412RTC
#include <MemoryFree.h>             //http://playground.arduino.cc/Code/AvailableMemory
#include <movingAvg.h>              //http://github.com/JChristensen/movingAvg
#include <NTP.h>
#include <SPI.h>                    //http://arduino.cc/en/Reference/SPI
#include <Streaming.h>              //http://arduiniana.org/libraries/streaming/
#include <Time.h>                   //http://www.arduino.cc/playground/Code/Time
#include <Timezone.h>               //https://github.com/JChristensen/Timezone
#include "GroveStreams.h"

//pin assignments
const uint8_t WIZ_RESET = 9;            //WIZnet module reset pin
const uint8_t DS_PIN = A1;              //DS18B20 temperature sensor
const uint8_t GRN_LED = A2;             //heartbeat LED
const uint8_t RED_LED = A3;             //waiting for server response

//network settings
uint8_t macIP[] = { 2, 0, 192, 168, 0, 201 };    //mac and ip addresses, ip is last four bytes
const IPAddress GATEWAY(192, 168, 0, 1);
const IPAddress SUBNET(255, 255, 255, 0);

//US Eastern Time Zone (New York, Detroit)
TimeChangeRule myDST = { "EDT", Second, Sun, Mar, 2, -240};    //Daylight time = UTC - 4 hours
TimeChangeRule mySTD = { "EST", First, Sun, Nov, 2, -300};     //Standard time = UTC - 5 hours
Timezone myTZ(myDST, mySTD);

EthernetClient client;
int txSec = 10;            //transmit data once per minute, on this second

char gsServer[] = "grovestreams.com";
char* PROGMEM gsOrgID = "66d58106-8c04-34d9-9e8c-17499a8942d7";
char* PROGMEM gsApiKey = "cbc8d222-6f25-3e26-9f6e-edfc3364d7fd";
char* PROGMEM gsCompID = "192.168.0.201";
char* PROGMEM gsCompName = "Test-2";

GroveStreams GS(gsServer, gsOrgID, gsApiKey, gsCompID, gsCompName, RED_LED);

//trap the MCUSR value after reset to determine the reset source
//and ensure the watchdog is reset. this code does not work with a bootloader.
uint8_t mcusr __attribute__ ((section (".noinit")));
void wdt_init(void) __attribute__((naked)) __attribute__((section(".init3")));

void wdt_init(void)
{
    mcusr = MCUSR;
    MCUSR = 0;        //must clear WDRF in MCUSR in order to clear WDE in WDTCSR
    wdt_reset();
    wdt_disable();
}

void setup(void)
{
    pinMode(WIZ_RESET, OUTPUT);
    pinMode(RED_LED, OUTPUT);
    Serial.begin(115200);
    Serial << endl << millis() << F(" MCU reset 0x0") << _HEX(mcusr);
    if (mcusr & _BV(WDRF))  Serial << F(" WDRF");
    if (mcusr & _BV(BORF))  Serial << F(" BORF");
    if (mcusr & _BV(EXTRF)) Serial << F(" EXTRF");
    if (mcusr & _BV(PORF))  Serial << F(" PORF");
    Serial << endl;
    delay(1);
    digitalWrite(WIZ_RESET, HIGH);
    Serial << millis() << F(" WIZnet module reset") << endl;
    delay(500);
    Ethernet.begin(rtcID + 2);    //DHCP
    Serial << millis() << F(" Ethernet started, IP=") << Ethernet.localIP() << endl;
    NTP.begin();
    GS.begin();
    mcp9802.begin();
    mcp9802.writeConfig(ADC_RES_12BITS);
}

enum STATE_t { INIT, RUN } STATE;

void loop(void)
{
    time_t utc;
    time_t local;
    static time_t utcLast;
    static time_t nextTransmit;          //time for next data transmission
    static time_t nextTimePrint;         //next time to print the local time to serial
    char buf[96];
    static int tF10;
    static uint8_t socketsAvailable;

    NTP.run();                           //run the NTP state machine
    hb.type(NTP.syncStatus == STATUS_RECD ? HB_LONG : HB_SHORT);
    hb.update();

    switch (STATE) {

    case INIT:
        //wait until we have a good time from the NTP server
        if (NTP.lastSyncType == TYPE_PRECISE) {
            nextTimePrint = nextMinute();
            nextTransmit = nextTimePrint + txSec;
            STATE = RUN;
        }
        break;

    case RUN:
        utc = now();
        if (utc != utcLast) {
            utcLast = utc;
            local = myTZ.toLocal(utc);
            ds.readSensor(second(utc));

            if (utc >= nextTransmit) {              //once-per-minute transmission window?
                nextTransmit += 60;
                sprintf(buf,"&1=%u&2=%lu&3=%lu&4=%lu&5=%u&6=%u&7=%u&8=%u&9=%i.%i&A=%u", GS.seq, GS.connTime, GS.respTime, GS.discTime, GS.success, GS.fail, GS.timeout, GS.freeMem, tF10/10, tF10%10, socketsAvailable);
                if ( !GS.send(buf) ) {
                    Serial << F("Post FAIL");
                    ++GS.fail;
                }
                else {
                    Serial << F("Post OK");
                    ++GS.success;
                }
                ++GS.seq;
                tF10 = ds.avgTF10;
                Serial << F(" seq=") << GS.seq << F(" connTime=") << GS.connTime << F(" respTime=") << GS.respTime << F(" discTime=") << GS.discTime << F(" success=") << GS.success;
                Serial << F(" fail=") << GS.fail << F(" timeout=") << GS.timeout << F(" Sock=") << socketsAvailable << F(" freeMem=") << GS.freeMem << F(" tempF=") << tF10 << endl;
            }

            if (utc >= nextTimePrint) {             //print time to Serial once per minute
                Serial << endl << millis() << F(" Local: ");
                printDateTime(local);
                nextTimePrint += 60;
                socketsAvailable = showSockStatus();
                Serial << millis() << ' ' << socketsAvailable << F(" Sockets available") << endl;
            }
        }
        break;
    }
}

byte socketStat[MAX_SOCK_NUM];

//status values
//class SnSR {
//public:
//  static const uint8_t CLOSED      = 0x00;
//  static const uint8_t INIT        = 0x13;
//  static const uint8_t LISTEN      = 0x14;
//  static const uint8_t SYNSENT     = 0x15;
//  static const uint8_t SYNRECV     = 0x16;
//  static const uint8_t ESTABLISHED = 0x17;
//  static const uint8_t FIN_WAIT    = 0x18;
//  static const uint8_t CLOSING     = 0x1A;
//  static const uint8_t TIME_WAIT   = 0x1B;
//  static const uint8_t CLOSE_WAIT  = 0x1C;
//  static const uint8_t LAST_ACK    = 0x1D;
//  static const uint8_t UDP         = 0x22;
//  static const uint8_t IPRAW       = 0x32;
//  static const uint8_t MACRAW      = 0x42;
//  static const uint8_t PPPOE       = 0x5F;
//};

uint8_t showSockStatus()
{
    uint8_t nAvailable = 0;
    
    for (int i = 0; i < MAX_SOCK_NUM; i++) {
        Serial.print(F("Socket#"));
        Serial.print(i);
        uint8_t s = W5100.readSnSR(i);
        socketStat[i] = s;
        if ( s == 0 ) ++nAvailable;
        Serial.print(F(":0x"));
        Serial.print(s,16);
        Serial.print(F(" "));
        Serial.print(W5100.readSnPORT(i));
        Serial.print(F(" D:"));
        uint8_t dip[4];
        W5100.readSnDIPR(i, dip);
        for (int j=0; j<4; j++) {
          Serial.print(dip[j],10);
          if (j<3) Serial.print(".");
        }
        Serial.print(F("("));
        Serial.print(W5100.readSnDPORT(i));
        Serial.println(F(")"));
      }
    return nAvailable;
}
