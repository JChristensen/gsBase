//XBee class for IoT base station (data concentrator)
//
//This work by Jack Christensen is licensed under
//CC BY-SA 4.0, http://creativecommons.org/licenses/by-sa/4.0/

#ifndef baseXBee_h
#define baseXBee_h

const uint8_t PAYLOAD_LEN = 80;     //must be at least as large as defined in nodes that transmit data
enum xbeeReadStatus_t { NO_TRAFFIC, TX_STATUS, COMMAND_RESPONSE, MODEM_STATUS, RX_NO_ACK, RX_TIMESYNC, RX_DATA, RX_UNKNOWN, UNKNOWN_FRAME };

class baseXBee : public XBee
{
    public:
        baseXBee(void);
        xbeeReadStatus_t read(void);
        void reqNodeID(void);
        void sendData(char* data);
        void sendTimeSync(time_t utc);
        uint8_t txSec;            //transmit once a minute on this second, derived from the XBee NI
        char payload[PAYLOAD_LEN];
        char rxNodeID[9];
        int8_t rss;                          //received signal strength, dBm
    
    private:
        void copyToBuffer(char* dest, uint32_t source);
        uint32_t getFromBuffer(char* source);
        void buildDataPayload(void);
        void queueTimeSync(void);
        void getRSS(void);
        ZBTxStatusResponse zbStat;
        AtCommandResponse atResp;
        ModemStatusResponse zbMSR;
        ZBRxResponse zbRX;
        ZBTxRequest zbTX;
        XBeeAddress64 coordAddr;
        uint32_t msTX;                       //last XBee transmission time from millis()
        char nodeID[9];
        XBeeAddress64 tsAddr;                //time sync requestor's address
        char tsNodeID[9];                    //time sync requestor node ID
};

baseXBee::baseXBee(void)
{
    coordAddr = XBeeAddress64(0x0, 0x0);
}


//process incoming traffic from the XBee
xbeeReadStatus_t baseXBee::read(void)
{
    uint8_t msrResponse, respLen, *atResponse, delyStatus, dscyStatus, txRetryCount;

    readPacket();
    if ( getResponse().isAvailable() ) {
        uint32_t ms = millis();
        char *p = &payload[0];

        switch (getResponse().getApiId()) {      //what kind of packet did we get?

        case ZB_RX_RESPONSE:                               //rx data packet
            getResponse().getZBRxResponse(zbRX);           //get the received data
            switch (zbRX.getOption() & 0x01) {             //check ack bit only
            case ZB_PACKET_ACKNOWLEDGED:
                for (uint8_t i=0; i<PAYLOAD_LEN; i++) {    //copy the received data to our buffer
                    *p++ = zbRX.getData(i);
                }
                *p = 0;                                    //put terminator on payload
                getRSS();                                  //get the received signal strength
                //process the received data
                Serial << endl << ms << F(" XB RX/ACK\n");
                switch (payload[0]) {                      //what type of packet
                case 'S':                                  //time sync request
                    queueTimeSync();
                    return RX_TIMESYNC;
                    break;
                case 'D':                                  //data headed for the web
                    strncpy(rxNodeID, &payload[1], 4);
                    rxNodeID[4] = 0;
                    return RX_DATA;
                    break;
                default:                                   //not expecting anything else
                    Serial << endl << ms << F(" XB unknown RX\n");
                    return RX_UNKNOWN;
                    break;
                }
                break;
            default:
                Serial << endl << ms << F(" XB RX no ACK\n");    //packet received and not ACKed
                return RX_NO_ACK;
                break;
            }
            break;

        case ZB_TX_STATUS_RESPONSE:                        //transmit status for packets we've sent
            getResponse().getZBTxStatusResponse(zbStat);
            delyStatus = zbStat.getDeliveryStatus();
            dscyStatus = zbStat.getDiscoveryStatus();
            txRetryCount = zbStat.getTxRetryCount();
            switch (delyStatus) {
            case SUCCESS:
                Serial << ms << F(" XB TX OK ") << ms - msTX << F("ms R=");
                Serial << txRetryCount << F(" DSCY=") << dscyStatus << endl;
                break;
            default:
                Serial << ms << F(" XB TX FAIL ") << ms - msTX << F("ms R=");
                Serial << txRetryCount << F(" DELY=") << delyStatus << F(" DSCY=") << dscyStatus << endl;
                break;
            }
            return TX_STATUS;
            break;

        case AT_COMMAND_RESPONSE:                          //response to NI command
            atResp = AtCommandResponse();
            getResponse().getAtCommandResponse(atResp);
            if (atResp.isOk()) {
                respLen = atResp.getValueLength();
                atResponse = atResp.getValue();
                for (int i=0; i<respLen; i++) {
                    nodeID[i] = atResponse[i];
                }
                nodeID[respLen] = 0;                       //assume 4-byte NI of the form XXnn
                txSec = atoi(&nodeID[2]);                  //use nn to determine this node's transmit time
                Serial << ms << F(" XB NI=") << nodeID << endl;
            }
            else {
                Serial << ms << F(" XB NI FAIL\n");
            }
            return COMMAND_RESPONSE;
            break;

        case MODEM_STATUS_RESPONSE:                        //XBee administrative messages
            getResponse().getModemStatusResponse(zbMSR);
            msrResponse = zbMSR.getStatus();
            Serial << ms << ' ';
            switch (msrResponse) {
            case HARDWARE_RESET:
                Serial << F("XB HW RST\n");
                break;
            case ASSOCIATED:
                Serial << F("XB ASC\n");
                break;
            case DISASSOCIATED:
                Serial << F("XB DISASC\n");
                break;
            default:
                Serial << F("XB MDM STAT 0x") << _HEX(msrResponse) << endl;
                break;
            }
            return MODEM_STATUS;
            break;

        default:                                           //something else we were not expecting
            Serial << F("XB UNEXP TYPE\n");                //unexpected frame type
            return UNKNOWN_FRAME;
            break;
        }
    }
    return NO_TRAFFIC;
}

