/*
 * Arduino Touchscreen Clock Radio with 
 * Si4703 FM Stereo, MP3, 3.5" TFT LCD (HX8357)
 * For Arduino Feather M0
 * 
 * 
 * NEED TO COMPLETE/IMPROVE (11/7/2018):
 *  - MP3 playback still times out and locks up the mux at random intervals.
 *  - figure out how to improve responsiveness of touch control - how to interrupt reading/update of RDS in radio mode??  how to interrupt
 *    MP3 player during music play.  Still have ghost touch issues.  
 *  - TPA2016 performance - 4/12/18: Improved by turning off AGC and setting the gain at -10.  Still have response issues with RDS interrupting setAmp() in radio mode
 *  - RDS display: need to force a space at the end of the string somehow, smooth out scrolling vs. RDSupdate delays
 *  - Menus in development (amp settings, station lists for FM and internet, SD information)
 *  - Need to figure out how to use only SparkFunSi4703.h and not rely on radio.h (which invokes numerous chipset libraries)
 */ 
////////////////////////////////////////////      LIBRARIES      ////////////////////////////////////////////

#include <Adafruit_GFX.h>                                           //Core graphics library
#include <Fonts/FreeSans12pt7b.h>                                   //GFX Font library
#include <Adafruit_HX8357.h>                                        //LCD library to drive screen
#include <Adafruit_STMPE610.h>                                      //Library for touchscreen
#include <Wire.h>                                                   //I2C library
#include <SPI.h>                                                    //SPI Library for touchscreen and MP3 Control
#include <SD.h>                                                     //SD Card Library
#include <radio.h>                                                  //Radio library
#include <SparkFunSi4703.h>                                         //si4703 library
#include <Adafruit_TPA2016.h>                                       //TPA2016 Audio Amp library
#include "RTClib.h"                                                 //RTC Library
#include <Adafruit_VS1053.h>                                        //VS1053 MP3 Player
#include <WiFi101.h>                                                //WiFi Interface Library
#include <ZWiFiCentral.h>                                           //Private Network Library
#include <ZeroRegs.h>

////////////////////////////////////////////   MICROCONTROLLERS  ////////////////////////////////////////////

// Feather MO (ARDUINO_SAMD_FEATHER_M0)
#define STMPE_CS 17//A3                                                 // Default is pin 6 - modified tft 9/2/18 to accommodate VS1053 Music Maker
#define TFT_CS   16//A2                                                 // Default is pin 9 - modified tft 9/2/18 to accommodate VS1053 Music Maker
#define TFT_DC   15//A1                                                 // Default is pin 10 - modified tft 9/2/18 to accommodate VS1053 Music Maker
#define SD_CS    18//A4                                                 // Default is pin 5 - modified tft 9/2/18 to accommodate VS1053 Music Maker

#define VBATPIN A7
float measuredvbat;

#define CHARGEPIN A0
float charge;

////////////////////////////////////////////   TFT DEFINITIONS   ////////////////////////////////////////////

#define TFT_RST -1

Adafruit_HX8357 tft = Adafruit_HX8357(TFT_CS, TFT_DC, TFT_RST);
Adafruit_STMPE610 ts = Adafruit_STMPE610(STMPE_CS);

// This is calibration data for the raw touch data to the screen coordinates
#define TS_MINX 150
#define TS_MAXX 3800
#define TS_MINY 130
#define TS_MAXY 4000

#define MINPRESSURE 10
#define MAXPRESSURE 255

int horz;                                                           //var to hold x touch
int vert;                                                           //var to hold y touch

#define HX8357_BLACK       0x0000
#define HX8357_NAVY        0x000F
#define HX8357_DARKGREEN   0x03E0
#define HX8357_DARKCYAN    0x03EF
#define HX8357_MAROON      0x7800
#define HX8357_PURPLE      0x780F
#define HX8357_OLIVE       0x7BE0
#define HX8357_LIGHTGREY   0xC618
#define HX8357_DARKGREY    0x7BEF
#define HX8357_BLUE        0x001F
#define HX8357_GREEN       0x07E0
#define HX8357_CYAN        0x07FF
#define HX8357_RED         0xF800
#define HX8357_MAGENTA     0xF81F
#define HX8357_YELLOW      0xFFE0
#define HX8357_WHITE       0xFFFF
#define HX8357_ORANGE      0xFD20
#define HX8357_GREENYELLOW 0xAFE5
#define HX8357_PINK        0xF81F
#define HX8357_CRIMSON     0x8000
#define HX8357_AMBER       0xF4E0
#define HX8357_AQUA        0x7639
int currentScreen;                                                  //var to assign active screen for logic control
long lineColor = HX8357_RED;
long textColor = HX8357_ORANGE;

#define TOUCH_IRQ A5//                                              // A5 wired to IRQ on TFT to serve as external interrupt
volatile bool tftNotTouched = true;                                 // start out untouched for interrupt status
boolean lastTouch = LOW;                                            // variable containing previous button state
boolean currentTouch = LOW;                                         // variable containing current button state
int IRQThreshold = 700;                                             // sense threshold for updating tftNotTouched
int IRQValue;                                                       // variable to hold touch value to compare to threshold
//////////////////////////////////////////  Si4703 DEFINITIONS  ///////////////////////////////////////////////////

const int resetPin = 12; // (PGPIO 12 on Feather MO)
const int SDIO = 20;
const int SCLK = 21;    
char printBuffer[50];
char rdsBufferA[96];

Si4703_Breakout radio(resetPin, SDIO, SCLK);                        //create instance of SI4703 called radio

int channel = 911;                                                  //var to hold station 91.1 KLDV K-LOVE Morrison, CO
//int channel = 985;                                                //var to hold station 98.5 Denver, CO
//int channel = 1011;                                               //var to hold station 101.1 Denver, CO
//int channel = 1051;                                               //var to hold station 105.1 KXKL Kool 105.1 Denver, CO
int lastChannel;                                                    //var to hold last station listened to
int volume = 4;                                                     //var to hold volume - start at 2

//int rdsRefresh;                                                   //var to use with displaying refreshed rds data
//int dtRefresh;                                                    //var to use with displaying refreshed time data
const int myStations[6] = {911, 985, 1011, 1035, 1051};                   //array storing multiple stations
int stationIndex = 0;                                               //establish station array indexing at 0

///////////////////////////////////////////  TPA2016 DEFINITIONS  ///////////////////////////////////////////

const int ampPin = 13;                                                    //digital control of SHDN function (GPIO pin 13 on Feather M0)
int gain = 20;
Adafruit_TPA2016 audioamp = Adafruit_TPA2016();                     //create instance of Adafruit_TP2016 called audioamp
                                
////////////////////////////////////////////  VS1053 DEFINITIONS ////////////////////////////////////////////

#define VS1053_RESET   -1                                           // VS1053 reset pin (not used!)

// use with Feather MO

  #define VS1053_CS      6                                          // VS1053 chip select pin (output)
  #define VS1053_DCS     10                                         // VS1053 Data/command select pin (output)
  #define CARDCS          5                                         // Card chip select pin
  #define VS1053_DREQ     11                                        // Interrupt pin, VS1053 Data request - cut trace from pin 9 and jumpered to pin 11 due to conflict with VBATPIN on M0 controller 

Adafruit_VS1053_FilePlayer musicPlayer = 
  Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARDCS);

int MP3volume = 84;

#define BUFFER_LEN 50
const char* myAlbum_1_Track[14] = {"01.mp3", "02.mp3", "03.mp3","04.mp3","05.mp3","06.mp3","07.mp3","08.mp3","09.mp3","10.mp3","11.mp3","12.mp3","13.mp3"};
static const char* myAlbum_1_TrackNames[14] = {"Still Rolling Stones", "Rescue", "This Girl","Your Wings","You Say","Everything","Love Like This","Look Up Child","Losing My Religion","Remember","Rebel Heart","Inevitable","Turn Your Eyes Upon Jesus"};
static const char* myAlbum_1_Artist = "Lauren Daigle";
const char* nowPlaying;
int myAlbum_1_Index = 0;
char printMP3Buffer[BUFFER_LEN];
char printArtistBuffer[BUFFER_LEN];
int MP3BufferLength;
int ArtistBufferLength;
int playMusicState = 0;
boolean muted = false;

////////////////////////////////////////////  WiFi DEFINITIONS ////////////////////////////////////////////

ZWiFiCentral wifi;
char* ssid     = wifi.ssid();
char* password = wifi.passcode();

static const char* myWiFiHost[6] = {"ice.zradio.org","ice1.somafm.com","ice1.somafm.com","ice1.somafm.com","ice1.somafm.com"}; 
static const char* myWiFiHostName[6] = {"Z Radio","SomaFM","SomaFM","SomaFM","SomaFM"}; 
static const char* myWiFiPath[6] = {"/z/high.mp3", "/seventies-128-mp3", "/u80s-128-mp3", "/bagel-128-mp3", "/thistle-128-mp3" }; 
static const char* myWiFiName[6] = {"Z88.3 Christian","Left Coast 70's", "Underground 80's", "Bagel Radio", "Thistle Celtic Sounds" }; 
int WiFiPathIndex = 0;                                              //establish station array indexing at 0

const char *host;
const char *path;
int httpPort = 80;
const char *Metadata = "Icy-Metadata:1";                            //In development - for reading "Now Playing" data from stream

char printStreamBuffer[BUFFER_LEN];
int StreamBufferLength;
char printHostBuffer[BUFFER_LEN];
int HostBufferLength;
char printSSIDBuffer[BUFFER_LEN];
int SSIDBufferLength;

WiFiClient client;
uint8_t mp3buff[32];                                                // buffer of mp3 data (vs1053 likes 32 bytes at a time)

////////////////////////////////////////////  DS3231 DEFINITIONS ////////////////////////////////////////////

