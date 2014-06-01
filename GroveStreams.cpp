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

enum gsState_t { GS_WAIT_SEND, GS_WAIT_RECV, GS_DISCONNECT } GS_STATE;

//to do: differentiate additional statuses
//connect fail -- put fail (200 OK not recd) -- timeout -- ???
//status 0 = ok, !0 = error

//request data to be sent. returns 0 if accepted.
//returns -1 if e.g. transmission already in progress, waiting response, etc.
int GroveStreams::send(char* data) {
    if (GS_STATE = GS_WAIT_SEND) {
        _data = data;
        GS_STATE = GS_SEND;
        return 0;
    }
    else {
        return -1;
    }
}

int GroveStreams::_xmit(void)
{
    _msConnect = millis();
    Serial << _msConnect << F(" connecting") << endl;
    if (_ledPin >= 0) digitalWrite(_ledPin, HIGH);
    if(client.connect(serverIP, serverPort)) {
        _msConnected = millis();        
        Serial << _msConnected << F(" connected") << endl;
        freeMem = freeMemory();
        client << F("PUT /api/feed?&compId=") << _compID << F("&compName=") << _compName << F("&org=") << _orgID << "&api_key=" << _apiKey;
        client << *_data << F(" HTTP/1.1") << endl << F("Host: ") << serverIP << endl << F("Connection: close") << endl;
        client << F("X-Forwarded-For: ") << Ethernet.localIP() << endl << F("Content-Type: application/json") << endl << endl;
        _msPutComplete = millis();
        Serial << _msPutComplete << F(" PUT complete ") << strlen(data) << endl;
        connTime = _msConnected - _msConnect;
        return 0;
    } 
    else {
        _msConnected = millis();
        connTime = _msConnected - _msConnect;
        Serial << _msConnected << F(" connect failed") << endl;
        if (_ledPin >= 0) digitalWrite(_ledPin, LOW);
        return -1;
    }
}

int GroveStreams::run(void) {
    const char httpOKText[] = "HTTP/1.1 200 OK";
    static char statusBuf[sizeof(httpOK)];

    switch (GS_STATE) {
        
    case GS_WAIT_SEND:
        break;
        
    case GS_SEND;
        if ( _xmit() == 0 ) {
            GS_STATE = GS_WAIT_RECV;
            return 1;
        }
        else {
            GS_STATE = GS_WAIT_SEND;
            return -1;
        }
        break;
        
    case GS_RECV:
        boolean haveStatus = false;
        boolean httpOK = false;
        _msLastPacket = millis();    //initialize receive timeout
        while(client.connected()) {
            while(int nChar = client.available()) {
                _msLastPacket = millis();
                Serial << _msLastPacket << F(" received packet, len=") << nChar << endl;
                char* b = statusBuf;
                for (int i = 0; i < nChar; i++) {
                    char ch = client.read();
                    Serial << _BYTE(ch);
                    if ( !haveStatus && i < sizeof(statusBuf) ) {
                        if ( ch == '\r' || i == sizeof(statusBuf) - 1 ) {
                            haveStatus = true;
                            *b++ = 0;
                            if (strncmp(statusBuf, httpOKText, sizeof(httpOKText)) == 0) {
                                httpOK = true;
                                Serial << endl << endl << millis() << F(" HTTP OK") << endl;
                            }
                            else {
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
            if(millis() - _msLastPacket >= RECEIVE_TIMEOUT) {
                ++timeout;
                _msLastPacket = millis();
                Serial << endl << _msLastPacket << F(" Timeout") << endl;
                client.stop();
                if (_ledPin >= 0) digitalWrite(_ledPin, LOW);
                GS_STATE = DISC;
            }
        }
        break;
        
    case GS_DISCONNECT:
        break;

    

    // close client end
    _msDisconnecting = millis();
    Serial << _msDisconnecting << F(" disconnecting") << endl;
    client.stop();
    if (_ledPin >= 0) digitalWrite(_ledPin, LOW);
    _msDisconnected = millis();
    respTime = _msLastPacket - _msPutComplete;
    discTime = _msDisconnected - _msDisconnecting;
    Serial << _msDisconnected << F(" disconnected") << endl;
    return 1;
}

