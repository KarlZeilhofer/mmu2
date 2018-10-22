#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

#define MMU2_VERSION "0.5.0  2018-10-18"
#define FW_VERSION 50             // config.h  (MM-control-01 firmware)
#define FW_BUILDNR 85             // config.h  (MM-control-01 firmware)

//#define DEBUG
//#define DEBUGIDLER
//#define NOTDEF
//#define CRAZYIVAN
//#define TURNOFFSELECTORMOTOR

//#define DIY_BOARD
#define PRUSA_BOARD

#ifdef PRUSA_BOARD
//PAT9125 configuration
//#define PAT9125_SWI2C
#define PAT9125_HWI2C
#define PAT9125_I2C_ADDR  0x75  //ID=LO
//#define PAT9125_I2C_ADDR  0x79  //ID=HI
//#define PAT9125_I2C_ADDR  0x73  //ID=NC
#define PAT9125_XRES      0
#define PAT9125_YRES      240
#endif

//#define TEST_LEDs // successfully tested
//#define TEST_AXIS
#define TEST_FSENSOR

#endif // CONFIG_H





