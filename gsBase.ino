#include <Ethernet.h>               //http://arduino.cc/en/Reference/Ethernet
#include <EthernetUdp.h>
#include <MemoryFree.h>             //http://playground.arduino.cc/Code/AvailableMemory
#include <SPI.h>                    //http://arduino.cc/en/Reference/SPI
#include <Time.h>                   //http://www.arduino.cc/playground/Code/Time
#include <Streaming.h>              //http://arduiniana.org/libraries/streaming/
#include <Timezone.h>    //https://github.com/JChristensen/Timezone
#include <Button.h>        //https://github.com/JChristensen/Button
#include <ds18b20.h>                //http://github.com/JChristensen/ds18b20
#include <OneWire.h>                //http://www.pjrc.com/teensy/td_libs_OneWire.html
#include <movingAvg.h>              //http://github.com/JChristensen/movingAvg
//#include "SSSS_SPI.h"
#include "hbLED.h"
#include "ntpClass.h"
#include <Dns.h>
#include <avr/wdt.h>

//pin assignments
const uint8_t WIZ_RESET = 9;            //WIZnet module reset pin
const uint8_t DS_PIN = A1;              //DS18B20 temperature sensor
const uint8_t GRN_LED = A2;             //heartbeat LED
const uint8_t RED_LED = A3;             //waiting for server response
const uint8_t SSSSSS_PIN = 8;           //Sparkfun Serial Seven Segment Slave Select pin
const uint8_t BUTTON_PIN = 7;
const boolean PULLUP = true;
const boolean INVERT = true;
const unsigned long DEBOUNCE_MS = 25;

//network settings
uint8_t macIP[] = { 
    2, 0, 192, 168, 0, 201 };    //mac and ip addresses, ip is last four bytes
const IPAddress GATEWAY(192, 168, 0, 1);
const IPAddress SUBNET(255, 255, 255, 0);

//remote server
char* PROGMEM gsApiKey = "cbc8d222-6f25-3e26-9f6e-edfc3364d7fd";
char* PROGMEM gsOrg = "66d58106-8c04-34d9-9e8c-17499a8942d7";
const char serverName[] = "grovestreams.com";


//data to be posted
unsigned int seq;                   //post sequence number
unsigned long connTime;             //time to connect to server in milliseconds
unsigned long respTime;             //response time in milliseconds
unsigned long discTime;             //time to disconnect from server in milliseconds
unsigned int success;               //number of successful posts
unsigned int fail;                  //number of Ethernet connection failures
unsigned int timeout;               //number of Ethernet timeouts
unsigned int freeMem;               //bytes of free SRAM
int tF10;                           //temperature in °F times ten

//US Eastern Time Zone (New York, Detroit)
TimeChangeRule myDST = {
    "EDT", Second, Sun, Mar, 2, -240};    //Daylight time = UTC - 4 hours
TimeChangeRule mySTD = {
    "EST", First, Sun, Nov, 2, -300};     //Standard time = UTC - 5 hours
Timezone myTZ(myDST, mySTD);

hbLED hb(GRN_LED, 100, 900);
Button btnMode(BUTTON_PIN, PULLUP, INVERT, DEBOUNCE_MS);
EthernetClient client;
DS18B20 ds(DS_PIN);
int txSec = 10;

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
    //    SSSS.begin(SSSSSS_PIN);
    //    SSSS.reset();
    //    SSSS.decimals(0);
    //    SSSS.baudrate(BAUD_19200);
    //    SSSS.brightness(63 >> 4);
    Serial.begin(115200);
    Serial << endl << millis() << F(" MCU reset 0x0") << _HEX(mcusr);
    if (mcusr & _BV(WDRF))  Serial << F(" WDRF");
    if (mcusr & _BV(BORF))  Serial << F(" BORF");
    if (mcusr & _BV(EXTRF)) Serial << F(" EXTRF");
    if (mcusr & _BV(PORF))  Serial << F(" PORF");
    Serial << endl;
    digitalWrite(WIZ_RESET, LOW);
    delay(1);
    digitalWrite(WIZ_RESET, HIGH);
    Serial << millis() << F(" WIZnet module reset") << endl;
    delay(500);
    Ethernet.begin(macIP, macIP + 2, GATEWAY, GATEWAY, SUBNET);
    delay(1000);
    Serial << millis() << F(" Ethernet started, IP=") << Ethernet.localIP() << endl;
    NTP.begin();
    gsBegin();
}

enum STATE_t { 
    INIT, RUN } 
STATE;

void loop(void)
{
    time_t utc;
    time_t local;
    static time_t utcLast;
    static boolean dispSeconds;
    static time_t nextTransmit;          //time for next data transmission
    static time_t nextTimePrint;         //next time to print the local time to serial

    NTP.run();                           //run the NTP state machine
    //    btnMode.read();
    //    if (btnMode.wasReleased()) {
    //        utcLast -= 1;                        //force display update
    //        if (dispSeconds = !dispSeconds) SSSS.decimals(0x10);    //colon on for mm:ss display mode
    //    }

    digitalWrite(RED_LED, NTP.syncStatus == STATUS_RECD ? LOW : HIGH);
    hb.type(NTP.syncStatus == STATUS_RECD ? HB_LONG : HB_SHORT);
    hb.update();

    switch (STATE) {

    case INIT:
        //wait until we have a good time from the NTP server
        if (NTP.syncStatus == STATUS_RECD) {
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
                ++seq;
                tF10 = ds.avgTF10;
                if ( !postData() ) {
                    Serial << F("Post FAIL");
                    ++fail;
                }
                else {
                    Serial << F("Post OK");
                    ++success;
                }
                Serial << F(" seq=") << seq << F(" connTime=") << connTime << F(" respTime=") << respTime << F(" discTime=") << discTime << F(" success=") << success;
                Serial << F(" fail=") << fail << F(" timeout=") << timeout << F(" freeMem=") << freeMem << F(" tempF=") << tF10 << endl;
            }

            if (utc >= nextTimePrint) {             //print time to Serial once per minute
                Serial << endl << millis() << F(" Local: ");
                printDateTime(local);
                nextTimePrint += 60;
            }

            //            if (dispSeconds) {
            //                SSSS.dispInteger(minute(local) * 100 + second(local));
            //            }
            //            else {
            //                SSSS.dispInteger(hour(local) * 100 + minute(local));
            //                SSSS.decimals( utc & 1 ? 0x00 : 0x10 );
            //            }
        }
        break;
    }
}


