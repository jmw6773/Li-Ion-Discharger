/*
* Battery Capacity Checker
* Uses Nokia 5110 Display
* Uses an 8 Ohm power resister as both the shunt and load
*
* YouTube Video: https://www.youtube.com/embed/qtws6VSIoYk
* http://AdamWelch.Uk
* Original Design and Code by Adam Welch
* Modified and extended features added by Jonathan Wells
*
* Required Library - LCD5110_Graph.h - http://www.rinkydinkelectronics.com/library.php?id=47
*/

//#define HUMAN_DEBUG //uncomment to send serial info as human readable; comment for ease of computer parsing
#define NUM_BATTERIES 4

/* Display Setup */
#include "LCD5110_Graph.h"
#include <PWM.h>
//connect CS to GND on the LCD
LCD5110 myGLCD(13, 11, 12, 10, -1);  // Setup Nokia 5110 Screen SCLK/CLK=13, DIN/MOSI/DATA=11, DC/CS=12, RST=10, Chip Select/CE/SCE=12,

extern uint8_t SmallFont[];
extern uint8_t MediumNumbers[];
/* End Display Setup */


/* Temperature Setup */
#include <OneWire.h>
#include <DallasTemperature.h>

#define TEMP_PIN A5
#define MAX_TEMP 70 //Turn off temp in degrees C
OneWire oneWire(TEMP_PIN);
DallasTemperature temp_sensor(&oneWire);
DeviceAddress thermometerAddr;

boolean overTemp = false;
float currentTemp;

byte fanSpeed;
#define FAN_PWM 9

#define FAN_25  35
#define FAN_50  40
#define FAN_75  45
#define FAN_100 50
/* End Temp Setup */


/* Button Setup */
#include <PinChangeInterrupt.h>
#include <PinChangeInterruptBoards.h>
#include <PinChangeInterruptPins.h>
#include <PinChangeInterruptSettings.h>

#define UP_BUTTON 2
#define DOWN_BUTTON 3
#define OK_BUTTON 4
byte batterySetup = 2 * NUM_BATTERIES;
/* End Button Setup */


#define MINIMUM_BATTERY_VOLTAGE 2.9
#define MAXIMUM_BATTERY_VOLTAGE 4.2

#define BUZZER_PIN A0

#define interval 1000  //Interval (ms) between measurements

const int gatePins[NUM_BATTERIES] = {5, 6, 7, 8};
const int highPins[NUM_BATTERIES] = {A1, A2, A3, A4};

enum status_states {
  NO_BATTERY=0,
  SETUP_BEGIN=1,
  SETUP_END=2,
  RUNNING=3,
  FINISHED=4,
  ERROR_STATE
};

byte buzzerCycle = 0;
byte printInfoScreen = 0;
enum status_states curr_status[NUM_BATTERIES];

float shuntRes[NUM_BATTERIES] = {8.29, 8.29, 8.29, 8.29}; // In Ohms - Shunt resistor resistance
float current[NUM_BATTERIES];
float battLow[NUM_BATTERIES];
float battVolt[NUM_BATTERIES];
float mAh[NUM_BATTERIES];

float voltRef = 0.00; // Reference voltage (probe your 5V pin)

unsigned long previousMillis[NUM_BATTERIES] = {0, 0, 0, 0};
unsigned long startTime[NUM_BATTERIES];

/* vccRead - Reads the AVcc voltage and compares it to the internal 1.1v reference 
 *    - Having a low pass filter on AVcc will help stabilize the AVcc reading
 * @param - delayTime - the time to delay to wait for the internal 1.1v reference to settle
 * @return - the milliVolts of AVcc
 */
static int vccRead(byte delayTime = 100)
{
  // Read 1.1V reference against AVcc
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);  //bitSet(ADMUX,3);
  delay(delayTime); // Wait for VREF to settle
  bitSet(ADCSRA, ADSC); // Convert
  while (bit_is_set(ADCSRA, ADSC));
  word x = ADC;
  //return x ? 1126400L / x : -1;
  return x ? 1125300L / x : -1;
}

