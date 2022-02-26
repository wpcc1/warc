// Auto CQ Caller

// This project is based on portions of code from http://www.arduino.cc/cgi-bin/yabb2/YaBB.pl?num=1234149784 (Marcelo Shiniti Uchimura)
// and ACP loading code from https://codebender.cc/sketch:247685#ISD1700.ino (Code Bender - Grimar) 
// The rest was kludged together by John Forrest (VK3JNF)

#include <ISD1700.h>

ISD1700 chip(10);                               // Initialize chipcorder with SS at Arduino's digital pin 10
                                                // The following pins are pre-defined
                                                // in ISD1700.h
                                                // SCK_PIN   13
                                                // MISO_PIN  12
                                                // MOSI_PIN  11

// Define Input pin numbers
#define button_PTT 2                            // PTT (Press to Talk) pushbutton (Interrupt)
#define button_CQ 3                             // CQ pushutton 
#define button_SETUP 4                          // Message Setup pushutton 
#define buttonPlay 5                            // Play pushbutton
#define buttonStop 6                            // Stop pushbutton
#define buttonForward 7                         // Forward pushbutton
#define buttonRecord 8                          // Record pushbutton
#define buttonErase 9                           // Erase pushbutton

// Define Output pin numbers
#define RXLED 14                                // RX LED Pin
#define TXLED 15                                // Activate PTT, TX LED Pin
#define RECLED 16                               // Recorder Active LED pin
#define CQLED 17                                // CQ Caller Active LED Pin
#define QSOLED 18                               // QSO Active LED Pin

// Variables
int vol = 0;                                    //volume 0=MAX, 7=min
int volcon = false;                             // Volume Control true=Up, false=Down
int APC_Reg_Play = 0;                           // APC CQ Configuration Bit Storage
int APC_Reg_FT = 0;                             // APC QSO Configuration Bit Storage
int APC_Reg_Rec = 0;                            // APC Record Configuration Bit Storage
volatile int QSO_CQ_state;                      // QSO or CQ operation state (Interrupt variable)
int sensorReading;                              // Storage for QRZ delay control pot reading
unsigned long MAXdelay;                         // Maximum Listening Delay between CQ calls for QRZ response
unsigned long MINdelay;                         // Minimum Listening Delay between CQ calls for QRZ responses
unsigned long QRZdelay;                         // Listening Delay counter

void setup()
{
  Serial.begin(9600);

  // Load APC Register Bit Storage by OR masking in the appropriate bits
  // for each mode of operation.

  // Play Operation (Used in Recording and CQ modes)
  APC_Reg_Play = APC_Reg_Play | vol;            // D0, D1, D2
  APC_Reg_Play = APC_Reg_Play | 0x40;           // D4=0; D6=1 MIC Reord, No Feed Through, Output on analogue + Speaker
//  APC_Reg_Play = APC_Reg_Play | 0x80;           // D7=1; AUX ON
  APC_Reg_Play = APC_Reg_Play | 0x800;          // D11=1; EOM ON

  // Feed Thru Operation (Used in QSO Mode)
  APC_Reg_FT = APC_Reg_FT | vol;                // D0, D1, D2
//  APC_Reg_FT = APC_Reg_FT | 0x80;               // D7=1; AUX ON
  APC_Reg_FT = APC_Reg_FT | 0x100;              // D8=1; Analogue input Feed Through, Speaker Disabled, Output on analogue
  APC_Reg_FT = APC_Reg_FT | 0x800;              // D11=1; EOM ON
    
  // Record Operation (Used in Recording Mode)
  APC_Reg_Rec = APC_Reg_Rec | vol;              // D0, D1, D2
  APC_Reg_Rec = APC_Reg_Rec | 0x200;            // D9=1; Analogue Input Record, Analogue input Feed Through, AUD/AUX OFF
  APC_Reg_Rec = APC_Reg_Rec | 0x800;            // D11=1; EOM ON

  Serial.println(APC_Reg_Play,BIN);
  Serial.println(APC_Reg_FT,BIN);
  Serial.println(APC_Reg_Rec,BIN);
  
//Configure pins as inputs and enable the internal pull-up resistors
  pinMode(buttonPlay, INPUT_PULLUP);
  pinMode(buttonStop, INPUT_PULLUP);
  pinMode(buttonErase, INPUT_PULLUP);
  pinMode(buttonRecord, INPUT_PULLUP);
  pinMode(buttonForward, INPUT_PULLUP);
  pinMode(button_CQ, INPUT_PULLUP);
  pinMode(button_SETUP, INPUT_PULLUP);
  pinMode(button_PTT, INPUT_PULLUP);

  //configure pins as outputs
  pinMode(RECLED, OUTPUT);
  pinMode(RXLED, OUTPUT);
  pinMode(TXLED, OUTPUT);
  pinMode(CQLED, OUTPUT);
  pinMode(QSOLED, OUTPUT);

  // Set up Interrupt Pin
  attachInterrupt(digitalPinToInterrupt(button_PTT), checkPTT, CHANGE);

  // Set variables
  QSO_CQ_state = 1;                             // Start up in QSO state
  MAXdelay = 1500000;                           // Set maximum listening delay between CQ calls.
  MINdelay = 100000;                            // Set minimum listening delay between CQ calls.

  // Power Up the chip
  chip.pu();                                    //Enable SPI Control
  while (!chip.RDY()) {                         // Wait for ready
  }
}

