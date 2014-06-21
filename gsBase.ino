//Check fuses: H:FD, E:D6, L:FF (preserve EEPROM thru chip erase)

#include <utility/w5100.h>
//#include <util/atomic.h>
#include <Button.h>                 //http://github.com/JChristensen/Button
#include <DS3232RTC.h>              //http://github.com/JChristensen/DS3232RTC
#include <avr/eeprom.h>
#include <Ethernet.h>               //http://arduino.cc/en/Reference/Ethernet
#include <MCP980X.h>
#include <LiquidTWI.h>              //http://forums.adafruit.com/viewtopic.php?f=19&t=21586&p=113177
#include <MemoryFree.h>             //http://playground.arduino.cc/Code/AvailableMemory
#include <movingAvg.h>              //http://github.com/JChristensen/movingAvg
#include <NTP.h>
#include <SPI.h>                    //http://arduino.cc/en/Reference/SPI
#include <Streaming.h>              //http://arduiniana.org/libraries/streaming/
#include <Time.h>                   //http://www.arduino.cc/playground/Code/Time
#include <Timezone.h>               //http://github.com/JChristensen/Timezone
#include <Wire.h>                   //http://arduino.cc/en/Reference/Wire
#include "GroveStreams.h"
#include "classes.h"

//pin assignments
const uint8_t INT2_PIN = 2;             //RTC interrupt
const uint8_t GM_INPUT = 10;            //INT0
const uint8_t WIZ_RESET = 12;           //WIZnet module reset pin
const uint8_t HB_LED = 13;              //heartbeat LED
const uint8_t NTP_LED = 14;             //ntp time sync
const uint8_t WAIT_LED = 15;            //waiting for server response
const uint8_t GM_PULSE_LED = 18;        //the LED to blink
const uint8_t SET_BUTTON = 19;
const uint8_t UP_BUTTON = 20;
const uint8_t DN_BUTTON = 21;
const uint8_t GM_POWER = 22;            //geiger power enable pin

uint8_t gmIntervalIdx;                      //index to geiger sample interval array
EEMEM uint8_t ee_gmIntervalIdx;             //copy persisted in EEPROM
const uint8_t gmIntervalIdx_DEFAULT = 2;    //index to the default value (i.e. 2 -> 10 min)
const int gmIntervals[] = { 1, 5, 10, 15, 20, 30, 60 };

const unsigned long PULSE_DUR = 50;         //blink duration for the LED, ms
oneShotLED countLED;

const bool PULLUP = true;
const bool INVERT = true;
const unsigned long DEBOUNCE_MS = 25;

const uint8_t maxNtpTimeouts = 3;
ntpClass NTP(maxNtpTimeouts, NTP_LED);

//US Eastern Time Zone (New York, Detroit)
//TimeChangeRule myDST = { "EDT", Second, Sun, Mar, 2, -240 };    //Daylight time = UTC - 4 hours
//TimeChangeRule mySTD = { "EST", First, Sun, Nov, 2, -300 };     //Standard time = UTC - 5 hours
//Timezone myTZ(myDST, mySTD);

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
Timezone *timezones[] = { &UTC, &Eastern, &Central, &Mountain, &Pacific };
Timezone *tz;               //pointer to the time zone
uint8_t tzIndex;            //index to the timezones[] array and the tzNames[] array
EEMEM uint8_t ee_tzIndex;   //copy persisted in EEPROM
char *tzNames[] = { "UTC     ", "Eastern ", "Central ", "Mountain", "Pacific " };
TimeChangeRule *tcr;        //pointer to the time change rule, use to get TZ abbrev

EthernetClient client;
int txSec = 10;                         //transmit data once per minute, on this second
time_t utc, local;

//GroveStreams
char gsServer[] = "grovestreams.com";
char* PROGMEM gsOrgID = "66d58106-8c04-34d9-9e8c-17499a8942d7";
char* PROGMEM gsApiKey = "cbc8d222-6f25-3e26-9f6e-edfc3364d7fd";
char* PROGMEM gsCompID = "Test-2";
char* PROGMEM gsCompName = "Test-2";
GroveStreams GS(gsServer, gsOrgID, gsApiKey, gsCompID, gsCompName, WAIT_LED);

