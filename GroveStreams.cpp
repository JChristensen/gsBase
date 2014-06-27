//TO DO:  Differentiate additional statuses ... this should be good?
//        Count errors, reset MCU after n consecutive errors
//        Component name and ID part of send() call ... Looks like only comp ID needed!

//GroveStreams Class
#include "GroveStreams.h"

GroveStreams::GroveStreams( const char* serverName, const char* PROGMEM orgID, const char* PROGMEM apiKey, const char* PROGMEM compID, int ledPin)
{
    _serverName = serverName;
    _orgID = orgID;
    _apiKey = apiKey;
    _compID = compID;
    _ledPin = ledPin;
}

void GroveStreams::begin(void)
{
    int ret = dnsLookup(_serverName, serverIP);
    if (ret == 1) {
        Serial << millis() << F(" GroveStreams IP=") << serverIP << endl;
    }
    else {
        Serial << millis() << F(" GS DNS lookup fail, ret=") << ret << endl;
        wdt_enable(WDTO_4S);
        while (1);
    }
}

enum gsState_t { GS_WAIT, GS_SEND, GS_RECV, GS_DISCONNECT } GS_STATE;

ethernetStatus_t GroveStreams::run(void)
{
    ethernetStatus_t ret = NO_STATUS;
    const char httpOKText[] = "HTTP/1.1 200 OK";
    static char statusBuf[sizeof(httpOKText)];

    switch (GS_STATE)
    {
    case GS_WAIT:    //wait for next send
        break;

    case GS_SEND:
        if ( _xmit() == PUT_COMPLETE ) {
            _msLastPacket = millis();    //initialize receive timeout
            GS_STATE = GS_RECV;
            ret = PUT_COMPLETE;
        }
        else {
            GS_STATE = GS_WAIT;
            ret = CONNECT_FAILED;
        }
        break;

    case GS_RECV:
        {
        boolean haveStatus = false;
        boolean httpOK = false;

        if(client.connected()) {
            uint16_t nChar = client.available();
            if (nChar > 0) {
                _msLastPacket = millis();
                Serial << _msLastPacket << F(" received packet, len=") << nChar << endl;
                char* b = statusBuf;
                for (uint16_t i = 0; i < nChar; i++) {
                    char ch = client.read();
                    Serial << _BYTE(ch);
                    if ( !haveStatus && i < sizeof(statusBuf) ) {
                        if ( ch == '\r' || i == sizeof(statusBuf) - 1 ) {
                            haveStatus = true;
                            *b++ = 0;
                            if (strncmp(statusBuf, httpOKText, sizeof(httpOKText)) == 0) {
                                httpOK = true;
                                ret = HTTP_OK;
                                Serial << endl << endl << millis() << F(" HTTP OK") << endl;
                            }
                            else {
                                ret = HTTP_OTHER;
                                Serial << endl << endl << millis() << F(" HTTP STATUS: ") << statusBuf << endl;
                            }
                        }
                        else {
                            *b++ = ch;
                        }
                    }
                }
            }
            //if too much time has elapsed since the last packet, time out and close the connection from this end
            else if (millis() - _msLastPacket >= RECEIVE_TIMEOUT) {
                ++timeout;
                _msLastPacket = millis();
                Serial << endl << _msLastPacket << F(" Timeout") << endl;
                client.stop();
                if (_ledPin >= 0) digitalWrite(_ledPin, LOW);
                GS_STATE = GS_DISCONNECT;
                ret = TIMEOUT;
            }
        }
        else {
            GS_STATE = GS_DISCONNECT;
            ret = DISCONNECTING;
        }
        break;
    }

    case GS_DISCONNECT:
        // close client end
        _msDisconnecting = millis();
        Serial << _msDisconnecting << F(" disconnecting") << endl;
        client.stop();
        if (_ledPin >= 0) digitalWrite(_ledPin, LOW);
        _msDisconnected = millis();
        respTime = _msLastPacket - _msPutComplete;
        discTime = _msDisconnected - _msDisconnecting;
        Serial << _msDisconnected << F(" disconnected") << endl;
        GS_STATE = GS_WAIT;
        ret = DISCONNECTED;
        break;
    }
    if (ret != NO_STATUS) lastStatus = ret;
    return ret;
}

//request data to be sent. returns 0 if accepted.
//returns -1 if e.g. transmission already in progress, waiting response, etc.
ethernetStatus_t GroveStreams::send(char* data)
{
    if (GS_STATE == GS_WAIT) {
        _data = data;
        GS_STATE = GS_SEND;
        lastStatus = SEND_ACCEPTED;
    }
    else {
        lastStatus = SEND_BUSY;
    }
    return lastStatus;
}

ethernetStatus_t GroveStreams::_xmit(void)
{
    _msConnect = millis();
    Serial << _msConnect << F(" connecting") << endl;
    if (_ledPin >= 0) digitalWrite(_ledPin, HIGH);
    if(client.connect(serverIP, serverPort)) {
        _msConnected = millis();
        Serial << _msConnected << F(" connected") << endl;
        freeMem = freeMemory();
//        client << F("PUT /api/feed?&compId=") << _compID << F("&compName=") << _compName << F("&org=") << _orgID << "&api_key=" << _apiKey;
        client << F("PUT /api/feed?&compId=") << _compID << F("&org=") << _orgID << "&api_key=" << _apiKey;
        client << _data << F(" HTTP/1.1") << endl << F("Host: ") << serverIP << endl << F("Connection: close") << endl;
        client << F("X-Forwarded-For: ") << Ethernet.localIP() << endl << F("Content-Type: application/json") << endl << endl;
        _msPutComplete = millis();
        Serial << _msPutComplete << F(" PUT complete ") << strlen(_data) << endl;
        connTime = _msConnected - _msConnect;
        lastStatus = PUT_COMPLETE;
    }
    else {
        _msConnected = millis();
        connTime = _msConnected - _msConnect;
        Serial << _msConnected << F(" connect failed") << endl;
        if (_ledPin >= 0) digitalWrite(_ledPin, LOW);
        lastStatus = CONNECT_FAILED;
    }
    return lastStatus;
}
