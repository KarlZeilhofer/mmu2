#ifndef SHIFTREGISTER_H
#define SHIFTREGISTER_H

#include <stdint.h>

class ShiftRegister
{
public:
	ShiftRegister(uint8_t nrOfBits);

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
	 * @brief setData
	 * overwrite current data
	 * @param data
	 */
	void setData(uint32_t data);

	/**
	 * @brief writeData
	 * clocks current data out to the shift registers.
	 * MSB first.
	 */
	void writeData();

	/// Number of bits, usually 8, 16, 24 or 32 (max.)
	uint8_t N;
	uint32_t data;
};

#endif // SHIFTREGISTER_H
