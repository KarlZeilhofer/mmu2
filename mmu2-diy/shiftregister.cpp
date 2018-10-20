#include <Arduino.h>
#include "shiftregister.h"

ShiftRegister::ShiftRegister(uint8_t nrOfBits, PinNr data, PinNr clock, PinNr latch)
    : N(nrOfBits), dataPin(data), clockPin(clock), latchPin(latch)
{
    if (N > 32) {
        N = 32;
    }
    if (N < 8) {
        N = 8;
    }

    this->data = 0xffffffff; // set all bits by default

    pinMode(data, OUTPUT);
    pinMode(clock, OUTPUT);
    pinMode(latch, OUTPUT);
}

void ShiftRegister::transferData()
{
    uint32_t mask = 1ul << (N - 1);

    digitalWrite(latchPin, 0);
    while (mask) {
        digitalWrite(clockPin, 0);
        if (mask & data) {
            digitalWrite(dataPin, 1);
        } else {
            digitalWrite(dataPin, 0);
        }
        digitalWrite(clockPin, 1);
        mask >>= 1;
    }
    digitalWrite(latchPin, 1);
}

void ShiftRegister::writeBit(uint8_t bitNr, bool value)
{
    if (value) {
        data |= (1 << bitNr);
    } else {
        data &= ~(1 << bitNr);
    }
}

