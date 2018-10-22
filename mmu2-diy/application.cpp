// CSK MMU2 Controller Version
//
//  Code developed by Chuck Kozlowski
//  September 19, 2018
//
//  Code was developed because I am impatiently waiting for my MMU2 to arrive (due in December, 2018) so I thought
//  I would develop some code to operate the PRUSA MMU2 hardware
//
// This code uses 3 stepper motor controllers and 1 Pinda filament sensor, and 1 additional filament sensor on the mk3 extruder top
//
//
//  Work to be done:  Interface Control with the Einsy Board (MK3) - (work completed on 9.25.18)
//                    Refine speed and acceleration settings for each stepper motor
//                    Failure Recovery Modes - basically non-existent (work completed on 10.5.18)
//
//                    Uses the serial interface with a host computer at the moment - probably could do some smarter things
//                                                                                   like selection switches and some LEDs.
//                   10.14.18 Leave the Selector Stepper Motor ON ... appear to be losing position with the selector during operation
//                            [now using the J-4218HB2301 Stepper Motor - (45 N-cm torque) as the idler stepper motor]
//                   10.14.18 Added yet another idler command set (specialparkidler() and specialunparkidler) - used within the 'C' Command
//                            Idler management is a bit of challenge,  probably has more to do with my coding skills (or lack thereof).
//                   10.12.18 Minor fix to idler parking ... now use quickparkidler() after 'C' command and quickunparkidler() at beginning of 'T' command
//                              This helps to speed up the idler movements during 'T' and 'C' commands
//                   10.5.18  Made major tweak to the 'C' command, now matches the speed of the mk3 extruder gear (see slic3r 'load' setting)
//                   10.2.18  Moved from breadboard to RAMPS 1.6 Board and remapped ALL addresses
//                            Discovered the filament idler motor needed to be set at a higher torque (more current)
//                            (this was affected filament load consistency)
//                   10.2.18  Major Disaster, lost my codebase on my PC (I am an idiot)
//                            Thank God for github so I could recover a week old version of my code
//                   10.1.18  Added filament sensor to the extruder head (helps reliability


//#include <SoftwareSerialUI.h>
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "application.h"
#include "config.h"
#include "axis.h"
#include "pat9125.h"

#ifdef PRUSA_BOARD
#include "shiftregister.h"
#endif

static int isFilamentLoaded();
static void initIdlerPosition();
static void checkSerialInterface();
static void initColorSelector();
static void filamentLoadWithBondTechGear();
static void toolChange( char selection);
static void quickUnParkIdler();
static void unParkIdler();
static void unloadFilamentToFinda();
static void parkIdler();
static void activateColorSelector();
static void idlerSelector(char filament);
static void colorSelector(char selection);
static void loadFilamentToFinda();
static void fixTheProblem(String statement);
static void csTurnAmount(int steps, int direction);
static void feedFilament(unsigned int steps);


#define SERIAL1ENABLED    1


#define STEPSPERMM  144ul           // these are the number of steps required to travel 1 mm using the extruder motor


#define ORIGINALCODE 0            // code that is no longer needed for operational use
int command = 0;

// changed from 125 to 115 (10.13.18)
#define MAX_IDLER_STEPS 125         // number of steps that the roller bearing stepper motor can travel, in full steps
#define MAXSELECTOR_STEPS   1890   // maximum number of selector stepper motor (used to move all the way to the right or left, in full steps



#define MMU2TOEXTRUDERSTEPS STEPSIZE*STEPSPERREVOLUTION*19   // for the 'T' command 


#define INACTIVE 0                           // used for 3 states of the idler stepper motor (parked)
#define ACTIVE 1                             // not parked 
#define QUICKPARKED 2                            // quick parked


//************************************************************************************
//* this resets the selector stepper motor after the selected number of tool changes
//* changed from 25 to 10 (10.10.18)
//* chagned from 10 to 8 (10.14.18)
//*************************************************************************************
#define TOOLSYNC 20                         // number of tool change (T) commands before a selector resync is performed



// the MMU2 currently runs at 21mm/sec (set by Slic3r) for 2 seconds (good stuff to know)
//
// the load duration was chagned from 1 second to 1.1 seconds on 10.8.18 (as an experiment)
// increased from 1.1 to 1.5 seconds on 10.13.18 (another experiment)
#define LOAD_DURATION 1600                 // duration of 'C' command during the load process (in milliseconds)


// changed from 21 mm/sec to 30 mm/sec on 10.13.18
#define LOAD_SPEED 30                   // load speed (in mm/second) during the 'C' command (determined by Slic3r setting)



#define IDLERSTEPSIZE 23         // full steps to each roller bearing
//float bearingAbsPos[5] = {1, 24, 48, 72, 96}; // absolute position of roller bearing stepper motor
float bearingAbsPos[5] = {0, IDLERSTEPSIZE, IDLERSTEPSIZE * 2, IDLERSTEPSIZE * 3, IDLERSTEPSIZE * 4};



// changed position #2 to 372  (still tuning this little sucker)


#define CSSTEPS 357                        //                                                 
int selectorAbsPos[5] = {0, CSSTEPS * 1, CSSTEPS * 2, CSSTEPS * 3, CSSTEPS * 4}; // absolute position of selector stepper motor


int trackToolChanges = 0;
int extruderMotorStatus = INACTIVE;


int currentCSPosition = 0;         // color selector position
int currentPosition = 0;

int repeatTCmdFlag =
    INACTIVE;    // used by the 'C' command processor to avoid processing multiple 'C' commands

int oldBearingPosition = 0;      // this tracks the roller bearing position (top motor on the MMU)
int filamentSelection = 0;       // keep track of filament selection (0,1,2,3,4))
int dummy[100];
char currentExtruder = '0';

int firstTimeFlag = 0;
int earlyCommands = 0;           // forcing communications with the mk3 at startup

int toolChangeCount = 0;

char receivedChar;
boolean newData = false;
int idlerStatus = INACTIVE;
int colorSelectorStatus = INACTIVE;

#ifdef PRUSA_BOARD
ShiftRegister extPins(16, 9, 13, 10);
#endif

//*************************************************************************************************
//  Delay values for each stepper motor
//*************************************************************************************************
#define IDLERMOTORDELAY  540     //540 useconds      (idler motor)  was at '500' on 10.13.18
#define EXTRUDERMOTORDELAY 50     // 150 useconds    (controls filament feed speed to the printer)
#define COLORSELECTORMOTORDELAY 60 // 60 useconds    (selector motor)

//SoftwareSerial Serial1(10,11); // RX, TX (communicates with the MK3 controller board

// TODO 3: user arrays:
int f0Min = 1000, f1Min = 1000, f2Min = 1000, f3Min = 1000, f4Min = 1000;
int f0Max, f1Max, f2Max, f3Max, f4Max = 0;
int f0Avg, f1Avg, f2Avg, f3Avg, f4Avg;
long f0Distance, f1Distance, f2Distance, f3Distance,
     f4Distance = 0;              // changed from int to long type 10.5.18
int f0ToolChange, f1ToolChange, f2ToolChange, f3ToolChange, f4ToolChange = 0;

unsigned long time0, time1, time2, time3, time4, time5;
unsigned long timeCStart, timeCEnd;

#ifdef TEST_LEDs
void testLeds()
{
    while (1) {
        for (int i = 0; i < 10; i++) {
            if (i % 2 == 0) {
                pinWrite(redLedPins[i / 2], 1);
                delay(100);
                pinWrite(redLedPins[i / 2], 0);
                delay(300);
            } else {
                pinWrite(greenLedPins[i / 2], 1);
                delay(100);
                pinWrite(greenLedPins[i / 2], 0);
                delay(300);
            }
        }
    }
}

#endif

#ifdef TEST_AXIS
void testAxis()
{

}
#endif

#ifdef TEST_FSENSOR
void testFilamentSensor()
{
    while (1) {
        if (pat9125_update()) {
            SerialUI.println(pat9125_y);
        } else {
            SerialUI.println(F("No Sensor"));
        }
        delay(100);
    }
}
#endif


