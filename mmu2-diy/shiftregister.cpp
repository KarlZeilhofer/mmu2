#include "shiftregister.h"

ShiftRegister::ShiftRegister(uint8_t nrOfBits, PinNr data, PinNr clock, PinNr latch)
	:N(nrOfBits)
{
	if(N > 32){
		N = 32;
	}
	if(N < 8){
		N = 8;
	}

	data = 0; // clear all bits by default
}

void ShiftRegister::writeBit(uint8_t bitNr, bool value)
{
	if(value){
		data |= (1<<bitNr);
	}else{
		data &= ~(1<<bitNr);
	}
}

