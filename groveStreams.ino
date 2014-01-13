const int serverPort = 80;
IPAddress gsServerIP;
const unsigned long RECEIVE_TIMEOUT = 10000;    //wait 10 sec for packet to be received from server

void gsBegin(void)
{
    int ret = NTP.dnsLookup(serverName, gsServerIP);
    if (ret == 1) {
        Serial << millis() << F(" GroveStreams IP=") << gsServerIP << endl;
    }
    else {
        Serial << millis() << F(" DNS lookup fail, ret=") << ret << endl;
        wdt_enable(WDTO_4S);
        while (1);
    }
}

byte postData(void)
{
    char buf[96];
    const char httpOK[16] = "HTTP/1.1 200 OK";
    unsigned long msConnect = millis();
    unsigned long msConnected;
    unsigned long msPutComplete;
    unsigned long msLastPacket;
    unsigned long msDisconnecting;
    unsigned long msDisconnected;
    
    Serial << msConnect << F(" connecting") << endl;
    digitalWrite(RED_LED, HIGH);
    if(client.connect(gsServerIP, 80)) {
        msConnected = millis();        
        Serial << msConnected << F(" connected") << endl;
        freeMem = freeMemory();
        client << F("PUT /api/feed?&compId=t2&compName=Test-2&org=") << gsOrg << "&api_key=" << gsApiKey;
//        client << buf;
        sprintf(buf,"&1=%u&2=%lu&3=%lu&4=%lu&5=%u&6=%u&7=%u&8=%u&9=%i.%i HTTP/1.1", seq, connTime, respTime, discTime, success, fail, timeout, freeMem, tF10/10, tF10%10);
        client << buf << endl;
        client << F("Host: ") << serverName << endl;
        client << F("Connection: close") << endl;
//        client << F("X-Forwarded-For: t2") << endl;
        client << F("X-Forwarded-For: 192.168.0.201") << endl;
        client << F("Content-Type: application/json") << endl << endl;
        msPutComplete = millis();
        Serial << msPutComplete << F(" PUT complete ") << strlen(buf) << endl;
        connTime = msConnected - msConnect;
    } 
    else {
        msConnected = millis();
        connTime = msConnected - msConnect;
        Serial << msConnected << F(" connect failed") << endl;
        digitalWrite(RED_LED, LOW);
        return 0;
    }

    boolean haveStatus = false;
    msLastPacket = millis();    //initialize receive timeout
    while(client.connected()) {
        while(int nChar = client.available()) {
            msLastPacket = millis();
            Serial << msLastPacket << F(" received packet ") << nChar << endl;
            char *b = buf;
            for (int i=0; i<nChar; i++) {
                char ch = client.read();
                Serial << _BYTE(ch);
                if (!haveStatus) {
                    if (ch == '\r') {
                        haveStatus = true;
                        *b++ = 0;
//                        if (strncmp(buf, httpOK, 15) == 0) Serial << millis() << F(" HTTP OK") << endl;
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
            digitalWrite(RED_LED, LOW);
        }
    }

    // close client end
    msDisconnecting = millis();
    Serial << msDisconnecting << F(" disconnecting") << endl;
    client.stop();
    digitalWrite(RED_LED, LOW);
    msDisconnected = millis();
    respTime = msLastPacket - msPutComplete;
    discTime = msDisconnected - msDisconnecting;
    Serial << msDisconnected << F(" disconnected") << endl;
    return 1;
}

