/* 
 * VU Stick V1.0
 * by: Naked Ninja
 * 
 * Uploading:
 *  - Board:      Arduino Pro Mini
 *  - Processor:  ATmega328P (5V, 16MHz)
 *  - Programme:  AVR ISP
 * 
 * Connections:
 *  - Buttons:
 *    * Button 1 (select) - D2
 *    * Button 2 (prev)   - D3
 *    * Button 3 (next)   - D4
 *    (The buttons make use of the internal pullup in the ATmega328P)
 *  - Display:
 *    * GND - GND
 *    * VCC - 5V
 *    * SCL - A5 (SCL)
 *    * SDA - A4 (SDA)
 *  - Tea5767:
 *    * VCC - 5V
 *    * SDA - A4 (SDA)
 *    * SCL - A5 (SCL)
 *    * GND - GND
 *  - RGB LED Strip:
 *    * VCC   - 5V
 *    * DATA  - D9
 *    * GND   - GND
 * 
 * Tea5767 Audio Sampling ciruit:
 * 
 *                            [5V]----(10K Ohm)---|     |-------------|
 * |----------|                                   |     | ATmega328P  |
 * |        L |--(1k Ohm)---|          +|10uF|-   |     |             |
 * |TEA5767   |             |-------|---|Cap |----|-----|A3           |
 * |        R |--(1k Ohm)---|       |             |     |             |
 * |----------|                     |             |     |             |
 *                     |(100K Ohm)--|             |     |             |
 *                     |                          |     |             |
 *               [GND]-|--------------(10K Ohm)---|     |-------------|
 *                            
 * 
 */

//Libraries
#include <avr/pgmspace.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <SPI.h>
#include <TEA5767.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>

//Serial debugging
#define DEBUG false             // Flag to enable/disable debugging
#define Serial if(DEBUG)Serial 

//Preset FM Radio Stations.
char* const radioStations[][2] PROGMEM= {
  {"8860", "Slam FM"},        //88.6 MHz
  {"8930", "Radio West"},     //89.3 MHz
  {"9050", "Arrow Jazz FM "}, //90.5 MHz
  {"9180", "FunX"},           //91.8 MHz
  {"9390", "Megastad FM"},    //93.9 MHz 
  {"9470", "Radio 4 "},       //94.7 MHz
  {"9680", "3FM"},            //96.8 MHz
  {"10040", "Q Music"},       //100.4 MHz
  {"10150", "Sky Radio"},     //101.5 MHz
  {"10270", "Radio 538"}      //102.7 MHz
};

//Preset array size calculation
//#define ARRAY_COLUMNS ((int) sizeof(radioStations[0]) / sizeof(radioStations[0][0]) )
#define ARRAY_ROWS    ((int) sizeof(radioStations)    / sizeof(radioStations[0])    )

//Button pin definitions 
#define BUTTON_SELECT  2
#define BUTTON_PREV  3
#define BUTTON_NEXT  4
#define BUTTON_DEBOUNCE  100

//0.91 inch OLED display settings
#define SCREEN_WIDTH  128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32  // OLED display height, in pixels
#define OLED_RESET    4

//EEPROM check value & default EEPROM values
#define EEPROM_CHECK        8888  // EEPROM Flag
#define DEFAULT_FIXED_FM    0     // Default fixed FM station
#define DEFAULT_MANUAL_FM   7600  // Default manual FM frequency
#define DEFAULT_CALIBRATION 140   // Default RGB Calibration value
#define DEFAULT_LAST_MODE   0     // 0 = Fixed FM, 1 = Manual FM

//Last Mode options
#define FIXED_FM  0
#define MANUAL_FM 1

