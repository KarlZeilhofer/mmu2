#ifndef AXIS_H
#define AXIS_H

#include <stdint.h>
#include "config.h"
#include "defs.h"

#define FULL_STEP       1u
#define HALF_STEP       2u
#define QUARTER_STEP    4u
#define EIGTH_STEP      8u
#define SIXTEENTH_STEP  16u

#define ENABLE LOW                // 8825 stepper motor enable is active low
#define DISABLE HIGH              // 8825 stepper motor disable is active high

#define CW 0
#define CCW 1

#define STEPSIZE SIXTEENTH_STEP    // setup for each of the three stepper motors (jumper settings for M0,M1,M2) on the RAMPS 1.x board
#define STEPSPERREVOLUTION 200     // 200 steps per revolution  - 1.8 degree motors are being used

#define PINHIGH 10                    // how long to hold stepper motor pin high in microseconds
#define PINLOW  10                    // how long to hold stepper motor pin low in microseconds

#define INSTRUCTION_DELAY 25          // delay (in microseconds) of the loop


class Axis
{
public:
    Axis(PinNr enable, PinNr direcetion, PinNr step, PinNr chipSelect,
         uint16_t fullStepsPerRev, uint16_t resolution, uint16_t maxFullSteps);

    PinNr enPin;
    PinNr dirPin;
    PinNr stepPin;
    PinNr csPin;

    uint16_t fullStepsPerRev;
    uint16_t resolution; // in microsteps per fullstep
    uint16_t microStepsPerRev;
    uint16_t maxFullSteps;
};

#endif // AXIS_H