void Application::setup()
{
    SerialUI.begin(500000);  // startup the local serial interface (changed to 2 Mbaud on 10.7.18
    SerialUI.begin(115200);  // Karl set to 115200 baud
    while (!SerialUI) {
        ; // wait for serial port to connect. needed for native USB port only
        SerialUI.println(F("waiting for serial port"));
    }

    SerialUI.println(MMU2_VERSION);



#ifdef TEST_LEDs
    testLeds();
#endif

#ifdef TEST_AXIS
    axIdler = new Axis(idlerEnablePin, idlerDirPin, idlerStepPin, idlerCsPin, 200, 16, MAX_IDLER_STEPS);
    axSelector = new Axis(colorSelectorEnablePin, colorSelectorDirPin, colorSelectorStepPin,
                          colorSelectorCsPin, 200, 2, 1850);
    axPulley = new Axis(extruderEnablePin, extruderDirPin, extruderStepPin, extruderCsPin, 200, 2, 0);
    // TODO 0: hier gehts weiter!

    testAxis();
#endif

#ifdef TEST_FSENSOR
	bool initSuccess = false;
	for(int i=0; i<10000; i++){
		if( (initSuccess = pat9125_init()) ) {
			break;
		}
		SerialUI.print(F("pat9125_init() "));
		SerialUI.println(i);
	}
	if(!initSuccess){
		SerialUI.println(F("pat9125_init() failed - HALT"));
        while (1);
	}
	
    testFilamentSensor();
#endif


    // static int findaStatus;

    int waitCount;


    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // THIS DELAY IS CRITICAL DURING POWER UP/RESET TO PROPERLY SYNC WITH THE MK3 CONTROLLER BOARD
    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    delay(4000);                    // this is key to syncing to the MK3 controller - currently 4 seconds


    SerialPrinter.begin(115200);         // startup the mk3 serial
    // SerialPrinter.begin(115200;              // ATMEGA hardware serial interface

    //SerialUI.println(F("started the mk3 serial interface"));
    delay(100);


    SerialUI.println(F("Sending START command to mk3 controller board"));
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // THIS NEXT COMMAND IS CRITICAL ... IT TELLS THE MK3 controller that an MMU is present
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    SerialPrinter.print(
        F("start\n"));                 // attempt to tell the mk3 that the mmu is present

    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    //  check the serial interface to see if it is active
    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    waitCount = 0;
    while (!SerialPrinter.available()) {

        //delay(100);
        SerialUI.println(F("Waiting for message from mk3"));
        delay(1000);
        ++waitCount;
        if (waitCount >= 7) {
            SerialUI.println(F("7 seconds have passed, aborting wait for mk3 to respond"));
            goto continue_processing;
        }
    }
    SerialUI.println(F("inbound message from mk3"));

continue_processing:


#ifdef DIY_BOARD
    pinMode(idlerDirPin, OUTPUT);
    pinMode(idlerStepPin, OUTPUT);

    pinMode(findaPin, INPUT);                        // pinda Filament sensor
    pinMode(chindaPin, INPUT);

    pinMode(idlerEnablePin, OUTPUT);
    // pinMode(bearingRstPin, OUTPUT);

    pinMode(extruderEnablePin, OUTPUT);
    pinMode(extruderDirPin, OUTPUT);
    pinMode(extruderStepPin, OUTPUT);

    pinMode(colorSelectorEnablePin, OUTPUT);
    pinMode(colorSelectorDirPin, OUTPUT);
    pinMode(colorSelectorStepPin, OUTPUT);

    pinMode(greenLED, OUTPUT);                         // green LED used for debug purposes
#endif // DIY_BOARD

#ifdef PRUSA_BOARD

#endif

    SerialUI.println(F("finished setting up input and output pins"));



    // Turn on all three stepper motors
    pinWrite(idlerEnablePin, ENABLE);           // enable the roller bearing motor (motor #1)
    pinWrite(extruderEnablePin, ENABLE);        //  enable the extruder motor  (motor #2)
    pinWrite(colorSelectorEnablePin, ENABLE);  // enable the color selector motor  (motor #3)





    // moved these inits to the loop() section since the mk3 serial interface needs to be handled
    //

#ifdef NOTDEF
    if (isFilamentLoaded()) {               // check to see if filament in the bowden tube (between the mmu2 and mk3
        SerialUI.println(F("Filament was in the bowden tube at startup, unloading filament automatically"));
        unloadFilamentToFinda();            //
    }
#endif

    SerialUI.println(
        F("Syncing the Idler Selector Assembly"));             // do this before moving the selector motor
    initIdlerPosition();    // reset the roller bearing position


    SerialUI.println(F("Syncing the Filament Selector Assembly"));
    if (!isFilamentLoaded()) {
        initColorSelector();   // reset the color selector if there is NO filament present
    } else {
        SerialUI.println(F("Unable to clear the Color Selector, please remove filament"));
    }

    SerialUI.println(F("Inialialization Complete, let's multicolor print ...."));



}  // end of init() routine


// infinite loop - core of the program

void Application::loop()
{
    String kbString;


    // SerialUI.println(F("looping"));
    delay(100);                       // wait for 100 milliseconds
    checkSerialInterface();           // check the serial interface for input commands from the mk3
#ifdef NOTDEF
    while (1) {
        int fstatus;

        fstatus = digitalRead(chindaPin);
        SerialUI.print(F("Filament Status: "));
        SerialUI.println(fstatus);
        delay(1000);
    }
#endif

    // SerialUI.println(F("Enter Filament Selection (1-5),Disengage Roller (D), Load Filament (L), Unload Filament (U), Test Color Extruder(T)"));
    //SerialUI.print(F("FINDA status: "));
    //fstatus = digitalRead(findaPin);
    //SerialUI.println(fstatus);


    // check for keyboard input

    if (SerialUI.available()) {
        SerialUI.print(F("Key was hit "));
        //c1 = SerialUI.read();
        //c2 = SerialUI.read();
        //c3 = SerialUI.read();

        kbString = SerialUI.readString();
        // SerialUI.print(c1); SerialUI.print(F(" ")); SerialUI.println(F("c2"));
        SerialUI.print(kbString);

        if (kbString[0] == 'C') {
            //if (c1 == 'C') {
            SerialUI.println(F("Processing 'C' Command"));
            filamentLoadWithBondTechGear();

            //filamentLoadToMK3();
        }
        if (kbString[0] == 'T') {
            //if (c1 == 'T') {
            SerialUI.println(F("Processing 'T' Command"));
            toolChange(kbString[1]);                 // invoke the tool change command
            //toolChange(c2);
            // processKeyboardInput();
        }
        if (kbString[0] == 'U') {
            SerialUI.println(F("Processing 'U' Command"));

            // parkIdler();                      // reset the idler               // added on 10.7.18 ... get in known state


            if (idlerStatus == QUICKPARKED) {
                quickUnParkIdler();             // un-park the idler from a quick park
            }
            if (idlerStatus == INACTIVE) {
                unParkIdler();                    // turn on the idler motor
            }
            unloadFilamentToFinda();          //unload the filament

            parkIdler();                      // park the idler motor and turn it off
        }
    }



}   // end of infinite loop



// need to check the PINDA status

void checkSerialInterface()
{
    int cnt;
    String inputLine;
    int findaStatus;
    int index;


    // SerialUI.println(F("Waiting for communication with mk3"));

    // while (earlyCommands == 0) {
    // SerialUI.println(F("waiting for response from mk3"));
    index = 0;
    if ((cnt = SerialPrinter.available()) > 0) {

        //SerialUI.print(F("chars received: "));
        //SerialUI.println(cnt);

        inputLine =
            SerialPrinter.readString();      // fetch the command from the mmu2 serial input interface

        if (inputLine[0] != 'P') {
            SerialUI.print(F("MMU Command: "));
            SerialUI.println(inputLine);
        }
process_more_commands:  // parse the inbound command
        unsigned char c1, c2;

        c1 = inputLine[index++];                      // fetch single characer from the input line
        c2 = inputLine[index++];                      // fetch 2nd character from the input line
        inputLine[index++];                      // carriage return


        // process commands coming from the mk3 controller
        //***********************************************************************************
        // Commands still to be implemented:  X0 (MMU Reset), F0 (Filament type select),
        // E0->E4 (Eject Filament), R0 (recover from eject)
        //***********************************************************************************
        switch (c1) {
        case 'T':
            // request for idler and selector based on filament number
            time4 = millis();           // grab the current time

            if ((c2 >= '0')  && (c2 <= '4')) {
                toolChange(c2);

            } else {
                SerialUI.println(F("T: Invalid filament Selection"));
            }

            // delay(200);                      //removed this 200msec delay on 10.5.18
            SerialPrinter.print(F("ok\n"));              // send command acknowledge back to mk3 controller
            time5 = millis();          // grab the current time
            break;
        case 'C':
            // move filament from selector ALL the way to printhead
#ifdef NOTDEF
            SerialUI.println(F("C: Moving filament to Bondtech gears"));
#endif
            // filamentLoadToMK3();
            filamentLoadWithBondTechGear();
            // delay(200);
            SerialPrinter.print(F("ok\n"));
            break;

        case 'U':
            // request for filament unload

            SerialUI.println(F("U: Filament Unload Selected"));
            //*******************************************************************************************************
            //*  FIX:  don't go all the way to the end ... be smarter
            //******************************************************************************************************
            //* unparking is more elegant 10.12.18
            if (idlerStatus == QUICKPARKED) {
                quickUnParkIdler();             // un-park the idler from a quick park
            }
            if (idlerStatus == INACTIVE) {
                unParkIdler();                    // turn on the idler motor
            }

            if ((c2 >= '0') && (c2 <= '4')) {

                unloadFilamentToFinda();
                parkIdler();
                SerialUI.println(F("U: Sending Filament Unload Acknowledge to MK3"));
                delay(200);
                SerialPrinter.print(F("ok\n"));

            } else {
                SerialUI.println(F("U: Invalid filament Unload Requested"));
                delay(200);
                SerialPrinter.print(F("ok\n"));
            }
            break;
        case 'L':
            // request for filament load
            SerialUI.println(F("L: Filament Load Selected"));
            if (idlerStatus == QUICKPARKED) {
                quickUnParkIdler();             // un-park the idler from a quick park
            }
            if (idlerStatus == INACTIVE) {
                unParkIdler();                    // turn on the idler motor
            }


            if (colorSelectorStatus == INACTIVE) {
                activateColorSelector();    // turn on the color selector motor
            }

            if ((c2 >= '0') && (c2 <= '4')) {

                SerialUI.println(F("L: Moving the bearing idler"));
                idlerSelector(c2);   // move the filament selector stepper motor to the right spot
                SerialUI.println(F("L: Moving the color selector"));
                colorSelector(c2);     // move the color Selector stepper Motor to the right spot
                SerialUI.println(F("L: Loading the Filament"));
                // loadFilament(CCW);
                loadFilamentToFinda();
                parkIdler();             // turn off the idler roller

                SerialUI.println(F("L: Sending Filament Load Acknowledge to MK3"));

                delay(200);

                SerialPrinter.print(F("ok\n"));



            } else {
                SerialUI.println(F("Error: Invalid Filament Number Selected"));
            }
            break;

        case 'S':
            // request for firmware version
            // SerialUI.println(F("S Command received from MK3"));
            // this is a serious hack since the serial interface is flaky at this point in time
#ifdef NOTDEF
            if (command == 1) {
                SerialUI.println(F("S: Processing S2"));
                SerialPrinter.print(FW_BUILDNR);
                SerialPrinter.print(F("ok\n"));

                command++;

            }
            if (command == 0) {
                SerialUI.println(F("S: Processing S1"));
                SerialPrinter.print(FW_VERSION);
                SerialPrinter.print(F("ok\n"));

                command++;
            }
#endif

            switch (c2) {
            case '0':
                SerialUI.println(F("S: Sending back OK to MK3"));
                SerialPrinter.print(F("ok\n"));
                break;
            case '1':
                SerialUI.println(F("S: FW Version Request"));
                SerialPrinter.print(FW_VERSION);
                SerialPrinter.print(F("ok\n"));
                break;
            case '2':
                SerialUI.println(F("S: Build Number Request"));
                SerialUI.println(F("Initial Communication with MK3 Controller: Successful"));
                SerialPrinter.print(FW_BUILDNR);
                SerialPrinter.print(F("ok\n"));
                break;
            default:
                SerialUI.println(F("S: Unable to process S Command"));
                break;
            }  // switch(c2) check
            break;
        case 'P':

            // check FINDA status
            // SerialUI.println(F("Check FINDA Status Request"));
            findaStatus = digitalRead(findaPin);
            if (findaStatus == 0) {
                // SerialUI.println(F("P: FINDA INACTIVE"));
                SerialPrinter.print(F("0"));
            } else {
                // SerialUI.println(F("P: FINDA ACTIVE"));
                SerialPrinter.print(F("1"));
            }
            SerialPrinter.print(F("ok\n"));

            break;
        case 'F':                                         // 'F' command is acknowledged but no processing goes on at the moment
            // will be useful for flexible material down the road
            SerialUI.println(F("Filament Type Selected: "));
            SerialUI.println(c2);
            SerialPrinter.print(F("ok\n"));                        // send back OK to the mk3
            break;
        default:
            SerialUI.print(F("ERROR: unrecognized command from the MK3 controller"));
            SerialPrinter.print(F("ok\n"));


        }  // end of switch statement
#ifdef NOTDEF
        if (cnt != 3) {

            SerialUI.print(F("Index: "));
            SerialUI.print(index);
            SerialUI.print(F(" cnt: "));
            SerialUI.println(cnt);
        }
#endif
    }  // end of cnt > 0 check

    if (index < cnt) {
#ifdef NOTDEF
        SerialUI.println(F("More commands in the buffer"));
#endif

        goto process_more_commands;
    }
    // }  // check for early commands

}