//Display Modes
#define DISPLAY_HOME_SCREEN 0 // Home/Default screen
#define DISPLAY_MENU    1     // Menu screen
#define DISPLAY_FIXED_FM  2   // Fixed FM switching screen
#define DISPLAY_MANUAL_FM 3   // Manual FM switching screen
#define DISPLAY_CALIBRATE 4   // Calibration option screen
#define DISPLAY_CALIBRATE2  5 // Manual or Fixed FM screen
#define DISPLAY_CALIBRATE3  6 // Fixed calibration success screen

//RGB settings
#define SAMPLE_PIN  A3        // Audio sampling pin
#define SAMPLE_WINDOW  25     // Sample window width in mS (50 mS = 20Hz)
#define PEAK_FALL   6         // Rate of peak falling dot
#define NUM_LEDS    60        // Number of RGB LEDS  
#define RGB_OUT_PIN 9         // RGB data pin
#define BRIGHTNESS  25        // RGB brightness setting. The higher the brightness, the higher the power consumption.
#define COLOR_START 0         // Required value for RGB LED control
#define COLOR_FROM  0         // Required value for RGB LED control
#define COLOR_TO    255       // Required value for RGB LED control
#define CALIBRATION_MIN   50  // Minimum calibration value
#define CALIBRATION_MAX   500 // Maximum calibration value
#define CALIBRATION_LOOPS 100 // Amount of time RGBControl() is executed to get samples.
#define CALIBRATION_STEPS 10  // 

//Timeouts
#define TIMEOUT_MENU  10000  // Main menu timeout time in mS

//Class creations
TEA5767 radio;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_NeoPixel RGB_strip = Adafruit_NeoPixel(NUM_LEDS, RGB_OUT_PIN, NEO_GRB + NEO_KHZ800);

//Variables
int displayMode, displayState1, displayState2, displayState3, displayState4, selected, selected2;
int fixedFM, manualFM, calibration, lastMode;
int peakToPeakMem, peak, dotCount;

//Flags
bool menuFlag = false;

//Interrupt function to exit home/default screen
void pin_ISR(){
  delay(BUTTON_DEBOUNCE);                 
  if (digitalRead(BUTTON_SELECT) == LOW) {
    menuFlag = true;
    detachInterrupt(digitalPinToInterrupt(BUTTON_SELECT));
  }
  else {
    menuFlag = false;
    attachInterrupt(digitalPinToInterrupt(BUTTON_SELECT), pin_ISR, FALLING);
  }
}

//RGB LED calibration value
void calibrate(){ 
  Serial.println(F("calibrate()"));
  peak = 0;
  peakToPeakMem = 0;
  for (int i = 0; i < CALIBRATION_LOOPS; i++){
    RGBControl();
    calibration = (10*round(peakToPeakMem/10)) + CALIBRATION_STEPS;
  }
  Serial.println(calibration);
  peak = 0; 

  Serial.println(F("\tCalibration value recalibrated"));
  Serial.println();
}

//TEA5767 initialization
void radioSetup(){
  Serial.println(F("radioSetup()"));
  radio.init();                     // Initialize the Radio
  radio.setMono(false);             // Set mono as false
  radio.debugEnable(false);         // Enable information to the Serial port
  Serial.println(F("\tradio set"));
     
  if(lastMode == FIXED_FM){
    radio.setBandFrequency(RADIO_BAND_FM , (atoi((char *) pgm_read_word (&radioStations[fixedFM][0]))));  //Send set frequency to TEA module
    Serial.print(F("\tSet frequentie: "));
    Serial.println(radio.getFrequency());
  }
  else if (lastMode == MANUAL_FM){
    radio.setBandFrequency(RADIO_BAND_FM , manualFM);  //Send set frequency to TEA module
    Serial.print(F("\tSet frequentie: "));
    Serial.println(radio.getFrequency());
  } 
  Serial.println();
}

