//TO DO:  Review transmission timing (nextTransmit) and G-M timing.
//        Add uptime stat -- DONE
//        XBee.begin() is never called. -- FIXED.
//        Diagnostic stats on/off as a parameter saved in EEPROM
//        Base data: uptime, seq, temp, cpm, messages
//        Diagnostic stats: success, fail, timeout, connTime, respTime, discTime, freeMem, RTC sets, sockets(?)
//
//        LCD stats: uptime, success, fail, timeout, memory, sockets(?)
//
//        FIXED
//        {
//        SOMEONE (mainline code or GS class) needs to count errors returned by GS.run() -- e.g. CONNECT_FAILED status is not being tracked anywhere.
//        Failures when calling GS.send() are tracked in gsBase.ino in GS.fail, not sure about this.
//        Probably the GS class needs to track these stats at least mostly.
//        }
//
//        Check return from Ethernet.begin() (int, 1=success, 0=fail)
//        Check return from Ethernet.maintain() (byte, 1 or 3 = fail, 0, 2, 4 = success)
//        Test Ethernet.begin() for failure
//
//        In the state machine, can NTP_INIT and GS_INIT be interchanged? This would prevent
//        a lot of fairly frequent NTP requests if GS is down.
//
//  xx    Store GroveStreams component ID in external EEPROM. xxDONExx
//
//  xx    Buffer http traffic to reduce number of packets sent. Current PUT text is ~250 chars.
//        Print RTC time on startup after reset
//        Check stats, i.e. are they for base station or remote(s) or both?
//        GroveStreams library: Should X-Forwarded-For give the component ID rather than the local IP,
//        to help avoid hitting the 10-sec posting limit?
//
//        Pullups on for unused pins.

//Set fuses: E:FD, H:D6, L:FF (preserve EEPROM thru chip erase)

#include <avr/eeprom.h>
#include <utility/w5100.h>
#include <Button.h>                 //http://github.com/JChristensen/Button
#include <DS3232RTC.h>              //http://github.com/JChristensen/DS3232RTC
#include <Ethernet.h>               //http://arduino.cc/en/Reference/Ethernet
#include <extEEPROM.h>              //http://github.com/JChristensen/extEEPROM
#include <GroveStreams.h>           //http://github.com/JChristensen/GroveStreams
#include <gsXBee.h>
#include <LiquidTWI.h>              //http://forums.adafruit.com/viewtopic.php?t=21586
//also                              //http://dl.dropboxusercontent.com/u/35284720/postfiles/LiquidTWI-1.5.1.zip
#include <MCP980X.h>                //http://github.com/JChristensen/MCP980X
#include <MemoryFree.h>             //http://playground.arduino.cc/Code/AvailableMemory
#include <movingAvg.h>              //http://github.com/JChristensen/movingAvg
#include <NTP.h>
#include <SPI.h>                    //http://arduino.cc/en/Reference/SPI
#include <Streaming.h>              //http://arduiniana.org/libraries/streaming/
#include <Time.h>                   //http://www.arduino.cc/playground/Code/Time
#include <Timezone.h>               //http://github.com/JChristensen/Timezone
#include <Wire.h>                   //http://arduino.cc/en/Reference/Wire
#include <XBee.h>                   //http://code.google.com/p/xbee-arduino/
#include "classes.h"                //part of this project
//#include "xbee.h"                   //part of this project

//#define COUNT_LOOPS                 //count RUN state loops/sec

//pin assignments
const uint8_t
    RTC_1HZ(2),                     //RTC 1Hz interrupt, INT2
    GM_INPUT(10),                   //G-M pulse interrupt, INT0
    WIZ_RESET(12),                  //WIZnet module reset
    HB_LED(13),                     //heartbeat LED
    NTP_LED(14),                    //ntp time sync
    WAIT_LED(15),                   //waiting for server response
    GM_PULSE_LED(18),               //blink on pulse from G-M counter
    SET_BUTTON(19),                 //set button
    UP_BUTTON(20),                  //up button
    DN_BUTTON(21),                  //down button
    GM_POWER(22);                   //geiger power enable

