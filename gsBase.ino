#include <utility/w5100.h>
#include <util/atomic.h>
#include <DS3232RTC.h>              //http://github.com/JChristensen/DS3232RTC
#include <Ethernet.h>               //http://arduino.cc/en/Reference/Ethernet
#include <MCP980X.h>
//#include <MCP79412RTC.h>            //http://github.com/JChristensen/MCP79412RTC
#include <LiquidTWI.h>              //http://forums.adafruit.com/viewtopic.php?f=19&t=21586&p=113177
#include <MemoryFree.h>             //http://playground.arduino.cc/Code/AvailableMemory
#include <movingAvg.h>              //http://github.com/JChristensen/movingAvg
#include <NTP.h>
#include <SPI.h>                    //http://arduino.cc/en/Reference/SPI
#include <Streaming.h>              //http://arduiniana.org/libraries/streaming/
#include <Time.h>                   //http://www.arduino.cc/playground/Code/Time
#include <Timezone.h>               //https://github.com/JChristensen/Timezone
#include <Wire.h>                   //http://arduino.cc/en/Reference/Wire
#include "GroveStreams.h"

//pin assignments
const uint8_t INT2_PIN = 2;             //RTC interrupt
const uint8_t WIZ_RESET = 12;           //WIZnet module reset pin
const uint8_t HB_LED = 13;              //heartbeat LED
const uint8_t NTP_LED = 14;             //ntp time sync
const uint8_t WAIT_LED = 15;            //waiting for server response

//US Eastern Time Zone (New York, Detroit)
TimeChangeRule myDST = { "EDT", Second, Sun, Mar, 2, -240};    //Daylight time = UTC - 4 hours
TimeChangeRule mySTD = { "EST", First, Sun, Nov, 2, -300};     //Standard time = UTC - 5 hours
Timezone myTZ(myDST, mySTD);

EthernetClient client;
int txSec = 10;                         //transmit data once per minute, on this second
time_t utc, utcLast;

char gsServer[] = "grovestreams.com";
char* PROGMEM gsOrgID = "66d58106-8c04-34d9-9e8c-17499a8942d7";
char* PROGMEM gsApiKey = "cbc8d222-6f25-3e26-9f6e-edfc3364d7fd";
char* PROGMEM gsCompID = "192.168.0.201";
char* PROGMEM gsCompName = "Test-2";

GroveStreams GS(gsServer, gsOrgID, gsApiKey, gsCompID, gsCompName, WAIT_LED);
MCP980X mcp9802(0);
movingAvg avgTemp; 
LiquidTWI lcd(0); //i2c address 0 (0x20)

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
#if defined(MCP79412RTC_h)
    uint8_t rtcID[8];
#else
    uint8_t rtcID[8] = { 0xFF, 0xFF, 0x00, 0x1E, 0xC0, 0x98, 0x9C, 0x8D };
#endif

    pinMode(INT2_PIN, INPUT_PULLUP);
    pinMode(WIZ_RESET, OUTPUT);
    pinMode(HB_LED, OUTPUT);
    pinMode(NTP_LED, OUTPUT);
    pinMode(WAIT_LED, OUTPUT);
    Serial.begin(115200);
    Serial << endl << millis() << F(" MCU reset 0x0") << _HEX(mcusr);
    if (mcusr & _BV(WDRF))  Serial << F(" WDRF");
    if (mcusr & _BV(BORF))  Serial << F(" BORF");
    if (mcusr & _BV(EXTRF)) Serial << F(" EXTRF");
    if (mcusr & _BV(PORF))  Serial << F(" PORF");
    Serial << endl;
    delay(1);
    digitalWrite(WIZ_RESET, HIGH);
    mcp9802.begin();
    mcp9802.writeConfig(ADC_RES_12BITS);
    lcd.begin(16, 2);
    TWBR = 12;
    lcd.clear();
    lcd.setBacklight(HIGH);

    //set up RTC synchronization
    lcd << F("RTC SYNC");
    utc = RTC.get();                       //try to read the time from the RTC
    if ( utc == 0 ) {                      //couldn't read it, something wrong
        lcd << F(" FAIL");
        digitalWrite( WAIT_LED, HIGH);
        while (1);
    }
    RTC.set(utc);                          //start the rtc if not running
    RTC.squareWave(SQWAVE_1_HZ);