//Button control funtion
void buttonControl(){
  Serial.println(F("buttonControl()"));
  
  // Clear RGB LED Strip
  for(int i = RGB_strip.numPixels(); i >= 0; i--) {
    RGB_strip.setPixelColor(i, 0); 
  }
  RGB_strip.show();

  // Set variables and timers
  bool check = false;
  unsigned long buffT = millis();     //Buffer starter time
  unsigned long currentT = millis();  //Buffer current time
  selected = 1;
  selected2 = 1;
  displayState1 = 1;
  displayState2 = 0;
  displayState3 = 1;
  displayState4 = 0;
  displayMode = DISPLAY_MENU;

  // Display menu
  displayControl(displayMode);
  delay(500);                   // Debounce to ensure it enters menu properly

  // Main button menu loop
  Serial.println(F("\tEntering button loop"));
  while(currentT<(buffT+TIMEOUT_MENU)){       //Go into while loop while current time is smaller than starter time + TIMEOUT_MENU
    if (selected2 == 1 && displayMode == DISPLAY_CALIBRATE2) {
      RGBControl();
    }
    
    if (digitalRead(BUTTON_SELECT) == LOW) { 
      delay(BUTTON_DEBOUNCE);                
      if (digitalRead(BUTTON_SELECT) == LOW) {
        if (displayMode == DISPLAY_MENU) {
          buffT = millis();             //Update starter time
          switch(selected){
            case 1: //Fixed FM
              displayMode = DISPLAY_FIXED_FM;
              lastMode = FIXED_FM;
              radio.setBandFrequency(RADIO_BAND_FM , (atoi((char *) pgm_read_word (&radioStations[fixedFM][0]))));  //Send set frequency to TEA module
              Serial.print(F("\tSet frequentie: "));
              Serial.println(radio.getFrequency());
              break;
            case 2: //Manual FM
              displayMode = DISPLAY_MANUAL_FM;
              lastMode = MANUAL_FM;
              radio.setBandFrequency(RADIO_BAND_FM , manualFM);  //Send set frequency to TEA module
              Serial.print(F("\tSet frequentie: "));
              Serial.println(radio.getFrequency()); 
              break;
            case 3: //Calibrate
              displayMode = DISPLAY_CALIBRATE;
              Serial.print(F("\tEntering calibrate menu"));
              break;
            case 4: //Exit
              check = true;
              break;
          }
          displayControl(displayMode);
        }
        else if (displayMode == DISPLAY_FIXED_FM) {
          displayMode = DISPLAY_MENU;
        }
        else if (displayMode == DISPLAY_MANUAL_FM) {
          displayMode = DISPLAY_MENU;
        }
        else if (displayMode == DISPLAY_CALIBRATE) {
          if (selected2 == 3){      // exit
            displayMode = DISPLAY_MENU;
          }
          else if (selected2 == 2) { // automatic
            displayMode = DISPLAY_CALIBRATE2;
          }
          else if (selected2 == 1) { // manual
            displayMode = DISPLAY_CALIBRATE2;
          }
        }
        else if (displayMode == DISPLAY_CALIBRATE2) {
          if (selected2 == 1) {
            displayMode = DISPLAY_CALIBRATE;
          }
        }
        displayControl(displayMode);
      }
    }
    if (digitalRead(BUTTON_PREV) == LOW) {   
      delay(BUTTON_DEBOUNCE);                
      if (digitalRead(BUTTON_PREV) == LOW) { 
        buffT = millis();                     //Update starter time
        Serial.println(F("\tBUTTON_PREV"));
        if (displayMode == DISPLAY_MENU) {
          displayState1 = 1;
          if (displayState2 <= 0) {
            displayState2 = 3;
          } else {
            displayState2--;
          }
        }
        else if (displayMode == DISPLAY_FIXED_FM) {
          if (fixedFM > 0) {        // If fixed station is higer than 0
            fixedFM = fixedFM - 1;  // Move to previous in list
          }
          else if(fixedFM <= 0){     // If fixed station is smaller than or equal to 0
            fixedFM = (ARRAY_ROWS-1);// Frequency set to last value in list
          }
          radio.setBandFrequency(RADIO_BAND_FM , (atoi((char *) pgm_read_word (&radioStations[fixedFM][0]))));  //Send set frequency to TEA module
          Serial.print(F("\tSet frequentie: "));
          Serial.println(radio.getFrequency());
        }
        else if (displayMode == DISPLAY_MANUAL_FM) {
          if (manualFM > 7600) {      // If frequency is higer than 7600
            manualFM = manualFM - 10; // Substract 10
          }
          else if(manualFM <= 7600){  // If frequency is 7600
            manualFM = 10800;         // Frequency set to 10800
          }
          radio.setBandFrequency(RADIO_BAND_FM , manualFM);  //Send set frequency to TEA module
          Serial.print(F("\tSet frequentie: "));
          Serial.println(radio.getFrequency());
        }
        else if (displayMode == DISPLAY_CALIBRATE) {
          displayState3 = 1;
          if (displayState4 <= 0) {
            displayState4 = 2;
          } else {
            displayState4--;
          }
        }
        else if (displayMode == DISPLAY_CALIBRATE2) {
          if (calibration > CALIBRATION_MIN) {     
            calibration = calibration - CALIBRATION_STEPS;
          }
          else if(calibration < CALIBRATION_MIN){  
            calibration = CALIBRATION_MAX;
          }
          peak=0;
          delay(100);
          Serial.print(F("\tSet calibration: "));
          Serial.println(calibration);
        }
        displayControl(displayMode);
      }
    }
    if (digitalRead(BUTTON_NEXT) == LOW) {
      delay(BUTTON_DEBOUNCE);             
      if (digitalRead(BUTTON_NEXT) == LOW) {
        buffT = millis();                   //Update starter time
        Serial.println(F("\tBUTTON_NEXT"));
        if (displayMode == DISPLAY_MENU) {
          displayState1 = 0;
          if (displayState2 >= 4) {
            displayState2 = 0;
          } else {
            displayState2++;
          }
        }
        else if (displayMode == DISPLAY_FIXED_FM) {
          if (fixedFM < (ARRAY_ROWS-1)) {   // If fixed station is lower than array length
            fixedFM = fixedFM + 1;          // Move to next in list
          }
          else if(fixedFM >= (ARRAY_ROWS-1)){ // If fixed station is higher than or equal to array length
            fixedFM = 0;                      // Set fixedFM to first station in list
          }
          radio.setBandFrequency(RADIO_BAND_FM , (atoi((char *) pgm_read_word (&radioStations[fixedFM][0]))));  // Send set frequency to TEA module
          Serial.print(F("\tSet frequentie: "));
          Serial.println(radio.getFrequency());
        }
        else if (displayMode == DISPLAY_MANUAL_FM) {
          if (manualFM < 10800) {     // If frequency is lower than 10800
            manualFM = manualFM + 10; // Add 10
          }
          else if(manualFM >= 10800){ // If frequency is 10800
            manualFM = 7600;          // Frequency is 7600
          }
          radio.setBandFrequency(RADIO_BAND_FM , manualFM);  // Send set frequency to TEA module
          Serial.print(F("\tSet frequentie: "));
          Serial.println(radio.getFrequency());
        }
        else if (displayMode == DISPLAY_CALIBRATE) {
          displayState3 = 0;
          if (displayState4 >= 3) {
            displayState4 = 1;
          } else {
            displayState4++;
          }
        }
        else if (displayMode == DISPLAY_CALIBRATE2) {
          if (calibration < CALIBRATION_MAX) {
            calibration = calibration + CALIBRATION_STEPS; 
          }
          else if(calibration >= CALIBRATION_MAX){ 
            calibration = CALIBRATION_MIN;         
          }
          peak=0;
          delay(100);
          Serial.print(F("\tSet calibration: "));
          Serial.println(calibration);
        }
        displayControl(displayMode);
      }
    }
  
    currentT = millis();                      // Update current time
    if (selected2 == 2 && displayMode == DISPLAY_CALIBRATE2){
      calibrate();
      displayMode = DISPLAY_CALIBRATE3;
      displayControl(displayMode);
      delay(2000);
      displayMode = DISPLAY_MENU;
      displayControl(displayMode);
    }
    if (check) {
      break;
    }
  }
  saveEEPROM();       // Save settings to EEPROM
  displayControl(DISPLAY_HOME_SCREEN);  // Display home/default screen
  menuFlag = false;   // Reset menu flag
  Serial.println();   
  delay(500);
  attachInterrupt(digitalPinToInterrupt(BUTTON_SELECT), pin_ISR, LOW);
}

