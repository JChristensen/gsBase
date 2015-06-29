//XBee class for IoT base station (data concentrator)
//
//This work by Jack Christensen is licensed under
//CC BY-SA 4.0, http://creativecommons.org/licenses/by-sa/4.0/

// #ifndef baseXBee_h
// #define baseXBee_h

//const uint8_t PAYLOAD_LEN = 250;    //must be at least as large as defined in nodes that transmit data
//enum xbeeReadStatus_t { 
//    NO_TRAFFIC, TX_STATUS, COMMAND_RESPONSE, MODEM_STATUS, RX_NO_ACK, RX_TIMESYNC, RX_DATA, RX_ERROR, RX_UNKNOWN, UNKNOWN_FRAME };

// // class baseXBee : 
// // public XBee
// // {
// // public:
    // baseXBee(void);
    // xbeeReadStatus_t read(void);
    // void sendTimeSync(time_t utc);
    // void atCommand(uint8_t* cmd);

    // uint8_t txSec;                //transmit once a minute on this second, 0 <= txSec < 60
    // uint8_t txInterval;           //transmission interval in minutes, 0 <= txInterval < 100
    // uint8_t txOffset;             //minute offset to transmit, 0 <= txOffset < txInterval
    // uint8_t txWarmup;             //seconds to wake before transmission time, to allow sensors to produce data, etc.
    // char payload[PAYLOAD_LEN];
    // char sendingCompID[10];       //sender's component ID from received packet
    // int8_t rss;                   //received signal strength, dBm

// private:
    // bool parsePacket(void);
    // void queueTimeSync(void);
    // void getRSS(void);
    // void copyToBuffer(char* dest, uint32_t source);
    // uint32_t getFromBuffer(char* source);
    // void parseNodeID(char* ni);

    // ZBTxStatusResponse zbStat;
    // AtCommandResponse atResp;
    // ModemStatusResponse zbMSR;
    // ZBRxResponse zbRX;
    // ZBTxRequest zbTX;
    // XBeeAddress64 coordAddr;
    // uint32_t msTX;                       //last XBee transmission time from millis()
    // char compID[10];                     //our component ID
    // XBeeAddress64 sendingAddr;           //address of node that sent packet
    // char tsCompID[10];                   //time sync requestor's component ID
    // char packetType;                     //D = data packet, S = time sync request
// };

// baseXBee::baseXBee(void)
// {
    // coordAddr = XBeeAddress64(0x0, 0x0);
    // strcpy(compID, "ZC10");              //fake it for now
// }