//global variables
time_t utc, local;                  //current times
time_t startupTime;                 //sketch start time
uint8_t gmIntervalIdx;              //index to geiger sample interval array
EEMEM uint8_t ee_gmIntervalIdx;     //copy persisted in EEPROM
const uint8_t gmIntervalIdx_DEFAULT(3);    //index to the default value (i.e. 3 -> 15 min)
const int gmIntervals[] = { 1, 5, 10, 15, 20, 30, 60 };
bool wdtEnable;                     //wdt enable flag
EEMEM uint8_t ee_wdtEnable;         //copy persisted in EEPROM
const uint32_t RESET_DELAY(60);     //seconds before resetting the MCU for initialization failures
const char* NTP_POOL = "pool.ntp.org";
//object instantiations
const char* gsServer = "grovestreams.com";
PROGMEM const char gsApiKey[] = "cbc8d222-6f25-3e26-9f6e-edfc3364d7fd";
GroveStreams GS(gsServer, (const __FlashStringHelper *) gsApiKey, WAIT_LED);
ntpClass NTP(NTP_LED);
MCP980X mcp9802(0);
movingAvg avgTemp;
LiquidTWI lcd(0); //i2c address 0 (0x20)
extEEPROM eep(kbits_2, 1, 8);
const unsigned long PULSE_DUR(50);  //blink duration for the G-M one-shot LED, ms
oneShotLED geigerLED;
gsXBee XB;

const bool PULLUP(true);
const bool INVERT(true);
const unsigned long DEBOUNCE_MS(25);
Button btnSet(SET_BUTTON, PULLUP, INVERT, DEBOUNCE_MS);
Button btnUp(UP_BUTTON, PULLUP, INVERT, DEBOUNCE_MS);
Button btnDn(DN_BUTTON, PULLUP, INVERT, DEBOUNCE_MS);