#if defined(MCP79412RTC_h)
    RTC.calibWrite( (int8_t)RTC.eepromRead(127) );
    RTC.idRead(rtcID);
#endif
    lcd.clear();
    lcd << F("MAC address:");
    lcd.setCursor(0, 1);
    for (int i=2; i<8; ++i) {
        if (rtcID[i] < 16) lcd << '0';
        lcd << _HEX( rtcID[i] );
    }
    delay(1000);                           //allow some time for the ethernet chip to boot up
    Ethernet.begin(rtcID + 2);             //DHCP
    Serial << millis() << F(" Ethernet started, IP=") << Ethernet.localIP() << endl;
    lcd.clear();
    lcd << F("Ethernet IP:");
    lcd.setCursor(0, 1);
    lcd << Ethernet.localIP();
    delay(1000);
    lcd.clear();
    NTP.begin();
    lcd << F("NTP Server IP:");
    lcd.setCursor(0, 1);
    lcd << NTP.serverIP;
    utc = RTC.get();
    NTP.setTime(utc);
    Serial << millis() << F(" System time set from RTC: ") << endl;
    printDateTime(utc);

    delay(1000);
    lcd.clear();
    GS.begin();
    lcd << F("GroveStreams IP:");
    lcd.setCursor(0, 1);
    lcd << GS.serverIP;
    delay(1000);
    lcd.clear();
}

enum STATE_t { INIT, RUN } STATE;

void loop(void)
{
    time_t local;
    static time_t nextTransmit;          //time for next data transmission
    static time_t nextTimePrint;         //next time to print the local time to serial
    char buf[96];
    static uint8_t socketsAvailable;
    static int tF10;
    static uint8_t utcS, utcM;

    bool ntpSync = NTP.run();            //run the NTP state machine
    digitalWrite(NTP_LED, NTP.syncStatus == STATUS_RECD);

    switch (STATE) {

    case INIT:
        //wait until we have a good time from the NTP server
        if (ntpSync && NTP.lastSyncType == TYPE_PRECISE) {
            nextTimePrint = nextMinute();
            nextTransmit = nextTimePrint + txSec;
            STATE = RUN;
            Serial << millis() << F(" ***Init complete, NTP Time ");
            printDateTime(NTP.now());
        }
        break;

    case RUN:
        utc = NTP.now();
        if (utc != utcLast) {                 //once-per-second processing 
            utcLast = utc;
            utcM = minute(utc);
            utcS = second(utc);
            local = myTZ.toLocal(utc);
            
            if ( second(utc) % 10 == 0 ) {    //read temperature every 10 sec
                tF10 = avgTemp.reading( mcp9802.readTempF10(AMBIENT) );
            }
            digitalWrite(HB_LED, !(utcS & 1));
            lcd.setCursor(0, 0);        //time & temp on first row
            printTime(lcd, local);
            lcd << tF10 / 10 << '.' << tF10 % 10 << '\xDF';
            lcd.setCursor(0, 1);        //date on second row
            printDayDate(lcd, local);

            if (utc >= nextTransmit) {        //time to send data?
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
                Serial << F(" seq=") << GS.seq << F(" connTime=") << GS.connTime << F(" respTime=") << GS.respTime << F(" discTime=") << GS.discTime << F(" success=") << GS.success;
                Serial << F(" fail=") << GS.fail << F(" timeout=") << GS.timeout << F(" Sock=") << socketsAvailable << F(" freeMem=") << GS.freeMem << F(" tempF=") << tF10/10 << '.' << tF10%10 << endl;
            }

            if (utc >= nextTimePrint) {             //print time to Serial once per minute
                Serial << endl << millis() << F(" Local: ");
                printDateTime(local);
                nextTimePrint += 60;
//                socketsAvailable = showSockStatus();
//                Serial << millis() << ' ' << socketsAvailable << F(" Sockets available") << endl;
                //renew the DHCP lease hourly
                if (utcM == 0 && utcS == 0) {
                    unsigned long msStart = millis();
                    uint8_t mStat = Ethernet.maintain();
                    unsigned long msEnd = millis();
                    Serial << msEnd << ' ' << F("Ethernet.maintain=") << mStat << ' ' << msEnd - msStart << endl;
                }
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