// //process incoming traffic from the XBee
// xbeeReadStatus_t baseXBee::read(void)
// {
    // uint8_t msrResponse, respLen, *atResponse, delyStatus, dscyStatus, txRetryCount;

    // readPacket();
    // if ( getResponse().isAvailable() ) {
        // uint32_t ms = millis();

        // switch (getResponse().getApiId())                  //what kind of packet did we get?
        // {
        // case ZB_RX_RESPONSE:                               //rx data packet
            // getResponse().getZBRxResponse(zbRX);           //get the received data
            // switch (zbRX.getOption() & 0x01)               //check ack bit only
            // {
            // case ZB_PACKET_ACKNOWLEDGED:
                // //process the received data
                // Serial << ms << F(" XB RX/ACK\n");
                // if ( parsePacket() )
                // {
                    // switch (packetType)                    //what type of packet
                    // {
                    // case 'S':                              //time sync request
                        // queueTimeSync();
                        // return RX_TIMESYNC;
                        // break;
                    // case 'D':                              //data headed for the web
                        // getRSS();                          //get the received signal strength
                        // return RX_DATA;
                        // break;
                    // default:                               //not expecting anything else
                        // Serial << endl << ms << F(" XB unknown packet type\n");
                        // return RX_UNKNOWN;
                        // break;
                    // }
                // }
                // else
                // {
                    // uint8_t *d = zbRX.getData();
                    // uint8_t nChar = zbRX.getDataLength();
                    // Serial << ms << F(" Malformed packet: /");
                    // for ( uint8_t i = 0; i < nChar; ++i ) Serial << (char)*d++;
                    // Serial << '/' << nChar << endl;
                    // return RX_ERROR;
                // }
                // break;
            // default:
                // Serial << endl << ms << F(" XB RX no ACK\n");    //packet received and not ACKed
                // return RX_NO_ACK;
                // break;
            // }
            // break;

        // case ZB_TX_STATUS_RESPONSE:                        //transmit status for packets we've sent
            // getResponse().getZBTxStatusResponse(zbStat);
            // delyStatus = zbStat.getDeliveryStatus();
            // dscyStatus = zbStat.getDiscoveryStatus();
            // txRetryCount = zbStat.getTxRetryCount();
            // switch (delyStatus) {
            // case SUCCESS:
                // Serial << ms << F(" XB TX OK ") << ms - msTX << F("ms R=");
                // Serial << txRetryCount << F(" DSCY=") << dscyStatus << endl;
                // break;
            // default:
                // Serial << ms << F(" XB TX FAIL ") << ms - msTX << F("ms R=");
                // Serial << txRetryCount << F(" DELY=") << delyStatus << F(" DSCY=") << dscyStatus << endl;
                // break;
            // }
            // return TX_STATUS;
            // break;

        // case AT_COMMAND_RESPONSE:                          //response to NI command
            // atResp = AtCommandResponse();
            // getResponse().getAtCommandResponse(atResp);
            // if (atResp.isOk()) {
                // respLen = atResp.getValueLength();
                // atResponse = atResp.getValue();
                // for (int i=0; i<respLen; i++) {
                    // //                    nodeID[i] = atResponse[i];
                // }
                // //                nodeID[respLen] = 0;                       //assume 4-byte NI of the form XXnn
                // //                txSec = atoi(&nodeID[2]);                  //use nn to determine this node's transmit time
                // //                Serial << ms << F(" XB NI=") << nodeID << endl;
            // }
            // else {
                // Serial << ms << F(" XB NI FAIL\n");
            // }
            // return COMMAND_RESPONSE;
            // break;

        // case MODEM_STATUS_RESPONSE:                        //XBee administrative messages
            // getResponse().getModemStatusResponse(zbMSR);
            // msrResponse = zbMSR.getStatus();
            // Serial << ms << ' ';
            // switch (msrResponse) {
            // case HARDWARE_RESET:
                // Serial << F("XB HW RST\n");
                // break;
            // case ASSOCIATED:
                // Serial << F("XB ASC\n");
                // break;
            // case DISASSOCIATED:
                // Serial << F("XB DISASC\n");
                // break;
            // default:
                // Serial << F("XB MDM STAT 0x") << _HEX(msrResponse) << endl;
                // break;
            // }
            // return MODEM_STATUS;
            // break;

        // default:                                           //something else we were not expecting
            // Serial << F("XB UNEXP TYPE\n");                //unexpected frame type
            // return UNKNOWN_FRAME;
            // break;
        // }
    // }
    // return NO_TRAFFIC;
// }

// //respond to a previously queued time sync request
// void baseXBee::sendTimeSync(time_t utc)
// {
    // if (tsCompID[0] != 0) {                      //is there a request queued?
        // const char SOH = 0x01;                   //start of header
        // const char STX = 0x02;                   //start of text

        // char *p = payload;
        // *p++ = SOH;
        // *p++ = 'S';                              //time sync packet
        // char *c = compID;
        // while ( *p++ = *c++ );                   //copy in component ID
        // *(p - 1) = STX;                          //overlay the string terminator
        // copyToBuffer(p, utc);                    //send current UTC

        // uint8_t len = strlen(compID) + 7;        //build the tx request
        // zbTX.setAddress64(sendingAddr);
        // zbTX.setAddress16(0xFFFE);
        // zbTX.setPayload((uint8_t*)payload);
        // zbTX.setPayloadLength(len);
        // send(zbTX);
        // msTX = millis();
        // Serial << endl << millis() << F(" Time sync ") << tsCompID << ' ' << len << endl;
        // tsCompID[0] = 0;                         //request was serviced, none queued
    // }
// }

// //send an AT command to the XBee.
// //response is processed in read().
// void baseXBee::atCommand(uint8_t* cmd)
// {
    // AtCommandRequest atCmdReq = AtCommandRequest(cmd);
    // send(atCmdReq);
    // Serial << endl << millis() << F(" XB CMD ") << (char*)cmd << endl;
// }