void colorSelector(char selection)
{

    int findaStatus;

    // this error check was added on 10.4.18

    if ((selection < '0') || (selection > '4')) {
        SerialUI.println(F("colorSelector():  Error, invalid filament selection"));
        return;
    }

    // SerialUI.println(F("Entering colorSelector() routine"));

loop:
    findaStatus = digitalRead(
                      findaPin);    // check the pinda status ( DO NOT MOVE THE COLOR SELECTOR if filament is present)
    if (findaStatus == 1) {
        fixTheProblem("colorSelector(): Error, filament is present between the MMU2 and the MK3 Extruder:  UNLOAD FILAMENT!!!");
        goto loop;
    }



    switch (selection) {
    case '0':                                       // position '0' is always just a move to the left
        // added the '+10' on 10.5.18 (force selector carriage all the way to the left
        csTurnAmount(currentPosition + 10,
                     CCW);       // the '+10' is an attempt to move the selector ALL the way left (puts the selector into known position)
        currentPosition = selectorAbsPos[0];
        break;
    case '1':
        if (currentPosition <= selectorAbsPos[1]) {
            csTurnAmount((selectorAbsPos[1] - currentPosition), CW);
        } else {
            csTurnAmount((currentPosition - selectorAbsPos[1]), CCW);
        }
        currentPosition = selectorAbsPos[1];
        break;
    case '2':
        if (currentPosition <= selectorAbsPos[2]) {
            csTurnAmount((selectorAbsPos[2] - currentPosition), CW);
        } else {
            csTurnAmount((currentPosition - selectorAbsPos[2]), CCW);

        }
        currentPosition = selectorAbsPos[2];
        break;
    case '3':
        if (currentPosition <= selectorAbsPos[3]) {
            csTurnAmount((selectorAbsPos[3] - currentPosition), CW);
        } else {
            csTurnAmount((currentPosition - selectorAbsPos[3]), CCW);

        }
        currentPosition = selectorAbsPos[3];
        break;
    case '4':
        if (currentPosition <= selectorAbsPos[4]) {
            csTurnAmount((selectorAbsPos[4] - currentPosition), CW);
        } else {
            csTurnAmount((currentPosition - selectorAbsPos[4]), CCW);

        }
        currentPosition = selectorAbsPos[4];
        break;

    }



}  // end of colorSelector routine()

//****************************************************************************************************
//* this routine is the common routine called for fixing the filament issues (loading or unloading)
//****************************************************************************************************
void fixTheProblem(String statement)
{
    SerialUI.println(F(""));
    SerialUI.println(F("********************* ERROR ************************"));
    SerialUI.println(statement);       // report the error to the user
    SerialUI.println(F("********************* ERROR ************************"));
    SerialUI.println(F("Clear the problem and then hit any key to continue "));
    SerialUI.println(F(""));

    parkIdler();                                    // park the idler stepper motor
    pinWrite(colorSelectorEnablePin, DISABLE);  // turn off the selector stepper motor

    //quickParkIdler();                   // move the idler out of the way
    // specialParkIdler();

    while (!SerialUI.available()) {
        //  wait until key is entered to proceed  (this is to allow for operator intervention)
    }
    SerialUI.readString();  // clear the keyboard buffer

    unParkIdler();                             // put the idler stepper motor back to its' original position
    pinWrite(colorSelectorEnablePin, ENABLE);  // turn ON the selector stepper motor
    delay(1);                                  // wait for 1 millisecond

    //specialUnParkIdler();
    //unParkIdler();
    //quickUnParkIdler();                 // re-enage the idler
}


// this is the selector motor with the lead screw (final stage of the MMU2 unit)

void csTurnAmount(int steps, int direction)
{
    pinWrite(colorSelectorEnablePin, ENABLE );    // turn on the color selector motor
    // delayMicroseconds(1500);                                       // wait for 1.5 milliseconds          added on 10.4.18

    if (direction == CW) {
        pinWrite(colorSelectorDirPin, LOW);    // set the direction for the Color Extruder Stepper Motor
    } else {
        pinWrite(colorSelectorDirPin, HIGH);
    }
    // wait 1 milliseconds
    delayMicroseconds(
        1500);                      // changed from 500 to 1000 microseconds on 10.6.18, changed to 1500 on 10.7.18)

#ifdef DEBUG
    int scount;

    SerialUI.print(F("raw steps: "));
    SerialUI.println(steps);

    scount = steps * STEPSIZE;
    SerialUI.print(F("total number of steps: "));
    SerialUI.println(scount);
#endif

    for (uint16_t i = 0; i <= (steps * STEPSIZE);
            i++) {                      // fixed this to '<=' from '<' on 10.5.18
        pinWrite(colorSelectorStepPin, HIGH);
        delayMicroseconds(PINHIGH);               // delay for 10 useconds
        pinWrite(colorSelectorStepPin, LOW);
        delayMicroseconds(PINLOW);               // delay for 10 useconds  (added back in on 10.8.2018)
        delayMicroseconds(COLORSELECTORMOTORDELAY);         // wait for 400 useconds
    }

#ifdef TURNOFFSELECTORMOTOR                         // added on 10.14.18
    pinWrite(colorSelectorEnablePin, DISABLE);    // turn off the color selector motor
#endif

}





// test code snippet for moving a stepper motor
//  (not used operationally)
void completeRevolution()
{
    for (uint16_t i = 0; i < STEPSPERREVOLUTION * STEPSIZE; i++) {
        pinWrite(idlerStepPin, HIGH);
        delayMicroseconds(PINHIGH);               // delay for 10 useconds
        pinWrite(idlerStepPin, LOW);
        delayMicroseconds(PINLOW);               // delay for 10 useconds

        delayMicroseconds(IDLERMOTORDELAY);
        //delayValue = 64/stepSize;
        //delay(delayValue);           // wait for 30 milliseconds
    }
}

