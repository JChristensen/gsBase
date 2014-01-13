#ifndef SSSS_SPI_h
#define SSSS_SPI_h

enum baudrate_t {BAUD_2400, BAUD_4800, BAUD_9600, BAUD_14400, BAUD_19200, BAUD_38400, BAUD_57600};

class SparkfunSerialSevenSegmentSPI
{
    public:
        void begin(uint8_t ssPin);
        void reset();
        void decimals(uint8_t mask);
        void brightness(uint8_t br);
        void dispInteger(int val);
        void baudrate(baudrate_t baud);
        
    private:
        uint8_t _SS;    //slave select pin
};

extern SparkfunSerialSevenSegmentSPI SSSS;

#endif