void setup()
{
  //Serial.begin(115200);
  UCSR0B = (1<<TXEN0);//enable UART TX       | (1<<RXEN0); /Enable UART RX
  UBRR0L = 8; //sets BAUD to 115200

  //pinMode(UP_BUTTON, INPUT_PULLUP);
  //pinMode(DOWN_BUTTON, INPUT_PULLUP);
  //pinMode(OK_BUTTON, INPUT_PULLUP);
  PORTD = (1<<PORTD2)|(1<<PORTD3)|(1<<PORTD4);
  
  attachPCINT(digitalPinToPCINT(UP_BUTTON), upButton, RISING);
  attachPCINT(digitalPinToPCINT(DOWN_BUTTON), downButton, RISING);
  attachPCINT(digitalPinToPCINT(OK_BUTTON), okButton, RISING);

  InitTimersSafe();
  bool success = SetPinFrequencySafe(FAN_PWM, 20000);
  pwmWrite(FAN_PWM, 255); // fan full on
  
  for (int i = 0; i < NUM_BATTERIES; i++)
  {
    digitalWrite(gatePins[i], LOW);
    curr_status[i] = NO_BATTERY;
    battLow[i] = 3.5;
    current[i] = 0.0;
    battVolt[i] = 0.0;
    mAh[i] = 0.0;
  }

  for (int i = 0; i < 10; i++)
  {
    voltRef += vccRead();
  }
  voltRef = voltRef / 10000.0;

  //start the Dallas Temp sensor
  temp_sensor.begin();
  delay(200);
  
  if (!temp_sensor.getAddress(thermometerAddr, 0))
  {
    #ifdef HUMAN_DEBUG
      Serial.println(F("Unable to find the temp sensor."));
    #endif
  }
  
  if (temp_sensor.getResolution(thermometerAddr) != 10)
  {
    temp_sensor.setResolution(thermometerAddr, 10, true);
  }
  currentTemp = readTemp();
  
#ifdef HUMAN_DEBUG
  Serial.println(F("Battery Capacity Checker v2.1"));
  Serial.print(F("Arduino Voltage: "));
  Serial.println(voltRef);
  Serial.print(F("Heat Sink Temperature: "));
  Serial.println(currentTemp);
#endif

  myGLCD.InitLCD(); //initialize LCD with default contrast of 70
  myGLCD.setContrast(60);
  myGLCD.setFont(SmallFont); // Set default font size. tinyFont 4x6, smallFont 6x8, mediumNumber 12x16, bigNumbers 14x24
  myGLCD.clrScr();

  myGLCD.print(F("Voltage"), CENTER, 0);
  myGLCD.printNumF(voltRef, 2, CENTER, 10);
  myGLCD.print(F("Temp:"), CENTER, 20);
  myGLCD.printNumF(currentTemp, 1, CENTER, 30);
  myGLCD.print(F("Please Wait"), CENTER, 40);
  myGLCD.update();
  delay(4000);
  myGLCD.clrScr();
  
  setFanSpeed();
}

float readVoltage(int PIN)
{
  int sumVolt = 0;
  for (int i = 0; i < 10; i++)
  {
    sumVolt += analogRead(PIN);
  }
  return sumVolt / 10230.0 * voltRef;
}

/* readTemp - reads/returns the temperature
 *  @return - the temperature in degrees C
 */
float readTemp()
{
  temp_sensor.requestTemperatures(); // Send the command to get temperatures
  return temp_sensor.getTempC(thermometerAddr);
}

