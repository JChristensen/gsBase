//GroveStreams Class
#include "GroveStreams.h"

GroveStreams::GroveStreams(char* serverName, char* PROGMEM orgID, char* PROGMEM apiKey, char* PROGMEM compID, char* PROGMEM compName, int ledPin)
{
    _serverName = serverName;
    _orgID = orgID;
    _apiKey = apiKey;
    _compID = compID;
    _compName = compName;
    _ledPin = ledPin;
}

uint8_t GroveStreams::begin(void)
{
    int ret = dnsLookup(_serverName, gsServerIP);
    if (ret == 1) {
        Serial << millis() << F(" GroveStreams IP=") << gsServerIP << endl;
    }
    else {
        Serial << millis() << F(" GS DNS lookup fail, ret=") << ret << endl;
        wdt_enable(WDTO_4S);
        while (1);
    }
}

//to do: differentiate additional statuses
//connect fail -- put fail (200 OK not recd) -- timeout -- ???
//status 0 = ok, !0 = error
uint8_t GroveStreams::send(char* data)
{
    const char httpOK[16] = "HTTP/1.1 200 OK";
    
    msConnect = millis();
    Serial << msConnect << F(" connecting") << endl;
    if (_ledPin >= 0) digitalWrite(_ledPin, HIGH);
    if(client.connect(gsServerIP, serverPort)) {
        msConnected = millis();        
        Serial << msConnected << F(" connected") << endl;
        freeMem = freeMemory();
        client << F("PUT /api/feed?&compId=") << _compID << F("&compName=") << _compName << F("&org=") << _orgID << "&api_key=" << _apiKey;
        client << data << F(" HTTP/1.1") << endl << F("Host: ") << gsServerIP << endl << F("Connection: close") << endl;
        client << F("X-Forwarded-For: ") << Ethernet.localIP() << endl << F("Content-Type: application/json") << endl << endl;
        msPutComplete = millis();
        Serial << msPutComplete << F(" PUT complete ") << strlen(data) << endl;
        connTime = msConnected - msConnect;
    } 
    else {
        msConnected = millis();
        connTime = msConnected - msConnect;
        Serial << msConnected << F(" connect failed") << endl;
        if (_ledPin >= 0) digitalWrite(_ledPin, LOW);
        return 0;
    }

    boolean haveStatus = false;
    msLastPacket = millis();    //initialize receive timeout
    while(client.connected()) {
        while(int nChar = client.available()) {
            msLastPacket = millis();
            Serial << msLastPacket << F(" received packet ") << nChar << endl;
            char* b = data;    //use the caller's buffer
            for (int i=0; i<nChar; i++) {
                char ch = client.read();
                Serial << _BYTE(ch);
                if (!haveStatus) {
                    if (ch == '\r') {
                        haveStatus = true;
                        *b++ = 0;
                        if (strncmp(data, httpOK, 15) == 0) Serial << millis() << F(" HTTP OK") << endl;
                    }
                    else {
                        *b++ = ch;
                    }
                }
            }
        }

        //if too much time has elapsed since the last packet, time out and close the connection from this end
        if(millis() - msLastPacket >= RECEIVE_TIMEOUT) {
            ++timeout;
            msLastPacket = millis();
            Serial << endl << msLastPacket << F(" Timeout") << endl;
            client.stop();
            if (_ledPin >= 0) digitalWrite(_ledPin, LOW);
        }
    }

    // close client end
    msDisconnecting = millis();
    Serial << msDisconnecting << F(" disconnecting") << endl;
    client.stop();
    if (_ledPin >= 0) digitalWrite(_ledPin, LOW);
    msDisconnected = millis();
    respTime = msLastPacket - msPutComplete;
    discTime = msDisconnected - msDisconnecting;
    Serial << msDisconnected << F(" disconnected") << endl;
    return 1;
}