//Continental US Time Zones
TimeChangeRule EDT = { "EDT", Second, Sun, Mar, 2, -240 };    //Daylight time = UTC - 4 hours
TimeChangeRule EST = { "EST", First, Sun, Nov, 2, -300 };     //Standard time = UTC - 5 hours
Timezone Eastern(EDT, EST);
TimeChangeRule CDT = { "CDT", Second, Sun, Mar, 2, -300 };    //Daylight time = UTC - 5 hours
TimeChangeRule CST = { "CST", First, Sun, Nov, 2, -360 };     //Standard time = UTC - 6 hours
Timezone Central(CDT, CST);
TimeChangeRule MDT = { "MDT", Second, Sun, Mar, 2, -360 };    //Daylight time = UTC - 6 hours
TimeChangeRule MST = { "MST", First, Sun, Nov, 2, -420 };     //Standard time = UTC - 7 hours
Timezone Mountain(MDT, MST);
TimeChangeRule PDT = { "PDT", Second, Sun, Mar, 2, -420 };    //Daylight time = UTC - 7 hours
TimeChangeRule PST = { "PST", First, Sun, Nov, 2, -480 };     //Standard time = UTC - 8 hours
Timezone Pacific(PDT, PST);
TimeChangeRule utcRule = { "UTC", First, Sun, Nov, 2, 0 };    //No change for UTC
Timezone UTC(utcRule, utcRule);
Timezone* timezones[] = { &UTC, &Eastern, &Central, &Mountain, &Pacific };
const char* tzNames[] = { "UTC     ", "Eastern ", "Central ", "Mountain", "Pacific " };
Timezone* tz;                       //pointer to the time zone
uint8_t tzIndex;                    //index to the timezones[] array and the tzNames[] array
EEMEM uint8_t ee_tzIndex;           //copy persisted in EEPROM
TimeChangeRule* tcr;                //pointer to the time change rule, use to get TZ abbrev
extern const char* tzUTC;

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
    //pin inits
    pinMode(RTC_1HZ, INPUT_PULLUP);
    pinMode(WIZ_RESET, OUTPUT);
    pinMode(HB_LED, OUTPUT);
    pinMode(WAIT_LED, OUTPUT);
    pinMode(GM_INPUT, INPUT_PULLUP);

    //report the reset source
    Serial.begin(115200);
    Serial << F( "\n" __FILE__ " " __DATE__ " " __TIME__ "\n" );
    Serial << F("MCU reset 0x0") << _HEX(mcusr);
    if (mcusr & _BV(WDRF))  Serial << F(" WDRF");
    if (mcusr & _BV(BORF))  Serial << F(" BORF");
    if (mcusr & _BV(EXTRF)) Serial << F(" EXTRF");
    if (mcusr & _BV(PORF))  Serial << F(" PORF");
    Serial << endl;

    //XBee initialization
    Serial.flush();
    if ( !XB.begin(Serial) ) XB.mcuReset(RESET_DELAY * 1000UL);    //reset if XBee initialization fails
    XB.isTimeServer = true;

    //get geiger interval from eeprom and ensure that it's valid
    gmIntervalIdx = eeprom_read_byte( &ee_gmIntervalIdx );
    if ( gmIntervalIdx >= sizeof(gmIntervals) / sizeof(gmIntervals[0]) )
    {
        gmIntervalIdx = gmIntervalIdx_DEFAULT;
        eeprom_write_byte( &ee_gmIntervalIdx, gmIntervalIdx);
    }

    //same for the time zone index
    tzIndex = eeprom_read_byte( &ee_tzIndex );
    if ( tzIndex >= sizeof(tzNames) / sizeof(tzNames[0]) )
    {
        tzIndex = 0;
        eeprom_write_byte( &ee_tzIndex, tzIndex);
    }
    tz = timezones[tzIndex];                //set the tz

    //and wdt flag (debug)
    wdtEnable = eeprom_read_byte( &ee_wdtEnable );

    //device inits
    delay(1);
    digitalWrite(WIZ_RESET, HIGH);
    mcp9802.begin();
    lcd.begin(16, 2);
    eep.begin(twiClock400kHz);
    mcp9802.writeConfig(ADC_RES_12BITS);
    lcd.clear();
    lcd.setBacklight(HIGH);

    //RTC initialization
    lcd << F("RTC Sync");
    utc = RTC.get();                       //try to read the time from the RTC
    if ( utc == 0 )                        //couldn't read it, something wrong
    {
        lcd << F(" FAIL");
        digitalWrite( WAIT_LED, HIGH);
        while (1);
    }
    RTC.squareWave(SQWAVE_1_HZ);           //1Hz interrupts for timekeeping
    delay(1000);

    //get MAC address & display
    uint8_t mac[6];
    lcd.clear();
    lcd << F("MAC address");
    lcd.setCursor(0, 1);
    eep.read(0xFA, mac, 6);
    for (int i=0; i<6; ++i)
    {
        if (mac[i] < 16) lcd << '0';
        lcd << _HEX( mac[i] );
    }

    //start Ethernet, display IP
    if ( !Ethernet.begin(mac) )            //DHCP
    {
        Serial << millis() << F(" DHCP fail\n");
        Serial.flush();
        digitalWrite(WAIT_LED, HIGH);
        GS.mcuReset(RESET_DELAY * 1000UL);
    }
    Serial << millis() << F(" Ethernet started ") << Ethernet.localIP() << endl;
    lcd.clear();
    lcd << F("IP Address");
    lcd.setCursor(0, 1);
    lcd << Ethernet.localIP();
    delay(1000);

    //connect to GroveStreams, display IP
    GS.begin();
    lcd.clear();
    lcd << F("GroveStreams");
    lcd.setCursor(0, 1);
    lcd << GS.serverIP;
    delay(1000);

    //initialization state machine
    enum INIT_STATES_t { INIT_GS, INIT_WAIT_GS, INIT_WAIT_DISC, INIT_NTP, INIT_WAIT_NTP, INIT_COMPLETE };
    INIT_STATES_t INIT_STATE = INIT_GS;
    char buf[96];
    unsigned long ms;
    
    while (1)
    {
        ethernetStatus_t gsStatus = GS.run();   //run the GroveStreams state machine
        switch ( INIT_STATE)
        {
            //build reset message, send to GroveStreams
            case INIT_GS:
                strcpy(buf, "&msg=MCU%20reset%200x");
                if (mcusr < 16) strcat(buf, "0");
                itoa(mcusr, buf + strlen(buf), 16);
                if (mcusr & _BV(WDRF))  strcat(buf, "%20WDRF");
                if (mcusr & _BV(BORF))  strcat(buf, "%20BORF");
                if (mcusr & _BV(EXTRF)) strcat(buf, "%20EXTRF");
                if (mcusr & _BV(PORF))  strcat(buf, "%20PORF");
                ms = millis();
                GS.send(XB.compID, buf);
                INIT_STATE = INIT_WAIT_GS;
                break;

            case INIT_WAIT_GS:
                if (gsStatus == HTTP_OK)
                {
                    ms = millis();
                    INIT_STATE = INIT_WAIT_DISC;
                }
                else if ( millis() - ms >= 10000 )
                {
                    Serial << millis() << F(" GroveStreams send fail, resetting MCU\n");
                    mcuReset();
                }
                break;
                
            case INIT_WAIT_DISC:
                if (gsStatus == DISCONNECTED)
                {
                    Serial << millis() << F(" GS initialized\n");
                    INIT_STATE = INIT_NTP;
                }
            
                else if ( millis() - ms >= 10000 )
                {
                    Serial << millis() << F(" GroveStreams disc fail, resetting MCU\n");
                    mcuReset();
                }
                break;
        
            case INIT_NTP:
                //start NTP, display server IP
                NTP.begin(NTP_POOL);
                lcd.clear();
                lcd << F("NTP Server");
                lcd.setCursor(0, 1);
                lcd << NTP.serverIP;
                delay(1000);
                //set system time from RTC
                utc = RTC.get();
                while (utc == RTC.get()) delay(10);        //synchronize with the interrupts
//                utc = RTC.get();
//                while (utc == RTC.get()) delay(10);
                utc = RTC.get();
                NTP.setTime(utc);
                Serial << millis() << F(" RTC set the system time: ");
                printDateTime(utc, tzUTC);
                lcd.clear();
                lcd << F("NTP Sync...");
                geigerLED.begin(GM_PULSE_LED, PULSE_DUR);
                INIT_STATE = INIT_COMPLETE;
                break;
                
            case INIT_COMPLETE:
                return;
                break;
        }
    }
}

