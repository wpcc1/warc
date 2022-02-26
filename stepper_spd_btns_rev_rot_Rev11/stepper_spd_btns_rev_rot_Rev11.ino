
/*
  Stepper Motor Control

  (1) Two momentary switches are connected to digital pins 11 and 12 to
      engage the motor in a CW or CCW motion when pressed.
  (2)  An 8 Digit Counter included as an indicator.
  (3) Stepper Disable Counter to reduce static heating and current.
  Added by John Forrest 13 Jun 2018 to use with Mag Loop Trombone rack drive

  A Rotary Encoder is connected to Hardware Interupt digital pins 2 and 3 to rotate the motor
  CW or ACW one step at a time.
  Code by Simon Merrett, based on insight from Oleg Mazurov, Nick Gammon, rt, Steve Spence
*/
/*
 *https://www.warc.asn.au/2021/09/17/basic-magnetic-loop-tuner/
 *https://www.pololu.com/product/1182
 */
 */
#include <Arduino.h>
#include <LedControl.h>
#include "A4988.h"

//Define pins
#define CLK 2         // Binary Encoder Clock
#define DATA 3        //    "      "    Data
#define DIR 4         // Stepper Controller Direction 
#define STEP 5        //    "         "     Step
#define ENABLE 6      //    "         "     Enable
#define MS1 7         //    "         "     Microstep 1
#define MS2 8         //    "         "     Microstep 2
#define MS3 9         //    "         "     Microstep 3
#define CLOCK 10      // MAX7219 Display Clock
#define LOAD 11       //    "       "    Load
#define DIN 12        //    "       "    Data In 
#define Led1Pin 13    // Zero LED
#define button1Pin 14 // Clockwise Button
#define button2Pin 15 // Counter Clockwise Button
#define button3Pin 16 // Zero Position Microswitch

//Define Stepper Parameters
// Motor steps per revolution. Most steppers are 200 steps or 1.8 degrees/step
#define MOTOR_STEPS 200
#define RPM 120
#define EOT_SOFT 10000 // Set this number for soft end of travel (Revolutions)
#define STEP_MODE1 1  // Zero and Pushbutton Step Mode value, 1=Full, 1/2 = Half, 4 = 1/4, 8 = 1/8 and 16 = 1/16 step
#define STEP_MODE2 4  // Encoder Step Mode value, 1=Full, 1/2 = Half, 4 = 1/4, 8 = 1/8 and 16 = 1/16 step

// Speed Variable
int setDelay  = 0;//Speed Delay Setting

// Encoder Variables
static uint8_t prevNextCode = 0;
static uint16_t store = 0;
static int8_t val;

// Disable/Enable Counter Variables
int disCount = 0;

// Stepper Positioning
int ZERO_FLAG = false;      //Intialisation to zero position flag
int long stepPos = 0;       // Current position of the stepper motor in steps from zero.
int long stepPosOld = 0;    // Last displayed step count
int long revCount = 0;      // Current position of the stepper motor in revolutions from zero.
int long revCountOld = 0;   // Last displayed revolution count

//Display Counters
int REVSTEP_FLAG = false;     // Display Revolutions = true, Display steps = false.
unsigned int count_one = 0; // BCD
unsigned int count_two = 0;

//  Initialize the LedControl library:
/* Pin 12 is connected to the DATA IN-pin
  Pin 11 is connected to the CLK-pin
  Pin 10 is connected to the LOAD-pin
  There will only be a single MAX7221 attached to the arduino
*/
LedControl mydisplay = LedControl(DIN, CLOCK, LOAD, 1);

A4988 stepper(MOTOR_STEPS, DIR, STEP, ENABLE, MS1, MS2, MS3);

// Setup Pins;