RTC_DS3231 rtc;
static const char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char months[12][12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
DateTime now;
char timeBuffer[BUFFER_LEN];
int timeBufferLength;
int hr;
char dateBuffer[BUFFER_LEN];
int dateBufferLength;
char dateTimeBuffer[BUFFER_LEN];
int dateTimeBufferLength;

int setHour, setMinute, setDate, setMonth, setYear;                 //for use with clock set function
//int setData;                                                        //unused - developmental
int currentMinute, currentSecond;
int setAlarmHour, setAlarmMinute, origAlarmHour, origAlarmMinute;
static const char* alarmSched[5] = {"WKDAY", "DAILY", "WKEND", "NEXT"};
int setSchedIndex = 0;
const char* nowScheduled;
boolean alarmSet = false; 
boolean currentAlarmState = false;
boolean previousAlarmState = false;
boolean currentSnoozeState = false;
boolean previousSnoozeState = false;
int alarmVolume = 85;
unsigned long alarmStartMillis; unsigned long alarmMillis=0;
unsigned long currentMillis; unsigned long previousMillis=0;
unsigned long currentSnoozeMillis; unsigned long previousSnoozeMillis=0;
const unsigned long alarmInterval = 20000;
const unsigned long snoozeInterval = 20000;
int alarmTimer = 30;
int snoozeTime = 2;
int snoozeCount = 0;

////////////////////////////////////////////    M0 SERIAL CONV   ////////////////////////////////////////////

#if defined(ARDUINO_SAMD_ZERO) && defined(SERIAL_PORT_USBVIRTUAL)
// Required for Serial on Zero based boards
#define Serial SERIAL_PORT_USBVIRTUAL
#endif

////////////////////////////////////////////         SETUP       ////////////////////////////////////////////

void setup(void) {
  WiFi.setPins(8,7,4,2);                                            //Configure pins for Adafruit ATWINC1500 Feather
  Serial.begin(115200);                                             //serial comms for debug
  tft.begin(HX8357D);                                               //start screen using chip identifier hex
  ts.begin();                                                       //start touchscreen
  rtc.begin();                                                      //start clock
  tft.setRotation(1);                                               //set rotation for wide screen
  WiFi.lowPowerMode();
  pinMode(12,OUTPUT);                                               //initialize RESET pin for Radio(adapted for MO controller)
  pinMode(13,OUTPUT);                                               //initialize SHDN pin for Amp (adapted for MO controller)
  pinMode(CHARGEPIN,INPUT);                                         //initialize CHARGEPIN as input (read presence of USB/adapter)
  pinMode(TOUCH_IRQ,INPUT);                                         //initialize TFT IRQ as interrupt
  delay(1000);                                                      // wait for console opening
//  regDump();

//  
//  if (! rtc.begin()) {
//    Serial.println("Couldn't find RTC");
//    while (1);
//  }
//
//  if (rtc.lostPower()) {
//    Serial.println("RTC lost power, lets set the time!");
//    // following line sets the RTC to the date & time this sketch was compiled
//    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
//    // This line sets the RTC with an explicit date & time, for example to set
//    // January 21, 2014 at 3am you would call:
//    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
//  }
  attachInterrupt(digitalPinToInterrupt(TOUCH_IRQ),tsControlInt,RISING);

  currentScreen = 1;
  startMainScreen();
//  currentScreen = 3;
//  startMP3Screen();
}  

////////////////////////////////////////          ISR          /////////////////////////////////////////////
  
  void tsControlInt(){
    IRQValue = analogRead(TOUCH_IRQ);
    if (IRQValue > IRQThreshold) {
      tftNotTouched = !tftNotTouched;                               //change state of tftNotTouched 
    }
  }
  
////////////////////////////////////////        FUNCTIONS       /////////////////////////////////////////////
  
//  boolean debounce(boolean last){                                   //debounce touchscreen touches      
//    boolean current = digitalRead(TOUCH_IRQ);                       //read touchscreen state from interrupt pin
//    if (last != current){
//      delay (5);
//      current = digitalRead(TOUCH_IRQ);
//    }
//    Serial.println("touch debounced");
//    return current;
//  }

  void regDump() {                                                  //serial print M0 register
    
    while (! Serial) {}  // wait for serial monitor to attach
    ZeroRegOptions opts = { Serial, false };
    printZeroRegs(opts);
  }
  
  void startMainScreen(){
    ampOff();                                                       //using SHDN to shut amp off
    tft.fillScreen(HX8357_BLACK);                                   //fill screen with black (ersatz clear)
    tft.setCursor(0, 0); 
    tft.setTextColor(textColor);                                    //set text color white
    tft.setTextSize(2);                                             //set text size to 2
    tft.println("           Arduino Clock Radio");                  //print header to screen
    
    tft.drawRoundRect(10, 20, 460, 290, 6, lineColor);              //draw screen outline
    tft.drawRoundRect(30, 170, 200, 50, 6, lineColor);              //draw FM stereo button
    tft.drawRoundRect(250, 170, 200, 50, 6, lineColor);             //draw MP3 button
    tft.drawRoundRect(30, 240, 200, 50, 6, lineColor);              //draw Internet Radio
    tft.drawRoundRect(250, 240, 200, 50, 6, lineColor);             //draw menu button
    tft.setCursor(78, 188);                                         
    tft.setTextSize(2);                                             
    tft.setTextColor(textColor);
    tft.println("FM STEREO");                                       //write "FM STEREO" in button
    tft.setCursor(290, 188);                                        
    tft.setTextSize(2);                                             
    tft.setTextColor(textColor);
    tft.println("MP3 PLAYER");                                      //write "MP3" in button
    tft.setCursor(78, 258);                                         
    tft.setTextSize(2);                                             
    tft.setTextColor(textColor);
    tft.println("WEB RADIO");                                       //write "WEB RADIO" in menu button
    tft.setCursor(323, 258);                                        
    tft.setTextSize(2);                                             
    tft.setTextColor(textColor);
    tft.println("MENU");                                            //write "MENU" in menu button
    printTime();
    if (alarmSet){
      printAlarmIndicator();
    }
  }

  void controlMainScreen(){                                         //logic for main screen control
    TS_Point p = ts.getPoint();                                     //get touch
    DateTime now = rtc.now();                                       //get current time and date

    if (millis() % 1000 == 0){
      checkCharge();
    }

    if (alarmSet){
      if (setAlarmHour == now.hour() && setAlarmMinute == now.minute()){
        currentAlarmState = true;
        currentSnoozeState = false;
//        previousAlarmState = currentAlarmState;
//        previousSnoozeState = currentSnooze State;
        currentScreen = 8;
        startWakeScreen();
      } 
    }
     
    if (currentMinute != now.minute()){        
      printTime();
      if (alarmSet){
        printAlarmIndicator();
      }
    }

    vert = tft.height() - p.x;
    horz = p.y;

//    Serial.print("X = "); Serial.print(horz);  
//    Serial.print("\tY = "); Serial.print(vert);
//    Serial.print("\tPressure = "); Serial.println(p.z);  
//    Serial.print("Current Screen = "); Serial.println(currentScreen);
                                     
    p.x = map(p.x, TS_MINY, TS_MAXY, 0, tft.height());                // Scale using the calibration #'s and rotate coordinate system    
    p.y = map(p.y, TS_MINX, TS_MAXX, 0, tft.width());                 
    
    if (ts.touched()) {
      if(horz>400 && horz<1890){                                      //select FM Stereo
        if(vert>-1480 && vert<-1000){
          delay(100);
          currentScreen = 2;                                          //send status to loop() to activate controlStereo 
          getLastTouch();
          delay(100);
          startStereoScreen();                                        //call startStereoScreen function
        }
      }
      if(horz>2150 && horz<3600){                                     //select MP3 Player
        if(vert>-1480 && vert<-1000){
          delay(100);
          Si4703PowerOff();
          currentScreen = 3;                                          //send status to loop() to activate controlMP3 
          getLastTouch();                                             
          delay(100);
          startMP3Screen();                                           //call startMP3Screen function
        }  
      }
      if(horz>400 && horz<1890){                                      //reserved for Web Radio 
        if(vert>-550 && vert<-200){
          delay(100);
          currentScreen = 4;
          getLastTouch();
          delay(100);
          startWebRadioScreen();                                      //call startWebRadioScreen function
        }
      }
      if(horz>2150 && horz<3600){                                     //select Menu
        if(vert>-550 && vert<-200){
          delay(100);
          currentScreen = 5;
          getLastTouch();
          delay(100);
          startMenuScreen();                                          //call startMenuScreen function
        }
      }
    }
  }
  
  void startStereoScreen(){
  ampOn();                                                          //activate Amp and run setAmp function
  delay(50);
//  audioamp.setGain(gain);
//  delay(500);
  tft.fillScreen(HX8357_BLACK);                                     //fill screen with black
  tft.setCursor(0, 0); 
  tft.setTextColor(textColor);                                  //set text color white
  tft.setTextSize(2);                                               //set text size to 2 (1-6)
  tft.println("        Arduino Si4703 FM Radio");                   //print header to screen

  tft.drawRoundRect(10, 20, 460, 290, 6, lineColor);               //draw screen outline
  tft.drawRoundRect(20, 30, 50, 50, 6, lineColor);                 //draw seek left box
  tft.drawRoundRect(80, 30, 50, 50, 6, lineColor);                 //draw seek right box
  tft.drawRoundRect(140, 30, 200, 50, 6, lineColor);               //draw station box
  tft.drawRoundRect(20, 90, 260, 50, 6, lineColor);                //draw volume box
  tft.drawRoundRect(290, 90, 50, 50, 6, lineColor);                //draw mute box
  tft.drawRoundRect(20, 150,440, 90, 6, lineColor);                //draw RDS box
  tft.drawRoundRect(20, 250,380, 50, 6, lineColor);                //draw Time/Date box
  tft.drawRoundRect(410, 250,50, 50, 6, lineColor);                //draw Exit box
  tft.drawRoundRect(350, 30, 50, 50, 6, lineColor);                //draw station up buton
  tft.drawRoundRect(410, 30, 50, 50, 6, lineColor);                //draw station down buton
  tft.drawRoundRect(350, 90, 50, 50, 6, lineColor);                //draw volume up buton
  tft.drawRoundRect(410, 90, 50, 50, 6, lineColor);                //draw volume down buton
  tft.drawTriangle(375, 44, 362, 65, 388, 65,textColor);        //draw up triangle for station
  tft.drawTriangle(435, 65, 422, 44, 448, 44,textColor);        //draw down triangle for station
  tft.drawTriangle(32, 54, 55, 44, 55, 65,textColor);           //draw left triangle for preset station
  tft.drawTriangle(95, 44, 95, 65, 118, 54,textColor);          //draw right triangle for preset station
  tft.drawTriangle(375, 104, 362, 125, 388, 125,textColor);     //draw up triangle for volume
  tft.drawTriangle(435, 125, 422, 104, 448, 104,textColor);     //draw down triangle for volume
  tft.setCursor(308, 103);                                          //put cursor in mute box
  tft.setTextSize(3);                                               //set text size 3
  tft.setTextColor(textColor);
  tft.println("M");                                                 //write a "M" in mute box
  tft.setCursor(428, 265);                                          //put cursor in Exit box
  tft.setTextSize(3);                                               //set text size 3
  tft.setTextColor(textColor);
  tft.println("X");                                                 //write a "X" in Exit box

  radio.powerOn();                                                  //activate Si4703
  radio.setChannel(myStations[0]);                                  //set band and freq
  stationIndex = 1;
  radio.setVolume(volume);                                          //set volume
  volume = constrain(volume, 1, 15);                                //constrain radio volume from 1 to 15
//  radio.readRDS(rdsBufferA, 5000);                                  //original setting 15 seconds - reduced to 5 for development 
  
  tft.setCursor(190, 47);
  tft.setTextColor(textColor);
  tft.setTextSize(2);
  sprintf(printBuffer, "%02d.%01d MHz", channel / 10, channel % 10); // convert station to decimal format using print buffer
  tft.setTextColor(textColor);
  tft.print(printBuffer);                                           //write station
  tft.setCursor(40, 108);
  tft.setTextColor(textColor);
  tft.setTextSize(2);
  tft.print("Volume:    ");
  tft.setTextColor(textColor);
  tft.print(volume);                                                //write volume
  tft.setTextColor(textColor);
  tft.setCursor(40, 162);
  tft.setTextSize(2);
  printTime();
}

  void controlStereo(){                                             //logic for radio control screen

    TS_Point p = ts.getPoint();                                       //get touch
    DateTime now = rtc.now();                                         //get current time and date
    
    if (currentMinute != now.minute()){
      printTime();
    }
    
    if (millis() % 500 == 0){
      checkCharge();
                                                
      radio.readRDS(rdsBufferA, 500);                                //re-read RDS data and write to rdsBuffer
      String text = rdsBufferA;
      tft.setTextColor(textColor, HX8357_BLACK);
      tft.setTextWrap(false);                                         //don't wrap text to next line while scrolling
      tft.setTextSize(3);
      const int width = 9;                                            //set width of the marquee display (in characters)
      for (int offset = 0; offset < text.length(); offset++){         //loop once through the string
        
        String t = "";                                                //construct the string to display for this iteration
        for (int i = 0; i < width; i++){
      
          t += text.charAt((offset + i) % text.length());
          tft.setCursor(160, 184);                                    //set cursor for left boundary of marquee
          tft.print(t);                                               //print the string for this iteration
          delay (1);                                                 //speed may need adjustment based on variations in signal reception
        }
      }
    }  
    
    vert = tft.height() - p.x;
    horz = p.y;
  
//    Serial.print("X = "); Serial.print(horz);  
//    Serial.print("\tY = "); Serial.print(vert);
//    Serial.print("\tPressure = "); Serial.println(p.z);  
//    Serial.print("Current Screen = "); Serial.println(currentScreen);
    
    p.x = map(p.x, TS_MINY, TS_MAXY, 0, tft.height());
    p.y = map(p.y, TS_MINX, TS_MAXX, 0, tft.width());
    
    if(tftNotTouched){
      if (ts.touched()) {                                                 //if touch is detected
          if(horz>350 && horz<650){                                     //station up
            if(vert>-3200 && vert<-2650){
                delay(100);
                if(stationIndex<5){ 
                  channel = myStations[stationIndex];                   //trying to manually select through the array
                  tft.fillRoundRect(160, 42, 160, 25,6, HX8357_BLACK);
                  radio.setChannel(channel);
                  delay(250);
                  stationIndex++;
                  tft.setCursor(190, 47);
                  tft.setTextColor(textColor);
                  tft.setTextSize(2);
                  sprintf(printBuffer, "%02d.%01d MHz", channel / 10, channel % 10); // convert station to decimal format using print buffer
                  tft.setTextColor(textColor);
                  tft.print(printBuffer);                               //write station
                  getLastTouch();
                  delay(100);
                } else stationIndex=0;
            }
          }
          if(horz>750 && horz<1100){                                     //preset station down
            if(vert>-3200 && vert<-2650){
              delay(100);
              if (stationIndex>0){
                stationIndex--;
                channel = myStations[stationIndex];                     //trying to manually select through the array
                tft.fillRoundRect(160, 42, 160, 25,6, HX8357_BLACK);
                radio.setChannel(channel);
                delay(250);
                tft.setCursor(190, 47);
                tft.setTextColor(textColor);
                tft.setTextSize(2);
                sprintf(printBuffer, "%02d.%01d MHz", channel / 10, channel % 10); // convert station to decimal format using print buffer
                tft.setTextColor(textColor);
                tft.print(printBuffer);                                //write station
                getLastTouch();
                delay(100);
                //lastChannel = channel;
              } else stationIndex=5;    
            }
          }
          if(horz>2900 && horz<3200){                                    //station seek up
            if(vert>-3200 && vert<-2650){
              delay(100);
              if (channel < 1079){
                channel = radio.seekUp();
                tft.fillRoundRect(160, 42, 160, 25,6, HX8357_BLACK);
                radio.setChannel(channel);
                tft.setCursor(190, 47);
                tft.setTextColor(textColor);
                tft.setTextSize(2);
                sprintf(printBuffer, "%02d.%01d MHz", channel / 10, channel % 10); // convert station to decimal format using print buffer
                tft.setTextColor(textColor);
                tft.print(printBuffer);                                //write station
                delay (1000);
                getLastTouch();
                delay(100);
                //lastChannel = channel;
              } //else lastChannel = channel; 
            }
          }
          if(horz>3350 && horz<3650){                                     //station seek down
            if(vert>-3200 && vert<-2650){
              delay(100);
              if (channel > 881){
                channel = radio.seekDown();
                tft.fillRoundRect(160, 42, 160, 25,6, HX8357_BLACK);
                radio.setChannel(channel);
                tft.setCursor(190, 47);
                tft.setTextColor(textColor);
                tft.setTextSize(2);
                sprintf(printBuffer, "%02d.%01d MHz", channel / 10, channel % 10); // convert station to decimal format using print buffer
                tft.setTextColor(textColor);
                tft.print(printBuffer);                                 //write station
                delay (1000);
                getLastTouch();
                delay(100);
                //lastChannel = channel;
              } //else lastChannel = channel; 
            }
          }
          if(horz>2900 && horz<3200){                                     //volume up
            if(vert>-2400 && vert<-2000){
              delay(100);
              if (volume < 15){
                volume++;
                tft.fillRoundRect(140, 102, 80, 25, 6, HX8357_BLACK);
                radio.setVolume(volume);
                delay(200);
                tft.setCursor(40, 108);
                tft.setTextColor(textColor);
                tft.setTextSize(2);
                tft.print("Volume:    ");
                tft.setTextColor(textColor);
                tft.print(volume);                                      //write volume
                getLastTouch();
                delay(100);
              }
            }
          }
          if(horz>3350 && horz<3650){                                     //volume down
            if(vert>-2400 && vert<-2000){
              delay(100);
              if (volume > 0){
                volume--;
                tft.fillRoundRect(140, 102, 80, 25, 6, HX8357_BLACK);
                radio.setVolume(volume);
                delay(200);
                tft.setCursor(40, 108);
                tft.setTextColor(textColor);
                tft.setTextSize(2);
                tft.print("Volume:    ");
                tft.setTextColor(textColor);
                tft.print(volume);                                      //write volume
                getLastTouch();
                delay(100);
              }
            }
          }
          if(horz>2430 && horz<2730){                                     //mute
            if(vert>-2400 && vert<-2000){
              int lastRadioVolume = volume;
              delay(100);
              if(!muted){ 
                tft.fillRoundRect(160, 102, 80, 25, 6, HX8357_BLACK);
                audioamp.enableChannel(false,false);
                tft.setCursor(40, 108);
                tft.setTextColor(textColor);
                tft.setTextSize(2);
                tft.print("Volume:    ");
                tft.setTextColor(textColor);
                tft.print("MUTED");                 //write volume
                muted = true;
                Serial.print ("muted = ");Serial.println(muted);
                getLastTouch();
                delay(100);
            }else if(muted){
                tft.fillRoundRect(160, 102, 80, 25, 6, HX8357_BLACK);
                radio.setVolume(lastRadioVolume);
                audioamp.enableChannel(true,true);
                tft.setCursor(40, 108);
                tft.setTextColor(textColor);
                tft.setTextSize(2);
                tft.print("Volume:    ");
                tft.setTextColor(textColor);
                tft.print(volume);                 //write volume
                muted = false;
                Serial.print ("muted = ");Serial.println(muted);
                getLastTouch();
                delay(100);
                }    
              }
          }
          if(horz>3370 && horz<3650){                                     //exit to main screen
            if(vert>-580 && vert<-110){
    //          radio.getChannel();
              delay(100);
              Si4703PowerOff();
              currentScreen = 1;
              delay(100);
              getLastTouch();
              delay(100);
              startMainScreen();
            }
          }
        }
      }
    }

  void startMP3Screen(){                                              
    ampOn();
    tft.fillScreen(HX8357_BLACK);                                    //fill screen with black
    tft.setCursor(0, 0); 
    tft.setTextColor(textColor);                                     //set text color white
    tft.setTextSize(2);                                              //set text size to 2 (1-6)
    tft.println("           Arduino MP3 Player");                    //print header to screen
  
    tft.drawRoundRect(10, 20, 460, 290, 6, lineColor);               //draw screen outline
    tft.drawRoundRect(20, 130, 50, 50, 6, lineColor);                //draw _________ button
    tft.drawRoundRect(80, 130, 50, 50, 6, lineColor);                //draw _________ button
    tft.drawRoundRect(140, 130, 50, 50, 6, lineColor);               //draw stop button
    tft.drawRoundRect(200, 130, 80, 50, 6, lineColor);               //draw play button
    tft.drawRoundRect(290, 130, 50, 50, 6, lineColor);               //draw pause button
    tft.drawRoundRect(20, 190, 260, 50, 6, lineColor);               //draw volume box
    tft.drawRoundRect(290, 190, 50, 50, 6, lineColor);               //draw mute box
    tft.drawRoundRect(20, 30,440, 90, 6, lineColor);                 //draw Artist/Title box
    tft.drawRoundRect(20, 250,380, 50, 6, lineColor);                //draw Time/Date box
    tft.drawRoundRect(410, 250,50, 50, 6, lineColor);                //draw Exit box
    tft.drawRoundRect(350, 130, 50, 50, 6, lineColor);               //draw station up buton
    tft.drawRoundRect(410, 130, 50, 50, 6, lineColor);               //draw station down buton
    tft.drawRoundRect(350, 190, 50, 50, 6, lineColor);               //draw volume up buton
    tft.drawRoundRect(410, 190, 50, 50, 6, lineColor);               //draw volume down buton
    tft.fillTriangle(220, 140, 221, 170, 260, 155,textColor);        //draw play button triangle 
    tft.fillRect(300,140, 10, 30, textColor);                        //draw left bar pause
    tft.fillRect(320,140, 10, 30, textColor);                        //draw right bar pause
    tft.fillRoundRect(150,140, 30, 30, 4, textColor);                //draw stop button square
    tft.drawTriangle(375, 144, 362, 165, 388, 165,textColor);        //draw up triangle for station
    tft.drawTriangle(435, 165, 422, 144, 448, 144,textColor);        //draw down triangle for station
    tft.drawTriangle(375, 204, 362, 225, 388, 225,textColor);        //draw up triangle for volume
    tft.drawTriangle(435, 225, 422, 204, 448, 204,textColor);        //draw down triangle for volume
    tft.setCursor(308, 203);                                         //put cursor in mute box
    tft.setTextSize(3);                                              //set text size 3
    tft.setTextColor(textColor);
    tft.println("M");                                                //write a "M" in mute box
    tft.setCursor(428, 265);                                         //put cursor in Exit box
    tft.setTextSize(3);                                              //set text size 3
    tft.setTextColor(textColor);
    tft.println("X");                                                //write a "X" in Exit box
  
  
  //  while (!Serial) { delay(1); }
  
    Serial.println("\n\nAdafruit VS1053 Feather Test");
    
    if (!musicPlayer.begin()) { // initialise the music player
       Serial.println(F("Couldn't find VS1053, do you have the right pins defined?"));
       delay(2000);     
       currentScreen = 1;
       startMainScreen(); 
    }
  
    Serial.println(F("VS1053 found"));
//    musicPlayer.setVolume(MP3volume, MP3volume);
//    musicPlayer.sineTest(0x44, 100);    // Make a tone to indicate VS1053 is working
      
    if (!SD.begin(CARDCS)) {
      Serial.println(F("SD failed, or not present"));
      delay(100);
//      Serial.println(F("Starting Main Screen"));
      currentScreen = 1;
      startMainScreen();
    }
    Serial.println("SD OK!");

    if (! musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT)){
      Serial.println(F("DREQ pin is not an interrupt pin"));
    }
    
    musicPlayer.setVolume(MP3volume, MP3volume);
    tft.setCursor(40, 147);
    tft.setTextColor(textColor);
    tft.setTextSize(2);
    tft.setTextColor(textColor);
    tft.setCursor(40, 208);
    tft.setTextColor(textColor);
    tft.setTextSize(2);
    tft.print("Volume:    ");
    tft.setTextColor(textColor);
    tft.print(MP3volume);                                          //write MP3volume
    printTime();
  }

  void printMP3Tracks(){
    Serial.print(F("Now queued: "));Serial.println(F(myAlbum_1_TrackNames[myAlbum_1_Index]));
    tft.fillRoundRect(40, 52, 400, 25,6, HX8357_BLACK);
    sprintf(printMP3Buffer, "%s", myAlbum_1_TrackNames[myAlbum_1_Index]); 
    MP3BufferLength = strlen(printMP3Buffer);
    MP3BufferLength *= 12;                                         //since font size is 2, multiply by 12 pixels per character
    MP3BufferLength /= 2;                                          //divide the adjusted buffer length
    tft.setCursor((240-MP3BufferLength), 55);                      //subtract the adjusted "pixelized" buffer length  
    tft.setTextColor(textColor);
    tft.setTextSize(2);
    tft.print(printMP3Buffer);//tft.print(MP3BufferLength);
    tft.fillRoundRect(40, 77, 400, 25,6, HX8357_BLACK);
    sprintf(printArtistBuffer, "by %s", myAlbum_1_Artist);
    ArtistBufferLength = strlen(printArtistBuffer);
    ArtistBufferLength *= 12;                                      //since font size is 2, multiply by 12 pixels per character
    ArtistBufferLength /= 2;                                       //divide the adjusted buffer length
    tft.setCursor((240-ArtistBufferLength), 80);                   //subtract the adjusted "pixelized" buffer length  
    tft.setTextColor(textColor);
    tft.setTextSize(2);
    tft.print(printArtistBuffer);//tft.print(ArtistBufferLength);
  }
  
  void playMP3Tracks(){
    printMP3Tracks();
    if (! musicPlayer.startPlayingFile(nowPlaying)) {
      Serial.println("Could not open file: ");Serial.print(nowPlaying);
      currentScreen = 1;
      startMainScreen();  
//          while (1);
    }
    Serial.print("Now Playing: ");Serial.println(myAlbum_1_TrackNames[myAlbum_1_Index]);
//    playMusicState = 0;
    while (musicPlayer.playingMusic){
      if (millis() % 500 == 0){
        Serial.println(F("In while loop"));
        controlMP3();
      }
    }
  }
  
  void printInLoop(){
    Serial.print(".");
    delay (1000);  
  }

  void controlMP3(){                                                //control MP3 screen
    if(musicPlayer.playingMusic || musicPlayer.stopped() ) {
      
      TS_Point p = ts.getPoint();                                   
      DateTime now = rtc.now();                                       //get current time and date

      if (millis() % 1000 == 0){
        checkCharge();
      }

//      if (currentMinute != now.minute()){        
//        printTime();
//      }

      vert = tft.height() - p.x;
      horz = p.y;
    
//      Serial.print("X = "); Serial.print(horz);  
//      Serial.print("\tY = "); Serial.print(vert);
//      Serial.print("\tPressure = "); Serial.println(p.z);  
//      Serial.print("Current Screen = "); Serial.println(currentScreen);
      
      p.x = map(p.x, TS_MINY, TS_MAXY, 0, tft.height());              // Scale using the calibration #'s and rotate coordinate system
      p.y = map(p.y, TS_MINX, TS_MAXX, 0, tft.width());
  
      if(tftNotTouched){
//        Serial.println("TOUCH_IRQ is active"); 
        if (ts.touched()){// && tftNotTouched) {                                             //if touch is detected
          if(horz>1720 && horz<2280){                                   //play track
            if(vert>-2000 && vert<-1580){
              Serial.println("play track touched");
              delay(100);

//                for (myAlbum_1_Index = 1; myAlbum_1_Index < 13; myAlbum_1_Index++){ 
                nowPlaying = myAlbum_1_Track[myAlbum_1_Index];
//                playMusicState = 1;  //added in lieu of calling playMP3Tracks from within this function
                getLastTouch();
                delay(100);
                playMP3Tracks(); //commented out to test if calling the function within controlMP3 prevents interrupt
            }
          }
          if(horz>2480 && horz<2750){                                   //pause track
            if(vert>-2000 && vert<-1580){
              Serial.print("pause track touched");            
              delay(100);
                Serial.println("pausing or restarting");              
                if (! musicPlayer.paused()) {
                  Serial.println("Paused");
                  musicPlayer.pausePlaying(true);
                } else { 
                  Serial.println("Resumed");
                  musicPlayer.pausePlaying(false);
                }
              getLastTouch();
              delay(100);
            }
          }
          if(horz>1280 && horz<1580){                                   //stop track
            if(vert>-2000 && vert<-1580){
              delay(100);
                Serial.println("STOPPING");
                musicPlayer.stopPlaying();
                getLastTouch();
                delay(100);

            }
          }
          if(horz>780 && horz<1100){                                   //right blank button
            if(vert>-2000 && vert<-1580){
              delay(100);
              Serial.println("STOPPING");
              musicPlayer.stopPlaying();
              getLastTouch();
              delay(100);
            }
          }
          if(horz>320 && horz<620){                                    //left blank button
            if(vert>-2000 && vert<-1580){
              delay(100);
              musicPlayer.stopPlaying();
              getLastTouch();
              delay(100);
            }
          }
          if(horz>2900 && horz<3200){                                   //next track
            if(vert>-2000 && vert<-1580){
              delay(100);
              Serial.println("STOPPING");
              musicPlayer.stopPlaying();
              if(myAlbum_1_Index<12){
                myAlbum_1_Index ++;
                nowPlaying = myAlbum_1_Track[myAlbum_1_Index];
                printMP3Tracks();
                getLastTouch();
                delay(100);
              } else myAlbum_1_Index = 0;
            }
          }
          if(horz>3350 && horz<3650){                                   //previous track
            if(vert>-2000 && vert<-1580){
              delay(100);
              Serial.println("STOPPING");
              musicPlayer.stopPlaying();
              if (myAlbum_1_Index>0){
                myAlbum_1_Index --;
                nowPlaying = myAlbum_1_Track[myAlbum_1_Index];
                printMP3Tracks();
                getLastTouch();
                delay(100);
              } else myAlbum_1_Index = 13; 
            }
          }
          if(horz>3350 && horz<3650){                                   //volume down
            if(vert>-1340 && vert<-840){
              Serial.println("volume down touched");
              delay(100);
              if (MP3volume < 100){
                MP3volume = MP3volume + 2;
                tft.fillRoundRect(160, 202, 60, 25, 6, HX8357_BLACK);
                musicPlayer.setVolume(MP3volume,MP3volume);
                tft.setCursor(40, 208);
                tft.setTextColor(textColor);
                tft.setTextSize(2);
                tft.print("Volume:    ");
                tft.setTextColor(textColor);
                Serial.print("VOLUME IS NOW:   ");Serial.println(MP3volume);
                tft.print(MP3volume);                 //write volume
                getLastTouch();
                delay(100);
              } else if (MP3volume >= 100){
                  MP3volume = 100;
                  tft.fillRoundRect(160, 202, 80, 25, 6, HX8357_BLACK);
                  musicPlayer.setVolume(MP3volume,MP3volume);
                  tft.setCursor(40, 208);
                  tft.setTextColor(textColor);
                  tft.setTextSize(2);
                  tft.print("Volume:    ");
                  tft.setTextColor(textColor);
                  tft.print(MP3volume);                 //write volume
                  getLastTouch();
                  delay(100);
              }
            }
          }
          if(horz>2900 && horz<3200){                                     //volume up
            if(vert>-1340 && vert<-840){
              Serial.println("volume up touched");
              delay(100);
              if (MP3volume > 50){
                MP3volume = MP3volume - 2;
                tft.fillRoundRect(160, 202, 80, 25, 6, HX8357_BLACK);
                musicPlayer.setVolume(MP3volume,MP3volume);
                tft.setCursor(40, 208);
                tft.setTextColor(textColor);
                tft.setTextSize(2);
                tft.print("Volume:    ");
                Serial.print("VOLUME IS NOW:   ");Serial.println(MP3volume);
                tft.setTextColor(textColor);
                tft.print(MP3volume);                 //write volume
                getLastTouch();
                delay(100);
              } else if (MP3volume <= 50){
                  MP3volume = 50;
                  tft.fillRoundRect(160, 202, 80, 25, 6, HX8357_BLACK);
                  musicPlayer.setVolume(MP3volume,MP3volume);
                  tft.setCursor(40, 208);
                  tft.setTextColor(textColor);
                  tft.setTextSize(2);
                  tft.print("Volume:    ");
                  tft.setTextColor(textColor);
                  tft.print(MP3volume);                 //write volume
                  getLastTouch();
                  delay(100);
                }
            }
          }
          if(horz>2430 && horz<2730){                                     //mute
            if(vert>-1340 && vert<-840){
              int lastMP3volume = MP3volume;
              delay(100);
              if(!muted){ 
                tft.fillRoundRect(160, 202, 80, 25, 6, HX8357_BLACK);
                audioamp.enableChannel(false,false);
                tft.setCursor(40, 208);
                tft.setTextColor(textColor);
                tft.setTextSize(2);
                tft.print("Volume:    ");
                tft.setTextColor(textColor);
                tft.print("MUTED");                 //write volume
                muted = true;
                Serial.print ("muted = ");Serial.print(muted);
                getLastTouch();
                delay(100);
            }else if(muted){
                tft.fillRoundRect(160, 202, 80, 25, 6, HX8357_BLACK);
                musicPlayer.setVolume(lastMP3volume,lastMP3volume);
                audioamp.enableChannel(true,true);
                tft.setCursor(40, 208);
                tft.setTextColor(textColor);
                tft.setTextSize(2);
                tft.print("Volume:    ");
                tft.setTextColor(textColor);
                tft.print(MP3volume);                 //write volume
                muted = false;
                Serial.print ("muted = ");Serial.print(muted);
                getLastTouch();
                delay(100);
                }
            }
          }
          if(horz>3370 && horz<3650){                                     //exit to main screen
            if(vert>-580 && vert<-110){
              delay(100);
              currentScreen = 1;
              musicPlayer.stopPlaying();
              delay(100);
              getLastTouch();
              delay(100);
              startMainScreen();
            }
          }
        }
      }
    }
  }
  
  void startWebRadioScreen(){                                        
    ampOn();
    WiFiPathIndex=0;

    tft.fillScreen(HX8357_BLACK);                                     //fill screen with black
    tft.setCursor(0, 0); 
    tft.setTextColor(textColor);                                  //set text color white
    tft.setTextSize(2);                                               //set text size to 2 (1-6)
    tft.println("           Arduino WiFi Radio");                     //print header to screen
  
    tft.drawRoundRect(10, 20, 460, 290, 6, lineColor);               //draw screen outline
    tft.drawRoundRect(20, 130, 320, 50, 6, lineColor);               //draw station box
    tft.drawRoundRect(20, 190, 260, 50, 6, lineColor);               //draw volume box
    tft.drawRoundRect(290, 190, 50, 50, 6, lineColor);               //draw mute box
    tft.drawRoundRect(20, 30,440, 90, 6, lineColor);                 //draw Artist/Title box
    tft.drawRoundRect(20, 250,380, 50, 6, lineColor);                //draw Time/Date box
    tft.drawRoundRect(410, 250,50, 50, 6, lineColor);                //draw Exit box
    tft.drawRoundRect(350, 130, 50, 50, 6, lineColor);               //draw station up buton
    tft.drawRoundRect(410, 130, 50, 50, 6, lineColor);               //draw station down buton
    tft.drawRoundRect(350, 190, 50, 50, 6, lineColor);               //draw volume up buton
    tft.drawRoundRect(410, 190, 50, 50, 6, lineColor);               //draw volume down buton
    tft.drawTriangle(375, 144, 362, 165, 388, 165,textColor);     //draw up triangle for station
    tft.drawTriangle(435, 165, 422, 144, 448, 144,textColor);     //draw down triangle for station
    tft.drawTriangle(375, 204, 362, 225, 388, 225,textColor);     //draw up triangle for volume
    tft.drawTriangle(435, 225, 422, 204, 448, 204,textColor);     //draw down triangle for volume
    tft.setCursor(308, 203);                                          //put cursor in mute box
    tft.setTextSize(3);                                               //set text size 3
    tft.setTextColor(textColor);
    tft.println("M");                                                 //write a "M" in mute box
    tft.setCursor(428, 265);                                          //put cursor in Exit box
    tft.setTextSize(3);                                               //set text size 3
    tft.setTextColor(textColor);
    tft.println("X");                                                 //write a "X" in Exit box
    printTime();
  
    Serial.println("\n\nAdafruit VS1053 Feather WiFi Radio");
    
    if (!musicPlayer.begin()) { // initialise the music player
       Serial.println(F("Couldn't find VS1053, do you have the right pins defined?"));
       while (1) delay (10);
    }
  
    Serial.println(F("VS1053 found"));
  
    musicPlayer.setVolume(MP3volume, MP3volume);
  //  musicPlayer.sineTest(0x44, 100);    // Make a tone to indicate VS1053 is working
  
    Serial.print("Connecting to SSID "); Serial.println(ssid);
    WiFi.begin(ssid, password);
    
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
   
    Serial.println("WiFi connected");  
    Serial.println("IP address: ");  Serial.println(WiFi.localIP());
    
    initStream();
  }

  void initStream(){
    host = myWiFiHost[WiFiPathIndex];
    path = myWiFiPath[WiFiPathIndex];
    Serial.print("connecting to ");  Serial.println(host);
    
    if (!client.connect(host, httpPort)) {
      Serial.println("Connection failed");
      return;
    }
    
    Serial.print("Requesting URL: ");                                 // We now create a URI for the request
    Serial.println(path);
    
    // This will send the request to the server
    client.print(String("GET ") + path + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" + 
                 "Connection: close\r\n\r\n");
  
  //  client.print(String("GET ") + path + " HTTP/1.1\r\n" +          // under development - trying to capture Metadata from stream
  //               "Host: " + host + "\r\n" + Metadata + "\r\n" +
  //               "Connection: close\r\n\r\n");
  
    if (! musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT)){
      Serial.println(F("DREQ pin is not an interrupt pin"));
    }
    
    sprintf(printStreamBuffer, "%s on %s", myWiFiName[WiFiPathIndex], myWiFiHostName[WiFiPathIndex]); // 
    StreamBufferLength = strlen(printStreamBuffer);
    StreamBufferLength *= 12;                                         //since font size is 2, multiply by 12 pixels per character
    StreamBufferLength /= 2;                                          //divide the adjusted buffer length
    tft.fillRoundRect(40, 62, 400, 25,6, HX8357_BLACK);
    tft.setCursor((240-StreamBufferLength), 67);                      //subtract the adjusted "pixelized" buffer length
    tft.setTextColor(textColor);
    tft.setTextSize(2);
    tft.print(printStreamBuffer);
    sprintf(printSSIDBuffer, "%s", ssid);
    SSIDBufferLength = strlen(printSSIDBuffer);
    SSIDBufferLength *= 12;                                         //since font size is 2, multiply by 12 pixels per character
    SSIDBufferLength /= 2;                                          //divide the adjusted buffer length
    tft.fillRoundRect(40, 142, 280, 25,6, HX8357_BLACK);
    tft.setCursor((190-SSIDBufferLength), 147);                      //subtract the adjusted "pixelized" buffer length
    tft.setTextColor(textColor);
    tft.setTextSize(2);
    tft.print(printSSIDBuffer);
