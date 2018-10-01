/*
 * Arduino Touchscreen Clock Radio with 
 * Si4703 FM Stereo, MP3, 3.5" TFT LCD (HX8357)
 * For Arduino Feather M0
 * 
 * 
 * NEED TO COMPLETE/IMPROVE (9/22/2018):
 *  - Finish mapping main screen after adding WEB RADIO - DONE 4/11/18
 *  - Display of time and date on main screen - 1 second delay causes blink, but allows better touch response - how to set update on dt.second=zero without delaying touch?
 *  - Touchscreen control seems to time out after several seconds upon sitting idle for several seconds.
 *  - array for tuning to stations from a list of favorites in addition to seek control - array figured out and seek function added 4/13/18 
 *  - figure out how to improve responsiveness of touch control - how to interrupt reading/update of RDS??  how to interrupt
 *    MP3 player during music play.  
 *  - TPA2016 performance - DONE 4/12/18: Improved by turning off AGC and setting the gain at -10
 *  - Char string vs. int for current screen control???  Why doesn't char work?
 *  - RDS display: need to force a space at the end of the string somehow, smooth out scrolling vs. RDSupdate delays
 *  - MP3 module - framework copied from stereo screen - need to finish modifying (stalled 9/22/18 until interrupt
 *    is figured out).
 *  - Web radio module - functioning on initial host/path request 9/15/18.  DONE 9/18/18
 *  - Menus in development (clock set, amp settings, station lists for FM and internet, SD information)
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

#define TOUCH_IRQ 19//A5                                            // A5 wired to IRQ on TFT to serve as external interrupt
volatile bool tftNotTouched = true;                                 // start out untouched for interrupt status
//////////////////////////////////////////  Si4703 DEFINITIONS  ///////////////////////////////////////////////////

int resetPin = 12; // (PGPIO 12 on Feather MO)
int SDIO = 20;
int SCLK = 21;    
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
int myStations[6] = {911, 985, 1011, 1035, 1051};                   //array storing multiple stations
int stationIndex = 0;                                               //establish station array indexing at 0

///////////////////////////////////////////  TPA2016 DEFINITIONS  ///////////////////////////////////////////

int ampPin = 13;                                                    //digital control of SHDN function (GPIO pin 13 on Feather M0)
int gain = 10;
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

int MP3volume = 70;

#define BUFFER_LEN 50
char* myAlbum_1_Track[14] = {"01.mp3", "02.mp3", "03.mp3","04.mp3","05.mp3","06.mp3","07.mp3","08.mp3","09.mp3","10.mp3","11.mp3","12.mp3","13.mp3"};
char* myAlbum_1_TrackNames[14] = {"Still Rolling Stones", "Rescue", "This Girl","Your Wings","You Say","Everything","Love Like This","Look Up Child","Losing My Religion","Remember","Rebel Heart","Inevitable","Turn Your Eyes Upon Jesus"};
char* myAlbum_1_Artist = "Lauren Daigle";
char* nowPlaying;
int myAlbum_1_Index = 0;
char printMP3Buffer[BUFFER_LEN];
char printArtistBuffer[BUFFER_LEN];
int MP3BufferLength;
int ArtistBufferLength;

////////////////////////////////////////////  WiFi DEFINITIONS ////////////////////////////////////////////

ZWiFiCentral wifi;
char* ssid     = wifi.ssid();
char* password = wifi.passcode();

const char* myWiFiHost[6] = {"ice.zradio.org","ice1.somafm.com","ice1.somafm.com","ice1.somafm.com","ice1.somafm.com"}; 
char* myWiFiHostName[6] = {"Z Radio","SomaFM","SomaFM","SomaFM","SomaFM"}; 
const char* myWiFiPath[6] = {"/z/high.mp3", "/seventies-128-mp3", "/u80s-128-mp3", "/bagel-128-mp3", "/thistle-128-mp3" }; 
char* myWiFiName[6] = {"Z88.3 Christian","Left Coast 70's", "Underground 80's", "Bagel Radio", "Thistle Celtic Sounds" }; 
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
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
char months[12][12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
DateTime now;
char timeBuffer[BUFFER_LEN];
int timeBufferLength;
int hr;
char dateBuffer[BUFFER_LEN];
int dateBufferLength;
char dateTimeBuffer[BUFFER_LEN];
int dateTimeBufferLength;

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
  pinMode(12,INPUT_PULLUP);                                         //initialize RESET pin for Radio(adapted for MO controller)
  pinMode(13,INPUT_PULLUP);                                         //initialize SHDN pin for Amp (adapted for MO controller)
  pinMode(CHARGEPIN,INPUT);                                         //initialize CHARGEPIN as input (read presence of USB/adapter)
  pinMode(TOUCH_IRQ, INPUT_PULLUP);                                 //initialize TFT IRQ as interrupt
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
  attachInterrupt(digitalPinToInterrupt(TOUCH_IRQ),tsControlInt,CHANGE);
  currentScreen = 1;
  startMainScreen();
}  

////////////////////////////////////////          ISR          /////////////////////////////////////////////
  
  void tsControlInt(){
    tftNotTouched = false;                                              //under development
  }

////////////////////////////////////////        FUNCTIONS       /////////////////////////////////////////////

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
  }

  void controlMainScreen(){                                         //logic for main screen control

    TS_Point p = ts.getPoint();                                     //get touch
    DateTime now = rtc.now();                                       //get current time and date
    if (!ts.touched()) {                                            //if no touch is sensed
      printTime();
    }

    vert = tft.height() - p.x;
    horz = p.y;

    Serial.print("X = "); Serial.print(horz);  
    Serial.print("\tY = "); Serial.print(vert);
    Serial.print("\tPressure = "); Serial.println(p.z);  
    Serial.print("Current Screen = "); Serial.println(currentScreen);
                                     
    p.x = map(p.x, TS_MINY, TS_MAXY, 0, tft.height());                // Scale using the calibration #'s and rotate coordinate system    
    p.y = map(p.y, TS_MINX, TS_MAXX, 0, tft.width());                 
  
  
  
    if (ts.touched()) {
      if(horz>400 && horz<1890){                                      //select FM Stereo
        if(vert>-1480 && vert<-1000){
          delay(50);
          currentScreen = 2;                                          //send status to loop() to activate controlStereo 
          getLastTouch();
          startStereoScreen();                                        //call startStereoScreen function
        }
      }
      
      if(horz>2150 && horz<3600){                                     //select MP3 Player
        if(vert>-1480 && vert<-1000){
          delay(50);
          Si4703PowerOff();
          currentScreen = 3;                                          //send status to loop() to activate controlMP3 
          getLastTouch();                                             
          startMP3Screen();                                           //call startMP3Scrren function
        }  
      }
      
      if(horz>2150 && horz<3600){                                     //select Menu
        if(vert>-550 && vert<-200){
          delay(50);
          currentScreen = 1;
          getLastTouch();
          startMainScreen();                                          //call startMenuScreen function
        }
      }
      
      if(horz>400 && horz<1890){                                      //reserved for Web Radio 
        if(vert>-550 && vert<-200){
          delay(50);
          currentScreen = 4;
          getLastTouch();
          startWebRadioScreen();                                      //call startWebRadioScreen function
        }
      }
    }
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
  }

  void ampOff(){ 
      digitalWrite(ampPin, LOW);                                    //using SHDN to power down the TPA2016 to avoid noise when not in use
  }
  
  void ampOn(){ 
      digitalWrite(ampPin, HIGH);                                   //Powering up from powered-down state
//      audioamp.begin();
      setAmp();
  }                                         
  
  void setAmp(){ 
  
  Serial.println("Setting AGC Compression");
//  audioamp.setAGCCompression(TPA2016_AGC_OFF);                      // AGC can be TPA2016_AGC_OFF (no AGC) or
  audioamp.setAGCCompression(TPA2016_AGC_2);                        // TPA2016_AGC_2 (1:2 compression)
  audioamp.setReleaseControl(0);                                    // turn off the release to really turn off AGC
//  audioamp.setAGCCompression(TPA2016_AGC_4);                        // TPA2016_AGC_2 (1:4 compression)
//  audioamp.setAGCCompression(TPA2016_AGC_8);                        // TPA2016_AGC_2 (1:8 compression)

  Serial.println("Setting Limit Level");
  audioamp.setLimitLevelOn();                                       // See Datasheet page 22 for value -> dBV conversion table
                                                                    // or turn off with setLimitLevelOff()
  audioamp.setLimitLevel(20);                                       // range from 0 (-6.5dBv) to 31 (9dBV)
  
  Serial.println("Setting AGC Attack");
  audioamp.setAttackControl(1);                                     // See Datasheet page 23 for value -> ms conversion table
  
  Serial.println("Setting AGC Hold");
  audioamp.setHoldControl(0);                                       // See Datasheet page 24 for value -> ms conversion table
  
  Serial.println("Setting AGC Release");
  audioamp.setReleaseControl(1);                                   // See Datasheet page 24 for value -> ms conversion table
  }

  void startStereoScreen(){
  ampOn();                                                          //activate Amp and run setAmp function
  audioamp.setGain(gain);
  delay(1000);
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
  radio.readRDS(rdsBufferA, 5000);                                  //original setting 15 seconds - reduced to 5 for development 
  
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
//  clearTSBuffer();
}

  void controlStereo(){                                             //logic for radio control screen

  TS_Point p = ts.getPoint();                                       //get touch
  DateTime now = rtc.now();                                         //get current time and date
  if (!ts.touched()) {
    printTime();                                             
    radio.readRDS(rdsBufferA, 1000);                                //re-read RDS data and write to rdsBuffer
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
        delay (10);                                                 //speed may need adjustment based on variations in signal reception
      }
    }
  }
  
  vert = tft.height() - p.x;
  horz = p.y;

  Serial.print("X = "); Serial.print(horz);  
  Serial.print("\tY = "); Serial.print(vert);
  Serial.print("\tPressure = "); Serial.println(p.z);  
  Serial.print("Current Screen = "); Serial.println(currentScreen);
  
  // Scale using the calibration #'s
  // and rotate coordinate system
  p.x = map(p.x, TS_MINY, TS_MAXY, 0, tft.height());
  p.y = map(p.y, TS_MINX, TS_MAXX, 0, tft.width());

  if (ts.touched()) {                                                 //if touch is detected
      if(horz>350 && horz<650){                                     //station up
        if(vert>-3200 && vert<-2650){
            delay(50);
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
            } else stationIndex=0;
        }
      }
      
      if(horz>750 && horz<1100){                                     //preset station down
        if(vert>-3200 && vert<-2650){
          delay(50);
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
            //lastChannel = channel;
          } else stationIndex=5;    
        }
      }

      if(horz>2900 && horz<3200){                                    //station seek up
        if(vert>-3200 && vert<-2650){
          delay(50);
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
            //lastChannel = channel;
          } //else lastChannel = channel; 
        }
      }

      if(horz>3350 && horz<3650){                                     //station seek down
        if(vert>-3200 && vert<-2650){
          delay(50);
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
            //lastChannel = channel;
          } //else lastChannel = channel; 
        }
      }

      if(horz>2900 && horz<3200){                                     //volume up
        if(vert>-2400 && vert<-2000){
          delay(50);
          if (volume < 15){
            volume++;
            tft.fillRoundRect(140, 102, 60, 25, 6, HX8357_BLACK);
            radio.setVolume(volume);
            delay(250);
            tft.setCursor(40, 108);
            tft.setTextColor(textColor);
            tft.setTextSize(2);
            tft.print("Volume:    ");
            tft.setTextColor(textColor);
            tft.print(volume);                                      //write volume
            getLastTouch();
          }
        }
      }
      if(horz>3350 && horz<3650){                                     //volume down
        if(vert>-2400 && vert<-2000){
          delay(50);
          if (volume > 0){
            volume--;
            tft.fillRoundRect(140, 102, 60, 25, 6, HX8357_BLACK);
            radio.setVolume(volume);
            delay(250);
            tft.setCursor(40, 108);
            tft.setTextColor(textColor);
            tft.setTextSize(2);
            tft.print("Volume:    ");
            tft.setTextColor(textColor);
            tft.print(volume);                                      //write volume
            getLastTouch();
          }
        }
      }
      
      if(horz>2430 && horz<2730){                                     //mute
        if(vert>-2400 && vert<-2000){
          delay(50);
          volume=0;
          tft.fillRoundRect(140, 102, 60, 25, 6, HX8357_BLACK);
          radio.setVolume(volume);
          getLastTouch();
          }
      }
    
      if(horz>3370 && horz<3650){                                     //exit to main screen
        if(vert>-580 && vert<-110){
//          radio.getChannel();
          delay(50);
          Si4703PowerOff();
          delay(50);
          ampOff();
          delay(50);
          currentScreen = 1;
          getLastTouch();
          startMainScreen();
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
  //     while (1);
    }
  
    Serial.println(F("VS1053 found"));
  //  musicPlayer.setVolume(MP3volume, MP3volume);
  //  musicPlayer.sineTest(0x44, 100);    // Make a tone to indicate VS1053 is working
      
    if (!SD.begin(CARDCS)) {
      Serial.println(F("SD failed, or not present"));
      delay(2000);
      Serial.println(F("Starting Main Screen"));
      currentScreen = 1;
      startMainScreen();
  //    while (1);  // don't do anything more
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
  }

  void printMP3Tracks(){
    Serial.print(F("Now playing: "));Serial.println(myAlbum_1_TrackNames[myAlbum_1_Index]);
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
    
    musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);           // DREQ int
    printMP3Tracks();
    if (! musicPlayer.startPlayingFile(nowPlaying)) {
          Serial.println("Could not open file: ");Serial.print(nowPlaying);
          while (1);
    }
    while (musicPlayer.playingMusic) {
        // file is now playing in the 'background' so now's a good time
        // to do something else like handling LEDs or buttons :)
      
      Serial.print(".");
//      printTime();
      delay(1000);
//       break;
        }
    currentScreen = 3;
    Serial.print("Current Screen is: ");Serial.println(currentScreen);    
    return;
//    controlMP3();
  }
  
    

  void controlMP3(){                                                //control MP3 screen
    while (musicPlayer.playingMusic || musicPlayer.stopped() ) {
      
      TS_Point p = ts.getPoint();                                   
      DateTime now = rtc.now();                                       //get current time and date
      if (!ts.touched()) {                       
        printTime();
      }

      vert = tft.height() - p.x;
      horz = p.y;
    
      Serial.print("X = "); Serial.print(horz);  
      Serial.print("\tY = "); Serial.print(vert);
      Serial.print("\tPressure = "); Serial.println(p.z);  
      Serial.print("Current Screen = "); Serial.println(currentScreen);
  
      
      p.x = map(p.x, TS_MINY, TS_MAXY, 0, tft.height());              // Scale using the calibration #'s and rotate coordinate system
      p.y = map(p.y, TS_MINX, TS_MAXX, 0, tft.width());
  
      if (ts.touched()) {                                             //if touch is detected
        if(horz>1720 && horz<2280){                                   //play track
          if(vert>-2000 && vert<-1580){
            delay(50);
            musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);           // DREQ int
//            for (myAlbum_1_Index = 1; myAlbum_1_Index < 13; myAlbum_1_Index++){ 
              nowPlaying = myAlbum_1_Track[myAlbum_1_Index];
              getLastTouch();
              playMP3Tracks();
//            }            
          }
        }
        if(horz>2480 && horz<2750){                                   //pause track
          if(vert>-2000 && vert<-1580){
            delay(50);
            if (! musicPlayer.paused()) {
              Serial.println("Paused");
              musicPlayer.pausePlaying(true);
            } else { 
              Serial.println("Resumed");
              musicPlayer.pausePlaying(false);
            }
            getLastTouch();
//            clearTSBuffer();  
              
          }
        }
        if(horz>1280 && horz<1580){                                   //stop track
          if(vert>-2000 && vert<-1580){
            delay(50);
            musicPlayer.stopPlaying();
            getLastTouch();
          }
        }
        if(horz>780 && horz<1100){                                   //right blank button
          if(vert>-2000 && vert<-1580){
            delay(50);
            musicPlayer.stopPlaying();
            getLastTouch();
          }
        }
        if(horz>320 && horz<620){                                    //left blank button
          if(vert>-2000 && vert<-1580){
            delay(50);
            musicPlayer.stopPlaying();
            getLastTouch();
          }
        }
        if(horz>2900 && horz<3200){                                   //next track
          if(vert>-2000 && vert<-1580){
            delay(50);
            musicPlayer.stopPlaying();
            if(myAlbum_1_Index<12){
              myAlbum_1_Index ++;
              nowPlaying = myAlbum_1_Track[myAlbum_1_Index];
              printMP3Tracks();
              getLastTouch();
            } else myAlbum_1_Index = 0;
          }
        }
        if(horz>3350 && horz<3650){                                   //previous track
          if(vert>-2000 && vert<-1580){
            delay(50);
            if (myAlbum_1_Index>0){
              myAlbum_1_Index --;
              nowPlaying = myAlbum_1_Track[myAlbum_1_Index];
              printMP3Tracks();
              getLastTouch();
            } else myAlbum_1_Index = 13; 
          }
        }
        if(horz>3350 && horz<3650){                                   //volume down
          if(vert>-1340 && vert<-840){
            delay(50);
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
              delay(100);
              getLastTouch();
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
            }
          }
        }
        if(horz>2900 && horz<3200){                                     //volume up
          if(vert>-1340 && vert<-840){
            delay(50);
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
              delay(100);
              getLastTouch();
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
              }
           }
        }
        if(horz>2430 && horz<2730){                                     //mute
          if(vert>-1340 && vert<-840){
            delay(50);
            MP3volume=150;
            tft.fillRoundRect(160, 202, 60, 25, 6, HX8357_BLACK);
            musicPlayer.setVolume(MP3volume,MP3volume);
            tft.setCursor(40, 208);
            tft.setTextColor(textColor);
            tft.setTextSize(2);
            tft.print("Volume:    ");
            tft.setTextColor(textColor);
            tft.print("MUTED");                 //write volume
            getLastTouch();
          }
        }
        if(horz>3370 && horz<3650){                                     //exit to main screen
          if(vert>-580 && vert<-110){
            delay(50);
            musicPlayer.stopPlaying();
            ampOff();
            currentScreen = 1;
            getLastTouch();
            startMainScreen();
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
  
  //  while (!Serial) { delay(1); }
  
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
  
  //  client.print(String("GET ") + path + " HTTP/1.1\r\n" +
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
//      printTime();
    }
  }


  void controlWebRadio(){                       
//while (musicPlayer.playingMusic || musicPlayer.stopped() ) {
  TS_Point p = ts.getPoint();                                         //get touch
  DateTime now = rtc.now();                                           //get current time and date
  
  playStream();    
  
//  if (!ts.touched()) {                       
//    printTime();
//  }


  
  vert = tft.height() - p.x;
  horz = p.y;

  Serial.print("X = "); Serial.print(horz);  
  Serial.print("\tY = "); Serial.print(vert);
  Serial.print("\tPressure = "); Serial.println(p.z);  
  Serial.print("Current Screen = "); Serial.println(currentScreen);
  
  // Scale using the calibration #'s
  // and rotate coordinate system
  p.x = map(p.x, TS_MINY, TS_MAXY, 0, tft.height());
  p.y = map(p.y, TS_MINX, TS_MAXX, 0, tft.width());

    if (ts.touched()) {                                                 //if touch is detected
      if(horz>2900 && horz<3200){                                       //station up
        if(vert>-1800 && vert<-1400){
          delay(50);
          if(WiFiPathIndex<4){ 
            WiFiPathIndex++;
            tft.fillRoundRect(40, 62, 400, 25,6, HX8357_BLACK);
            tft.fillRoundRect(40, 142, 280, 25,6, HX8357_BLACK);
            initStream();        
            musicPlayer.setVolume(MP3volume, MP3volume);
            playStream();    
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
        } 
      }
       
  
    if(horz>3350 && horz<3650){           //station down
      if(vert>-1800 && vert<-1400){
        delay(50);
        if (WiFiPathIndex > 0){
          WiFiPathIndex--;
          tft.fillRoundRect(40, 62, 400, 25,6, HX8357_BLACK);
          tft.fillRoundRect(40, 142, 280, 25,6, HX8357_BLACK);
          initStream();        
          musicPlayer.setVolume(MP3volume, MP3volume);
          getLastTouch();
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
              playStream();         
        }
      }
      if(horz>3350 && horz<3650){                                     //volume down
        if(vert>-1340 && vert<-840){
          delay(50);
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
            }
        }
      }
      if(horz>2900 && horz<3200){                                     //volume up
        if(vert>-1340 && vert<-840){
          delay(50);
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
            }
        }
      }
      if(horz>2430 && horz<2730){                                     //mute
        if(vert>-1340 && vert<-840){
          delay(50);
          MP3volume=150;
          tft.fillRoundRect(160, 202, 60, 25, 6, HX8357_BLACK);
          musicPlayer.setVolume(MP3volume,MP3volume);
          tft.setCursor(40, 208);
          tft.setTextColor(textColor);
          tft.setTextSize(2);
          tft.print("Volume:    ");
          tft.setTextColor(textColor);
          tft.print("MUTED");                 //write volume
          getLastTouch();
        }
      }
      if(horz>3370 && horz<3650){                                     //exit to main screen
        if(vert>-580 && vert<-110){
          delay(500);
          musicPlayer.stopPlaying();
          ampOff();
          currentScreen = 1;
          getLastTouch();
          startMainScreen();
        }
      }
    }
//  }
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
  }

  void drawChargeIndicator(){
      tft.drawRect(430,3,32,12,lineColor);                           //draw battery body
      tft.fillRect(462,6,3,5,lineColor);                             //draw battery "+" terminal
      tft.fillRect(432,5,28,8,HX8357_BLACK);                       
      tft.drawLine(437,10,448,7,textColor);                       //draw charge indicator
      tft.drawLine(448,7,448,9,textColor);                        // |       |         | 
      tft.drawLine(448,9,455,8,textColor);                        // |       |         |
      tft.drawLine(455,8,446,11,textColor);                       // |       |         |
      tft.drawLine(446,11,446,9,textColor);                       // |       |         |
      tft.drawLine(446,9,437,10,textColor);                       // |       |         |
      tft.drawLine(447,8,447,10,textColor);                       //fill charge indicator
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
    while (!ts.bufferEmpty()){
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
        tft.fillRoundRect(36, 45, 410, 65, 6, HX8357_BLACK);          //fill black rectangle to erase previous reading
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
        delay(1000);
        tft.setCursor(50, 140);                                    
        tft.setTextSize(2);                                         
        tft.fillRoundRect(36, 135, 85, 25, 6, HX8357_BLACK);
        checkCharge();
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
          delay(1000);
          checkCharge();
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
          delay(1000);
          checkCharge();
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
          delay(1000);
          checkCharge();
        } while (now.second() == 0);
        break;
    }
  }

void loop(void) {
 
    switch (currentScreen){
      case 1:
        controlMainScreen();
        break;
    
      case 2:
        controlStereo();
        break;
    
      case 3:
        controlMP3();
        break;
  
      case 4:
        controlWebRadio();
        break;
  
      case 5:
        controlMainScreen();
        break;
  } 
}
  
  
  
