#include <SPI.h>
#include "SSSS_SPI.h"

SparkfunSerialSevenSegmentSPI SSSS;

void SparkfunSerialSevenSegmentSPI::begin(uint8_t ssPin)
{
    _SS = ssPin;
    pinMode(_SS, OUTPUT);
    digitalWrite(_SS, HIGH);
    SPI.setBitOrder(MSBFIRST);
    SPI.setDataMode(SPI_MODE0);
//    SPI.setClockDivider(SPI_CLOCK_DIV128);    //spi clock freq = 125kHz
    SPI.setClockDivider(SPI_CLOCK_DIV64);     //spi clock freq = 250kHz
    SPI.begin();    //initialize SPI
}

void SparkfunSerialSevenSegmentSPI::reset()
{
    digitalWrite(_SS, LOW);
    SPI.transfer(0x76);
    digitalWrite(_SS, HIGH);
}

void SparkfunSerialSevenSegmentSPI::decimals(uint8_t mask)
{
    digitalWrite(_SS, LOW);
    SPI.transfer(0x77);
    SPI.transfer(mask);
    digitalWrite(_SS, HIGH);
}

void SparkfunSerialSevenSegmentSPI::brightness(uint8_t br)
{
    digitalWrite(_SS, LOW);
    SPI.transfer(0x7A);
    SPI.transfer(br);
    digitalWrite(_SS, HIGH);
}

void SparkfunSerialSevenSegmentSPI::baudrate(baudrate_t baud)
{
    digitalWrite(_SS, LOW);
    SPI.transfer(0x7F);
    SPI.transfer(baud);
    digitalWrite(_SS, HIGH);
}

void SparkfunSerialSevenSegmentSPI::dispInteger(int val)
{
    uint8_t d4 = val % 10;  val /= 10;
    uint8_t d3 = val % 10;  val /= 10;
    uint8_t d2 = val % 10;  val /= 10;
    uint8_t d1 = val;
    digitalWrite(_SS, LOW);
    SPI.transfer(d1);
    SPI.transfer(d2);
    SPI.transfer(d3);
    SPI.transfer(d4);
    digitalWrite(_SS, HIGH);

}
