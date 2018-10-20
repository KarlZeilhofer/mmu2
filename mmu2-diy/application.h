#ifndef APPLICATION_H
#define APPLICATION_H

#include <Arduino.h>
#include "config.h"
#include "defs.h"


class Axis;


#ifdef DIY_BOARD

#define pinWrite digitalWrite

// added this pin as a debug pin (lights a green LED so I can see the 'C0' command in action
static const PinNr greenLED = 14;

// modified code on 10.2.18 to accomodate RAMPS 1.6 board mapping
//
static const PinNr idlerDirPin = A7;
static const PinNr idlerStepPin = A6;
static const PinNr idlerEnablePin = A2;



static const PinNr extruderDirPin = 48; //  pin 48 for extruder motor direction pin
static const PinNr extruderStepPin = 46; //  pin 48 for extruder motor stepper motor pin
static const PinNr extruderEnablePin = A8; //  pin A8 for extruder motor rst/sleep motor pin

static const PinNr colorSelectorDirPin =
    A1; //color selector stepper motor (driven by trapezoidal screw)
static const PinNr colorSelectorStepPin = A0;
static const PinNr colorSelectorEnablePin = 38;

static const PinNr findaPin = A3;
// this is pin D3 on the arduino MEGA 2650
static const PinNr chindaPin =
    3;       // this switch was added on 10.1.18 to help with filament loading (X- signal on the RAMPS board)

#endif // DIY_BOARD



#ifdef PRUSA_BOARD

// outputs:
static const PinNr greenLED = 0x100 + 7; // pin on shift register

static const PinNr idlerDirPin = 0x100 + 4;
static const PinNr idlerStepPin = 12; // Arduino D12
static const PinNr idlerEnablePin = 0x100 + 5;
static const PinNr idlerCsPin = 11; // Arduino D11

static const PinNr extruderDirPin = 0x100 + 0; // pin on shift register
static const PinNr extruderStepPin = 8; // Arduino D8
static const PinNr extruderEnablePin = 0x100 + 1; // pin on shift register
static const PinNr extruderCsPin = 5; // Arduino D5

static const PinNr colorSelectorDirPin = 0x100 + 2; // pin on shift register
static const PinNr colorSelectorStepPin = 4; // Arduino D4
static const PinNr colorSelectorEnablePin = 0x100 + 3; // pin on shift register
static const PinNr colorSelectorCsPin = 6; // Arduino D6

static const PinNr redLedPins[] = {0x100 + 7, 0x100 + 15, 0x100 + 13, 0x100 + 11, 0x100 + 9};
static const PinNr greenLedPins[] =   {0x100 + 6, 0x100 + 14, 0x100 + 12, 0x100 + 10, 0x100 + 8};

// inputs:
static const PinNr findaPin = A1;
static const PinNr chindaPin = 7; // Arduiono D7; pin 6 on connector P7
// chinda combines Chuck with Finda :)

#endif // DIY_BOARD


class Application
{
public:
    Application();

    void setup();
    void loop();

    Axis *axPulley;
    Axis *axIdler;
    Axis *axSelector;
};

#endif // APPLICATION_H