//
// turn the idler stepper motor
//
void idlerturnamount(int steps, int dir)
{
#ifdef NOTDEF
    SerialUI.println(F("moving the idler ..."));
    SerialUI.print(F("steps: "));
    SerialUI.print(steps);
    SerialUI.print(F("dir: "));
    SerialUI.println(dir);
#endif

    pinWrite(idlerEnablePin, ENABLE);   // turn on motor
    pinWrite(idlerDirPin, dir);
    delay(1);                               // wait for 1 millisecond

    // pinWrite(ledPin, HIGH);

    //pinWrite(idlerDirPin, dir);
    //delay(1);                               // wait for 1 millsecond

    // these command actually move the IDLER stepper motor
    //
    for (uint16_t i = 0; i < steps * STEPSIZE; i++) {
        pinWrite(idlerStepPin, HIGH);
        delayMicroseconds(PINHIGH);               // delay for 10 useconds
        pinWrite(idlerStepPin, LOW);
        //delayMicroseconds(PINLOW);               // delay for 10 useconds (removed on 10.7.18

        delayMicroseconds(IDLERMOTORDELAY);
    }
#ifdef NOTDEF
    SerialUI.println(F("finished moving the idler ..."));
#endif

}  // end of idlerturnamount() routine


// turns on the extruder motor
void loadFilamentToFinda()
{
    int findaStatus;
    unsigned long startTime, currentTime;

    pinWrite(extruderEnablePin, ENABLE);  // added on 10.14.18
    pinWrite(extruderDirPin, CCW);  // set the direction of the MMU2 extruder motor
    delay(1);

    startTime = millis();

loop:
    currentTime = millis();
    if ((currentTime - startTime) >
            10000) {         // 10 seconds worth of trying to unload the filament
        fixTheProblem("UNLOAD FILAMENT ERROR:   timeout error, filament is not unloading past the FINDA sensor");
        startTime = millis();   // reset the start time clock
    }
    // changed this on 10.12.18 to step 1 mm instead of a single step at a time

    // feedFilament(1);        // 1 step and then check the pinda status
    feedFilament(STEPSPERMM);  // go 144 steps (1 mm) and then check the finda status

    findaStatus = digitalRead(findaPin);
    if (findaStatus == 0) {            // keep feeding the filament until the pinda sensor triggers
        goto loop;
    }

#ifdef NOTDEF
    SerialUI.println(F("Pinda Sensor Triggered during Filament Load"));
#endif
    //
    // for a filament load ... need to get the filament out of the selector head !!!
    //
    pinWrite(extruderDirPin, CW);   // back the filament away from the selector

#ifdef NOTDEF
    unsigned int steps;
    steps = 200 * STEPSIZE + 50;
    feedFilament(steps);
#endif

    feedFilament(STEPSPERMM * 23);      // after hitting the FINDA sensor, back away by 23 mm
#ifdef NOTDEF
    SerialUI.println(F("Loading Filament Complete ..."));
#endif

    // pinWrite(ledPin, LOW);     // turn off LED
}

//*********************************************************************************************
// unload Filament using the FINDA sensor
// turns on the extruder motor
//*********************************************************************************************
void unloadFilamentToFinda()
{
    unsigned long startTime, currentTime, startTime1;
    int fStatus;

    if (!isFilamentLoaded()) {               // if the filament is already unloaded, do nothing

        SerialUI.println(F("unloadFilamentToFinda():  filament already unloaded"));
        return;
    }

    pinWrite(extruderEnablePin, ENABLE);  // turn on the extruder motor
    pinWrite(extruderDirPin, CW);  // set the direction of the MMU2 extruder motor
    delay(1);

    startTime = millis();
    startTime1 = millis();

loop:

    currentTime = millis();

    //************************************************************************************************************
    //* added filament sensor status check (10.14.18)
    //************************************************************************************************************

    fStatus = digitalRead(
                  chindaPin);          // read the filament switch (on the top of the mk3 extruder)

    if (fStatus ==
            0) {                             // filament Switch is still ON, check for timeout condition

        if ((currentTime - startTime1) > 2000) {  // has 2 seconds gone by ?
            fixTheProblem("UNLOAD FILAMENT ERROR: filament not unloading properly, stuck in mk3 head");
            startTime1 = millis();
        }
    } else {                                          // check for timeout waiting for FINDA sensor to trigger

        if ((currentTime - startTime) >
                10000) {         // 10 seconds worth of trying to unload the filament

            fixTheProblem("UNLOAD FILAMENT ERROR: filament is not unloading properly, stuck between mk3 and mmu2");
            startTime = millis();   // reset the start time
        }
    }
    feedFilament(STEPSPERMM);        // 1mm and then check the pinda status




    if (isFilamentLoaded()) {       // keep unloading until we hit the FINDA sensor
        goto loop;
    }

    //      findaStatus = digitalRead(findaPin);

    //      if (findaStatus == 1)              // keep feeding the filament until the pinda sensor triggers

    //          goto loop;

#ifdef NOTDEF
    SerialUI.println(F("unloadFilamenttoFinda(): Pinda Sensor Triggered during Filament unload"));
#endif
    //
    // for a filament unload ... need to get the filament out of the selector head !!!
    //
    pinWrite(extruderDirPin, CW);   // back the filament away from the selector

    //steps = 200 * STEPSIZE + 50;
    //feedFilament(steps);

    feedFilament(STEPSPERMM * 23);     // back the filament away from the selector by 23mm

#ifdef NOTDEF
    SerialUI.println(F("unloadFilamentToFinda(): Unloading Filament Complete ..."));
#endif

    // pinWrite(ledPin, LOW);     // turn off LED
}


void loadFilament(int direction)
{
    int findaStatus;
    unsigned int steps;


    // pinWrite(ledPin, HIGH);          // turn on LED to indicate extruder motor is running
    pinWrite(extruderDirPin, direction);  // set the direction of the MMU2 extruder motor


    switch (direction) {
    case CCW:                     // load filament
loop:
        feedFilament(1);        // 1 step and then check the pinda status

        findaStatus = digitalRead(findaPin);
        if (findaStatus == 0) {            // keep feeding the filament until the pinda sensor triggers
            goto loop;
        }
        SerialUI.println(F("Pinda Sensor Triggered"));
        // now feed the filament ALL the way to the printer extruder assembly

        steps = 17ul * STEPSPERREVOLUTION * STEPSIZE;

        SerialUI.print(F("steps: "));
        SerialUI.println(steps);
        feedFilament(steps);    // 17 complete revolutions
        SerialUI.println(F("Loading Filament Complete ..."));
        break;

    case CW:                      // unload filament
loop1:
        feedFilament(STEPSPERMM);            // 1 mm and then check the pinda status
        findaStatus = digitalRead(findaPin);
        if (findaStatus == 1) {      // wait for the filament to unload past the pinda sensor
            goto loop1;
        }
        SerialUI.println(F("Pinda Sensor Triggered, unloading filament complete"));

        feedFilament(STEPSPERMM * 23);      // move 23mm so we are out of the way of the selector


        break;
    default:
        SerialUI.println(F("loadFilament:  I shouldn't be here !!!!"));
    }
}

//
// this routine feeds filament by the amount of steps provided
//  144 steps = 1mm of filament (using the current mk8 gears in the MMU2)
//
void feedFilament(unsigned int steps)
{
#ifdef NOTDEF
    if (steps > 1) {
        SerialUI.print(F("Steps: "));
        SerialUI.println(steps);
    }
#endif

    for (uint16_t i = 0; i <= steps; i++) {
        pinWrite(extruderStepPin, HIGH);
        delayMicroseconds(PINHIGH);               // delay for 10 useconds
        pinWrite(extruderStepPin, LOW);
        delayMicroseconds(PINLOW);               // delay for 10 useconds

        delayMicroseconds(EXTRUDERMOTORDELAY);         // wait for 400 useconds
        //delay(delayValue);           // wait for 30 milliseconds

    }
}


void recoverfilamentSelector()
{

}

// this routine drives the 5 position bearings (aka idler) on the top of the MMU2 carriage
//
void idlerSelector(char filament)
{
    int newBearingPosition;
    int newSetting;

#ifdef DEBUG
    SerialUI.print(F("idlerSelector(): Filament Selected: "));
    SerialUI.println(filament);
#endif

    //* added on 10.14.18  (need to turn the extruder stepper motor back on since it is turned off by parkidler()
    pinWrite(extruderEnablePin, ENABLE);


    if ((filament < '0') || (filament > '4')) {
        SerialUI.println(F("idlerSelector() ERROR, invalid filament selection"));
        SerialUI.print(F("idlerSelector() filament: "));
        SerialUI.println(filament);
        return;
    }
    // move the selector back to it's origin state

#ifdef DEBUG
    SerialUI.print(F("Old Idler Roller Bearing Position:"));
    SerialUI.println(oldBearingPosition);
    SerialUI.println(F("Moving filament selector"));
#endif

    if (filament >= '0' && filament <= '4') {
        newBearingPosition = bearingAbsPos[filament -
                                                    '0'];                       // idler set to 1st position
        filamentSelection = filament - '0';
        currentExtruder = filament;
    } else {
        SerialUI.println(F("idlerSelector(): ERROR, Invalid Idler Bearing Position"));
    }

    // turnAmount(newFilamentPosition,CCW);                        // new method


    newSetting = newBearingPosition - oldBearingPosition;

#ifdef NOTDEF
    SerialUI.print(F("Old Bearing Position: "));
    SerialUI.println(oldBearingPosition);
    SerialUI.print(F("New Bearing Position: "));
    SerialUI.println(newBearingPosition);

    SerialUI.print(F("New Setting: "));
    SerialUI.println(newSetting);
#endif

    if (newSetting < 0) {
        idlerturnamount(-newSetting, CW);                     // turn idler to appropriate position
    } else {
        idlerturnamount(newSetting, CCW);                     // turn idler to appropriate position
    }

    oldBearingPosition = newBearingPosition;

}


