#include <Arduino.h>
#include "axis.h"

Axis::Axis(PinNr enable, PinNr direcetion, PinNr step, PinNr chipSelect, uint16_t fullStepsPerRev,
           uint16_t resolution, uint16_t maxFullSteps)
    : enPin(enable), dirPin(direction), stepPin(step), csPin(chipSelect),
      fullStepsPerRev(fullStepsPerRev), resolution(resolution, maxFullSteps(maxFullSteps))
{
    setPinAsOutput(enPin);
    setPinAsOutput(dirPin);
    setPinAsOutput(stepPin);
    setPinAsOutput(csPin);

    pinWrite(enPin, DISABLE);
    pinWrite(dirPin, CW);
    pinWrite(stepPin, 0);
    pinWrite(csPin, LOW);
}