enum STATE_t { INIT, RUN, RESET_WARN, RESET_WAIT } STATE;

void loop(void)
{
#ifdef COUNT_LOOPS
    static uint16_t loopCount;
#endif
    static char buf[96];
    static time_t utcLast;
    static time_t nextTransmit;          //time for next data transmission
    static time_t nextTimePrint;         //next time to print the local time to serial
    static int tF10;
    static unsigned int rtcSet;
    static int cpm;
    static bool haveCPM = false;

    wdt_reset();
    utc = NTP.now();
    if (XB.read() == RX_DATA)
    {
        char rss[8];
        itoa(XB.rss, rss, 10);
        strcat(XB.payload, "&rss=");
        strcat(XB.payload, rss);
        if ( STATE == RUN )
        {
            if ( GS.send(XB.sendingCompID, XB.payload) == SEND_ACCEPTED )
            {
                Serial << F("\nSend OK\n");
            }
            else
            {
                Serial << F("Send FAIL\n");
            }
        }
        else
        {
            Serial << F("...ignored\n");
        }
    }
    btnSet.read();
    btnUp.read();
    btnDn.read();
    geigerLED.run();

    //check for data from the G-M counter
    if (GEIGER.run(&cpm, utc))
    {
        haveCPM = true;
        printDateTime(utc, tzUTC, false);
        Serial << F(" G-M counts/min ") << cpm << endl;
    }

    if ( GEIGER.pulse() ) geigerLED.on();    //blip the LED

    //note that setting the time *may* cause an interrupt, i.e. cause a falling edge on the SQW pin
    //depending on whether the SQW pin is high at the time the time is set. turning the square wave
    //off temporarily avoids this.
    ntpStatus_t ntpStatus = NTP.run();      //run the NTP state machine
    if (ntpStatus == NTP_SYNC && NTP.lastSyncType == TYPE_PRECISE)
    {
        utc = NTP.now();
        RTC.squareWave(SQWAVE_NONE);        //drives the INT/SQW pin high
        RTC.set(utc + 1);
        RTC.squareWave(SQWAVE_1_HZ);        //drives the INT/SQW pin low
        Serial << millis() << F(" NTP set the RTC: ");
        printDateTime(utc, tzUTC);
        ++rtcSet;
    }
    else if (ntpStatus == NTP_RESET)
    {
        STATE = RESET_WARN;
    }

    ethernetStatus_t gsStatus = GS.run();   //run the GroveStreams state machine
    runDisplay(tF10, cpm);                  //run the LCD display

    switch (STATE)
    {
    static unsigned long msSend;
        
    case INIT:
        //wait until we have a good time from the NTP server
        if ( (ntpStatus == NTP_SYNC && NTP.lastSyncType == TYPE_PRECISE) || NTP.lastSyncType == TYPE_SKIPPED )
        {
            nextTimePrint = nextMinute();
            nextTransmit = utc - utc % (XB.txInterval * 60) + XB.txOffset * 60 + XB.txSec;
            if ( nextTransmit <= utc ) nextTransmit += XB.txInterval * 60;
            startupTime = utc;
            Serial << millis() << F(" NTP init\n");
            GEIGER.begin(gmIntervals[gmIntervalIdx], GM_POWER, utc);
            if (wdtEnable) wdt_enable(WDTO_8S);
            Serial << millis() << F(" Watchdog Timer ") << (wdtEnable ? F("ON\n") : F("OFF\n"));
            STATE = RUN;
        }
        break;

    case RUN:
#ifdef COUNT_LOOPS
        ++loopCount;
#endif

        //process user button input
        //        if (btnMode.wasPressed()) {
        //            NTP.schedSync(5);
        //            Serial << millis() << F(" NTP Sync in 5 seconds\n");
        //        }

        if ( utc != utcLast )                 //once-per-second processing
        {
            utcLast = utc;
            XB.sendTimeSync(utc);
            uint8_t utcM = minute(utc);
            uint8_t utcS = second(utc);
#ifdef COUNT_LOOPS
            Serial << millis() << ' ';
            printTime(utc);
            Serial << F(" loopCount=") << loopCount << endl;
            loopCount = 0;
#endif

            if (utc >= nextTransmit)          //time to send data?
            {
                nextTransmit += XB.txInterval * 60;
                char upBuf[12];
                timeSpan(upBuf, utc - startupTime);    //uptime to send to GS
                sprintf(buf,"&u=%s&a=%u&c=%i.%i&j=%u&k=%u&l=%u&m=%lu&n=%lu&p=%lu&r=%u", upBuf, GS.sendSeq, tF10/10, tF10%10, GS.httpOK, GS.connFail, GS.recvTimeout, GS.connTime, GS.respTime, GS.discTime, rtcSet);
                if (haveCPM)                  //have a reading from the g-m counter, add it on
                {
                    char aBuf[8];
                    itoa(cpm, aBuf, 10);
                    strcat(buf, "&b=");
                    strcat(buf, aBuf);
                    haveCPM = false;
                }
                if ( GS.send(XB.compID, buf) == SEND_ACCEPTED )
                {
                    Serial << F("Send OK");
                }
                else
                {
                    Serial << F("Send FAIL");
                }
                Serial << F(" seq=") << GS.sendSeq << F(" tempF=") << tF10/10 << '.' << tF10%10 << F(" success=") << GS.httpOK << F(" fail=") << GS.connFail << F(" timeout=") << GS.recvTimeout;
                Serial << F(" cnct=") << GS.connTime << F(" resp=") << GS.respTime << F(" disc=") << GS.discTime << F(" rtcSet=") << rtcSet << endl;
            }

            digitalWrite(HB_LED, !(utcS & 1));    //run the heartbeat LED

            if ( utcS % 10 == 0 )                 //read temperature every 10 sec
            {
                tF10 = avgTemp.reading( mcp9802.readTempF10(AMBIENT) );
            }

            if (utc >= nextTimePrint)             //print time to Serial once per minute
            {
                timeSpan(buf, utc - startupTime);
                Serial << endl << millis() << F(" Local: ");
                printDateTime(local, tcr -> abbrev, false);
                Serial << F(" Uptime: ") << buf << endl;
                nextTimePrint += 60;
                uint8_t nSock = showSockStatus();
                //renew the DHCP lease hourly
                if (utcM == 0 && utcS == 0)
                {
                    unsigned long msStart = millis();
                    uint8_t mStat = Ethernet.maintain();
                    unsigned long msEnd = millis();
                    Serial << msEnd << ' ' << F("Ethernet.maintain=") << mStat << ' ' << msEnd - msStart << endl;
                }
            }
        }
        break;

    case RESET_WARN:
        if (millis() - msSend >= 500)          //keep retrying if needed
        {
            msSend = millis();
            strcpy(buf, "&msg=Lost%20NTP%20server%20-%20reset%20MCU");
            if (GS.send(XB.compID, buf) == SEND_ACCEPTED) STATE = RESET_WAIT;
        }
        break;

    //All hope abandon, ye who enter here...
    case RESET_WAIT:
        break;
    }
}