// perform this function only at power up/reset
//
void initIdlerPosition()
{

#ifdef NOTDEF
    SerialUI.println(F("initIdlerPosition(): resetting the Idler Roller Bearing position"));
#endif

    pinWrite(idlerEnablePin, ENABLE);   // turn on the roller bearing motor
    delay(1);
    oldBearingPosition = 125;                // points to position #1
    idlerturnamount(MAX_IDLER_STEPS, CW);
    idlerturnamount(MAX_IDLER_STEPS, CCW);                // move the bearings out of the way
    pinWrite(idlerEnablePin, DISABLE);   // turn off the idler roller bearing motor

    filamentSelection = 0;       // keep track of filament selection (0,1,2,3,4))
    currentExtruder = '0';


}

// perform this function only at power up/reset
//
void initColorSelector()
{

#ifdef NOTDEF
    SerialUI.println(F("Syncing the Color Selector Assembly"));
#endif
    pinWrite(colorSelectorEnablePin, ENABLE);   // turn on the stepper motor
    delay(1);                                       // wait for 1 millisecond

    csTurnAmount(MAXSELECTOR_STEPS, CW);             // move to the right
    csTurnAmount(MAXSELECTOR_STEPS + 20, CCW);      // move all the way to the left

    pinWrite(colorSelectorEnablePin, DISABLE);   // turn off the stepper motor

}


// this function is performed by the 'T' command after so many moves to make sure the colorselector is synchronized
//
void syncColorSelector()
{
    int moveSteps;

    pinWrite(colorSelectorEnablePin, ENABLE);   // turn on the selector stepper motor
    delay(1);                                       // wait for 1 millecond

    SerialUI.print(F("syncColorSelelector()   current Filament selection: "));
    SerialUI.println(filamentSelection);
    moveSteps = MAXSELECTOR_STEPS - selectorAbsPos[filamentSelection];

    SerialUI.print(F("syncColorSelector()   moveSteps: "));
    SerialUI.println(moveSteps);

    csTurnAmount(moveSteps, CW);                    // move all the way to the right
    csTurnAmount(MAXSELECTOR_STEPS + 20, CCW);      // move all the way to the left

#ifdef TURNOFFSELECTORMOTOR                        // added on 10.14.18
    pinWrite(colorSelectorEnablePin, DISABLE);   // turn off the selector stepper motor
#endif
}


// this just energizes the roller bearing extruder motor
//
void activateRollers()
{

    pinWrite(idlerEnablePin, ENABLE);   // turn on the roller bearing stepper motor

    // turnAmount(120, CW);   // move the rollers to filament position #1
    // oldBearingPosition = 45;  // filament position #1

    // oldBearingPosition = MAXROLLERTRAVEL;   // not sure about this CSK

    idlerStatus = ACTIVE;
}

// move the filament Roller pulleys away from the filament

void parkIdler()
{
    int newSetting;

    pinWrite(idlerEnablePin, ENABLE);
    delay(1);

    // commented out on 10.13.18
    //oldBearingPosition = bearingAbsPos[filamentSelection];          // fetch the bearing position based on the filament state

#ifdef DEBUGIDLER
    SerialUI.print(F("parkIdler() oldBearingPosition: "));
    SerialUI.print(oldBearingPosition);
#endif
#ifdef DEBUG
    SerialUI.print(F("   filamentSelection: "));
    SerialUI.println(filamentSelection);
#endif

    newSetting = MAX_IDLER_STEPS - oldBearingPosition;

#ifdef DEBUG
    SerialUI.print(F("parkIdler() DeactiveRoller newSetting: "));
    SerialUI.println(newSetting);
#endif

    idlerturnamount(newSetting, CCW);     // move the bearing roller out of the way
    oldBearingPosition = MAX_IDLER_STEPS;   // record the current roller status  (CSK)

    idlerStatus = INACTIVE;
    pinWrite(idlerEnablePin,
             DISABLE);    // turn off the roller bearing stepper motor  (nice to do, cuts down on CURRENT utilization)
    // added on 10.14.18
    pinWrite(extruderEnablePin, DISABLE); // turn off the extruder stepper motor as well

}


// turn on the idler bearing rollers

void unParkIdler()
{
    int rollerSetting;

    pinWrite(idlerEnablePin, ENABLE);   // turn on (enable) the roller bearing motor
    // added on 10.14.18
    pinWrite(extruderEnablePin, ENABLE);  // turn on (enable) the extruder stepper motor as well

    delay(1);                              // wait for 10 useconds

    //SerialUI.println(F("Activating the Idler Rollers"));

    rollerSetting = MAX_IDLER_STEPS - bearingAbsPos[filamentSelection];
    //************** added on 10.13.18

    oldBearingPosition =
        bearingAbsPos[filamentSelection];                   // update the idler bearing position


    //SerialUI.print(F("unParkIdler() Idler Setting: "));
    //SerialUI.println(rollerSetting);

    idlerturnamount(rollerSetting, CW);    // restore the old position
    idlerStatus = ACTIVE;                   // mark the idler as active


}

// attempt to disengage the idler bearing after a 'T' command instead of parking the idler
//  this is trying to save significant time on re-engaging the idler when the 'C' command is activated

void quickParkIdler()
{
    pinWrite(idlerEnablePin, ENABLE);                          // turn on the idler stepper motor
    delay(1);

    //**************************************************************************************************
    //*  this is flawed logic, if I have done a special park idler the oldBearingPosition doesn't map exactly to the filamentSelection
    //*   discovered on 10.13.18
    //*  In fact,  I don't need to update the 'oldBearingPosition' value, it is already VALID
    //********************************************************************************************************************************
    // oldBearingPosition = bearingAbsPos[filamentSelection];          // fetch the bearing position based on the filament state


    //newSetting = MAXROLLERTRAVEL - oldBearingPosition;
    //*************************************************************************************************
    //*  this is a new approach to moving the idler just a little bit (off the filament)
    //*  in preparation for the 'C' Command

    //*************************************************************************************************
#ifdef NOTDEF
    SerialUI.print(F("quickparkidler():  currentExtruder: "));
    SerialUI.println(currentExtruder);
#endif

    //* COMMENTED OUT THIS SECTION OF CODE on 10.13.18  (don't think it is necessary)
#ifdef CRAZYIVAN
    if (currentExtruder == 4) {
        //newSetting = oldBearingPosition - IDLERSTEPSIZE;
        idlerturnamount(IDLERSTEPSIZE, CW);
    } else {
#endif

        //newSetting = oldBearingPosition + IDLERSTEPSIZE;       // try to move 12 units (just to disengage the roller)
        idlerturnamount(IDLERSTEPSIZE, CCW);

#ifdef CRAZYIVAN
    }
#endif

    //oldBearingPosition = MAXROLLERTRAVEL;   // record the current roller status  (CSK)
    //************************************************************************************************
    //* record the idler position
    //* had to be fixed on 10.13.18
    //***********************************************************************************************
    oldBearingPosition = oldBearingPosition +
                         IDLERSTEPSIZE;       // record the current position of the IDLER bearing
#ifdef NOTDEF
    SerialUI.print(F("quickparkidler() oldBearingPosition: "));
    SerialUI.println(oldBearingPosition);
#endif

    idlerStatus =
        QUICKPARKED;                 // use this new state to show the idler is pending the 'C0' command

    //*********************************************************************************************************
    //* DO NOT TURN OFF THE IDLER ... needs to be held in position
    //*********************************************************************************************************

    //pinWrite(idlerEnablePin, DISABLE);    // turn off the roller bearing stepper motor  (nice to do, cuts down on CURRENT utilization)

}

//*********************************************************************************************
//  this routine is called by the 'C' command to re-engage the idler bearing
//*********************************************************************************************
void quickUnParkIdler()
{
    int rollerSetting;

    //*********************************************************************************************************
    //* don't need to turn on the idler ... it is already on (from the 'T' command)
    //*********************************************************************************************************

    //pinWrite(idlerEnablePin, ENABLE);   // turn on the roller bearing motor
    //delay(1);                              // wait for 1 millisecond
    //if (idlerStatus != QUICKPARKED) {
    //    SerialUI.println(F("quickUnParkIdler(): idler already parked"));
    //    return;                              // do nothing since the idler is not 'quick parked'
    //}

#ifdef NOTDEF
    SerialUI.print(F("quickunparkidler():  currentExtruder: "));
    SerialUI.println(currentExtruder);
#endif


    // re-enage the idler bearing that was only moved 1 position (for quicker re-engagement)
    //
#ifdef CRAZYIVAN
    if (currentExtruder == 4) {
        rollerSetting = oldBearingPosition + IDLERSTEPSIZE;
        idlerturnamount(IDLERSTEPSIZE, CCW);
    } else {
#endif

        rollerSetting = oldBearingPosition -
                        IDLERSTEPSIZE;   // go back IDLERSTEPSIZE units (hopefully re-enages the bearing
        idlerturnamount(IDLERSTEPSIZE, CW);   // restore old position

#ifdef CRAZYIVAN
    }
#endif

    //SerialUI.print(F("unParkIdler() Idler Setting: "));
    //SerialUI.println(rollerSetting);

    //************************************************************************************************
    //* track the absolute position of the idler  (changed on 10.13.18
    //***********************************************************************************************
    SerialUI.print(F("quickunparkidler(): oldBearingPosition"));
    SerialUI.println(oldBearingPosition);
    oldBearingPosition = rollerSetting - IDLERSTEPSIZE;    // keep track of the idler position

    idlerStatus = ACTIVE;                   // mark the idler as active


}

