#include <PubSubClient.h>
#include <Streaming.h>          // http://arduiniana.org/libraries/streaming/

// MQTT Mailer class to send emails via the mqttmail.py program.
// Derived from the PubSubClient class.

class MQTT_Mailer : public PubSubClient
{
    enum m_states_t {CONNECT, WAIT_CONNECT, WAIT, PUBLISH};
    public:
        MQTT_Mailer(Client& client, const char *clientID)
            : PubSubClient(client), m_connectRetry(10), m_clientID(clientID) {}
        void setTopic(const char *topic) {m_pubTopic = topic;}
        void sendmail(const char *to, const char *subj, const char *text);
        bool run();

    private:
        m_states_t m_state;
        uint32_t m_connectRetry;        // connect retry interval, seconds
        uint32_t m_msLastConnect;
        const char
            *m_clientID,                // unique ID required for each client
            *m_pubTopic,                // the topic to publish to
            *m_to,                      // email address
            *m_subj,                    // email subject
            *m_text;                    // email body text
        bool m_pubFlag;                 // ready to publish
};

void MQTT_Mailer::sendmail(const char *to, const char *subj, const char *text)
{
    m_to = to;
    m_subj = subj;
    m_text = text;
    m_pubFlag = true;
}

// run the state machine. returns true if connected to the broker.
bool MQTT_Mailer::run()
{
    switch(m_state) {
        case CONNECT:
            if (connect(m_clientID)) {
                m_state = WAIT;
                Serial << millis() << F(" Connected to MQTT broker\n");
            }
            else {
                m_state = WAIT_CONNECT;
                Serial << millis() << F(" Failed to connect to MQTT broker, rc=") << state() << endl;
                Serial << millis() << F(" Retry in ") << m_connectRetry << F(" seconds.\n");
                m_msLastConnect = millis();
            }
            break;

        case WAIT_CONNECT:
            if (millis() - m_msLastConnect >= m_connectRetry * 1000) {
                m_state = CONNECT;
            }
            break;

        case WAIT:
            if (connected()) {
                loop();
                if (m_pubFlag) {
                    m_state = PUBLISH;
                }
            }
            else {
                m_state = CONNECT;
                Serial << millis() << F(" Lost connection to MQTT broker\n");
            }
            break;

        case PUBLISH:
            m_state = WAIT;
            m_pubFlag = false;
            Serial << millis() << F(" MQTT publish: \"") << m_to << F("\",\"") << m_subj << F("\",\"") << m_text << '"' << endl;
            uint16_t lenPayload = strlen(m_to) + strlen(m_subj) + strlen(m_text) + 8;   // 6 quotes + 2 commas
            beginPublish(m_pubTopic, lenPayload, false);
            print('"');
            print(m_to);
            print("\",\"");
            print(m_subj);
            print("\",\"");
            print(m_text);
            print('"');
            endPublish();
            break;
    }
    return (m_state == WAIT || m_state == PUBLISH);
}