void setup() {

  //Debug Printer
  Serial.begin(115200); // start the serial monitor link

  //Buttons
  pinMode(button1Pin, INPUT);
  pinMode(button2Pin, INPUT);
  pinMode(button3Pin, INPUT);

  //LEDs
  pinMode(Led1Pin, OUTPUT);

  //Binary Encoder
  pinMode(CLK, INPUT);
  pinMode(CLK, INPUT_PULLUP);
  pinMode(DATA, INPUT);
  pinMode(DATA, INPUT_PULLUP);

  //A4988 Stepper Motor Controller
  stepper.begin(RPM);
  stepper.enable();

  //Four Digit 7 Segment Display
  mydisplay.shutdown(0, false);  //turns on display
  mydisplay.setIntensity(0, 15); // 15 = brightest
  mydisplay.setDigit(0, 0, 9, false);
  mydisplay.setDigit(0, 1, 8, false);
  mydisplay.setDigit(0, 2, 7, false);
  mydisplay.setDigit(0, 3, 6, false);
  mydisplay.setDigit(0, 4, 5, true);
  mydisplay.setDigit(0, 5, 4, false);
  mydisplay.setDigit(0, 6, 3, false);
  mydisplay.setDigit(0, 7, 2, false);
}

void loop() {

  //********* Disable Counter ***********
  // Disabling the A4899 Controller Module to reduce current heating of the stepper and Module.
  // The A4899 Controller is disabled after approximately 1 second of inactivity and enabled
  // prior to any stepper activity.  This is required to prevent timing problems with the
  // disabling of the module in the loop during encoder single stepping.  As the stepper is driving a screw
  // rack it does not need to lock in position.

  if (disCount > 1000) {
    stepper.disable();
  } else {
    disCount ++; //Increment the Disable/Enable Counter
  }

  // *********** Set delay for the motor speed *************
  setDelay = 700;

  //***All References to Clockwise/Counter Clockwise are based on looking at the FRONT of the Stepper Motor ***********

  //*********Zero Stepper Motor Position***********

  if (ZERO_FLAG == false) {   //false on power up
    stepper.setMicrostep(STEP_MODE1); // Set microstep mode
    zeroStep();
  }
  //********** Button Step Counter Clock Wise ************

  if ((digitalRead(button1Pin) == HIGH) && (digitalRead(button2Pin) == LOW)) {
    stepper.setMicrostep(STEP_MODE1); // Set microstep mode
    stepper.enable();
    disCount = 0;
    stepCCW();
  }
  //*********** Button Step Clock Wise *************

  if ((digitalRead(button1Pin) == LOW) && (digitalRead(button2Pin) == HIGH)) {
    stepper.setMicrostep(STEP_MODE1); // Set microstep mode
    stepper.enable();
    disCount = 0;
    stepCW();
  }
  //********** Encoder Step ************

  if ( val = read_rotary() ) {
    stepper.setMicrostep(STEP_MODE2); // Set microstep mode
    stepper.enable();
    disCount = 0;
    stepEncoder(val);
  }
}

// End of Loop


//**********************  Rotary Encoder  ************************
// A valid CW or  CCW move returns 1 or -1, invalid returns 0.
int8_t read_rotary() {
  static int8_t rot_enc_table[] = {0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0};

  prevNextCode <<= 2;
  if (digitalRead(DATA)) prevNextCode |= 0x02;
  if (digitalRead(CLK)) prevNextCode |= 0x01;
  prevNextCode &= 0x0f;

  // If valid then store as 16 bit data.
  if  (rot_enc_table[prevNextCode] ) {
    store <<= 4;
    store |= prevNextCode;
    //if (store==0xd42b) return 1;
    //if (store==0xe817) return -1;
    if ((store & 0xff) == 0x2b) return -1;
    if ((store & 0xff) == 0x17) return 1;
  }
  return 0;
}

//***************************** Coverts hex into BCD *******************************************
//This function will covert hex into BCD
// param[in] byte
//param[out] byte

unsigned int HexToBCD(unsigned int number)
{
  unsigned char i = 0;
  unsigned int k = 0;
  while (number)
  {
    k = (k) | ((number % 10) << i * 4);
    number = number / 10;
    i++;
  }

  return (k);
}

//**************************** Zero Stepper *********************

void zeroStep() {
  do {
    stepper.move(-1);//CCW
    delayMicroseconds(setDelay);
    stepPos -= (16 / STEP_MODE1);             // Decrement stepper position
    revCount = stepPos / 3200;                // Each revolution is equal to 3200 1/16 steps
    if (revCount != revCountOld) {
      revCountOld = revCount;
      DisplayUpdate(revCount);
    }
  } while (digitalRead(button3Pin) == LOW);//End of travel sensor
  digitalWrite(Led1Pin, HIGH);
  stepPos = 0; //Zero step position
  revCount = 0;//Zero revolutions
  ZERO_FLAG = true;//set zero initialisation
  DisplayUpdate(revCount);
}