//***************************************************************************************************************
//* called by 'C' command to park the idler
//***************************************************************************************************************
void specialParkIdler()
{
    int idlerSteps;

    pinWrite(idlerEnablePin, ENABLE);                          // turn on the idler stepper motor
    delay(1);

    // oldBearingPosition = bearingAbsPos[filamentSelection];          // fetch the bearing position based on the filament state

    //*************************************************************************************************
    //*  this is a new approach to moving the idler just a little bit (off the filament)
    //*  in preparation for the 'C' Command

    //*************************************************************************************************
    if (IDLERSTEPSIZE % 2) {
        idlerSteps = IDLERSTEPSIZE / 2 +
                     1;                         // odd number processing, need to round up

    } else {
        idlerSteps = IDLERSTEPSIZE / 2;

    }

#ifdef NOTDEF
    SerialUI.print(F("SpecialParkIdler()   idlersteps: "));
    SerialUI.println(idlerSteps);
#endif

    //newSetting = oldBearingPosition + idlerSteps;     // try to move 6 units (just to disengage the roller)
    idlerturnamount(idlerSteps, CCW);

    //************************************************************************************************
    //* record the idler position  (get back to where we were)
    //***********************************************************************************************
    oldBearingPosition = oldBearingPosition +
                         idlerSteps;       // record the current position of the IDLER bearingT

#ifdef DEBUGIDLER
    SerialUI.print(F("SpecialParkIdler()  oldBearingPosition: "));
    SerialUI.println(oldBearingPosition);
#endif

    idlerStatus =
        QUICKPARKED;                 // use this new state to show the idler is pending the 'C0' command

    //* SPECIAL DEBUG (10.13.18 - evening)
    //* turn off the idler stepper motor
    // pinWrite(idlerEnablePin, DISABLE);    // turn off the roller bearing stepper motor  (nice to do, cuts down on CURRENT utilization)

#ifdef NOTDEF
    pinWrite(extruderEnablePin, DISABLE);
    extruderMotorStatus = INACTIVE;
#endif

}

//*********************************************************************************************
//  this routine is called by the 'C' command to re-engage the idler bearing
//*********************************************************************************************
void specialUnParkIdler()
{
    int idlerSteps;

    // re-enage the idler bearing that was only moved 1 position (for quicker re-engagement)
    //
    if (IDLERSTEPSIZE % 2) {
        idlerSteps = IDLERSTEPSIZE / 2 +
                     1;                         // odd number processing, need to round up

    } else {
        idlerSteps = IDLERSTEPSIZE / 2;
    }

#ifdef NOTDEF
    SerialUI.print(F("SpecialUnParkIdler()   idlersteps: "));
    SerialUI.println(idlerSteps);
#endif

#ifdef DEBUGIDLER
    SerialUI.print(F("SpecialUnParkIdler()   oldBearingPosition (beginning of routine): "));
    SerialUI.println(oldBearingPosition);
#endif

    //newSetting = oldBearingPosition - idlerSteps; // go back IDLERSTEPSIZE units (hopefully re-enages the bearing
    idlerturnamount(idlerSteps, CW); // restore old position

    // MIGHT BE A BAD IDEA
    oldBearingPosition = oldBearingPosition - idlerSteps;    // keep track of the idler position

#ifdef DEBUGIDLER
    SerialUI.print(F("SpecialUnParkIdler()  oldBearingPosition: (end of routine):  "));
    SerialUI.println(oldBearingPosition);
#endif

    idlerStatus = ACTIVE;                   // mark the idler as active


}

void deActivateColorSelector()
{
#ifdef TURNOFFSELECTORMOTOR
    pinWrite(colorSelectorEnablePin,
             DISABLE);    // turn off the color selector stepper motor  (nice to do, cuts down on CURRENT utilization)
    delay(1);
    colorSelectorStatus = INACTIVE;
#endif
}

void activateColorSelector()
{
    pinWrite(colorSelectorEnablePin, ENABLE);
    delay(1);
    colorSelectorStatus = ACTIVE;
}




void recvOneChar()
{
    if (SerialUI.available() > 0) {
        receivedChar = SerialUI.read();
        newData = true;
    }
}

void showNewData()
{
    if (newData == true) {
        SerialUI.print(F("This just in ... "));
        SerialUI.println(receivedChar);
        newData = false;
    }
}

#ifdef ORIGINALCODE

void processKeyboardInput()
{


    while (newData == false) {
        recvOneChar();
    }

    showNewData();      // character received

    SerialUI.print(F("Filament Selected: "));
    SerialUI.println(receivedChar);

    switch (receivedChar) {
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
        if (idlerStatus == INACTIVE) {
            activateRollers();
        }

        if (colorSelectorStatus == INACTIVE) {
            activateColorSelector();    // turn on the color selector motor
        }


        idlerSelector(receivedChar);   // move the filament selector stepper motor to the right spot
        colorSelector(receivedChar);     // move the color Selector stepper Motor to the right spot

        break;
    case 'd':                             // de-active the bearing roller stepper motor and color selector stepper motor
    case 'D':
        parkIdler();
        deActivateColorSelector();
        break;
    case 'l':                            // start the load process for the filament
    case 'L':
        // unParkIdler();
        if (idlerStatus == INACTIVE) {
            unParkIdler();
        }
        loadFilament(CCW);
        parkIdler();          // move the bearing rollers out of the way after a load is complete
        break;
    case 'u':                           // unload the filament from the MMU2 device
    case 'U':
        unParkIdler();           // working on this command
        loadFilament(CW);
        parkIdler();         // after the unload of the filament, move the bearing rollers out of the way
        break;
    case 't':
    case 'T':
        csTurnAmount(200, CW);
        delay(1000);
        csTurnAmount(200, CCW);
        break;
    default:
        SerialUI.println(F("Invalid Serial Output Selection"));
    } // end of switch statement
}
#endif

//***********************************************************************************
//* this routine is executed as part of the 'T' Command (Load Filament)
//***********************************************************************************
void filamentLoadToMK3()
{
    int findaStatus;
    int flag;
    int filamentDistance;
    int fStatus;
    int startTime, currentTime;


    if ((currentExtruder < '0')  || (currentExtruder > '4')) {
        SerialUI.println(F("filamentLoadToMK3(): fixing current extruder variable"));
        currentExtruder = '0';
    }
#ifdef DEBUG
    SerialUI.println(F("Attempting to move Filament to Print Head Extruder Bondtech Gears"));
    //unParkIdler();
    SerialUI.print(F("filamentLoadToMK3():  currentExtruder: "));
    SerialUI.println(currentExtruder);
#endif

    // idlerSelector(currentExtruder);        // active the idler before the filament load

    deActivateColorSelector();

    pinWrite(extruderEnablePin, ENABLE); // turn on the extruder stepper motor (10.14.18)
    pinWrite(extruderDirPin, CCW);      // set extruder stepper motor to push filament towards the mk3
    delay(1);                               // wait 1 millisecond

    startTime = millis();

loop:
    // feedFilament(1);        // 1 step and then check the pinda status
    feedFilament(STEPSPERMM);  // feed 1 mm of filament into the bowden tube

    findaStatus = digitalRead(findaPin);              // read the FINDA sensor in the MMU2
    currentTime = millis();

    // added this timeout feature on 10.4.18 (2 second timeout)
    if ((currentTime - startTime) > 2000) {
        fixTheProblem("FILAMENT LOAD ERROR:  Filament not detected by FINDA sensor, check the selector head in the MMU2");

        startTime = millis();
    }
    if (findaStatus == 0) {            // keep feeding the filament until the pinda sensor triggers
        goto loop;
    }
    //***************************************************************************************************
    //* added additional check (10.10.18) - if the filament switch is already set this might mean there is a switch error or a clog
    //*       this error condition can result in 'air printing'
    //***************************************************************************************************************************
loop1:
    fStatus = digitalRead(chindaPin);
    if (fStatus == 0) {                    // switch is active (this is not a good condition)
        fixTheProblem("FILAMENT LOAD ERROR: Filament Switch in the MK3 is active (see the RED LED), it is either stuck open or there is debris");
        goto loop1;
    }



    //SerialUI.println(F("filamentLoadToMK3(): Pinda Sensor Triggered during Filament Load"));
    // now loading from the FINDA sensor all the way down to the NEW filament sensor

    feedFilament(STEPSPERMM * 350);       // go 350 mm then look for the 2nd filament sensor
    filamentDistance = 350;

    //delay(15000);                         //wait 15 seconds
    //feedFilament(STEPSPERMM*100);         //go 100 more mm
    //delay(15000);
    //goto skipeverything;

    startTime = millis();
    flag = 0;
    //filamentDistance = 0;

    // wait until the filament sensor on the mk3 extruder head (microswitch) triggers
    while (flag == 0) {

        currentTime = millis();
        if ((currentTime - startTime) > 8000) { // only wait for 8 seconds
            fixTheProblem("FILAMENT LOAD ERROR: Filament not detected by the MK3 filament sensor, check the bowden tube for clogging/binding");
            startTime = millis();         // reset the start Time

        }

        feedFilament(STEPSPERMM);        // step forward 1 mm
        filamentDistance++;
        fStatus = digitalRead(chindaPin);             // read the filament switch on the mk3 extruder
        if (fStatus == 0) {
            // SerialUI.println(F("filament switch triggered"));
            flag = 1;

            SerialUI.print(F("Filament distance traveled (mm): "));
            SerialUI.println(filamentDistance);

            switch (filamentSelection) {
            case 0:
                if (filamentDistance < f0Min) {
                    f0Min = filamentDistance;
                }
                if (filamentDistance > f0Max) {
                    f0Max = filamentDistance;
                }
                f0Distance += filamentDistance;
                f0ToolChange++;
                f0Avg = f0Distance / f0ToolChange;
                break;
            case 1:
                if (filamentDistance < f1Min) {
                    f1Min = filamentDistance;
                }
                if (filamentDistance > f1Max) {
                    f1Max = filamentDistance;
                }
                f1Distance += filamentDistance;
                f1ToolChange++;
                f1Avg = f1Distance / f1ToolChange;
                break;

            case 2:
                if (filamentDistance < f2Min) {
                    f2Min = filamentDistance;
                }
                if (filamentDistance > f2Max) {
                    f2Max = filamentDistance;
                }
                f2Distance += filamentDistance;
                f2ToolChange++;
                f2Avg = f2Distance / f2ToolChange;
                break;
            case 3:
                if (filamentDistance < f3Min) {
                    f3Min = filamentDistance;
                }
                if (filamentDistance > f3Max) {
                    f3Max = filamentDistance;
                }
                f3Distance += filamentDistance;
                f3ToolChange++;
                f3Avg = f3Distance / f3ToolChange;
                break;
            case 4:
                if (filamentDistance < f4Min) {
                    f4Min = filamentDistance;
                }
                if (filamentDistance > f4Max) {
                    f4Max = filamentDistance;
                }

                f4Distance += filamentDistance;
                f4ToolChange++;
                f4Avg = f4Distance / f4ToolChange;
                break;
            default:
                SerialUI.println(F("Error, Invalid Filament Selection"));

            }
            // printFilamentStats();

        }
    }
    // feed filament an additional 32 mm to hit the middle of the bondtech gear
    // go an additional 32mm (increased to 32mm on 10.4.18)

    feedFilament(STEPSPERMM * 32);




    //#############################################################################################################################
    //# NEWEXPERIMENT:  removed the parkIdler() command on 10.5.18 to improve timing between 'T' command followng by 'C' command
    //#############################################################################################################################
    // parkIdler();              // park the IDLER (bearing) motor

    //delay(200);             // removed on 10.5.18
    //SerialPrinter.print(F("ok\n"));    // send back acknowledge to the mk3 controller (removed on 10.5.18)

}

