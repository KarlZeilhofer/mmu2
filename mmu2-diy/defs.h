#ifndef DEFS_H
#define DEFS_H

#include "config.h"



#ifdef DIY_BOARD

typedef uint8_t PinNr;

#define SerialUI Serial // USB Serial
#define SerialPrinter Serial1 // 5V UART to Prusa MK3

#endif // DIY_BOARD





#ifdef PRUSA_BOARD

#define SerialUI Serial // USB Serial
#define SerialPrinter Serial1 // 5V UART to Prusa MK3

typedef uint16_t PinNr;

/**
 * @brief pinWrite
 * abstracts port pins and shift register pins into a single
 * space of pin numbers. Pins of shift register start from 0x100
 * @param pinNr
 */
void pinWrite(PinNr pinNr, bool value);


#endif // PRUSA_BOARD





#endif // DEFS_H

