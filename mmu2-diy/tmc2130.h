// tmc2130.h - Trinamic stepper driver
#ifndef _TMC2130_H
#define _TMC2130_H

#include <inttypes.h>
#include <stdbool.h>
#include "config.h"

#define TMC2130_SG_THR         4       // SG_THR default
#define TMC2130_TCOOLTHRS      450     // TCOOLTHRS default

#define TMC2130_CHECK_SPI 0x01
#define TMC2130_CHECK_MSC 0x02
#define TMC2130_CHECK_MOC 0x04
#define TMC2130_CHECK_STP 0x08
#define TMC2130_CHECK_DIR 0x10
#define TMC2130_CHECK_ENA 0x20
#define TMC2130_CHECK_OK  0x3f


class Tmc2130
{
public:
	enum Mode{HOMING_MODE, NORMAL_MODE, STEALTH_MODE};

	Tmc2130();
	int8_t init(uint8_t mode);

	int8_t init_axis(uint8_t mode);
	int8_t init_axis_current_normal(uint8_t current_h, uint8_t current_r);
	int8_t init_axis_current_stealth(uint8_t current_h, uint8_t current_r);
	void disable_axis(uint8_t mode);

	uint16_t read_sg();

private:
	int8_t wr_CHOPCONF(uint8_t toff, uint8_t hstrt, uint8_t hend, uint8_t fd3,
							   uint8_t disfdcc, uint8_t rndtf, uint8_t chm, uint8_t tbl, uint8_t vsense, uint8_t vhighfs,
							   uint8_t vhighchm, uint8_t sync, uint8_t mres, uint8_t intpol, uint8_t dedge, uint8_t diss2g);
	void wr_PWMCONF(uint8_t pwm_ampl, uint8_t pwm_grad, uint8_t pwm_freq,
							uint8_t pwm_auto, uint8_t pwm_symm, uint8_t freewheel);
	void wr_TPWMTHRS(uint32_t val32);
	int8_t setup_chopper(uint8_t mres, uint8_t current_h, uint8_t current_r);
	uint8_t mres();
	int8_t init_axis(Mode mode);
	inline void cs_high();
	inline void cs_low();


	void wr(uint8_t addr, uint32_t wval);
	uint8_t rd(uint8_t addr, uint32_t *rval);

public:
	uint16_t tCoolThrs;
	uint8_t stallGuardThreshold;
	uint16_t microStepsResolution;

	uint8_t current_running_normal;
	uint8_t current_running_stealth;
	uint8_t current_holding_normal;
	uint8_t current_holding_stealth;
	uint8_t current_homing;

	uint8_t csPin;
	uint8_t directionPin;
	uint8_t stepPin;
};

#endif //_TMC2130_H