void printFilamentStats()
{
    SerialUI.println(F(" "));
    SerialUI.print(F("F0 Min: "));
    SerialUI.print(f0Min);
    SerialUI.print(F("  F0 Max: "));
    SerialUI.print(f0Max);
    SerialUI.print(F("  F0 Avg: "));
    SerialUI.print(f0Avg);
    SerialUI.print(F("  F0 Length: "));
    SerialUI.print(f0Distance);
    SerialUI.print(F("  F0 count: "));
    SerialUI.println(f0ToolChange);

    SerialUI.print(F("F1 Min: "));
    SerialUI.print(f1Min);
    SerialUI.print(F("  F1 Max: "));
    SerialUI.print(f1Max);
    SerialUI.print(F("  F1 Avg: "));
    SerialUI.print(f1Avg);
    SerialUI.print(F("  F1 Length: "));
    SerialUI.print(f1Distance);
    SerialUI.print(F("  F1 count: "));
    SerialUI.println(f1ToolChange);

    SerialUI.print(F("F2 Min: "));
    SerialUI.print(f2Min);
    SerialUI.print(F("  F2 Max: "));
    SerialUI.print(f2Max);
    SerialUI.print(F("  F2 Avg: "));
    SerialUI.print(f2Avg);
    SerialUI.print(F("  F2 Length: "));
    SerialUI.print(f2Distance);
    SerialUI.print(F("  F2 count: "));
    SerialUI.println(f2ToolChange);

    SerialUI.print(F("F3 Min: "));
    SerialUI.print(f3Min);
    SerialUI.print(F("  F3 Max: "));
    SerialUI.print(f3Max);
    SerialUI.print(F("  F3 Avg: "));
    SerialUI.print(f3Avg);
    SerialUI.print(F("  F3 Length: "));
    SerialUI.print(f3Distance);
    SerialUI.print(F("  F3 count: "));
    SerialUI.println(f3ToolChange);

    SerialUI.print(F("F4 Min: "));
    SerialUI.print(f4Min);
    SerialUI.print(F("  F4 Max: "));
    SerialUI.print(f4Max);
    SerialUI.print(F("  F4 Avg: "));
    SerialUI.print(f4Avg);
    SerialUI.print(F("  F4 Length: "));
    SerialUI.print(f4Distance);
    SerialUI.print(F("  F4 count: "));
    SerialUI.println(f4ToolChange);

}

int isFilamentLoaded()
{
    int findaStatus;

    findaStatus = digitalRead(findaPin);
    return (findaStatus);
}

//
// (T) Tool Change Command - this command is the core command used my the mk3 to drive the mmu2 filament selection
//
void toolChange( char selection)
{
    int newExtruder;

    ++toolChangeCount;                             // count the number of tool changes
    ++trackToolChanges;

    //**********************************************************************************
    // * 10.10.18 added an automatic reset of the tracktoolchange counter since going to
    //            filament position '0' move the color selection ALL the way to the left
    //*********************************************************************************
    if (selection == '0')  {
        // SerialUI.println(F("toolChange()  filament '0' selected: resetting tracktoolchanges counter"));
        trackToolChanges = 0;
    }

    SerialUI.print(F("Tool Change Count: "));
    SerialUI.println(toolChangeCount);


    newExtruder = selection - 0x30;                // convert ASCII to a number (0-4)


    //***********************************************************************************************
    // code snippet added on 10.8.18 to help the 'C' command processing (happens after 'T' command
    //***********************************************************************************************
    if (newExtruder == filamentSelection) {  // already at the correct filament selection

        if (!isFilamentLoaded() ) {            // no filament loaded

            SerialUI.println(F("toolChange: filament not currently loaded, loading ..."));

            idlerSelector(selection);   // move the filament selector stepper motor to the right spot
            colorSelector(selection);     // move the color Selector stepper Motor to the right spot
            filamentLoadToMK3();
            quickParkIdler();           // command moved here on 10.13.18
            //****************************************************************************************
            //*  added on 10.8.18 to help the 'C' command
            //***************************************************************************************
            repeatTCmdFlag = INACTIVE;   // used to help the 'C' command
            //loadFilamentToFinda();
        } else {
            SerialUI.println(F("toolChange:  filament already loaded to mk3 extruder"));
            //*********************************************************************************************
            //* added on 10.8.18 to help the 'C' Command
            //*********************************************************************************************
            repeatTCmdFlag = ACTIVE;     // used to help the 'C' command to not feed the filament again
        }

        //                               else {                           // added on 9.24.18 to
        //                                     SerialUI.println(F("Filament already loaded, unloading the filament"));
        //                                     idlerSelector(selection);
        //                                     unloadFilamentToFinda();
        //                               }

    }  else {                                 // different filament position
        //********************************************************************************************
        //* added on 19.8.18 to help the 'C' Command
        //************************************************************************************************
        repeatTCmdFlag = INACTIVE;              // turn off the repeat Commmand Flag (used by 'C' Command)
        if (isFilamentLoaded()) {
            //**************************************************************
            // added on 10.5.18 to get the idler into the correct state
            // idlerSelector(currentExtruder);
            //**************************************************************
#ifdef DEBUG
            SerialUI.println(F("Unloading filament"));
#endif

            idlerSelector(currentExtruder);    // point to the current extruder

            unloadFilamentToFinda();          // have to unload the filament first
        }




        if (trackToolChanges >
                TOOLSYNC) {             // reset the color selector stepper motor (gets out of alignment)
            SerialUI.println(F("Synchronizing the Filament Selector Head"));
            //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
            // NOW HAVE A MORE ELEGANT APPROACH - syncColorSelector (and it works)
            // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
            syncColorSelector();
            //initColorSelector();              // reset the color selector


            activateColorSelector();                  // turn the color selector motor back on
            currentPosition = 0;                   // reset the color selector

            // colorSelector('0');                       // move selector head to position 0

            trackToolChanges = 0;

        }
#ifdef DEBUG
        SerialUI.println(F("Selecting the proper Idler Location"));
#endif
        idlerSelector(selection);
#ifdef DEBUG
        SerialUI.println(F("Selecting the proper Selector Location"));
#endif
        colorSelector(selection);
#ifdef DEBUG
        SerialUI.println(F("Loading Filament: loading the new filament to the mk3"));
#endif

        filamentLoadToMK3();                // moves the idler and loads the filament


        filamentSelection = newExtruder;
        currentExtruder = selection;
        quickParkIdler();                    // command moved here on 10.13.18
    }

    //******************************************************************************************
    //* barely move the idler out of the way
    //* WARNING:  THIS MAY NOT WORK PROPERLY ... NEEDS TO BE DEBUGGED (10.7.18)
    //******************************************************************************************
    // quickParkIdler();                       // 10.7.2018 ... attempt to speed up idler for the follow-on 'C' command

    //******************************************************************************************
    //* this was how it was normally done until the above command was attempted
    //******************************************************************************************
    //parkIdler();                            // move the idler away


}  // end of ToolChange processing