void loop()
{

  checkState();                                 // Check all input buttons

  switch (QSO_CQ_state) {
 
  //********************************* QSO ***************************************
    
    case 1:
               
      digitalWrite(QSOLED, HIGH);               // QSO LED On
      digitalWrite(TXLED, LOW);                 // Outgoing PTT inactve, TX LED Off
      digitalWrite(RXLED, HIGH);                // RX LED On
 
       chip.wr_apc2(APC_Reg_FT);                //Load APC Register with Feed Thru parameters
 
      while (QSO_CQ_state == 1) {               // Wait for PTT
         
        if (digitalRead(button_PTT) == LOW){
                            
         digitalWrite(TXLED, HIGH);             // Outgoing PTT actve, TX LED on
          digitalWrite(RXLED, LOW);             // RX LED off
                         
          while ((digitalRead(button_PTT) == LOW)) { // Wait here until QSO done
          }
             
          digitalWrite(TXLED, LOW);             // Outgoing PTT inactive, TX LED off
          digitalWrite(RXLED, HIGH);            // RX LED on  
        }

        checkState();                           // Check all input buttons
      }      
      digitalWrite(RXLED, LOW);                 // RX LED off
      digitalWrite(QSOLED, LOW);                // QSO LED Off

    break;
 

 //********************************* CQ ***************************************
    
    case 2:  
              

      digitalWrite(CQLED, HIGH);                // CQ LED On
      digitalWrite(RXLED, HIGH);                // RX LED on
      
      chip.wr_apc2(APC_Reg_Play);               //Load the APC Register with Play parameters      
 
      while (QSO_CQ_state == 2) {               // Keep Calling while in CQ state
        digitalWrite(TXLED, HIGH);              // Outgoing PTT active, TX LED on
        digitalWrite(RXLED, LOW);               // RX LED Off
          
        delay(300);                             // Short delay for PTT activation
        chip.play();                            // Call CQ
        delay(500);
        
        while (!chip.RDY()) {                   // Wait for CQ Call to end
          if (digitalRead(button_CQ) == LOW){           // CQ Button  
            volAdj();        
            chip.wr_apc2(APC_Reg_Play);             //Load the APC Register with Play parameters      
            delay(500);
          }
        }
                
        delay(300);                             // Short delay for PTT deactivation
        digitalWrite(TXLED, LOW);               // Outgoing PTT inactive, TX LED off
        digitalWrite(RXLED, HIGH);              // RX LED on
      
        // QRZ delay       
        // read the sensor value:
        int sensorReading = analogRead(A5);
        // map it to a range from 0 to 100:
        QRZdelay = map(sensorReading, 0, 1023, MINdelay, MAXdelay);
                
        while(QRZdelay>0){
                                         
          checkState();                         // Check all input buttons

          if (QSO_CQ_state != 2) {              // Check if still in CQ state
          
            delay(300);                         // Short delay 
                        
            QRZdelay=0;
          }else{
            --QRZdelay;


//Serial.println(QRZdelay);
            
          }
        }
      }  
 
      digitalWrite(RXLED, LOW);                 // RX LED Off
      digitalWrite(CQLED, LOW);                 // CQ LED Off

      break;
   
    
    default:
    
  //************************************** Record/Playback Functions **************************
    
    digitalWrite(RECLED, HIGH);                 // Setup LED on

    chip.wr_apc2(APC_Reg_Play);                 //Load the APC Register with Play parameters      
   
    while (QSO_CQ_state == 3) {                 // Keep in Recording while in Recording state
            
      if (digitalRead(button_SETUP) == LOW) {   // Setup Button Pressed again for Volume change
        volAdj();        
        chip.wr_apc2(APC_Reg_Play);             //Load the APC Register with Play parameters      
        delay(500);
      }
      
      if (digitalRead(buttonPlay) == LOW) {     // Play current recording file
        chip.play();
        delay(500);
      }
      if (digitalRead(buttonStop) == LOW) {     // Stop Recording or Playing current File
        chip.stop();
        chip.wr_apc2(APC_Reg_Play);             //Load the APC Register with Play parameters      
       }
      if (digitalRead(buttonErase) == LOW) {    // Erase current Recording File
        chip.erase();
      }
      if (digitalRead(buttonRecord) == LOW) {   // Record to current file
        chip.wr_apc2(APC_Reg_Rec);              //Load the APC Register with Record parameters      
        chip.rec();
      }
      if (digitalRead(buttonForward) == LOW) {  // Step forward through recording files
        chip.fwd();
      }
      checkState();                             // Check system State
    }
    digitalWrite(RECLED, LOW);                  // Setup LED Off    
    break;
  }
}