//Display control funtion
void displayControl(int input){
  Serial.println(F("displayControl()"));
  display.clearDisplay();         // Clear display
  display.setCursor(0, 0);        // Set cursor to base point
  switch (input){
    case DISPLAY_HOME_SCREEN: //Home Screen
      display.println(F("VUstick by")); // Starting screen
      display.print(F("NakedNinja"));   // Starting screen
      display.display();                // Execute everything infront of this line on the display
      break;
    case DISPLAY_MENU: //Menu
      if (displayState1 == 0) {
        switch (displayState2){
          case 1:
            display.println(F(" Fixed FM"));
            display.println(F(">Manual FM"));
            display.display();
            selected = 2;
            break;
          case 2:
            display.println(F(" Manual FM"));
            display.println(F(">Calibrate"));
            display.display();
            selected = 3;
            break;
          case 3:
            display.println(F(" Calibrate"));
            display.println(F(">Exit"));
            display.display();
            selected = 4;
            break;
          case 4:
            display.println(F(" Exit"));
            display.println(F(">Fixed FM"));
            display.display();
            selected = 1;
            break;
        } 
      } 
      else if (displayState1 == 1) {
        switch (displayState2){
          case 0:
            display.println(F(">Fixed FM"));
            display.println(F(" Manual FM"));
            display.display();
            selected = 1;
            break;
          case 1:
            display.println(F(">Manual FM"));
            display.println(F(" Calibrate"));
            display.display();
            selected = 2;
            break;
          case 2:
            display.println(F(">Calibrate"));
            display.println(F(" Exit"));
            display.display();
            selected = 3;
            break;
          case 3:
            display.println(F(">Exit"));
            display.println(F(" Fixed FM"));
            display.display();
            selected = 4;
            break;
        } 
      }
      break;
    case DISPLAY_FIXED_FM: //Fixed FM
      display.println(F("Channel"));   
      display.print((char *) pgm_read_word (&radioStations[fixedFM][1]));
      display.display();              
      break;
    case DISPLAY_MANUAL_FM: //Manual FM
      display.println(F("Frequency"));
      int q1 = manualFM;              
      int fp = q1 / 100;
      int sp = q1 - (fp * 100);
      display.print(fp);              
      display.print(F("."));
      display.print(sp);
      display.print(F(" Mhz"));
      display.display();             
      break;
    default:
//      Serial.println(F("\tdefault"));
      break;
  }
  switch (input){
    case DISPLAY_CALIBRATE:
      if (displayState3 == 0) {
        switch (displayState4){
          case 1:
            display.println(F(" Manual"));
            display.println(F(">Automatic"));
            display.display();
            selected2 = 2;
            break;
          case 2:
            display.println(F(" Automatic"));
            display.println(F(">Exit"));
            display.display();
            selected2 = 3;
            break;
          case 3:
            display.println(F(" Exit"));
            display.println(F(">Manual"));
            display.display();
            selected2 = 1;
            break;
        } 
      } 
      else if (displayState3 == 1) {
        switch (displayState4){
          case 0:
            display.println(F(">Manual"));
            display.println(F(" Automatic"));
            display.display();
            selected2 = 1;
            break;
          case 1:
            display.println(F(">Automatic"));
            display.println(F(" Exit"));
            display.display();
            selected2 = 2;
            break;
          case 2:
            display.println(F(">Exit"));
            display.println(F(" Manual"));
            display.display();
            selected2 = 3;
            break;
        } 
      }
    case DISPLAY_CALIBRATE2:
      if (selected2 == 1) { //manual
        display.println(F("Cal. value"));
        display.println(calibration);
        display.display();
      }
      else if (selected2 == 2) { //automatic
        display.println(F("Calibrating..."));
        display.display();
      }
      break;
    case DISPLAY_CALIBRATE3:
      display.println(F("Product"));
      display.println(F("Calibrated"));
      display.display();
      break;
    default:
//      Serial.println(F("\tdefault"));
      break;
  }
  Serial.println();
}