//    sprintf(printHostBuffer, "%s", myWiFiHostName[WiFiPathIndex]);
//    HostBufferLength = strlen(printHostBuffer);
//    HostBufferLength *= 12;                                         //since font size is 2, multiply by 12 pixels per character
//    HostBufferLength /= 2;                                          //divide the adjusted buffer length
//    tft.setCursor((190-HostBufferLength), 147);                      //subtract the adjusted "pixelized" buffer length
//    tft.print(printHostBuffer);
    tft.setCursor(40, 208);
    tft.setTextColor(textColor);
    tft.setTextSize(2);
    tft.print("Volume:    ");
    tft.setTextColor(textColor);
    tft.print(MP3volume);                                              //write volume
  }

  void playStream(){
//    musicPlayer.setVolume(MP3volume, MP3volume);
    musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);                // DREQ int
            // wait till mp3 wants more data
    if (musicPlayer.readyForData()) {
      Serial.print("ready ");
      
      //wants more data! check we have something available from the stream
      if (client.available() > 0) {
        Serial.print("set ");
        // yea! read up to 32 bytes
        uint8_t bytesread = client.read(mp3buff, 32);
  //      Serial.println("mp3buffer results:");
          for (uint8_t i = 0; i <= sizeof(mp3buff); i++) {
  //            Serial.print(mp3buff[i]);
  //            Serial.print("/");
          }
          Serial.println();
          musicPlayer.playData(mp3buff, bytesread);       // push to mp3
          Serial.println("stream!");
      }
    }
  }

  void controlWebRadio(){                       
    TS_Point p = ts.getPoint();                                         //get touch
    DateTime now = rtc.now();                                           //get current time and date
  
    playStream();    

    if (millis() % 1000 == 0){
      checkCharge();
    }
    
    if (currentMinute != now.minute()){        
      printTime();
    }
    
    vert = tft.height() - p.x;
    horz = p.y;
  
//    Serial.print("X = "); Serial.print(horz);  
//    Serial.print("\tY = "); Serial.print(vert);
//    Serial.print("\tPressure = "); Serial.println(p.z);  
//    Serial.print("Current Screen = "); Serial.println(currentScreen);
    
    p.x = map(p.x, TS_MINY, TS_MAXY, 0, tft.height());
    p.y = map(p.y, TS_MINX, TS_MAXX, 0, tft.width());
  
    if (ts.touched()) {                                                 //if touch is detected
      if(horz>2900 && horz<3200){                                       //station up
        if(vert>-1800 && vert<-1400){
          delay(100);
          if(WiFiPathIndex<4){ 
            WiFiPathIndex++;
            tft.fillRoundRect(40, 62, 400, 25,6, HX8357_BLACK);
            tft.fillRoundRect(40, 142, 280, 25,6, HX8357_BLACK);
            initStream();        
            musicPlayer.setVolume(MP3volume, MP3volume);
            playStream();
            delay(100);    
          } else WiFiPathIndex = 0;
              host = myWiFiHost[WiFiPathIndex];
              path = myWiFiPath[WiFiPathIndex];                           //select through the array
              delay(250);
              Serial.println("out of the index loop");
              Serial.println(WiFiPathIndex);
              initStream();        
              musicPlayer.setVolume(MP3volume, MP3volume);
              getLastTouch();
              playStream();
              delay(100); 
        } 
      }
      if(horz>3350 && horz<3650){           //station down
        if(vert>-1800 && vert<-1400){
          delay(100);
          if (WiFiPathIndex > 0){
            WiFiPathIndex--;
            tft.fillRoundRect(40, 62, 400, 25,6, HX8357_BLACK);
            tft.fillRoundRect(40, 142, 280, 25,6, HX8357_BLACK);
            initStream();        
            musicPlayer.setVolume(MP3volume, MP3volume);
            getLastTouch();
            delay(100);
            playStream();    
            } else WiFiPathIndex = 4;
                host = myWiFiHost[WiFiPathIndex];
                path = myWiFiPath[WiFiPathIndex];                           //select through the array
                delay(250);
                Serial.println("out of the index loop");
                Serial.println(WiFiPathIndex);
                initStream();        
                musicPlayer.setVolume(MP3volume, MP3volume);
                getLastTouch();
                delay(100);
                playStream();         
        }
      }
      if(horz>3350 && horz<3650){                                     //volume down
        if(vert>-1340 && vert<-840){
          delay(100);
          if (MP3volume < 100){
            MP3volume=MP3volume+2;
            tft.fillRoundRect(160, 202, 60, 25, 6, HX8357_BLACK);
            musicPlayer.setVolume(MP3volume,MP3volume);
            tft.setCursor(40, 208);
            tft.setTextColor(textColor);
            tft.setTextSize(2);
            tft.print("Volume:    ");
            tft.setTextColor(textColor);
            Serial.print("VOLUME IS NOW:   ");Serial.println(MP3volume);
            tft.print(MP3volume);                 //write volume
            getLastTouch();
            delay(100);
          } else if (MP3volume >= 100){
              MP3volume = 100;
              tft.fillRoundRect(160, 202, 80, 25, 6, HX8357_BLACK);
              musicPlayer.setVolume(MP3volume,MP3volume);
              tft.setCursor(40, 208);
              tft.setTextColor(textColor);
              tft.setTextSize(2);
              tft.print("Volume:    ");
              tft.setTextColor(textColor);
              tft.print(MP3volume);                 //write volume
              getLastTouch();
              delay(100);
            }
        }
      }
      if(horz>2900 && horz<3200){                                     //volume up
        if(vert>-1340 && vert<-840){
          delay(100);
          if (MP3volume > 50){
            MP3volume=MP3volume-2;
            tft.fillRoundRect(160, 202, 80, 25, 6, HX8357_BLACK);
            musicPlayer.setVolume(MP3volume,MP3volume);
            tft.setCursor(40, 208);
            tft.setTextColor(textColor);
            tft.setTextSize(2);
            tft.print("Volume:    ");
            tft.setTextColor(textColor);
            tft.print(MP3volume);                 //write volume
            getLastTouch();
            delay(100);
          } else if (MP3volume <= 50){
              MP3volume = 50;
              tft.fillRoundRect(160, 202, 80, 25, 6, HX8357_BLACK);
              musicPlayer.setVolume(MP3volume,MP3volume);
              tft.setCursor(40, 208);
              tft.setTextColor(textColor);
              tft.setTextSize(2);
              tft.print("Volume:    ");
              tft.setTextColor(textColor);
              tft.print(MP3volume);                 //write volume
              getLastTouch();
              delay(100);
            }
        }
      }
      if(horz>2430 && horz<2730){                                     //mute
        if(vert>-1340 && vert<-840){
          int lastMP3volume = MP3volume;
          delay(100);
          if(!muted){ 
            tft.fillRoundRect(160, 202, 80, 25, 6, HX8357_BLACK);
            audioamp.enableChannel(false,false);
            tft.setCursor(40, 208);
            tft.setTextColor(textColor);
            tft.setTextSize(2);
            tft.print("Volume:    ");
            tft.setTextColor(textColor);
            tft.print("MUTED");                 //write volume
            muted = true;
            Serial.print ("muted = ");Serial.println(muted);
            getLastTouch();
            delay(100);
        }else if(muted){
            tft.fillRoundRect(160, 202, 80, 25, 6, HX8357_BLACK);
            musicPlayer.setVolume(lastMP3volume,lastMP3volume);
            audioamp.enableChannel(true,true);
            tft.setCursor(40, 208);
            tft.setTextColor(textColor);
            tft.setTextSize(2);
            tft.print("Volume:    ");
            tft.setTextColor(textColor);
            tft.print(MP3volume);                 //write volume
            muted = false;
            Serial.print ("muted = ");Serial.println(muted);
            getLastTouch();
            delay(100);
            }
        }
      }
      if(horz>3370 && horz<3650){                                     //exit to main screen
        if(vert>-580 && vert<-110){
          delay(500);
          musicPlayer.stopPlaying();
          currentScreen = 1;
          getLastTouch();
          delay(100);
          startMainScreen();
        }
      }
    }
  }

  void startMenuScreen(){
    tft.fillScreen(HX8357_BLACK);                                   //fill screen with black (ersatz clear)
    tft.setCursor(0, 0); 
    tft.setTextColor(lineColor);                                    //set text color
    tft.setTextSize(2);                                             //set text size to 2
    tft.println("           Arduino Menu Screen");                  //print header to screen
    
    tft.drawRoundRect(10, 20, 460, 290, 6, textColor);              //draw screen outline
    tft.drawRoundRect(30, 170, 200, 50, 6, textColor);              //draw Clock Set button
    tft.drawRoundRect(250, 170, 200, 50, 6, textColor);             //draw Amp Set button
    tft.drawRoundRect(30, 240, 200, 50, 6, textColor);              //draw Alarm Set button
    tft.drawRoundRect(250, 240, 200, 50, 6, textColor);             //draw Main Menu button
    tft.setCursor(78, 188);                                         
    tft.setTextSize(2);                                             
    tft.setTextColor(lineColor);
    tft.println("CLOCK SET");                                       //write "CLOCK SET" in button
    tft.setCursor(308, 188);                                        
    tft.setTextSize(2);                                             
    tft.setTextColor(lineColor);
    tft.println("AMP SET");                                         //write "AMP SET" in button
    tft.setCursor(78, 258);                                         
    tft.setTextSize(2);                                             
    tft.setTextColor(lineColor);
    tft.println("ALARM SET");                                       //write "ALARM SET" in menu button
    tft.setCursor(298, 258);                                        
    tft.setTextSize(2);                                             
    tft.setTextColor(lineColor);
    tft.println("MAIN MENU");                                       //write "MAIN MENU" in menu button
    printTime();
  }

  void controlMenuScreen(){                                         //logic for menu screen control
    TS_Point p = ts.getPoint();                                     //get touch
    DateTime now = rtc.now();                                       //get current time and date
    
    if (millis() % 1000 == 0){
      checkCharge();
    }

    if (currentMinute != now.minute()){        
      printTime();
      if (alarmSet){
        printAlarmIndicator();
      }
    }

    vert = tft.height() - p.x;
    horz = p.y;

//    Serial.print("X = "); Serial.print(horz);  
//    Serial.print("\tY = "); Serial.print(vert);
//    Serial.print("\tPressure = "); Serial.println(p.z);  
//    Serial.print("Current Screen = "); Serial.println(currentScreen);
                                     
    p.x = map(p.x, TS_MINY, TS_MAXY, 0, tft.height());                // Scale using the calibration #'s and rotate coordinate system    
    p.y = map(p.y, TS_MINX, TS_MAXX, 0, tft.width());                 
    
    if (ts.touched()) {
      if(horz>400 && horz<1890){                                      //select Clock Set Screen
        if(vert>-1480 && vert<-1000){
          delay(100);
          currentScreen = 6;                                          //send status to loop() to activate controlClockSetScreen
          getLastTouch();
          delay(100);
          startClockSetScreen();                                      //call startClockSetScreen function
        }
      }
      if(horz>2150 && horz<3600){                                     //select Amp Settings
        if(vert>-1480 && vert<-1000){
//          delay(100);
//          currentScreen = 9;                                          //send status to loop() to activate controlAmpSetScreen 
//          getLastTouch();                                             
//          delay(100);
//          startAmpSetScreen();                                        //call startAmpSetScreen function
        }  
      }
      if(horz>2150 && horz<3600){                                     //select Main Menu
        if(vert>-550 && vert<-200){
          delay(100);
          currentScreen = 1;
          getLastTouch();
          delay(100);
          startMainScreen();                                          //call startMenuScreen function
        }
      }
      if(horz>400 && horz<1890){                                      //reserved for Alarm Settings 
        if(vert>-550 && vert<-200){
          delay(100);
          currentScreen = 7;
          getLastTouch();
          delay(100);
          startAlarmSetScreen();                                      //call startAlarmScreen function
        }
      }
    }
  }
  
  void startClockSetScreen(){
    tft.fillScreen(HX8357_BLACK);                                     //fill screen with black
    tft.setCursor(0, 0); 
    tft.setTextColor(textColor);                                      //set text color white
    tft.setTextSize(2);                                               //set text size to 2 (1-6)
    tft.println("         Arduino Clock Set Menu");                   //print header to screen
  
    tft.drawRoundRect(10, 20, 460, 290, 6, lineColor);                //draw screen outline
    tft.drawRoundRect(20, 45, 100, 40, 6, lineColor);                 //draw hour box
    tft.drawRoundRect(20, 95, 100, 40, 6, lineColor);                 //draw minute box
    tft.drawRoundRect(20, 145, 100, 40, 6, lineColor);                //draw date box
    tft.drawRoundRect(20, 195, 100, 40, 6, lineColor);                //draw month box
    tft.drawRoundRect(20, 245, 100, 40, 6, lineColor);                //draw year box
    tft.drawRoundRect(420, 145, 40, 40, 6, lineColor);                //draw Set box
    tft.drawRoundRect(420, 260, 40, 40, 6, lineColor);                //draw Exit box
  
    tft.drawRoundRect(250, 45, 40, 40, 6, lineColor);                 //draw hour up buton
    tft.drawRoundRect(300, 45, 40, 40, 6, lineColor);                 //draw hour down buton
    tft.drawRoundRect(250, 95, 40, 40, 6, lineColor);                 //draw minute up buton
    tft.drawRoundRect(300, 95, 40, 40, 6, lineColor);                 //draw minute down buton
    tft.drawRoundRect(250, 145, 40, 40, 6, lineColor);                //draw date up buton
    tft.drawRoundRect(300, 145, 40, 40, 6, lineColor);                //draw date down buton
    tft.drawRoundRect(250, 195, 40, 40, 6, lineColor);                //draw month up buton
    tft.drawRoundRect(300, 195, 40, 40, 6, lineColor);                //draw month down buton
    tft.drawRoundRect(250, 245, 40, 40, 6, lineColor);                //draw year up buton
    tft.drawRoundRect(300, 245, 40, 40, 6, lineColor);                //draw year down buton
      
    tft.drawTriangle(270, 55, 258, 75, 282, 75,textColor);            //draw up triangle for hour
    tft.drawTriangle(320, 75, 308, 55, 332, 55,textColor);            //draw down triangle for hour
    tft.drawTriangle(270, 105, 258, 125, 282, 125,textColor);         //draw up triangle for minute
    tft.drawTriangle(320, 125, 308, 105, 332, 105,textColor);         //draw down triangle for minute
    tft.drawTriangle(270, 155, 258, 175, 282, 175,textColor);         //draw up triangle for date
    tft.drawTriangle(320, 175, 308, 155, 332, 155,textColor);         //draw down triangle for date
    tft.drawTriangle(270, 205, 258, 225, 282, 225,textColor);         //draw up triangle for month
    tft.drawTriangle(320, 225, 308, 205, 332, 205,textColor);         //draw down triangle for month
    tft.drawTriangle(270, 255, 258, 275, 282, 275,textColor);         //draw up triangle for year
    tft.drawTriangle(320, 275, 308, 255, 332, 255,textColor);         //draw down triangle for year
  
    tft.drawLine(380, 65, 380, 265, lineColor);
    tft.drawLine(360, 65, 380, 65, lineColor);
    tft.drawLine(360, 265, 380, 265, lineColor);
    tft.drawLine(380, 165, 400, 165, lineColor);
    
    tft.setTextSize(2);                                               
    tft.setTextColor(textColor);
    tft.setCursor(145,57);                                          
    tft.print("Hour");
    tft.setCursor(145,107);
    tft.print("Minute");
    tft.setCursor(145,157);
    tft.print("Date");
    tft.setCursor(145,207);
    tft.print("Month");
    tft.setCursor(145,257);
    tft.print("Year");

    estabTimeSet();
    delay(50);
    
    tft.setCursor(50,57);                                          
    tft.print(setHour);
    tft.setCursor(50,107);
    tft.print(setMinute);
    tft.setCursor(50,157);
    tft.print(setDate);
    tft.setCursor(50,207);
    tft.print(setMonth);
    tft.setCursor(50,257);
    tft.print(setYear);
    
    tft.setTextSize(3);                                               
    tft.setTextColor(textColor);
    tft.setCursor(433, 155);                                          //put cursor in Set box
    tft.println("S");                                                 //write a "S" in Set box
    tft.setCursor(433, 270);                                          //put cursor in Exit box
    tft.println("X");                                                 //write a "X" in Exit box
  }
  
  void controlClockSetScreen(){
      TS_Point p = ts.getPoint();                                     //get touch
  
      vert = tft.height() - p.x;
      horz = p.y;
  
//      Serial.print("X = "); Serial.print(horz);  
//      Serial.print("\tY = "); Serial.print(vert);
//      Serial.print("\tPressure = "); Serial.println(p.z);  
//      Serial.print("Current Screen = "); Serial.println(currentScreen);
                                       
      p.x = map(p.x, TS_MINY, TS_MAXY, 0, tft.height());                // Scale using the calibration #'s and rotate coordinate system    
      p.y = map(p.y, TS_MINX, TS_MAXX, 0, tft.width());                 
  
      if (ts.touched()) {
        if(horz>2100 && horz<2370){                                      //hour up
          if(vert>-2900 && vert<-2590){
            delay(100);
            if (setHour < 23){
              setHour ++;
              tft.fillRoundRect(25, 50, 90, 30, 6, HX8357_RED);
              tft.setTextSize(2);
              tft.setCursor(50,57);
              tft.print(setHour);
              getLastTouch();
              delay(100);
            } else setHour = 0;
          }
        }
        if(horz>2500 && horz<2770){                                     //hour down
          if(vert>-2900 && vert<-2590){
            delay(100);
            if (setHour > 0){
              setHour --;
              tft.fillRoundRect(25, 50, 90, 30, 6, HX8357_RED);
              tft.setTextSize(2);
              tft.setCursor(50,57);
              tft.print(setHour);
              getLastTouch();
              delay(100);
            } else setHour = 23;
          }
        }
        if(horz>2100 && horz<2370){                                      //minute up
          if(vert>-2300 && vert<-1950){
            delay(100);
            if (setMinute < 59){
              setMinute ++;
              tft.fillRoundRect(25, 100, 90, 30, 6, HX8357_RED);
              tft.setTextSize(2);
              tft.setCursor(50,107);
              tft.print(setMinute);
              getLastTouch();
              delay(100);
            } else setMinute = 0;
          }
        }
        if(horz>2500 && horz<2770){                                     //minute down
          if(vert>-2300 && vert<-1950){
            delay(100);
            if (setMinute > 0){
              setMinute --;
              tft.fillRoundRect(25, 100, 90, 30, 6, HX8357_RED);
              tft.setTextSize(2);
              tft.setCursor(50,107);
              tft.print(setMinute);
              getLastTouch();
              delay(100);
            } else setMinute = 59;
          }
        }
        if(horz>2100 && horz<2370){                                      //date up
          if(vert>-1750 && vert<-1350){
            delay(100);
            if (setDate < 31){
              setDate ++;
              tft.fillRoundRect(25, 150, 90, 30, 6, HX8357_RED);
              tft.setTextSize(2);
              tft.setCursor(50,157);
              tft.print(setDate);
              getLastTouch();
              delay(100);
            } else setDate = 0;
          }
        }
        if(horz>2500 && horz<2770){                                     //date down
          if(vert>-1750 && vert<-1350){
            delay(100);
            if (setDate > 1){
              setDate --;
              tft.fillRoundRect(25, 150, 90, 30, 6, HX8357_RED);
              tft.setTextSize(2);
              tft.setCursor(50,157);
              tft.print(setDate);
              getLastTouch();
              delay(100);
            } else setDate = 32;
          }
        }
        if(horz>2100 && horz<2370){                                      //month up
          if(vert>-1120 && vert<-730){
            delay(100);
            if (setMonth < 13){
              setMonth ++;
              tft.fillRoundRect(25, 200, 90, 30, 6, HX8357_RED);
              tft.setTextSize(2);
              tft.setCursor(50,207);
              tft.print(setMonth);
              getLastTouch();
              delay(100);
            } else setMonth = 0;
          }
        }
        if(horz>2500 && horz<2770){                                     //month down
          if(vert>-1120 && vert<-730){
            delay(100);
            if (setMonth > 1){
              setMonth --;
              tft.fillRoundRect(25, 200, 90, 30, 6, HX8357_RED);
              tft.setTextSize(2);
              tft.setCursor(50,207);
              tft.print(setMonth);
              getLastTouch();
              delay(100);
            } else setMonth = 13;
          }
        }
        if(horz>2100 && horz<2370){                                      //year up
          if(vert>-550 && vert<-160){
            delay(100);
            if (setYear < 2050){
              setYear ++;
              tft.fillRoundRect(25, 250, 90, 30, 6, HX8357_RED);
              tft.setTextSize(2);
              tft.setCursor(50,257);
              tft.print(setYear);
              getLastTouch();
              delay(100);
            } else setYear = 2018;
          }
        }
        if(horz>2500 && horz<2770){                                     //year down
          if(vert>-550 && vert<-160){
            delay(100);
            if (setYear > 2018){
              setYear --;
              tft.fillRoundRect(25, 250, 90, 30, 6, HX8357_RED);
              tft.setTextSize(2);
              tft.setCursor(50,257);
              tft.print(setYear);
              getLastTouch();
              delay(100);
            } else setYear = 2050;
          }
        }
        if(horz>3450 && horz<3670){                                     //set Time
          if(vert>-1760 && vert<-1330){
            delay(100);
            printSetTime();
            rtc.adjust(DateTime(setYear, setMonth, setDate, setHour, setMinute, 0));
            getLastTouch();
            delay(100);
          }
        }
        if(horz>3370 && horz<3650){                                     //exit to menu screen
          if(vert>-580 && vert<-110){
            delay(100);
            currentScreen = 5;
            getLastTouch();
            delay(100);
            startMenuScreen();
          }
        } 
      }
  }

  void checkCharge(){
    float charge = analogRead(CHARGEPIN);
    charge *= 3.3;
    charge /= 1024; // convert to voltage (12 bit ADC should be 4096?)
    //Serial.print(charge);Serial.println("v = charger/USB connected");
    if (charge>2.2){
      drawChargeIndicator();
    } else measureVBat();
  }
  
  void measureVBat(){
    float measuredvbat = analogRead(VBATPIN);
    measuredvbat *= 2; // we divided by 2, so multiply back
    measuredvbat *= 3.3; // Multiply by 3.3V, our reference voltage
    measuredvbat /= 1024; // convert to voltage (12 bit ADC should be 4096??) 
    float charge = analogRead(CHARGEPIN);
    charge *= 3.3;
    charge /= 1024; // convert to voltage (12 bit ADC should be 4096??)
    Serial.print("VBat: " ); Serial.println(measuredvbat);
//    tft.setCursor(50, 140);                                    
//    tft.setTextSize(2);                                         
//    tft.fillRoundRect(36, 135, 185, 25, 6, HX8357_BLACK);
//    tft.print(measuredvbat); tft.print("v");tft.print(" || ");tft.print(charge);
    drawBatIndicator();        
  }

  void drawBatIndicator(){
    float measuredvbat = analogRead(VBATPIN);
    measuredvbat *= 2; // we divided by 2, so multiply back
    measuredvbat *= 3.3; // Multiply by 3.3V, our reference voltage
    measuredvbat /= 1024; // convert to voltage (12 bit ADC should be 4096?) 
    tft.drawRect(430,3,32,12,lineColor);                           //draw battery body
    tft.fillRect(462,6,3,5,lineColor);                             //draw battery "+" terminal
    tft.fillRect(432,5,28,8,HX8357_BLACK);                       
    if (measuredvbat <= 3.6){
      tft.fillRect(432, 5, 8, 8, textColor);                    //draw 1/3 battery level
    } else if (measuredvbat > 3.6 && measuredvbat <= 3.9){
        tft.fillRect(432, 5, 8, 8, textColor);                  //draw 1/3 battery level
        tft.fillRect(442, 5, 8, 8, textColor);                  //draw 2/3 battery level
    } else if (measuredvbat > 3.9){
        tft.fillRect(432, 5, 8, 8, textColor);                  //draw 1/3 battery level
        tft.fillRect(442, 5, 8, 8, textColor);                  //draw 2/3 battery level
        tft.fillRect(452, 5, 8, 8, textColor);                  //draw 3/3 battery level
    }
    currentSecond = now.second();         
  }

  void drawChargeIndicator(){
      tft.drawRect(430,3,32,12,lineColor);                      //draw battery body
      tft.fillRect(462,6,3,5,lineColor);                        //draw battery "+" terminal
      tft.fillRect(432,5,28,8,HX8357_BLACK);                       
      tft.drawLine(437,10,448,7,textColor);                     //draw charge indicator
      tft.drawLine(448,7,448,9,textColor);                      // |       |         | 
      tft.drawLine(448,9,455,8,textColor);                      // |       |         |
      tft.drawLine(455,8,446,11,textColor);                     // |       |         |
      tft.drawLine(446,11,446,9,textColor);                     // |       |         |
      tft.drawLine(446,9,437,10,textColor);                     // |       |         |
      tft.drawLine(447,8,447,10,textColor);                     //fill charge indicator
      currentSecond = now.second();
    }

