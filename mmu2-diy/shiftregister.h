#ifndef SHIFTREGISTER_H
#define SHIFTREGISTER_H

#include <stdint.h>
#include "defs.h"

class ShiftRegister
{
public:
    ShiftRegister(uint8_t nrOfBits, PinNr data, PinNr clock, PinNr latch);

    /**
     * @brief setBits
     * sets all bits in current data to one, where mask has a 1.
     * @param mask
     */
    void setBits(uint32_t mask);

    /**
     * @brief clearBits
     * sets all bits in current data to zero, where mask has a 1.
     * @param mask
     */
    void clearBits(uint32_t mask);

    /**
     * @brief writeData
     * overwrite current data
     * @param data
     */
    void writeData(uint32_t data);

    /**
     * @brief transferData
     * clocks current data out to the shift registers.
     * MSB first.
     */
    void transferData();

    /**
     * @brief writeBit
     * Writes a single bit to the data in RAM
     * @param bitNr starting from 0 until N-1
     * @param value: 0 or 1
     */
    void writeBit(uint8_t bitNr, bool value);

    /// Number of bits, usually 8, 16, 24 or 32 (max.)
    uint8_t N;
    uint32_t data;
    PinNr dataPin;
    PinNr clockPin;
    PinNr latchPin;
};

#endif // SHIFTREGISTER_H