enum dispStates_t { DISP_CLOCK, SET_TZ, SET_INTERVAL, SET_WDT } DISP_STATE;

//user interface, display and buttons
void runDisplay(int tF10, int cpm)
{
    static time_t utcLast;
    static uint8_t dispMode;
    char lcdBuf[18];
    const char spaces[8] = "      ";
    const char cpmText[] = " counts/min";

    switch (DISP_STATE)
    {
    case DISP_CLOCK:
        if (btnSet.wasReleased())
        {
            DISP_STATE = SET_TZ;
            lcd.clear();
            lcd << F("Timezone:");
            lcd.setCursor(0, 1);
            lcd << tzNames[tzIndex];
        }
        if ( utc != utcLast )
        {
            utcLast = utc;
            local = (*tz).toLocal(utc, &tcr);
            lcd.setCursor(0, 0);        //lcd display, time & temp on first row
            printTime(lcd, local);
            lcd << tcr -> abbrev << ' ' << (tF10 + 5) / 10 << '\xDF';
            lcd.setCursor(0, 1);        //move to second row
            if (dispMode >= 2)
            {
                printDayDate(lcd, local);
            }
            else
            {
                itoa(cpm, lcdBuf, 10);
                strcat(lcdBuf, cpmText);
                strncat( lcdBuf, spaces, 16 - strlen(lcdBuf) );
                lcd << lcdBuf;
            }
            if (++dispMode >= 4) dispMode = 0;
        }
        break;

    case SET_TZ:
        if ( btnSet.wasReleased() )
        {
            DISP_STATE = SET_INTERVAL;
            tz = timezones[tzIndex];
            eeprom_update_byte( &ee_tzIndex, tzIndex );
            lcd.clear();
            lcd << F("Geiger interval:");
            lcd.setCursor(0, 1);
            lcd << gmIntervals[gmIntervalIdx] << F(" minute") << ( (gmIntervals[gmIntervalIdx] > 1) ? "s  " : "   ");
        }
        else if ( btnUp.wasPressed() )
        {
            if ( ++tzIndex >= sizeof(tzNames) / sizeof(tzNames[0]) ) tzIndex = 0;
            lcd.setCursor(0, 1);
            lcd << tzNames[tzIndex];
        }
        else if ( btnDn.wasPressed() )
        {
            if ( --tzIndex >= sizeof(tzNames) / sizeof(tzNames[0]) ) tzIndex = sizeof(tzNames) / sizeof(tzNames[0]) - 1;
            lcd.setCursor(0, 1);
            lcd << tzNames[tzIndex];
        }
        break;

    case SET_INTERVAL:
        if ( btnSet.wasReleased() )
        {
            DISP_STATE = SET_WDT;
            GEIGER.setInterval( gmIntervals[gmIntervalIdx] );
            eeprom_update_byte( &ee_gmIntervalIdx, gmIntervalIdx );
            lcd.clear();
            lcd << F("Watchdog timer:");
            lcd.setCursor(0, 1);
            lcd << (wdtEnable ? F("ON") : F("OFF"));
        }
        else if ( btnUp.wasPressed() )
        {
            if ( ++gmIntervalIdx >= sizeof(gmIntervals) / sizeof(gmIntervals[0]) ) gmIntervalIdx = 0;
            lcd.setCursor(0, 1);
            lcd << gmIntervals[gmIntervalIdx] << F(" minute") << ( (gmIntervals[gmIntervalIdx] > 1) ? "s  " : "   ");
        }
        else if ( btnDn.wasPressed() )
        {
            if ( --gmIntervalIdx >= sizeof(gmIntervals) / sizeof(gmIntervals[0]) ) gmIntervalIdx = sizeof(gmIntervals) / sizeof(gmIntervals[0]) - 1;
            lcd.setCursor(0, 1);
            lcd << gmIntervals[gmIntervalIdx] << F(" minute") << ( (gmIntervals[gmIntervalIdx] > 1) ? "s  " : "   ");
        }
        break;

    case SET_WDT:
        if ( btnSet.wasReleased() )
        {
            DISP_STATE = DISP_CLOCK;
            lcd.clear();
            eeprom_update_byte( &ee_wdtEnable, wdtEnable );
            Serial << millis() << F(" Watchdog Timer ") << (wdtEnable ? F("ON\n") : F("OFF\n"));
            if (wdtEnable)
            {
                wdt_enable(WDTO_8S);
            }
            else
            {
                wdt_disable();
            }
        }
        else if ( btnUp.wasPressed() || btnDn.wasPressed() )
        {
            wdtEnable = !wdtEnable;
            lcd.setCursor(0, 1);
            lcd << (wdtEnable ? F("ON ") : F("OFF"));
        }
        break;

    }
}

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
    uint8_t socketStat[MAX_SOCK_NUM];
    uint8_t nAvailable = 0;
    
//    uint16_t rtr = W5100.readRTR();    //retry time
//    uint8_t rcr = W5100.readRCR();     //retry count
//    Serial << F("RTR=") << rtr << F(" RCR=") << rcr << endl;
    
    for (uint8_t i = 0; i < MAX_SOCK_NUM; i++)
    {
        Serial << F("Sock_") << i;
        uint8_t s = W5100.readSnSR(i);
        socketStat[i] = s;
        if ( s == 0 ) ++nAvailable;
        Serial << F(" 0x") << _HEX(s) << ' ' << W5100.readSnPORT(i) << F(" D:");
        uint8_t dip[4];
        W5100.readSnDIPR(i, dip);
        for (uint8_t j = 0; j < 4; j++)
        {
            Serial << dip[j];
            if (j < 3) Serial << '.';
        }
        Serial << '(' << W5100.readSnDPORT(i) << ')' << endl;
    }
    return nAvailable;
}