MCP980X mcp9802(0);
movingAvg avgTemp;
LiquidTWI lcd(0); //i2c address 0 (0x20)
Button btnSet(SET_BUTTON, PULLUP, INVERT, DEBOUNCE_MS);
Button btnUp(UP_BUTTON, PULLUP, INVERT, DEBOUNCE_MS);
Button btnDn(DN_BUTTON, PULLUP, INVERT, DEBOUNCE_MS);

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
    pinMode(INT2_PIN, INPUT_PULLUP);
    pinMode(WIZ_RESET, OUTPUT);
    pinMode(HB_LED, OUTPUT);
    pinMode(NTP_LED, OUTPUT);
    pinMode(WAIT_LED, OUTPUT);
    pinMode(GM_INPUT, INPUT_PULLUP);

    //report the reset source
    Serial.begin(115200);
    Serial << endl << millis() << F(" MCU reset 0x0") << _HEX(mcusr);
    if (mcusr & _BV(WDRF))  Serial << F(" WDRF");
    if (mcusr & _BV(BORF))  Serial << F(" BORF");
    if (mcusr & _BV(EXTRF)) Serial << F(" EXTRF");
    if (mcusr & _BV(PORF))  Serial << F(" PORF");
    Serial << endl;

    //get geiger interval from eeprom and ensure that it's valid
    gmIntervalIdx = eeprom_read_byte( &ee_gmIntervalIdx );
    if ( gmIntervalIdx >= sizeof(gmIntervals) / sizeof(gmIntervals[0]) ) {
        gmIntervalIdx = gmIntervalIdx_DEFAULT;
        eeprom_write_byte( &ee_gmIntervalIdx, gmIntervalIdx);
    }

    //same for the time zone index
    tzIndex = eeprom_read_byte( &ee_tzIndex );
    if ( tzIndex >= sizeof(tzNames) / sizeof(tzNames[0]) ) {
        tzIndex = 0;
        eeprom_write_byte( &ee_tzIndex, tzIndex);
    }
    tz = timezones[tzIndex];                //set the tz
//    tz = timezones[1];                      //eastern time -- FOR TEST ONLY UNTIL MENUS ARE IMPLEMENTED

    //device inits
    delay(1);
    digitalWrite(WIZ_RESET, HIGH);
    mcp9802.begin();
    mcp9802.writeConfig(ADC_RES_12BITS);
    lcd.begin(16, 2);
    TWBR = 12;            //400kHz
    lcd.clear();
    lcd.setBacklight(HIGH);

    //RTC initialization
    lcd << F("RTC SYNC");
    utc = RTC.get();                       //try to read the time from the RTC
    if ( utc == 0 ) {                      //couldn't read it, something wrong
        lcd << F(" FAIL");
        digitalWrite( WAIT_LED, HIGH);
        while (1);
    }
    RTC.squareWave(SQWAVE_1_HZ);           //1Hz interrupts for timekeeping

    //get MAC address & display
    uint8_t mac[6];
    lcd.clear();
    lcd << F("MAC address:");
    lcd.setCursor(0, 1);
    getMAC(mac);
    for (int i=0; i<6; ++i) {
        if (mac[i] < 16) lcd << '0';
        lcd << _HEX( mac[i] );
    }
    delay(1000);                           //allow some time for the ethernet chip to boot up

    //start Ethernet, display IP
    Ethernet.begin(mac);                   //DHCP
    Serial << millis() << F(" Ethernet started, IP=") << Ethernet.localIP() << endl;
    lcd.clear();
    lcd << F("Ethernet IP:");
    lcd.setCursor(0, 1);
    lcd << Ethernet.localIP();
    delay(1000);

    //start NTP, display server IP
    NTP.begin();
    lcd.clear();
    lcd << F("NTP Server IP:");
    lcd.setCursor(0, 1);
    lcd << NTP.serverIP;
    delay(1000);

    //connect to GroveStreams, display IP
    GS.begin();
    lcd.clear();
    lcd << F("GroveStreams IP:");
    lcd.setCursor(0, 1);
    lcd << GS.serverIP;
    delay(1000);

    lcd.clear();
    lcd << F("NTP Sync...");

    //set system time from RTC
    utc = RTC.get();
    while (utc == RTC.get()) delay(10);        //synchronize with the interrupts
    utc = RTC.get();
    while (utc == RTC.get()) delay(10);
    utc = RTC.get();
    NTP.setTime(utc);
    Serial << millis() << F(" RTC set the system time: ");
    printDateTime(utc);

    countLED.begin(GM_PULSE_LED, PULSE_DUR);
    GEIGER.begin(gmIntervals[gmIntervalIdx], GM_POWER, utc);
}