// //parse a received packet; check format, extract GroveStreams component ID and data.
// //returns false if there is an error in the format, else true.
// bool baseXBee::parsePacket(void)
// {
    // uint8_t *d = zbRX.getData();
    // uint8_t len = zbRX.getDataLength();
    // if ( *d++ != 0x01 ) return false;          //check for SOH start character
    // packetType = *d++;                         //save the packet type
    // char *c = sendingCompID;                   //now parse the component ID
    // uint8_t nChar = 0;
    // char ch;
    // while ( (ch = *d++) != 0x02 ) {            //look for STX
        // if ( ++nChar > 8 ) return false;       //missing
        // *c++ = ch;
    // }
    // *c++ = 0;                                  //string terminator
    // char *p = payload;                         //now copy the rest of the payload data
    // for (uint8_t i = nChar+2; i < len; ++i ) {
        // *p++ = *d++;
    // }
    // *p++ = 0;                                  //string terminator
    // sendingAddr = zbRX.getRemoteAddress64();   //save the sender's address
    // Serial << millis() << F(" XB RX ") << sendingCompID << ' ' << len << endl;
    // return true;
// }

// //queue a time sync request
// void baseXBee::queueTimeSync(void)
// {
    // if (tsCompID[0] == 0) {                    //can only queue one request, ignore request if already have one queued
        // strcpy(tsCompID, sendingCompID);       //save the sender's node ID
    // }
// }

// //returns received signal strength value for the last RF data packet.
// void baseXBee::getRSS(void)
// {
    // uint8_t atCmd[] = {
        // 'D', 'B'                            };
    // AtCommandRequest atCmdReq = AtCommandRequest(atCmd);
    // send(atCmdReq);
    // if (readPacket(10)) {
        // if (getResponse().getApiId() == AT_COMMAND_RESPONSE) {
            // AtCommandResponse atResp;
            // getResponse().getAtCommandResponse(atResp);
            // if (atResp.isOk()) {
                // uint8_t respLen = atResp.getValueLength();
                // if (respLen == 1) {
                    // uint8_t* resp = atResp.getValue();
                    // rss = -resp[0];
                // }
                // else {
                    // Serial << F("RSS LEN ERR\n");    //unexpected length
                // }
            // }
            // else {
                // Serial << F("RSS ERR\n");            //status not ok
            // }
        // }
        // else {
            // Serial << F("RSS UNEXP RESP\n");         //expecting AT_COMMAND_RESPONSE, got something else
        // }
    // }
    // else {
        // Serial << F("RSS NO RESP\n");                //timed out
    // }
// }

// //parse Node ID in format compID_ssmmnnww.
// //compID must be 1-8 characters, the remainder must be exact, "_" followed by 8 digits.
// //no checking for proper format is done; improper format will cause undefined behavior.
// void baseXBee::parseNodeID(char* ni)
// {
    // char* p = ni;              //copy the pointer to preserve ni pointing at the start of the string
    // p += strlen(p) - 2;        //point at ww
    // txWarmup = atoi(p);
    // *p = 0; 
    // p -= 2;            //put in terminator and back up to point at nn
    // txOffset = atoi(p);
    // *p = 0; 
    // p -= 2;            //mm
    // txInterval = atoi(p);
    // *p = 0; 
    // p -= 2;            //ss
    // txSec = atoi(p);
    // *(--p) = 0;                //terminator after component ID
    // strcpy(compID, ni);        //save the component ID
// }

// //copy a four-byte integer to the designated offset in the buffer
// void baseXBee::copyToBuffer(char* dest, uint32_t source)
// {
    // union charInt_t {
        // char c[4];
        // uint32_t i;
    // } 
    // data;

    // data.i = source;
    // dest[0] = data.c[0];
    // dest[1] = data.c[1];
    // dest[2] = data.c[2];
    // dest[3] = data.c[3];
// }

// //get a four-byte integer from the buffer starting at the designated offset
// uint32_t baseXBee::getFromBuffer(char* source)
// {
    // union charInt_t {
        // char c[4];
        // uint32_t i;
    // } 
    // data;

    // data.c[0] = source[0];
    // data.c[1] = source[1];
    // data.c[2] = source[2];
    // data.c[3] = source[3];

    // return data.i;
// }

// #endif