//  void clearTSBuffer(){
//    TS_Point p;
//    while (!ts.bufferEmpty()){
//        p = ts.getPoint();
//    }
////    ts.begin();
//    return;
//  }

  TS_Point getLastTouch(){    //In theory, this function should work without the ts.begin(). I'm
//    clearTSBuffer();      //leaving in as-is for future troubleshooting.
    TS_Point p;
    if (!ts.bufferEmpty()){
      p = ts.getPoint();
    }
    ts.begin();
    return p;
  }

  void printTime(){
    switch (currentScreen){
      case 1:
        do{
        DateTime now = rtc.now();                                     //get current time and date
        tft.fillRoundRect(120, 45, 240, 65, 6, HX8357_BLACK);          //fill black rectangle to erase previous reading
        hr =  (now.hour()==23 || now.hour()==12 )? 12 : (now.hour()>12)? now.hour()-12 : now.hour();
        sprintf(timeBuffer, "%01d:%02d %2s",hr,(now.minute()),((now.hour()>=12)? "pm" : "am"));
        timeBufferLength = strlen(timeBuffer);
        timeBufferLength *= 22;                                       //since font size is Sans12pt, multiply by 22 pixels per character
        timeBufferLength /= 2;                                        //divide the adjusted buffer length
        tft.setCursor((240-timeBufferLength), 83);                    //subtract the adjusted "pixelized" buffer length  
        tft.setTextColor(textColor);                              //set text color
        tft.setFont(&FreeSans12pt7b);                                 //set font 
        tft.print(timeBuffer);
        tft.fillRoundRect(36, 110, 410, 35, 6, HX8357_BLACK); 
        sprintf(dateBuffer, "%s, %s %02d, %04d",(daysOfTheWeek[now.dayOfTheWeek()]), (months[now.month()-1]), (now.day()), (now.year()));
        dateBufferLength = strlen(dateBuffer);
        dateBufferLength *=12;                                        //since font size is 2, multiply by 12 pixels per character     
        dateBufferLength /= 2;                                        //divide the adjusted buffer length
        tft.setCursor((240-dateBufferLength), 120);                   //subtract the adjusted "pixelized" buffer length  
        tft.setTextColor(textColor);                              //set text color
        tft.setFont();
        tft.setTextSize(2);                                         
        tft.print(dateBuffer);
        currentMinute = now.minute();      
        } while (now.second() == 0); 
        break;
      case 2:
        do{
          DateTime now = rtc.now();                                   //get current time and date
          tft.fillRoundRect(36, 260, 340, 23, 6, HX8357_BLACK);
          hr =  (now.hour()==23 || now.hour()==12 )? 12 : (now.hour()>12)? now.hour()-12 : now.hour();
          sprintf(dateTimeBuffer, "%02d/%02d/%04d   %01d:%02d %2s",(now.month()),(now.day()),(now.year()),hr,(now.minute()),((now.hour()>=12)? " PM" : " AM"));
          dateTimeBufferLength = strlen(dateTimeBuffer);
          dateTimeBufferLength *=12;                                  //since font size is 2, multiply by 12 pixels per c haracter     
          dateTimeBufferLength /= 2;                                  //divide the adjusted buffer length 
          tft.setCursor((210-dateTimeBufferLength), 268);             //subtract the adjusted "pixelized" buffer length  
          tft.setTextColor(textColor);
          tft.setTextSize(2);
          tft.print(dateTimeBuffer);
          currentMinute = now.minute();      
        } while (now.second() == 0);
        break;
      case 3:
        do{
          DateTime now = rtc.now();                                   //get current time and date
          tft.fillRoundRect(36, 260, 340, 23, 6, HX8357_BLACK);
          hr =  (now.hour()==23 || now.hour()==12 )? 12 : (now.hour()>12)? now.hour()-12 : now.hour();
          sprintf(dateTimeBuffer, "%02d/%02d/%04d   %01d:%02d %2s",(now.month()),(now.day()),(now.year()),hr,(now.minute()),((now.hour()>=12)? " PM" : " AM"));
          dateTimeBufferLength = strlen(dateTimeBuffer);
          dateTimeBufferLength *=12;                                  //since font size is 2, multiply by 12 pixels per c haracter     
          dateTimeBufferLength /= 2;                                  //divide the adjusted buffer length 
          tft.setCursor((210-dateTimeBufferLength), 268);             //subtract the adjusted "pixelized" buffer length  
          tft.setTextColor(textColor);
          tft.setTextSize(2);
          tft.print(dateTimeBuffer);
          currentMinute = now.minute();      
        } while (now.second() == 0);
        break;
      case 4:
        do{
          DateTime now = rtc.now();                                   //get current time and date
          tft.fillRoundRect(36, 260, 340, 23, 6, HX8357_BLACK);
          hr =  (now.hour()==23 || now.hour()==12 )? 12 : (now.hour()>12)? now.hour()-12 : now.hour();
          sprintf(dateTimeBuffer, "%02d/%02d/%04d   %01d:%02d %2s",(now.month()),(now.day()),(now.year()),hr,(now.minute()),((now.hour()>=12)? " PM" : " AM"));
          dateTimeBufferLength = strlen(dateTimeBuffer);
          dateTimeBufferLength *=12;                                  //since font size is 2, multiply by 12 pixels per c haracter     
          dateTimeBufferLength /= 2;                                  //divide the adjusted buffer length 
          tft.setCursor((210-dateTimeBufferLength), 268);             //subtract the adjusted "pixelized" buffer length  
          tft.setTextColor(textColor);
          tft.setTextSize(2);
          tft.print(dateTimeBuffer);
          currentMinute = now.minute();      
        } while (now.second() == 0);
        break;
      case 5:
        do{
          DateTime now = rtc.now();                                     //get current time and date
          tft.fillRoundRect(120, 45, 240, 65, 6, HX8357_BLACK);          //fill black rectangle to erase previous reading
          hr =  (now.hour()==23 || now.hour()==12 )? 12 : (now.hour()>12)? now.hour()-12 : now.hour();
          sprintf(timeBuffer, "%01d:%02d %2s",hr,(now.minute()),((now.hour()>=12)? "pm" : "am"));
          timeBufferLength = strlen(timeBuffer);
          timeBufferLength *= 22;                                       //since font size is Sans12pt, multiply by 22 pixels per character
          timeBufferLength /= 2;                                        //divide the adjusted buffer length
          tft.setCursor((240-timeBufferLength), 83);                    //subtract the adjusted "pixelized" buffer length  
          tft.setTextColor(lineColor);                              //set text color
          tft.setFont(&FreeSans12pt7b);                                 //set font 
          tft.print(timeBuffer);
          tft.fillRoundRect(36, 110, 410, 35, 6, HX8357_BLACK); 
          sprintf(dateBuffer, "%s, %s %02d, %04d",(daysOfTheWeek[now.dayOfTheWeek()]), (months[now.month()-1]), (now.day()), (now.year()));
          dateBufferLength = strlen(dateBuffer);
          dateBufferLength *=12;                                        //since font size is 2, multiply by 12 pixels per character     
          dateBufferLength /= 2;                                        //divide the adjusted buffer length
          tft.setCursor((240-dateBufferLength), 120);                   //subtract the adjusted "pixelized" buffer length  
          tft.setTextColor(lineColor);                              //set text color
          tft.setFont();
          tft.setTextSize(2);                                         
          tft.print(dateBuffer);      
          currentMinute = now.minute();
        } while (now.second() == 0); 
        break;
      case 8:
        do{
        DateTime now = rtc.now();                                     //get current time and date
        tft.fillRoundRect(120, 45, 240, 65, 6, HX8357_BLACK);          //fill black rectangle to erase previous reading
        hr =  (now.hour()==23 || now.hour()==12 )? 12 : (now.hour()>12)? now.hour()-12 : now.hour();
        sprintf(timeBuffer, "%01d:%02d %2s",hr,(now.minute()),((now.hour()>=12)? "pm" : "am"));
        timeBufferLength = strlen(timeBuffer);
        timeBufferLength *= 22;                                       //since font size is Sans12pt, multiply by 22 pixels per character
        timeBufferLength /= 2;                                        //divide the adjusted buffer length
        tft.setCursor((240-timeBufferLength), 83);                    //subtract the adjusted "pixelized" buffer length  
        tft.setTextColor(textColor);                              //set text color
        tft.setFont(&FreeSans12pt7b);                                 //set font 
        tft.print(timeBuffer);
        tft.fillRoundRect(36, 110, 410, 35, 6, HX8357_BLACK); 
        sprintf(dateBuffer, "%s, %s %02d, %04d",(daysOfTheWeek[now.dayOfTheWeek()]), (months[now.month()-1]), (now.day()), (now.year()));
        dateBufferLength = strlen(dateBuffer);
        dateBufferLength *=12;                                        //since font size is 2, multiply by 12 pixels per character     
        dateBufferLength /= 2;                                        //divide the adjusted buffer length
        tft.setCursor((240-dateBufferLength), 120);                   //subtract the adjusted "pixelized" buffer length  
        tft.setTextColor(textColor);                              //set text color
        tft.setFont();
        tft.setTextSize(2);                                         
        tft.print(dateBuffer);
        currentMinute = now.minute();      
        } while (now.second() == 0); 
        break;            
    }
    return;
  }
  
  void estabTimeSet(){                                              //establish current time to populate starting time/date on Clock Set Screen
    DateTime now = rtc.now();
    setHour = now.hour();
    setMinute = now.minute();
    setDate = now.day();
    setMonth = now.month();
    setYear = now.year();
  }

  void printSetTime(){                                              //populate time/date on Clock Set Screen
    tft.setTextSize(2);
    tft.fillRoundRect(25, 50, 90, 30, 6, HX8357_BLACK);  
    tft.setCursor(50,57);
    tft.print(setHour);
    tft.fillRoundRect(25, 100, 90, 30, 6, HX8357_BLACK);  
    tft.setCursor(50,107);
    tft.print(setMinute);
    tft.fillRoundRect(25, 150, 90, 30, 6, HX8357_BLACK);  
    tft.setCursor(50,157);
    tft.print(setDate);
    tft.fillRoundRect(25, 200, 90, 30, 6, HX8357_BLACK);  
    tft.setCursor(50,207);
    tft.print(setMonth);
    tft.fillRoundRect(25, 250, 90, 30, 6, HX8357_BLACK);  
    tft.setCursor(50,257);
    tft.print(setYear);
  }


