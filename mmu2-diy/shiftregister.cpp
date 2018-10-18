#include "shiftregister.h"

ShiftRegister::ShiftRegister(uint8_t nrOfBits)
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