//********************************** Subroutines *************************

// Check PTT Button // 

void checkPTT(){                                // PTT interrupt
  if (QSO_CQ_state != 1){                       // Switch State to QSO State
    QSO_CQ_state = 1;                           // Switch State to QSO State
    chip.stop();                                // Stop CQ Caller
    delay(500);
  }
}

// Check Other Buttons //
  
void checkState()
{
  if (digitalRead(button_CQ) == LOW){           // CQ Button  
        QSO_CQ_state = 2;                       // Switch State to CQ Caller
  }
  if (digitalRead(button_SETUP) == LOW) {       //  Setup Button
    QSO_CQ_state = 3;                           //  Switch state to Message Recording
  }
}

// Volume Adjust

void volAdj()
{
  
  if (volcon==true) {                     // Volume Up
    if (vol<7) {
      vol=++vol;
    }else {
      volcon=false;
    }
  }
  if (volcon==false) {                    // Volume Down
    if (vol>0) {
      vol=--vol;
    }else {
      volcon=true;
      vol=++vol;          
    }
  }
                
  // Update Play Register
  APC_Reg_Play = APC_Reg_Play >> 3;       // Shift data bits D0, D1, D2 out
  APC_Reg_Play = APC_Reg_Play << 3;       // Shift databack 3 bits back
  APC_Reg_Play = APC_Reg_Play | vol;      // OR data bits D0, D1, D2
  // Update Feed Thru Register
  APC_Reg_FT = APC_Reg_FT >> 3;           // Shift data bits D0, D1, D2 out
  APC_Reg_FT = APC_Reg_FT << 3;           // Shift databack 3 bits back
  APC_Reg_FT = APC_Reg_FT | vol;          // OR data bits D0, D1, D2
  // Update Record Register      
  APC_Reg_Rec = APC_Reg_Rec >> 3;         // Shift data bits D0, D1, D2 out
  APC_Reg_Rec = APC_Reg_Rec << 3;         // Shift databack 3 bits back
  APC_Reg_Rec = APC_Reg_Rec | vol;        // OR data bits D0, D1, D2
  
}