void loop()
{
  //record when the loop starts so you can calculate the sleep time for 1 sec
  unsigned long loopStart = millis();
  currentTemp = readTemp();

  //if temp sensor fails, a large negative number is returned. Set this to a value that will never reach.
  //if failure is detected, try to reset the sensor and turn the fan on full until next read
  if (currentTemp < -50)
  {
    //temp sensor error, fan full on!!
    temp_sensor.begin();
    delay(200);
    pwmWrite(FAN_PWM, 255);
#ifdef HUMAN_DEBUG
    Serial.println(F("Temp Sensor error. Re-initializing and setting fans to full on."));
#endif
  }

  //if over temperature is read, sound alarm and shut down all Mosfets until device is restarted (no auto-restart)
  if (currentTemp > MAX_TEMP || overTemp)
  {
  #ifdef HUMAN_DEBUG
    Serial.print(F("Over Temperature!!! -- "));
    Serial.println(currentTemp);
 #endif

    overTemp = true;
 
    // over temp - turn all gates off until device restart
    for (int i = 0; i < NUM_BATTERIES; i++)
    {
      digitalWrite(gatePins[i], LOW);
    }

    setFanSpeed();

    //only sound alarm while over max temperature
    if (currentTemp > MAX_TEMP)
      buzzerCycle = 4;
  }
  else
  {
    //set fan speed, determined by read teperature
    setFanSpeed();
    for (int i = 0; i < NUM_BATTERIES; i++)
    {
      //read voltage for battery
      battVolt[i] = readVoltage(highPins[i]);

      //battery inserted, flag battery for SETUP_BEGIN state
      if (curr_status[i] == NO_BATTERY && battVolt[i] > MINIMUM_BATTERY_VOLTAGE)
      {
        curr_status[i] = SETUP_BEGIN;
        if (battVolt[i] > 3.6)
          battLow[i] = 3.6;
        else
          battLow[i] = battVolt[i];
        batterySetup = i;
      }
      //setup has completed, flag battery for RUNNING state
      else if (curr_status[i] == SETUP_END)
      {
        current[i] = 0.0;
        mAh[i] = 0.0;
        previousMillis[i] = millis();
        startTime[i] = previousMillis[i];
  
        if (battLow[i] <= MAXIMUM_BATTERY_VOLTAGE && battLow[i] >= MINIMUM_BATTERY_VOLTAGE)
          curr_status[i] = RUNNING;
        else
          curr_status[i] = ERROR_STATE;
      }
      //battery in RUNNING state. Sum the discharge and log values
      else if (curr_status[i] == RUNNING && battVolt[i] >= battLow[i])
      {
        digitalWrite(gatePins[i], HIGH);

        current[i] = (battVolt[i]) / shuntRes[i];
        mAh[i] = mAh[i] + (current[i] * 1000.0) * ((millis() - previousMillis[i]) / 3600000.0);
        previousMillis[i] = millis();
      }
      //Battery voltage has fallen below discharge value or no battery is inserted.
      else if (battVolt[i] < battLow[i])
      {
        digitalWrite(gatePins[i], LOW);

        
        if ((int)battVolt[i] == 0) //battery was removed
        {
          //if battery was flagged for setup, remove flag
          if (batterySetup == i)
            batterySetup = NUM_BATTERIES + NUM_BATTERIES;

          //Reset Values
          current[i] = 0;
          mAh[i] = 0;
          startTime[i] = 0;
          curr_status[i] = NO_BATTERY;
        }
        //Flag battery as FINISHED and sound alarm
        else if (curr_status[i] == RUNNING)
        {
          buzzerCycle = 2;
          startTime[i] = millis() - startTime[i];
          curr_status[i] = FINISHED;
        }
      }
#ifndef HUMAN_DEBUG
      //Send battery details to over serial for machine logging
      Serial.print(F("-B")); //battery number
      Serial.print(i+1); //battery number
      Serial.print(F(",")); //delimeter
      Serial.print(curr_status[i]);
      Serial.print(F(","));
      Serial.print(battVolt[i]);
      Serial.print(F(","));
      Serial.print(current[i]); //current
      Serial.print(F(","));
      Serial.print(mAh[i]); //mAh
      Serial.print(F(","));
      if(curr_status[i] == RUNNING)
        Serial.print(millis() - startTime[i]); //current runtime for battery
      else if (curr_status[i] == FINISHED)
        Serial.print(startTime[i]); //current runtime for battery
      else
        Serial.print(0); //current runtime for battery
      Serial.println(F("-"));
#endif
    }
  }
  //update the LCD screen
  printScreen();
  
  //'interval' seconds minus how long it took this function to run
  //basically loopStart is how much time remains before 'interval' is hit.
  //the remainder time can be used for other tasks.
  loopStart = interval - (millis() - loopStart); 

#ifdef HUMAN_DEBUG
  Serial.print(F("Arduino Vcc: "));
  Serial.print(voltRef);
  Serial.print(F("     Temp: "));
  Serial.print(currentTemp);
  Serial.print(F("     Loop Length (ms): "));
  Serial.println(loopStart);
#else
//Send Arduono stats over serial for machine logging
  Serial.print(F("-V,"));
  Serial.print(voltRef);
  Serial.println(F("-"));
  Serial.print(F("-T,"));
  Serial.print(currentTemp);
  Serial.println(F("-"));
  Serial.print(F("-F,"));
  Serial.print((int)(fanSpeed / 255.0f * 100));
  Serial.println(F("-"));
#endif

  //if the buzzer should be sound, sound it for the remaining time
  if (buzzerCycle > 0)
  {
    int toneLength = loopStart / 2;
    tone(BUZZER_PIN,349,toneLength); //play note f
    delay(toneLength);
    tone(BUZZER_PIN,262,toneLength); //play note c
    delay(toneLength);
    buzzerCycle--;
  }
  //if batteries are being discharged, re-calculate the AVcc for the remaining time
  else if (loopStart > 0)
  {
    loopStart = loopStart / 10; // devide by the number of times to average VCC
    for (int i = 0; i < 10; i++)
    {
      voltRef += vccRead(loopStart);
    }
    voltRef = voltRef / 10000.0;
  }
#ifdef HUMAN_DEBUG
  Serial.println();
  Serial.println();
#endif
}