//  void backLiteOff(){
//    digitalWrite(backLitePin, LOW);  
//  }
//
//  void backLiteOn(){
//    digitalWrite(backLitePin, HIGH);  
//  }
  
  void Si4703PowerOff(){                                            // TODO: modify to use library function instead
    digitalWrite(resetPin, LOW);                                    //Put Si4703 into reset
    Serial.println("Radio is OFF");
  }

  void ampOff(){ 
    delay(50);
    digitalWrite(ampPin, LOW);                                    //using SHDN to power down the TPA2016 to avoid noise when not in use
    Serial.println("Amp is OFF");
  }
  
  void ampOn(){ 
    digitalWrite(ampPin, HIGH);                                   //Powering up from powered-down state
    Serial.println("Amp is ON");
    delay(50);
//      audioamp.setGain(25);
//      delay(50);
    setAmp();
    delay(50);
  }                                         
  
  void setAmp(){ 
  
    Serial.println("Setting AGC Compression");
//    audioamp.setAGCCompression(TPA2016_AGC_OFF);                      // AGC can be TPA2016_AGC_OFF (no AGC) or
    audioamp.setAGCCompression(TPA2016_AGC_2);                        // TPA2016_AGC_2 (1:2 compression)
//    audioamp.setReleaseControl(0);                                    // turn off the release to really turn off AGC
//    audioamp.setAGCCompression(TPA2016_AGC_4);                        // TPA2016_AGC_2 (1:4 compression)
//    audioamp.setAGCCompression(TPA2016_AGC_8);                        // TPA2016_AGC_2 (1:8 compression)

    Serial.println("Setting Limit Level");
    audioamp.setLimitLevelOn();                                       // See Datasheet page 22 for value -> dBV conversion table
                                                                      // or turn off with setLimitLevelOff()
    audioamp.setLimitLevel(28);                                       // range from 0 (-6.5dBv) to 31 (9dBV)
  
    Serial.println("Setting AGC Attack");
    audioamp.setAttackControl(1);                                     // See Datasheet page 23 for value -> ms conversion table
  
    Serial.println("Setting AGC Hold");
    audioamp.setHoldControl(0);                                       // See Datasheet page 24 for value -> ms conversion table
  
    Serial.println("Setting AGC Release");
    audioamp.setReleaseControl(1);                                   // See Datasheet page 24 for value -> ms conversion table
  }

  void estabAlarmSet(){
    DateTime now = rtc.now();
    setAlarmHour = now.hour();
    setAlarmMinute = now.minute();
    setSchedIndex = 0;
  }

  void printSetAlarmTime(){
    tft.setTextSize(2);
    tft.fillRoundRect(25, 50, 90, 30, 6, HX8357_BLACK);  
    tft.setCursor(50,57);
    tft.print(setAlarmHour);
    tft.fillRoundRect(25, 100, 90, 30, 6, HX8357_BLACK);  
    tft.setCursor(50,107);
    tft.print(setAlarmMinute);
    tft.fillRoundRect(25, 150, 90, 30, 6, HX8357_BLACK);  
    tft.setCursor(40,157);
    tft.print(nowScheduled);
//    tft.fillRoundRect(25, 200, 90, 30, 6, HX8357_BLACK);  
//    tft.setCursor(50,207);
//    tft.print(setMonth);
//    tft.fillRoundRect(25, 250, 90, 30, 6, HX8357_BLACK);  
//    tft.setCursor(50,257);
//    tft.print(setYear);
  }
  
  void startAlarmSetScreen(){
    tft.fillScreen(HX8357_BLACK);                                     //fill screen with black
    tft.setCursor(0, 0); 
    tft.setTextColor(textColor);                                      //set text color white
    tft.setTextSize(2);                                               //set text size to 2 (1-6)
    tft.println("         Arduino Clock Set Menu");                   //print header to screen
  
    tft.drawRoundRect(10, 20, 460, 290, 6, lineColor);                //draw screen outline
    tft.drawRoundRect(20, 45, 100, 40, 6, lineColor);                 //draw hour box
    tft.drawRoundRect(20, 95, 100, 40, 6, lineColor);                 //draw minute box
    tft.drawRoundRect(20, 145, 100, 40, 6, lineColor);                //draw date box
//    tft.drawRoundRect(20, 195, 100, 40, 6, lineColor);                //draw month box
//    tft.drawRoundRect(20, 245, 100, 40, 6, lineColor);                //draw year box
    tft.drawRoundRect(420, 145, 40, 40, 6, lineColor);                //draw Set box
    tft.drawRoundRect(420, 260, 40, 40, 6, lineColor);                //draw Exit box
  
    tft.drawRoundRect(250, 45, 40, 40, 6, lineColor);                 //draw hour up buton
    tft.drawRoundRect(300, 45, 40, 40, 6, lineColor);                 //draw hour down buton
    tft.drawRoundRect(250, 95, 40, 40, 6, lineColor);                 //draw minute up buton
    tft.drawRoundRect(300, 95, 40, 40, 6, lineColor);                 //draw minute down buton
    tft.drawRoundRect(250, 145, 40, 40, 6, lineColor);                //draw schedule up buton
    tft.drawRoundRect(300, 145, 40, 40, 6, lineColor);                //draw schedule down buton
//    tft.drawRoundRect(250, 195, 40, 40, 6, lineColor);                //draw month up buton
//    tft.drawRoundRect(300, 195, 40, 40, 6, lineColor);                //draw month down buton
//    tft.drawRoundRect(250, 245, 40, 40, 6, lineColor);                //draw year up buton
    tft.drawRoundRect(300, 245, 40, 40, 6, lineColor);                //draw year down buton
      
    tft.drawTriangle(270, 55, 258, 75, 282, 75,textColor);            //draw up triangle for hour
    tft.drawTriangle(320, 75, 308, 55, 332, 55,textColor);            //draw down triangle for hour
    tft.drawTriangle(270, 105, 258, 125, 282, 125,textColor);         //draw up triangle for minute
    tft.drawTriangle(320, 125, 308, 105, 332, 105,textColor);         //draw down triangle for minute
    tft.drawTriangle(270, 155, 258, 175, 282, 175,textColor);         //draw up triangle for schedule
    tft.drawTriangle(320, 175, 308, 155, 332, 155,textColor);         //draw down triangle for schedule
//    tft.drawTriangle(270, 205, 258, 225, 282, 225,textColor);         //draw up triangle for month
//    tft.drawTriangle(320, 225, 308, 205, 332, 205,textColor);         //draw down triangle for month
//    tft.drawTriangle(270, 255, 258, 275, 282, 275,textColor);         //draw up triangle for year
//    tft.drawTriangle(320, 275, 308, 255, 332, 255,textColor);         //draw down triangle for year
  
    tft.drawLine(380, 65, 380, 265, lineColor);
    tft.drawLine(360, 65, 380, 65, lineColor);
    tft.drawLine(360, 265, 380, 265, lineColor);
    tft.drawLine(380, 165, 400, 165, lineColor);
    
    tft.setTextSize(2);                                               
    tft.setTextColor(textColor);
    tft.setCursor(145,57);                                          
    tft.print("Hour");
    tft.setCursor(145,107);
    tft.print("Minute");
    tft.setCursor(145,157);
    tft.print("Schedule");
//    tft.setCursor(145,207);
//    tft.print("Month");
    if (alarmSet){
      tft.setCursor(214,257);
      tft.print("ON");
    } else if(!alarmSet){
      tft.setCursor(210,257);
      tft.print("OFF");
    }

    estabAlarmSet();
    nowScheduled = alarmSched[setSchedIndex];
    delay(50);
    
    tft.setCursor(50,57);                                          
    tft.print(setAlarmHour);
    tft.setCursor(50,107);
    tft.print(setAlarmMinute);
    tft.setCursor(40,157);
    tft.print(nowScheduled);
//    tft.setCursor(50,207);
//    tft.print(setMonth);
//    tft.setCursor(50,257);
//    tft.print(setYear);
    
    tft.setTextSize(3);                                               
    tft.setTextColor(textColor);
    tft.setCursor(433, 155);                                          //put cursor in Set box
    tft.println("S");                                                 //write a "S" in Set box
    tft.setCursor(312, 255);                                          //put cursor in On/Off box
    tft.println("A");                                                 //write a "A" in On/Off box
    tft.setCursor(433, 270);                                          //put cursor in Exit box
    tft.println("X");                                                 //write a "X" in Exit box
  }

  void soundAlarm(){
    if (currentAlarmState != previousAlarmState){
      ampOn();
      radio.powerOn();                                                  //activate Si4703
      radio.setChannel(myStations[0]);                                  //set band and freq
      radio.setVolume(volume);                                          //set volume
      volume = constrain(volume, 1, 15); 
      tft.fillRoundRect(360, 70, 90, 25, 6, HX8357_BLACK);
      tft.setTextSize(2);                                               
      tft.setTextColor(textColor);
      tft.setCursor(390, 75);                                          
      tft.println("WAKE");
//      Serial.print("ALARM TIME: "); Serial.println(alarmTimer);
      DateTime now = rtc.now();
    } else {
        ampOff();
        Si4703PowerOff();
    }
    currentAlarmState = false;
  }

  void printAlarmIndicator(){
      if (alarmSet && !currentSnoozeState && !currentAlarmState){
        tft.fillRoundRect(360, 70, 100, 25, 6, HX8357_BLACK);
        tft.setTextSize(2);                                               
        tft.setTextColor(textColor);
        tft.setCursor(380, 75);                                          
        tft.println("ALARM");
      }
      else if (alarmSet && currentSnoozeState && !currentAlarmState){
        tft.fillRoundRect(360, 70, 100, 25, 6, HX8357_BLACK);
        tft.setTextSize(2);                                               
        tft.setTextColor(textColor);
        tft.setCursor(380, 75);                                          
        tft.println("SNOOZE");
      }
      else if (alarmSet && currentAlarmState && !currentSnoozeState){
        tft.fillRoundRect(360, 70, 100, 25, 6, HX8357_BLACK);
        tft.setTextSize(2);                                               
        tft.setTextColor(textColor);
        tft.setCursor(390, 75);                                          
        tft.println("WAKE");
      } //else return;
  }

  void controlAlarmSetScreen(){
      TS_Point p = ts.getPoint();                                     //get touch
  
      vert = tft.height() - p.x;
      horz = p.y;
  
//      Serial.print("X = "); Serial.print(horz);  
//      Serial.print("\tY = "); Serial.print(vert);
//      Serial.print("\tPressure = "); Serial.println(p.z);  
//      Serial.print("Current Screen = "); Serial.println(currentScreen);
                                       
      p.x = map(p.x, TS_MINY, TS_MAXY, 0, tft.height());                // Scale using the calibration #'s and rotate coordinate system    
      p.y = map(p.y, TS_MINX, TS_MAXX, 0, tft.width());                 
  
      if (ts.touched()) {
        if(horz>2100 && horz<2370){                                      //hour up
          if(vert>-2900 && vert<-2590){
            delay(100);
            if (setAlarmHour < 23){
              setAlarmHour ++;
              origAlarmHour = setAlarmHour;
              tft.fillRoundRect(25, 50, 90, 30, 6, HX8357_RED);
              tft.setTextSize(2);
              tft.setCursor(50,57);
              tft.print(setAlarmHour);
              getLastTouch();
              delay(100);
            } else setAlarmHour = 0;
          }
        }
        if(horz>2500 && horz<2770){                                     //hour down
          if(vert>-2900 && vert<-2590){
            delay(100);
            if (setAlarmHour > 0){
              setAlarmHour --;
              origAlarmHour = setAlarmHour;
              tft.fillRoundRect(25, 50, 90, 30, 6, HX8357_RED);
              tft.setTextSize(2);
              tft.setCursor(50,57);
              tft.print(setAlarmHour);
              getLastTouch();
              delay(100);
            } else setAlarmHour = 23;
          }
        }
        if(horz>2100 && horz<2370){                                      //minute up
          if(vert>-2300 && vert<-1950){
            delay(100);
            if (setAlarmMinute < 59){
              setAlarmMinute ++;
              origAlarmMinute = setAlarmMinute;
              tft.fillRoundRect(25, 100, 90, 30, 6, HX8357_RED);
              tft.setTextSize(2);
              tft.setCursor(50,107);
              tft.print(setAlarmMinute);
              getLastTouch();
              delay(100);
            } else setAlarmMinute = 0;
          }
        }
        if(horz>2500 && horz<2770){                                     //minute down
          if(vert>-2300 && vert<-1950){
            delay(100);
            if (setAlarmMinute > 0){
              setAlarmMinute --;
              origAlarmMinute = setAlarmMinute;
              tft.fillRoundRect(25, 100, 90, 30, 6, HX8357_RED);
              tft.setTextSize(2);
              tft.setCursor(50,107);
              tft.print(setAlarmMinute);
              getLastTouch();
              delay(100);
            } else setAlarmMinute = 59;
          }
        }
        if(horz>2100 && horz<2370){                                      //schedule up
          if(vert>-1750 && vert<-1350){
            delay(100);
            if (setSchedIndex < 4 ){
              setSchedIndex ++;
              nowScheduled = alarmSched[setSchedIndex];
              tft.fillRoundRect(25, 150, 90, 30, 6, HX8357_RED);
              tft.setTextSize(2);
              tft.setCursor(40,157);
              tft.print(nowScheduled);
              getLastTouch();
              delay(100);
            } else setSchedIndex = 0;
          }
        }
        if(horz>2500 && horz<2770){                                     //schedule down
          if(vert>-1750 && vert<-1350){
            delay(100);
            if (setSchedIndex > 0){
              setSchedIndex --;
              nowScheduled = alarmSched[setSchedIndex];
              tft.fillRoundRect(25, 150, 90, 30, 6, HX8357_RED);
              tft.setTextSize(2);
              tft.setCursor(40,157);
              tft.print(nowScheduled);
              getLastTouch();
              delay(100);
            } else setSchedIndex = 4;
          }
        }
        if(horz>2500 && horz<2770){                                     //alarm on/off toggle
          if(vert>-550 && vert<-160){
            delay(100);
            if (!alarmSet){
              tft.fillRoundRect(200, 250, 60, 30, 6, HX8357_BLACK);
              tft.setTextSize(2);
              tft.setCursor(214,257);
              tft.print("ON");
              alarmSet = true;
              getLastTouch();
              delay(100);
            } else if(alarmSet) {
              tft.fillRoundRect(200, 250, 60, 30, 6, HX8357_BLACK);
              tft.setTextSize(2);
              tft.setCursor(210,257);
              tft.print("OFF");
              alarmSet = false;
              getLastTouch();
              delay(100);
            }
          }
        }
        if(horz>3450 && horz<3670){                                     //set Alarm
          if(vert>-1760 && vert<-1330){
            delay(100);
            printSetAlarmTime();
//            rtc.adjust(DateTime(setYear, setMonth, setDate, setHour, setMinute, 0));
            getLastTouch();
            delay(100);
          }
        }
        if(horz>3370 && horz<3650){                                     //exit to menu screen
          if(vert>-580 && vert<-110){
            delay(100);
            currentScreen = 1;
            getLastTouch();
            delay(100);
            startMainScreen();
          }
        } 
      }
  }  

  void startWakeScreen(){
    tft.fillScreen(HX8357_BLACK);                                    //fill screen with black (ersatz clear)
    tft.setCursor(0, 0); 
    tft.setTextColor(textColor);                                     //set text color white
    tft.setTextSize(2);                                              //set text size to 2
    tft.print("                 WAKE UP");                           //print header to screen
    
    tft.drawRoundRect(10, 20, 460, 290, 6, lineColor);               //draw screen outline
    tft.drawRoundRect(30, 170, 420, 50, 6, lineColor);               //draw snooze button
    tft.drawRoundRect(410, 250,50, 50, 6, lineColor);                //draw Off box
    tft.setCursor(428, 265);                                         //put cursor in Exit box
    tft.setTextSize(3);                                              //set text size 3
    tft.setTextColor(textColor);
    tft.println("X");
    tft.setCursor(210, 188);                                         
    tft.setTextSize(2);                                             
    tft.setTextColor(textColor);
    tft.println("SNOOZE");                                           //write "SNOOZE" in button
    printTime();
    printAlarmIndicator();
  }

  void controlWakeScreen(){                                         //logic for main screen control
    TS_Point p = ts.getPoint();                                     //get touch
    DateTime now = rtc.now();                                       //get current time and date

    if (millis() % 1000 == 0){
      checkCharge();
    }
    
    if (currentMinute != now.minute()){        
      printTime();
      printAlarmIndicator();
    }
    
//    if (setAlarmHour == now.hour() && setAlarmMinute == now.minute() && currentAlarmState){
//      currentAlarmState = true;
//
//    } else {
//        currentAlarmState = false; // THIS WILL TURN OFF THE ALARM AFTER ONE MINUTE
////        currentSnoozeState = false;
////          if (currentAlarmState != previousAlarmState) {
//          Si4703PowerOff();
//          currentScreen = 1;
//          startMainScreen();
////          }
//      }
    

    if (currentAlarmState && !currentSnoozeState) {   //THIS IS TRIGGERED WHEN THE ALARM TIME IS EQUAL TO THE ACTUAL TIME. THIS WILL CONTINUE TO RUN UNTIL 20s HAS ELAPSED.
//      currentMillis = millis();
//      if (currentMillis - previousMillis > alarmInterval){ //THIS CODE WILL RUN WHEN CURRENTMILLIS - PREVIOUSMILLIS IS GREATER THAN THE ALARMINTERVAL
//        previousMillis = currentMillis;  // GOING TO TRY SOMETHING COMMENT THESE LINES OUT
////         currentAlarmState = false;
//      } else {
//      // THE CODE BELOW WILL RUN WHILE CURRENT MILLIS IS LESS THAN THE ALARMINTERVAL
////          previousMillis = currentMillis;
//          Serial.print("Alarm Time: ");Serial.println((currentMillis - previousMillis)/1000, DEC);
////          previousAlarmState = currentAlarmState;
//        }
      if (currentAlarmState != previousAlarmState) {
        ampOn();
        radio.powerOn();                                                  //activate Si4703
        radio.setChannel(myStations[0]);                                  //set band and freq
        radio.setVolume(8);                                               //set volume
        volume = constrain(volume, 1, 15); 
        Serial.println("***SOUND ALARM***"); 
        DateTime now = rtc.now();
        previousAlarmState = currentAlarmState;
      }

    } else {
//        previousMillis = currentMillis;  // THIS WILL SET PREVIOUSMILLIS TO CURRENTMILLIS
        previousAlarmState = currentAlarmState;
      } 
      if (currentSnoozeState && !currentAlarmState){
////        currentAlarmState = false; 
        Serial.println ("***SNOOZING***");
//        currentSnoozeMillis = millis();
//        if (currentSnoozeMillis - previousSnoozeMillis > snoozeInterval){ //THIS CODE WILL RUN WHEN CURRENTMILLIS - PREVIOUSMILLIS IS GREATER THAN THE ALARMINTERVAL
//          previousSnoozeMillis = currentSnoozeMillis;  
////           currentAlarmState = true;
////           currentSnoozeState = false;
//        } else {
//          // THE CODE BELOW WILL RUN WHILE CURRENT SNOOZE MILLIS IS LESS THAN THE SNOOZEINTERVAL
//            Serial.print("Snooze Time: ");Serial.println((currentSnoozeMillis - previousSnoozeMillis)/1000, DEC);
//           
//        }
        if (currentSnoozeState != previousSnoozeState){
//          currentAlarmState = false;
//          DateTime now = rtc.now();
//          setAlarmHour = now.hour();
//          setAlarmMinute = now.minute();
//          setAlarmMinute = setAlarmMinute + 2;        
          Si4703PowerOff();
          ampOff();
          previousSnoozeState = currentSnoozeState;
        }
      } else {
//          previousSnoozeMillis = currentSnoozeMillis;
          previousSnoozeState = currentSnoozeState;
      }

    vert = tft.height() - p.x;
    horz = p.y;

//    Serial.print("X = "); Serial.print(horz);  
//    Serial.print("\tY = "); Serial.print(vert);
//    Serial.print("\tPressure = "); Serial.println(p.z);  
//    Serial.print("Current Screen = "); Serial.println(currentScreen);
                                     
    p.x = map(p.x, TS_MINY, TS_MAXY, 0, tft.height());                // Scale using the calibration #'s and rotate coordinate system    
    p.y = map(p.y, TS_MINX, TS_MAXX, 0, tft.width());                 
    
    if (ts.touched()) {
      if(horz>400 && horz<3600){                                      //select SNOOZE
        if(vert>-1480 && vert<-1000){
          delay(150);
          DateTime now = rtc.now();
          setAlarmHour = now.hour();
          setAlarmMinute = now.minute();
          setAlarmMinute = setAlarmMinute + snoozeTime;               //reset snooze time 
          currentAlarmState = false;
          previousAlarmState = currentAlarmState;
          currentSnoozeState = true;
          previousSnoozeState = currentSnoozeState;
          snoozeCount++;
          Serial.print("SNOOZE COUNT = ");Serial.println(snoozeCount);
          currentScreen = 1;                                          //send status to loop() to activate controlMain 
          getLastTouch();
          delay(100);
          startMainScreen();                                          //call startMainScreen function
        }
      }
      if(horz>3370 && horz<3650){                                     //exit to main screen
        if(vert>-580 && vert<-110){
          delay(100);
          DateTime now = rtc.now();
          currentAlarmState = false;
          previousAlarmState = currentAlarmState;
          currentSnoozeState = false;
          previousSnoozeState = currentSnoozeState;
          setAlarmHour = origAlarmHour;
          setAlarmMinute = origAlarmMinute;
          snoozeCount=0;
          Si4703PowerOff();
          currentScreen = 1;
          getLastTouch();
          delay(100);
          startMainScreen();
        }
      }
    }
  }
  
  void loop() {

// currentScreen = 3;
//  if (playMusicState == 1){
//  playMP3Tracks();
// } else controlMP3();
//  
 
    switch (currentScreen){
      case 1:
        controlMainScreen();
        break;
    
      case 2:
        controlStereo();
        break;
    
      case 3:
//        if (playMusicState == 1){ //testing whether or not calling playMP3Tracks from within contolMP3 is a problem
//          playMP3Tracks();
//        } else 
//          delay (100);
//          Serial.println(F("IN MAIN LOOP"));
          controlMP3();
        break;
  
      case 4:
        controlWebRadio();
        break;
  
      case 5:
        controlMenuScreen();
        break;
  
      case 6:
        controlClockSetScreen();
        break;
        
      case 7:
        controlAlarmSetScreen();
        break;

      case 8:
        controlWakeScreen();
        break;
  } 
}
  
  
  