// part of the 'C' command,  does the last little bit to load into the past the extruder gear
void filamentLoadWithBondTechGear()
{
    int findaStatus;
    int i;
    int delayFactor;                            // delay factor (in microseconds) for the filament load loop
    int stepCount;
    int tSteps;

    timeCStart = millis();

    //*****************************************************************************************************************
    //*  added this code snippet to not process a 'C' command that is essentially a repeat command


    if (repeatTCmdFlag == ACTIVE) {
        SerialUI.println(
            F("filamentLoadWithBondTechGear(): filament already loaded and 'C' command already processed"));
        repeatTCmdFlag = INACTIVE;
        return;
    }



    findaStatus = digitalRead(findaPin);

    if (findaStatus == 0) {
        SerialUI.println(
            F("filamentLoadWithBondTechGear()  Error, filament sensor thinks there is no filament"));
        return;
    }

    if ((currentExtruder < '0')  || (currentExtruder > '4')) {
        SerialUI.println(F("filamentLoadWithBondTechGear(): fixing current extruder variable"));
        currentExtruder = '0';
    }


    //*************************************************************************************************
    //* change of approach to speed up the IDLER engagement 10.7.18
    //*  WARNING: THIS APPROACH MAY NOT WORK ... NEEDS TO BE DEBUGGED
    //*  C command assumes there is always a T command right before it
    //*  (IF 2 'C' commands are issued by the MK3 in a row the code below might be an issue)
    //*
    //*************************************************************************************************
#ifdef NOTDEF
    long timeStart, timeEnd;
    timeStart = millis();
#endif
    if (idlerStatus ==
            QUICKPARKED) {                        // make sure idler is  in the pending state (set by quickparkidler() routine)
        // SerialUI.println(F("'C' Command: quickUnParking the Idler"));
        // quickUnParkIdler();
#ifdef NOTDEF
        SerialUI.println(F("filamentLoadWithBondTechGear()  calling specialunparkidler() routine"));
#endif
        specialUnParkIdler();                                // PLACEHOLDER attempt to speed up the idler engagement a little more 10.13.18
    }
    if (idlerStatus == INACTIVE) {
        unParkIdler();
    }

#ifdef NOTDEF
    else {
        SerialUI.println(
            F("filamentLoadWithBondTechGear(): looks like I received two 'C' commands in a row"));
        SerialUI.println(F("                                ignoring the 2nd 'C' command"));
        return;
    }

    long timeUnparking;
    timeUnparking = timeEnd - timeStart;
    timeEnd = millis();
#endif


    //*************************************************************************************************
    //* following line of code is currently disabled (in order to test out the code above
    //*  NOTE: I don't understand why the unParkIdler() command is not used instead ???
    //************************************************************************************************
    // idlerSelector(currentExtruder);        // move the idler back into position

    stepCount = 0;
    time0 = millis();
    pinWrite(greenLED, HIGH);                   // turn on the green LED (for debug purposes)
    //*******************************************************************************************
    // feed the filament from the MMU2 into the bondtech gear for 2 seconds at 10 mm/sec
    // STEPPERMM : 144, 1: duration in seconds,  21: feed rate (in mm/sec)
    // delay: 674 (for 10 mm/sec)
    // delay: 350 (for 21 mm/sec)
    // LOAD_DURATION:  1 second (time to spend with the mmu2 extruder active)
    // LOAD_SPEED: 21 mm/sec  (determined by Slic3r settings
    // INSTRUCTION_DELAY:  25 useconds  (time to do the instructions in the loop below, excluding the delayFactor)
    // #define LOAD_DURATION 1000       (load duration in milliseconds, currently set to 1 second)
    // #define LOAD_SPEED 21    // load speed (in mm/sec) during the 'C' command (determined by Slic3r setting)
    // #defefine INSTRUCTION_DELAY 25  // delay (in microseconds) of the loop

    // *******************************************************************************************
    // compute the loop delay factor (eventually this will replace the '350' entry in the loop)
    //       this computed value is in microseconds of time
    //********************************************************************************************
    // delayFactor = ((LOAD_DURATION * 1000.0) / (LOAD_SPEED * STEPSPERMM)) - INSTRUCTION_DELAY;   // compute the delay factor (in microseconds)

    // for (i = 0; i < (STEPSPERMM * 1 * 21); i++) {

    tSteps =   STEPSPERMM * ((float)LOAD_DURATION / 1000.0) *
               LOAD_SPEED;             // compute the number of steps to take for the given load duration
    delayFactor = (float(LOAD_DURATION * 1000.0) / tSteps) -
                  INSTRUCTION_DELAY;            // 2nd attempt at delayFactor algorithm

#ifdef NOTDEF
    SerialUI.print(F("Tsteps: "));
    SerialUI.println(tSteps);
#endif

    for (i = 0; i < tSteps; i++) {
        pinWrite(extruderStepPin, HIGH);  // step the extruder stepper in the MMU2 unit
        delayMicroseconds(PINHIGH);
        pinWrite(extruderStepPin, LOW);
        //*****************************************************************************************************
        // replace '350' with delayFactor once testing of variable is complete
        //*****************************************************************************************************
        // after further testing, the '350' can be replaced by delayFactor
        delayMicroseconds(
            delayFactor);             // this was calculated in order to arrive at a 10mm/sec feed rate
        ++stepCount;
    }
    pinWrite(greenLED, LOW);                      // turn off the green LED (for debug purposes)

    time1 = millis();


    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // copied from the MM-control-01/blob/master/motion.cpp routine
    // NO LONGER USED (abandoned in place on 10.7.18) ... came up with a better algorithm (see above)
    //
    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    //***********************************************************************************************************
    //* THIS CODE WORKS BUT IT LEADS TO SOME GRINDING AT THE MMU2 WHILE THE BONDTECH GEAR IS LOADING THE FILAMENT
    //***********************************************************************************************************
#ifdef NOTDEF
    for (i = 0; i <= 320; i++) {
        pinWrite(extruderStepPin, HIGH);
        delayMicroseconds(PINHIGH);               // delay for 10 useconds
        pinWrite(extruderStepPin, LOW);
        //delayMicroseconds(2600);             // originally 2600
        delayMicroseconds(800);              // speed up by a factor of 3

    }
    for (i = 0; i <= 450; i++) {
        pinWrite(extruderStepPin, HIGH);
        delayMicroseconds(PINHIGH);               // delay for 10 useconds
        pinWrite(extruderStepPin, LOW);
        // delayMicroseconds(2200);            // originally 2200
        delayMicroseconds(800);             // speed up by a factor of 3
    }
#endif

#ifdef DEBUG
    SerialUI.println(F("C Command: parking the idler"));
#endif
    //***************************************************************************************************************************
    //*  this disengags the idler pulley after the 'C' command has been exectuted
    //***************************************************************************************************************************
    // quickParkIdler();                           // changed to quickparkidler on 10.12.18 (speed things up a bit)

    specialParkIdler();                         // PLACEHOLDER (experiment attempted on 10.13.18)

    //parkIdler();                               // turn OFF the idler rollers when filament is loaded

    timeCEnd = millis();
    //*********************************************************************************************
    //* going back to the fundamental approach with the idler
    //*********************************************************************************************
    parkIdler();                               // cleanest way to deal with the idler



    printFilamentStats();   // print current Filament Stats

    SerialUI.print(F("'T' Command processing time (ms): "));
    SerialUI.println(time5 - time4);
    SerialUI.print(F("'C' Command processing time (ms): "));
    SerialUI.println(timeCEnd - timeCStart);

#ifdef NOTDEF
    SerialUI.print(F("Time 'T' Command Received: "));
    SerialUI.println(time4);
    SerialUI.print(F("Time 'T' Command Completed: "));
    SerialUI.println(time5);
#endif

#ifdef NOTDEF
    SerialUI.print(F("Time 'C' Command Received: "));
    SerialUI.println(time3);
#endif


    SerialUI.print(F("Time in Critical Load Loop: "));
    SerialUI.println(time1 - time0);

#ifdef NOTDEF
    SerialUI.print(F("Time at Parking the Idler Complete: "));
    SerialUI.println(time2);
    SerialUI.print(F("Number of commanded steps to the Extruder: "));
    SerialUI.println(stepCount);
    SerialUI.print(F("Computed Delay Factor: "));
    SerialUI.println(delayFactor);
    SerialUI.print(F("Time Unparking: "));
    SerialUI.println(timeUnparking);
#endif

#ifdef DEBUG
    SerialUI.println(F("filamentLoadToMK3(): Loading Filament to Print Head Complete"));
#endif

}

Application::Application()
{
    // nothing to do in the constructor
}


#ifdef PRUSA_BOARD
void pinWrite(PinNr pinNr, bool value, bool immediateTransfer)
{
    if (pinNr < 0x100) {
        digitalWrite(pinNr, value);
    } else {
        extPins.writeBit(pinNr & 0xff, value);
        if (immediateTransfer) {
            extPins.transferData();
        }
    }
}

void setPinAsOutput(PinNr pinNr)
{
    if (pinNr < 0x100) {
        pinMode(pinNr, OUTPUT);
    }
}

int putc(int __c, FILE *__stream)
{
    SerialUI.write(__c);
    return 0;
}

#endif