//Save values to EEPROM
void saveEEPROM(){
  Serial.println(F("saveEEPROM()"));
  Serial.println(F("\tSaved values"));
  Serial.print(F("\tfixedFM:"));
  Serial.println(fixedFM);
  Serial.print(F("\tmanualFM:"));
  Serial.println(manualFM);
  Serial.print(F("\tcalibration:"));
  Serial.println(calibration);
  Serial.print(F("\tlastMode:"));
  Serial.println(lastMode);

  EEPROM.put(0, EEPROM_CHECK);// Check value
  EEPROM.put(2, fixedFM);     // Save Fixed FM
  EEPROM.put(4, manualFM);    // Save Manual FM
  EEPROM.put(6, calibration); // Save calibration 
  EEPROM.put(8, lastMode);    // Save lastMode
  
  Serial.println();
}

//Load values from EEPROM
void loadEEPROM(){
  Serial.println(F("loadEEPROM()"));
  int EEPROMcheck;                  // Variable
  EEPROM.get(0, EEPROMcheck);       // Get first EEPROM value to see 
  if(EEPROMcheck == EEPROM_CHECK){  // if EEPROM is configured
    EEPROM.get(2, fixedFM);         // Fixed FM
    EEPROM.get(4, manualFM);        // Manual FM
    EEPROM.get(6, calibration);     // calibration
    EEPROM.get(8, lastMode);        // lastMode
    Serial.println(F("\tSettings successfully loaded."));
  }
  else{
    for (int i = 0 ; i < EEPROM.length() ; i++) { // Clear EEPROM
      EEPROM.write(i, 0);     
    }
    // Create all default EEPROM values
    EEPROM.put(0, EEPROM_CHECK);        // Check value
    EEPROM.put(2, DEFAULT_FIXED_FM);    // Fixed FM
    EEPROM.put(4, DEFAULT_MANUAL_FM);   // Manual FM
    EEPROM.put(6, DEFAULT_CALIBRATION); // Calibration
    EEPROM.put(8, DEFAULT_LAST_MODE);   // Last Mode
    
    // Create all default values to be used
    fixedFM = DEFAULT_FIXED_FM;
    manualFM = DEFAULT_MANUAL_FM;
    calibration = DEFAULT_CALIBRATION;
    lastMode = DEFAULT_LAST_MODE;
    
    Serial.println(F("\tEEPROM Empty."));
    Serial.println(F("\tSet and saved default value's"));
  }
  Serial.println();
}