//****************************  Step Counter Clockwise ********************

void stepCCW() {
  do {
    if (digitalRead(button3Pin) == LOW) {   //Not end of travel
      digitalWrite(Led1Pin, LOW);
      delayMicroseconds(setDelay);
      stepper.move(-1);                     //CCW
      stepPos -= (16 / STEP_MODE1);         // Decrement stepper position
      revCount = stepPos / 3200;            // Each revolution is equal to 3200 1/16 steps
      if (revCount != revCountOld) {
        revCountOld = revCount;
        DisplayUpdate(revCount);
      }
    } else {                                 //End of Travel
      digitalWrite(Led1Pin, HIGH);
      stepPos = 0;                          //Set back to Starting Position
      revCount = 0;                         //Zero revolutions
      ZERO_FLAG = true;                     //set zero initialisation
    }
  } while (digitalRead(button1Pin) == HIGH);
}

//******************************* Step Clockwise *************************

void stepCW() {
  do {
    if (revCount < EOT_SOFT) {              //Soft end of travel?
      digitalWrite(Led1Pin, LOW);
      delayMicroseconds(setDelay);
      stepper.move(1);//CW
      stepPos += (16 / STEP_MODE1);         // Increment stepper position
      revCount = stepPos / 3200;            // Each revolution is equal to 3200 1/16 steps
      if (revCount != revCountOld) {
        revCountOld = revCount;
        DisplayUpdate(revCount);
      }
    } else {                                //End of Travel
      digitalWrite(Led1Pin, HIGH);
    }
  } while (digitalRead(button2Pin) == HIGH);
}

//******************************* Encoder Step *****************************

void stepEncoder(int EnStep) {
  if (EnStep == 1) { //Counter Clock Wise
    if (revCount < EOT_SOFT) {               //Soft end of travel?
      digitalWrite(Led1Pin, LOW);
      stepper.move(1);//CW
      delayMicroseconds(setDelay);
      stepPos += (16 / STEP_MODE2);         // Increment stepper position
      if (revCount != revCountOld) {
        revCountOld = revCount;
      }
      if (stepPos != stepPosOld) {
        stepPosOld = stepPos;
        DisplayUpdate(stepPos);
      }
    } else {                                //End of Travel
      digitalWrite(Led1Pin, HIGH);
    }
  }
  if (EnStep == -1) { //Encoder Step CW
    if (digitalRead(button3Pin) == LOW) {   //Not end of travel
      digitalWrite(Led1Pin, LOW);
      stepper.move(-1);                     //CCW
      delayMicroseconds(setDelay);
      stepPos -= (16 / STEP_MODE2);          // Decrement stepper position
      if (revCount != revCountOld) {
        revCountOld = revCount;
      }
      if (stepPos != stepPosOld) {
        stepPosOld = stepPos;
        DisplayUpdate(stepPos);
      }
    } else {                                 //End of Travel
      digitalWrite(Led1Pin, HIGH);
      stepPos = 0;
      ZERO_FLAG = true;                     //set zero initialisation
    }
  }
}
//******************************* Update Display *****************************

void DisplayUpdate(int long count) {

  count_one = HexToBCD(count);
  count_two = HexToBCD(count);

  mydisplay.setDigit(0, 0, ((count_two >> 12) & 0x0F), false);
  mydisplay.setDigit(0, 1, ((count_two >> 8) & 0x0F), false);
  mydisplay.setDigit(0, 2, ((count_two >> 4) & 0x0F), false);
  mydisplay.setDigit(0, 3, ((count_two >> 0) & 0x0F), false);
  mydisplay.setDigit(0, 4, ((count_one >> 12) & 0x0F), false);
  mydisplay.setDigit(0, 5, ((count_one >> 8) & 0x0F), false);
  mydisplay.setDigit(0, 6, ((count_one >> 4) & 0x0F), false);
  mydisplay.setDigit(0, 7, (count_one & 0x0F), false);
}