/* setFanSpeed - set the PWM frequency for the fan
 */
void setFanSpeed()
{
  /*  #define FAN_25  35
      #define FAN_50  40
      #define FAN_75  45
      #define FAN_100 50
   */
   if (currentTemp < FAN_25)
      fanSpeed = 0;   //Off - 0%
   else if (currentTemp < FAN_50)
      fanSpeed = 64;  //25%
   else if (currentTemp < FAN_75)
      fanSpeed = 128; //50%
   else if (currentTemp < FAN_100)
      fanSpeed = 192; //75%
   else
      fanSpeed = 255; //full on

#ifdef HUMAN_DEBUG
    Serial.print(F("Fan Speed: "));
    Serial.print((int)(fanSpeed / 255.0f * 100));
    Serial.println(F("%"));
#endif

   pwmWrite(FAN_PWM, fanSpeed);
}

/* printScreen - check if any batteries have the setup flag
 *  if true, display the setup menu
 *  if false, display the battery information, then increment the next battery to display on the next loop
 */
void printScreen()
{
  //if over temp, batteries are off. Show over temperature status
  if (overTemp)
  {
    myGLCD.clrScr();
    myGLCD.print(F("Over Temp"), CENTER, 10);
    myGLCD.print(F("-----------"), CENTER, 20);

    myGLCD.printNumF(currentTemp, 1, CENTER, 30);
    myGLCD.print(F("C"), 77, 30);
    myGLCD.update();
    return;
  }

  //if a battery is in setup mode, display the setup menu
  if (batterySetup < NUM_BATTERIES && batterySetup >= 0)
  {
    displayMenu(batterySetup);
    return;
  }

  byte numBatteriesFound = 0;
  
  //find any that need setup, display the menu if any are found
  for (int i=0; i < NUM_BATTERIES; i++)
  {
     if(curr_status[i] == SETUP_BEGIN)
     {
       batterySetup = i;
       displayMenu(batterySetup);
       return;
     }
     else if (battVolt[i] > 0 && (curr_status[i] == RUNNING || curr_status[i] == FINISHED))
     {
       numBatteriesFound++;
     }
     
  }

  //if no batteries are inserted, display "No Battery Found" message
  if (numBatteriesFound == 0)
  {
    myGLCD.clrScr();
    
    myGLCD.print(F("T:"), 48, 0);
    myGLCD.printNumF(currentTemp, 1, RIGHT, 0);
    
    myGLCD.print(F("No Battery"), CENTER, 10);
    myGLCD.print(F("Found"), CENTER, 20);

    myGLCD.setFont(SmallFont);
    myGLCD.print(F("Insert Battery"), CENTER, 40);
    myGLCD.update();
    return;
  }
  //if the current battery to show the status of is blank, skip it
  else if (curr_status[printInfoScreen] == NO_BATTERY)
  {
    printInfoScreen = (printInfoScreen+1) % NUM_BATTERIES;
    printScreen();
    return;
  }
  //display the current battery
  else
  {
    if (curr_status[printInfoScreen] == RUNNING)
    {
      unsigned long currentTime = millis() - startTime[printInfoScreen];
      int hours = currentTime / 3600000;
      int minutes = (currentTime - (hours * 3600000)) / 60000;
      int seconds = (currentTime - (hours * 3600000) - (minutes * 60000)) / 1000;

      myGLCD.clrScr();
      
      myGLCD.print(F("B:"), LEFT, 0);
      myGLCD.printNumI(printInfoScreen+1, 12, 0);
      myGLCD.print(F("T:"), 48, 0);
      myGLCD.printNumF(currentTemp, 1, RIGHT, 0);
      
      myGLCD.print(F("00000000"), 15, 10);
      myGLCD.printNumI(hours, hours < 10 ? 21 : 15, 10);
      myGLCD.print(F("h"), 27, 10);
      myGLCD.printNumI(minutes, minutes < 10 ? 39 : 33, 10);
      myGLCD.print(F("m"), 45, 10);
      myGLCD.printNumI(seconds, seconds < 10 ? 57 : 51, 10);
      myGLCD.print(F("s"), 63, 10);
      //myGLCD.print(battLow[printInfoScreen] == STORAGE_VOLT ? "S" : "D", 77, 0);

      myGLCD.print(F("Voltage:"), LEFT, 20);
      myGLCD.printNumF(battVolt[printInfoScreen], 2, 50, 20);
      myGLCD.print(F("v"), RIGHT, 20);
      myGLCD.print(F("Current:"), LEFT, 30);
      myGLCD.printNumI(current[printInfoScreen] * 1000, 50, 30);
      myGLCD.print(F("mA"), RIGHT, 30);

      myGLCD.print(F("Cap:"), LEFT, 40);
      myGLCD.printNumI(mAh[printInfoScreen], 36, 40);
      myGLCD.print(F("mAh"), RIGHT, 40);
      myGLCD.update();

#ifdef HUMAN_DEBUG_BATT
      Serial.print(printInfoScreen+1);
      Serial.print(F("--  V: "));
      Serial.print(battVolt[printInfoScreen]);
      Serial.print(F("\tC: "));
      Serial.print(current[printInfoScreen]);
      Serial.print(F("\tmAh: "));
      Serial.print(mAh[printInfoScreen]);
      Serial.print(F("     Runtime: "));
      Serial.print(hours);
      Serial.print(F("h"));
      Serial.print(minutes);
      Serial.print(F("m"));
      Serial.print(seconds);
      Serial.println(F("s"));
#endif
    }
    else //FINISHED
    {
      myGLCD.clrScr();
      myGLCD.print(F("B:"), LEFT, 0);
      myGLCD.printNumI(printInfoScreen+1, 12, 0);
      myGLCD.print(F("T:"), 48, 0);
      myGLCD.printNumF(currentTemp, 1, RIGHT, 0);
      
      myGLCD.print(F("Complete"), CENTER, 10);
      myGLCD.print(F("Voltage:"), LEFT, 20);
      myGLCD.printNumF(battVolt[printInfoScreen], 2, 50, 20);
      myGLCD.print(F("v"), RIGHT, 20);
      myGLCD.setFont(MediumNumbers);
      myGLCD.printNumI(mAh[printInfoScreen], CENTER, 31);
      myGLCD.setFont(SmallFont);
      myGLCD.print(F("mAh"), RIGHT, 40);
      myGLCD.update();
    }
  }
  printInfoScreen = (printInfoScreen+1) % NUM_BATTERIES;
}