enum STATE_t { INIT, RUN } STATE;

void loop(void)
{
    //    static uint16_t loopCount;
    static time_t utcLast;
    static time_t nextTransmit;          //time for next data transmission
    static time_t nextTimePrint;         //next time to print the local time to serial
    static char buf[96];
    static uint8_t socketsAvailable;
    static int tF10;
    static int rtcSet;
    static int cpm;
    static bool haveCPM = false;

    wdt_reset();
    utc = NTP.now();
    btnSet.read();
    btnUp.read();
    btnDn.read();
    countLED.run();

    //check for data from the G-M counter
    if (GEIGER.run(&cpm, utc)) {
        haveCPM = true;
        timeStamp(Serial, utc);
        Serial << F("G-M counts/min ") << cpm << endl;
    }

    if ( GEIGER.pulse() ) countLED.on();    //blip the LED

    bool ntpSync = NTP.run();            //run the NTP state machine
    if (ntpSync && NTP.lastSyncType == TYPE_PRECISE) {
        utc = NTP.now();
        RTC.squareWave(SQWAVE_NONE);
        RTC.set(utc + 1);
        RTC.squareWave(SQWAVE_1_HZ);
        Serial << millis() << F(" NTP set the RTC: ");
        printDateTime(utc);
        ++rtcSet;
    }
//    digitalWrite(NTP_LED, NTP.syncStatus == STATUS_RECD);    //MOVE THIS FUNCTIONALITY TO NTP CLASS!

    int gsStatus = GS.run();            //run the GroveStreams state machine
    runDisplay(tF10, cpm);       //run the LCD display

    switch (STATE) {

    case INIT:
        //wait until we have a good time from the NTP server
        if ( (ntpSync && NTP.lastSyncType == TYPE_PRECISE) || NTP.lastSyncType == TYPE_SKIPPED ) {
            nextTimePrint = nextMinute();
            nextTransmit = nextTimePrint + txSec;
            STATE = RUN;
            Serial << millis() << F(" Init complete, begin Run state") << endl;
            wdt_enable(WDTO_8S);
            strcpy(buf, "&msg=MCU%20reset%200x");
            if (mcusr < 16) strcat(buf, "0");
            itoa(mcusr, buf + strlen(buf), 16);
            if (mcusr & _BV(WDRF))  strcat(buf, "%20WDRF");
            if (mcusr & _BV(BORF))  strcat(buf, "%20BORF");
            if (mcusr & _BV(EXTRF)) strcat(buf, "%20EXTRF");
            if (mcusr & _BV(PORF))  strcat(buf, "%20PORF");
//TO DO: Think about whether anything should be done if this send fails. Think about delay between this message send and the first data send.
            GS.send(buf);
        }
        break;

    case RUN:
        //        ++loopCount;

        //process user button input
        //        if (btnMode.wasPressed()) {
        //            NTP.schedSync(5);
        //            Serial << millis() << F(" NTP Sync in 5 seconds") << endl;
        //        }

        if (utc != utcLast) {                 //once-per-second processing
            utcLast = utc;
            //            Serial << millis() << ' ';
            //            printTime(utc);
            //            Serial << F(" loopCount=") << loopCount << endl;
            //            loopCount = 0;
            uint8_t utcM = minute(utc);
            uint8_t utcS = second(utc);

            if (utc >= nextTransmit) {        //time to send data?
                nextTransmit += 60;
                sprintf(buf,"&a=%u&b=%lu&c=%lu&d=%lu&e=%u&f=%u&g=%u&h=%u&i=%i.%i&j=%u", GS.seq, GS.connTime, GS.respTime, GS.discTime, GS.success, GS.fail, GS.timeout, GS.freeMem, tF10/10, tF10%10, rtcSet);
                if (haveCPM) {                //have a reading from the gm counter, add it on
                    char aBuf[8];
                    itoa(cpm, aBuf, 10);
                    strcat(buf, "&k=");
                    strcat(buf, aBuf);
                    haveCPM = false;
                }
                if ( GS.send(buf) == SEND_ACCEPTED ) {
                    Serial << F("Post OK");
                    ++GS.success;
                }
                else {
                    Serial << F("Post FAIL");
                    ++GS.fail;
                }
                ++GS.seq;
                Serial << F(" seq=") << GS.seq << F(" cnct=") << GS.connTime << F(" resp=") << GS.respTime << F(" disc=") << GS.discTime << F(" success=") << GS.success;
                Serial << F(" fail=") << GS.fail << F(" timeout=") << GS.timeout << F(" rtcSet=") << rtcSet << F(" mem=") << GS.freeMem << F(" tempF=") << tF10/10 << '.' << tF10%10 << endl;
            }

            digitalWrite(HB_LED, !(utcS & 1));    //run the heartbeat LED

            if ( utcS % 10 == 0 ) {           //read temperature every 10 sec
                tF10 = avgTemp.reading( mcp9802.readTempF10(AMBIENT) );
            }

            if (utc >= nextTimePrint) {             //print time to Serial once per minute
                Serial << endl << millis() << F(" Local: ");
                printDateTime(local);
                nextTimePrint += 60;
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

enum dispStates_t { DISP_CLOCK, SET_TZ, SET_INTERVAL } DISP_STATE;

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
        if (btnSet.wasReleased()) {
            DISP_STATE = SET_TZ;
            lcd.clear();
            lcd << F("Timezone:");
            lcd.setCursor(0, 1);
            lcd << tzNames[tzIndex];
        }
        if (utc != utcLast) {
            utcLast = utc;
            local = (*tz).toLocal(utc, &tcr);
            lcd.setCursor(0, 0);        //lcd display, time & temp on first row
            printTime(lcd, local);
            lcd << tcr -> abbrev << ' ' << (tF10 + 5) / 10 << '\xDF';
            lcd.setCursor(0, 1);        //move to second row
            if (dispMode >= 2) {
                printDayDate(lcd, local);
            }
            else {
                itoa(cpm, lcdBuf, 10);
                strcat(lcdBuf, cpmText);
                strncat( lcdBuf, spaces, 16 - strlen(lcdBuf) );
                lcd << lcdBuf;
            }
            if (++dispMode >= 4) dispMode = 0;
        }
        break;

    case SET_TZ:
        if (btnSet.wasReleased()) {
            DISP_STATE = SET_INTERVAL;
            tz = timezones[tzIndex];
            eeprom_update_byte( &ee_tzIndex, tzIndex );
            lcd.clear();
            lcd << F("Geiger interval:");
            lcd.setCursor(0, 1);
            lcd << gmIntervals[gmIntervalIdx] << F(" minute") << ( (gmIntervals[gmIntervalIdx] > 1) ? "s  " : "   ");
        }
        else if (btnUp.wasPressed()) {
            if ( ++tzIndex >= sizeof(tzNames) / sizeof(tzNames[0]) ) tzIndex = 0;
            lcd.setCursor(0, 1);
            lcd << tzNames[tzIndex];
        }
        else if (btnDn.wasPressed()) {
            if ( --tzIndex >= sizeof(tzNames) / sizeof(tzNames[0]) ) tzIndex = sizeof(tzNames) / sizeof(tzNames[0]) - 1;
            lcd.setCursor(0, 1);
            lcd << tzNames[tzIndex];
        }
        break;

    case SET_INTERVAL:
        if (btnSet.wasReleased()) {
            DISP_STATE = DISP_CLOCK;
            lcd.clear();
            GEIGER.setInterval( gmIntervals[gmIntervalIdx] );
            eeprom_update_byte(  &ee_gmIntervalIdx, gmIntervalIdx );
        }
        else if (btnUp.wasPressed()) {
            if ( ++gmIntervalIdx >= sizeof(gmIntervals) / sizeof(gmIntervals[0]) ) gmIntervalIdx = 0;
            lcd.setCursor(0, 1);
            lcd << gmIntervals[gmIntervalIdx] << F(" minute") << ( (gmIntervals[gmIntervalIdx] > 1) ? "s  " : "   ");
        }
        else if (btnDn.wasPressed()) {
            if ( --gmIntervalIdx >= sizeof(gmIntervals) / sizeof(gmIntervals[0]) ) gmIntervalIdx = sizeof(gmIntervals) / sizeof(gmIntervals[0]) - 1;
            lcd.setCursor(0, 1);
            lcd << gmIntervals[gmIntervalIdx] << F(" minute") << ( (gmIntervals[gmIntervalIdx] > 1) ? "s  " : "   ");
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