//RGB rainbow control
uint32_t wheel(byte wheelPos) {
  wheelPos = 255 - wheelPos;
  if(wheelPos < 45) {
   return RGB_strip.Color(255 - wheelPos * 3, 0, wheelPos * 3);
} else if(wheelPos < 170) {
    wheelPos -= 85;
   return RGB_strip.Color(0, wheelPos * 3, 255 - wheelPos * 3);
  } else {
   wheelPos -= 170;
   return RGB_strip.Color(wheelPos * 3, 255 - wheelPos * 3, 0);
  }
}

//RGB control funtion
void RGBControl() {
  unsigned long startMillis = millis();// Start of sample window
  unsigned int peakToPeak = 0;         // peak-to-peak level
  unsigned int signalMax = 0;
  unsigned int signalMin = 1023;
  unsigned int sample = 0;

  // collect data for 50 mS
  while (millis() - startMillis < SAMPLE_WINDOW) {
    sample = analogRead(SAMPLE_PIN);
    if (sample < 1024) {            // toss out spurious readings
      if (sample > signalMax) {
        signalMax = sample;         // save just the max levels
      }
      else if (sample < signalMin) {
        signalMin = sample;         // save just the min levels
      }
    }
  }
  peakToPeak = signalMax - signalMin;  // max - min = peak-peak amplitude
  
  if (peakToPeakMem < peakToPeak) { 
    peakToPeakMem = peakToPeak;
  }
  
  int led = map(peakToPeak, 0, calibration, 0, RGB_strip.numPixels()) -1;
  
  for(int i=0; i <= led; i++) {
    int color = map(i, COLOR_START, RGB_strip.numPixels(), COLOR_FROM, COLOR_TO);
    RGB_strip.setPixelColor(i, wheel(color));
  }
  
  for(int i = RGB_strip.numPixels(); i > led; i--) {
    RGB_strip.setPixelColor(i, 0); 
  }
  RGB_strip.show();
  
  if(led > peak) {peak = led;} // Keep 'peak' dot at top
  if(peak > 1 && peak <= RGB_strip.numPixels()-1) {RGB_strip.setPixelColor(peak,wheel(map(peak,0,RGB_strip.numPixels()-1,0,255)));}
  RGB_strip.show();
  
  // Every few frames, make the peak pixel drop by 1:
  if(++dotCount >= PEAK_FALL) { //fall rate 
    if(peak > 0) {peak--;}
    dotCount = 0;
  } 
}