/* displayMenu - display the setup menu
 * @param newBattery - the battery you are setting up
 */
void displayMenu(int newBattery)
{
#ifdef HUMAN_DEBUG
    Serial.print(F("Display menu for battery #"));
    Serial.println(newBattery+1);
#endif

   myGLCD.clrScr();
   myGLCD.print(F("Setup Batt:"), 4, 0);
   myGLCD.printNumI(newBattery+1, 74, 0);
   myGLCD.print(F("Discharge To"), CENTER, 15);
   
   myGLCD.printNumF(battLow[newBattery], 1, 36, 25);
   myGLCD.print(F(" v"), 54, 25);
   
   myGLCD.setFont(SmallFont);
   myGLCD.print(F("Current V:"), 0, 40);
   myGLCD.printNumF(battVolt[newBattery], 1, 60, 40);
   myGLCD.update();
}

/* upButton - the up button was pressed
 */
void upButton()
{
  if (battLow[batterySetup]+0.1 <= MAXIMUM_BATTERY_VOLTAGE && battLow[batterySetup]+0.1 <= battVolt[batterySetup])
  {
     battLow[batterySetup] += 0.1;
     displayMenu(batterySetup);
  }
}

/* downButton - the down button was pressed
 */
void downButton()
{
  if (battLow[batterySetup]-0.1 >= MINIMUM_BATTERY_VOLTAGE)
  {  
    battLow[batterySetup] -= 0.1;
     displayMenu(batterySetup);
  }
}

/* okButton - the ok button was pressed
 */
void okButton()
{
  if (batterySetup > NUM_BATTERIES || batterySetup < 0)
    return;

#ifdef HUMAN_DEBUG
  Serial.print(F("Setup for battery "));
  Serial.print(batterySetup+1);
  Serial.print(F(" complete.    "));
  Serial.print(F("Discharge Volt: "));
  Serial.print(battLow[batterySetup]);
#endif
  
  curr_status[batterySetup] = SETUP_END;
  batterySetup = NUM_BATTERIES + NUM_BATTERIES;
}