//queue a time sync request
void baseXBee::queueTimeSync(void)
{
    if (tsNodeID[0] == 0) {                            //can only queue one request, ignore request if already have one queued
        tsAddr = zbRX.getRemoteAddress64();            //save the sender's address
        strncpy(tsNodeID, &payload[1], 4);             //save the sender's node ID
        tsNodeID[4] = 0;                               //terminator
    }
}

//respond to a previously queued time sync request
void baseXBee::sendTimeSync(time_t utc)
{
    if (tsNodeID[0] != 0) {                            //is there a request queued?
        //build the payload
        payload[0] = 'S';                              //time sync packet type
        strncpy(&payload[1], nodeID, 4);               //send our NI
        copyToBuffer(&payload[5], utc);                //send current UTC
        //build the tx request
        zbTX.setAddress64(tsAddr);                     //return to sender
        zbTX.setAddress16(0xFFFE);
        zbTX.setPayload((uint8_t*)payload);
        zbTX.setPayloadLength(9);
        send(zbTX);
        msTX = millis();
        Serial << endl << millis() << F(" Time sync ") << tsNodeID << endl;
        tsNodeID[0] = 0;                               //request was serviced, none queued
    }
}

//request the XBee's Node ID (AT NI command).
//Response is processed in read().
void baseXBee::reqNodeID(void)
{
    uint8_t atCmd[] = { 'N', 'I' };
    AtCommandRequest atCmdReq = AtCommandRequest(atCmd);
    send(atCmdReq);
    Serial << endl << millis() << F(" XB REQ NI\n");
}

//returns received signal strength value for the last RF data packet.
void baseXBee::getRSS(void)
{
    uint8_t atCmd[] = {'D', 'B'};
    AtCommandRequest atCmdReq = AtCommandRequest(atCmd);
    send(atCmdReq);
    if (readPacket(10)) {
        if (getResponse().getApiId() == AT_COMMAND_RESPONSE) {
            AtCommandResponse atResp;
            getResponse().getAtCommandResponse(atResp);
            if (atResp.isOk()) {
                uint8_t respLen = atResp.getValueLength();
                if (respLen == 1) {
                    uint8_t* resp = atResp.getValue();
                    rss = -resp[0];
                }
                else {
                    Serial << F("RSS LEN ERR\n");    //unexpected length
                }
            }
            else {
                Serial << F("RSS ERR\n");            //status not ok
            }
        }
        else {
            Serial << F("RSS UNEXP RESP\n");         //expecting AT_COMMAND_RESPONSE, got something else
        }
    }
    else {
        Serial << F("RSS NO RESP\n");                //timed out
    }
}

//Build & send an XBee data packet.
//Our data packet is defined as follows:
//Byte  0:    Packet type, D=data, T=Tweet
//Bytes 1-16: 16-character ThingSpeak write API key or
//            ThingTweet API key
//Bytes 17-n: (D packet) Data to be sent to ThingSpeak, in CSV
//            format, terminated by a zero byte. Note it is the remote
//            unit's responsibility to format the CSV data.
//Bytes 17-n: (T packet) Text of tweet, terminated by a zero byte.
//
//The maximum XBee packet size is set by XBEE_PAYLOAD_LEN in the main
//module. Note there is an upper limit, see the XBee ATNP command.
void baseXBee::sendData(char* data)
{
    payload[0] = 'D';                      //start with D header for a data packet
    payload[1] = 0;
    strcat(payload, nodeID);               //node ID next
    strcat(payload, data);                 //copy the CSV data
    zbTX.setAddress64(coordAddr);          //build the tx request packet
    zbTX.setAddress16(0xFFFE);
    zbTX.setPayload((uint8_t*)payload);
    zbTX.setPayloadLength(strlen(payload));
    send(zbTX);
    msTX = millis();
    Serial << endl << msTX << F(" XB TX\n");
}

//copy a four-byte integer to the designated offset in the buffer
void baseXBee::copyToBuffer(char* dest, uint32_t source)
{
    union charInt_t {
        char c[4];
        uint32_t i;
    } data;
    
    data.i = source;
    dest[0] = data.c[0];
    dest[1] = data.c[1];
    dest[2] = data.c[2];
    dest[3] = data.c[3];
}

//get a four-byte integer from the buffer starting at the designated offset
uint32_t baseXBee::getFromBuffer(char* source)
{
    union charInt_t {
        char c[4];
        uint32_t i;
    } data;

    data.c[0] = source[0];
    data.c[1] = source[1];
    data.c[2] = source[2];
    data.c[3] = source[3];

    return data.i;
}

#endif