//Initialization
void setup() {
  // open the Serial port
  Serial.begin(115200);
  Serial.println(F("setup()"));
  Serial.println(F("VU Meter V0.3"));
  Serial.println(F("(c) Naked Ninja 2018"));
  Serial.println();
  
  delay(10);

  //Define buttons as input
  pinMode(BUTTON_SELECT, INPUT); //BUTTON_SELECT button (D8)
  pinMode(BUTTON_PREV, INPUT); //BUTTON_PREV button (D9)
  pinMode(BUTTON_NEXT, INPUT); //BUTTON_NEXT button (D10)

  //Enable internal pull-up resistors
  digitalWrite(BUTTON_SELECT, HIGH);
  digitalWrite(BUTTON_PREV, HIGH);
  digitalWrite(BUTTON_NEXT, HIGH);
  
  loadEEPROM();
  
  //Radio
  Serial.println(F("\tRadio Setup"));
  radioSetup(); //Radio setup
  delay(1000);   //delay to ensure this is done before display starts
  
  //Display Setup
  Serial.println(F("\tDisplay Setup"));
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.setRotation(2);
  display.setTextSize(2); // Draw 2X-scale text
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("VUstick by"));  //Starting screen
  display.print(F("NakedNinja"));    //Starting screen
  display.display();              //Execute everything infront of this line on the display
  delay(2000);

  //RGB Setup
  Serial.println(F("\tRGB Setup"));
  RGB_strip.begin();
  RGB_strip.setBrightness(BRIGHTNESS);
  RGB_strip.show(); // Initialize all pixels to 'off'

  //Reset variables
  peak = 0;
  dotCount = 0;
  
  //Start intterupt and clear interrupt flag
  Serial.println(F("\tEnableling interrupt and starting code"));
  Serial.println();
  attachInterrupt(digitalPinToInterrupt(BUTTON_SELECT), pin_ISR, LOW);
  menuFlag = false;
}

//Main loop
void loop() {
  RGBControl();
  if (menuFlag) {
    Serial.println(F("loop()"));
    Serial.println(F("\tmenu"));
    Serial.println();
    buttonControl();
  }
}
