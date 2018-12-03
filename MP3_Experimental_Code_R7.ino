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
unsigned long timeNow = 0;
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

int IRQThreshold = 700;                                             //The sense threshold for updating tftNotTouched
int IRQValue;

unsigned long currentTouchMillis, previousTouchMillis;
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

int MP3volume = 84;

#define BUFFER_LEN 50
static const char* myAlbum_1_Track[14] = {"01.mp3", "02.mp3", "03.mp3","04.mp3","05.mp3","06.mp3","07.mp3","08.mp3","09.mp3","10.mp3","11.mp3","12.mp3","13.mp3"};
static const char* myAlbum_1_TrackNames[14] = {"Still Rolling Stones", "Rescue", "This Girl","Your Wings","You Say","Everything","Love Like This","Look Up Child","Losing My Religion","Remember","Rebel Heart","Inevitable","Turn Your Eyes Upon Jesus"};
static const char* myAlbum_1_Artist = "Lauren Daigle";
const char* nowPlaying;
int myAlbum_1_Index = 0;
char printMP3Buffer[BUFFER_LEN];
char printArtistBuffer[BUFFER_LEN];
int MP3BufferLength;
int ArtistBufferLength;
//int playMusicState = 0;
boolean muted = false;

enum state {
  PLAYING,
  PAUSED,
  STOPPED
} playMusicState = STOPPED;
boolean AUTO_PLAY_NEXT = false;                                       //auto play next track when true
boolean SHUFFLE_PLAY = false;                                         //shuffle tracks when true
#define TRACKS_MAX      100                                           // Maximum number of files (tracks) to load

int currentTrack, totalTracks;
char trackListing[TRACKS_MAX][13] = {' '};
////////////////////////////////////////////  DS3231 DEFINITIONS ////////////////////////////////////////////

RTC_DS3231 rtc;
static const char* daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char* months[12][12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
DateTime now;
char timeBuffer[BUFFER_LEN];
int timeBufferLength;
int hr;
char dateBuffer[BUFFER_LEN];
int dateBufferLength;
char dateTimeBuffer[BUFFER_LEN];
int dateTimeBufferLength;

int currentMinute, currentSecond;

////////////////////////////////////////////    BUTTONS:IMAGES   ////////////////////////////////////////////

// SHUFFLE SYMBOL (width x height = 40,40)
static const uint8_t imageShuffle[] PROGMEM = {
  B00000000, B00000000, B00000000, B00000000, B00000000, 
  B00000000, B00000000, B00000000, B00000000, B00000000, 
  B00000000, B00000000, B00000000, B00000000, B00000000, 
  B00000000, B00000000, B00000000, B00000000, B00000000, 
  B00000000, B00000000, B00000000, B00000000, B00000000, 
  B00000000, B00000000, B00000000, B00000001, B10000000, 
  B00000000, B00000000, B00000000, B00000000, B11000000, 
  B00000000, B00000000, B00000000, B00000000, B11100000, 
  B00000000, B00000000, B00000000, B00000000, B01110000, 
  B01111111, B11100000, B00000011, B11111111, B11111000, 
  B01111111, B11111000, B00001111, B11111111, B11111100, 
  B01111111, B11111100, B00011111, B11111111, B11111100, 
  B01111111, B11111110, B00111111, B11111111, B11111000, 
  B00000000, B00011110, B01111100, B00000000, B01110000, 
  B00000000, B00001100, B01111000, B00000000, B01100000, 
  B00000000, B00000100, B11110000, B00000000, B11000000, 
  B00000000, B00000000, B11100000, B00000001, B10000000, 
  B00000000, B00000000, B11100000, B00000000, B00000000, 
  B00000000, B00000001, B11000000, B00000000, B00000000, 
  B00000000, B00000001, B11000000, B00000000, B00000000, 
  B00000000, B00000001, B11000000, B00000000, B00000000, 
  B00000000, B00000011, B10000000, B00000000, B00000000, 
  B00000000, B00000011, B10000000, B00000000, B00000000, 
  B00000000, B00000111, B00000000, B00000001, B11000000, 
  B00000000, B00001111, B00011000, B00000000, B11100000, 
  B00000000, B00011111, B00111000, B00000000, B01110000, 
  B00000000, B00111110, B00111110, B00000000, B01111000, 
  B01111111, B11111100, B00011111, B11111111, B11111100, 
  B01111111, B11111000, B00001111, B11111111, B11111110, 
  B01111111, B11110000, B00000111, B11111111, B11111100, 
  B00000000, B00000000, B00000000, B00000000, B01111000, 
  B00000000, B00000000, B00000000, B00000000, B01110000, 
  B00000000, B00000000, B00000000, B00000000, B11000000, 
  B00000000, B00000000, B00000000, B00000001, B11000000, 
  B00000000, B00000000, B00000000, B00000000, B00000000, 
  B00000000, B00000000, B00000000, B00000000, B00000000, 
  B00000000, B00000000, B00000000, B00000000, B00000000, 
  B00000000, B00000000, B00000000, B00000000, B00000000, 
  B00000000, B00000000, B00000000, B00000000, B00000000, 
  B00000000, B00000000, B00000000, B00000000, B00000000, 
  
};

// CONTINUOUS PLAY SYMBOL (width x height = 40,40)
static const uint8_t imageContPlay[] PROGMEM = {
  B00000000, B00000000, B00000000, B00000000, B00000000, 
  B00000000, B00000000, B00000000, B00000000, B00000000, 
  B00000000, B00000000, B00000000, B00000000, B00000000, 
  B00000000, B00000000, B00000001, B10000000, B00000000, 
  B00000000, B00000000, B00000000, B11000000, B00000000, 
  B00000000, B00000000, B00000000, B11100000, B00000000, 
  B00000000, B00000000, B00000000, B01110000, B00000000, 
  B00000000, B00000000, B00000000, B01111000, B00000000, 
  B00000000, B00001111, B11111111, B11111110, B00000000, 
  B00000000, B00111111, B11111111, B11111110, B00000000, 
  B00000000, B11111111, B11111111, B11111100, B00000000, 
  B00000011, B11111110, B00000000, B11111000, B00000000, 
  B00000111, B11110000, B00000000, B01110000, B00000000, 
  B00001111, B11000000, B00000000, B11100000, B00000000, 
  B00011111, B00000000, B00000000, B11000000, B00001000, 
  B00111110, B00000000, B00000011, B10000000, B00011100, 
  B00111100, B00000000, B00000000, B00000000, B00111110, 
  B01111000, B00000000, B00000000, B00000000, B00011110, 
  B01111000, B00000000, B00000000, B00000000, B00001110, 
  B01110000, B00000000, B00000000, B00000000, B00001110, 
  B01110000, B00000000, B00000000, B00000000, B00001110, 
  B01111000, B00000000, B00000000, B00000000, B00011110, 
  B01111000, B00000000, B00000000, B00000000, B00011110, 
  B00111000, B00000001, B10000000, B00000000, B00111100, 
  B00100000, B00000111, B00000000, B00000000, B01111100, 
  B00000000, B00001110, B00000000, B00000001, B11111000, 
  B00000000, B00011110, B00000000, B00000011, B11110000, 
  B00000000, B00111100, B00000000, B00011111, B11100000, 
  B00000000, B01111111, B10000001, B11111111, B10000000, 
  B00000000, B11111111, B11111111, B11111111, B00000000, 
  B00000000, B11111111, B11111111, B11111100, B00000000, 
  B00000000, B01111111, B11111111, B11000000, B00000000, 
  B00000000, B00111100, B00000000, B00000000, B00000000, 
  B00000000, B00001100, B00000000, B00000000, B00000000, 
  B00000000, B00000111, B00000000, B00000000, B00000000, 
  B00000000, B00000011, B00000000, B00000000, B00000000, 
  B00000000, B00000001, B10000000, B00000000, B00000000, 
  B00000000, B00000000, B00000000, B00000000, B00000000, 
  B00000000, B00000000, B00000000, B00000000, B00000000, 
  B00000000, B00000000, B00000000, B00000000, B00000000, 
  
};
////////////////////////////////////////////    M0 SERIAL CONV   ////////////////////////////////////////////

#if defined(ARDUINO_SAMD_ZERO) && defined(SERIAL_PORT_USBVIRTUAL)
// Required for Serial on Zero based boards
#define Serial SERIAL_PORT_USBVIRTUAL
#endif

////////////////////////////////////////////         SETUP       ////////////////////////////////////////////

void setup(void) {
  Serial.begin(115200);                                             //serial comms for debug
  tft.begin(HX8357D);                                               //start screen using chip identifier hex
  ts.begin();                                                       //start touchscreen
  audioamp.begin();
  rtc.begin();                                                      //start clock
  tft.setRotation(1);                                               //set rotation for wide screen
  pinMode(12,OUTPUT);                                         //initialize RESET pin for Radio(adapted for MO controller)
  pinMode(13,OUTPUT);                                         //initialize SHDN pin for Amp (adapted for MO controller)
  pinMode(CHARGEPIN,INPUT);                                         //initialize CHARGEPIN as input (read presence of USB/adapter)
  pinMode(TOUCH_IRQ, INPUT_PULLDOWN);
  delay(1000);                                                      // wait for console opening
//  regDump();

  currentScreen = 3;
  startMP3Screen();
}  

////////////////////////////////////////          ISR          /////////////////////////////////////////////
  
void tsControlInt(){
    noInterrupts();
    tftNotTouched = digitalRead(TOUCH_IRQ);
//    Serial.println(tftNotTouched);
//    delayMicroseconds(2200);
    if (musicPlayer.playingMusic){
      tftNotTouched = digitalRead(TOUCH_IRQ);
      if (tftNotTouched == 1) {
        digitalWrite(VS1053_DCS, LOW);
        digitalWrite(CARDCS, LOW);
//        Serial.println(tftNotTouched);
      }

      digitalWrite(VS1053_DCS, HIGH);
      digitalWrite(CARDCS, HIGH);
    //  tftNotTouched = 0;
//      tftNotTouched = digitalRead(TOUCH_IRQ);
//      Serial.println(tftNotTouched);
    }
    interrupts();
  }
  
////////////////////////////////////////        FUNCTIONS       /////////////////////////////////////////////
  void startMP3Screen(){                                              
    ampOn();
    tft.fillScreen(HX8357_BLACK);                                    //fill screen with black
    tft.setCursor(0, 0); 
    tft.setTextColor(textColor);                                     //set text color white
    tft.setTextSize(2);                                              //set text size to 2 (1-6)
    tft.println("           Arduino MP3 Player");                    //print header to screen
  
    tft.drawRoundRect(10, 20, 460, 290, 6, lineColor);               //draw screen outline
    tft.drawRoundRect(20, 130, 50, 50, 6, lineColor);                //draw autoPlay button
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
    tft.fillTriangle(220, 140, 220, 170, 260, 155,textColor);        //draw play button triangle 
    tft.fillRect(300,140, 10, 30, textColor);                        //draw left bar pause
    tft.fillRect(320,140, 10, 30, textColor);                        //draw right bar pause
    tft.fillRoundRect(150,140, 30, 30, 4, textColor);                //draw stop button square
    tft.drawTriangle(375, 144, 362, 165, 388, 165,textColor);        //draw up triangle for station
    tft.drawTriangle(435, 165, 422, 144, 448, 144,textColor);        //draw down triangle for station
    tft.drawTriangle(375, 204, 362, 225, 388, 225,textColor);        //draw up triangle for volume
    tft.drawTriangle(435, 225, 422, 204, 448, 204,textColor);        //draw down triangle for volume
//    tft.fillCircle(45,155,13,textColor);                             //draw continuous play symbol 
//    tft.fillCircle(45,155,10,HX8357_BLACK);                          //  ||
//    tft.fillTriangle(51,155,63,155,54,172,HX8357_BLACK);             //  ||
//    tft.fillTriangle(51,155,63,155,57,164,textColor);                //draw continuous play symbol 
    tft.drawBitmap(25,135, imageContPlay, 40, 40, textColor);        //draw continuous play symbol 
    tft.drawBitmap(85, 135, imageShuffle, 40, 40, textColor);        //draw shuffle symbol
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
       currentScreen = 3;
       startMP3Screen(); 
  //     while (1);
    }
  
    Serial.println(F("VS1053 found"));
//    musicPlayer.softReset();
    musicPlayer.setVolume(MP3volume, MP3volume);
  //  musicPlayer.sineTest(0x44, 100);    // Make a tone to indicate VS1053 is working
      
    if (!SD.begin(CARDCS)) {
      Serial.println(F("SD failed, or not present"));
      delay(2000);
      Serial.println(F("Starting Main Screen"));
      currentScreen = 3;
      startMP3Screen();
  //    while (1);  // don't do anything more
    }
    Serial.println("SD OK!");

    // Load list of tracks
    Serial.println("Track Listing");
    Serial.println("=============");  
    totalTracks = 0;
    loadTracks(SD.open("/"), 0);
    currentTrack = 0;

    if (! musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT)){
      Serial.println(F("DREQ pin is not an interrupt pin"));
    }
    
//    musicPlayer.setVolume(MP3volume, MP3volume);
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
    nowPlaying = myAlbum_1_Track[myAlbum_1_Index];
    printMP3Tracks();

    attachInterrupt(digitalPinToInterrupt(TOUCH_IRQ),tsControlInt,RISING);
  }

void loadTracks(File dir, int level) {
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) return;
    if (entry.isDirectory()) {
      // Recursive call to scan next dir level
      loadTracks(entry, level + 1);
    } else {
      // Only add files in root dir
      if (level == 0) {
        // And only if they have good names
        if (nameCheck(entry.name())) {
          strncpy(trackListing[totalTracks], entry.name(), 12);
          Serial.print(totalTracks); Serial.print("=");
          Serial.println(trackListing[totalTracks]);
          totalTracks++;
        }
      }
    }
    entry.close();
    // Stop scanning if we hit max
    if (totalTracks >= TRACKS_MAX) return;
  } 
}
bool nameCheck(char* name) {
  int len = strlen(name);
  // Check length
  if (len <= 4) return false;
  // Check extension
  char* ext = strrchr(name,'.');
  if (!(
    strcmp(ext,".MP3") == 0  ||
    strcmp(ext,".OGG") == 0
    )) return false;
  // Check first character
  switch(name[0]) {
    case '_': return false;
  }
  return true;
}


  void printMP3Tracks(){
    Serial.print(F("Now queued: "));Serial.println(myAlbum_1_TrackNames[myAlbum_1_Index]);
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
    musicPlayer.feedBuffer();
    if (! musicPlayer.startPlayingFile(nowPlaying)) {
      Serial.println("Could not open file: ");Serial.print(nowPlaying);
      currentScreen = 3;
      startMP3Screen();  
//          while (1);
    }
    Serial.print("Now Playing: ");Serial.println(myAlbum_1_TrackNames[myAlbum_1_Index]);
    while (musicPlayer.playingMusic){
      if (millis() % 1000 == 0){
        if (currentMinute != now.minute()){        
          noInterrupts();
          printTime();
          interrupts();
        }
      }
      if (millis() % 500 == 0){
        Serial.print(F("In while loop ")); 
        Serial.print("tftNotTouched: ");Serial.println(tftNotTouched);
        tftNotTouched = digitalRead(TOUCH_IRQ);
        if(tftNotTouched == 1){
          controlMP3();
        }
      }
    }
  }
  


  void controlMP3(){                                                //control MP3 screen
    tftNotTouched = digitalRead(TOUCH_IRQ);
//    tftNotTouched = 0;
    if (musicPlayer.playingMusic || musicPlayer.stopped() || musicPlayer.paused()) {

      TS_Point p = ts.getPoint();
      DateTime now = rtc.now();                                       //get current time and date
      
    if (millis() % 1000 == 0){
      checkCharge();
      Serial.print("tftNotTouched: ");Serial.println(tftNotTouched);
    }

    if (currentMinute != now.minute()){        
      printTime();
    }

    vert = tft.height() - p.x;
    horz = p.y;
     
    p.x = map(p.x, TS_MINY, TS_MAXY, 0, tft.height());              // Scale using the calibration #'s and rotate coordinate system
    p.y = map(p.y, TS_MINX, TS_MAXX, 0, tft.width());

    if (AUTO_PLAY_NEXT) {
      if (SHUFFLE_PLAY){
        if (playMusicState == PLAYING && musicPlayer.stopped()) {
          myAlbum_1_Index = random (0,13);
          nowPlaying = myAlbum_1_Track[myAlbum_1_Index];
          printMP3Tracks();
          playMusicState = PLAYING;
          playMP3Tracks();
        }
      } else
        if (playMusicState == PLAYING && musicPlayer.stopped()) {
          if(myAlbum_1_Index<12){
            myAlbum_1_Index ++;
            nowPlaying = myAlbum_1_Track[myAlbum_1_Index];
            printMP3Tracks();
            playMusicState = PLAYING;
            playMP3Tracks();            
          } else {
              myAlbum_1_Index = -1;
              playMusicState = PLAYING;
            }
        }
    }
      
    if(tftNotTouched == 0 || (musicPlayer.playingMusic && tftNotTouched == 1)){ 
      if (ts.touched()){                                              //if touch is detected
        if(horz>1720 && horz<2280){                                   //play track
          if(vert>-2000 && vert<-1580){
            delay(50);
            Serial.println("play track touched");
            delay(50);
            nowPlaying = myAlbum_1_Track[myAlbum_1_Index];
            playMusicState = PLAYING;  
            getLastTouch();
            delay(50);
            musicPlayer.feedBuffer();
            playMP3Tracks();
            Serial.println("back from function");
          }
        }
        if(horz>2480 && horz<2750){                                   //pause track
          if(vert>-2000 && vert<-1580){
            delay(50);
            if (!musicPlayer.stopped()) {
              if (musicPlayer.paused()) {
                delay(50);
                Serial.println("Resumed");
                musicPlayer.pausePlaying(false);
                playMusicState  = PLAYING;
              } else { 
                delay(50);
                Serial.println("Paused");
                musicPlayer.pausePlaying(true);
                playMusicState = PAUSED;
              }    
            }
            getLastTouch();
            delay(50);
          }
        }
        if(horz>1280 && horz<1580){                                   //stop track
          if(vert>-2000 && vert<-1580){
            delay(50);
            Serial.println("STOPPING");
            musicPlayer.stopPlaying();
            playMusicState  = STOPPED;
            getLastTouch();
            delay(50);
          }
        }
        if(horz>780 && horz<1100){                                   //shuffle play button
          if(vert>-2000 && vert<-1580){
            delay(10);
            musicPlayer.pausePlaying(true);
            if (SHUFFLE_PLAY == true){
              delay(50);
              SHUFFLE_PLAY = false;
              Serial.println("Shuffle Play OFF");
              tft.drawBitmap(85, 135, imageShuffle, 40, 40, textColor);
            } else { 
              delay(50);
              SHUFFLE_PLAY = true;
              Serial.println("Shuffle Play ON");
              tft.drawBitmap(85, 135, imageShuffle, 40, 40, HX8357_GREEN);
              }
            musicPlayer.pausePlaying(false);
            getLastTouch();
            delay(50);
          }
        }
        if(horz>320 && horz<620){                                    //continuous play button
          if(vert>-2000 && vert<-1580){
            delay(10);
            musicPlayer.pausePlaying(true);
            if (AUTO_PLAY_NEXT == true) {
                delay(50);
                AUTO_PLAY_NEXT = false;
                Serial.println("Continuous Play OFF");
                tft.drawBitmap(25,135, imageContPlay, 40, 40, textColor);
              } else { 
                delay(50);
                AUTO_PLAY_NEXT = true;
                Serial.println("Continuous Play ON");
                tft.drawBitmap(25,135, imageContPlay, 40, 40, HX8357_GREEN);
                }
            musicPlayer.pausePlaying(false);
            getLastTouch();
            delay(50);
          }
        }
        if(horz>2900 && horz<3200){                                   //next track
          if(vert>-2000 && vert<-1580){
            delay(50);
            Serial.println("STOPPING");
            musicPlayer.stopPlaying();
            playMusicState  = STOPPED;
            if(myAlbum_1_Index<12){
              myAlbum_1_Index ++;
              nowPlaying = myAlbum_1_Track[myAlbum_1_Index];
              printMP3Tracks();
              getLastTouch();
              delay(50);
              } else myAlbum_1_Index = -1;
          }
        }
        if(horz>3350 && horz<3650){                                   //previous track
          if(vert>-2000 && vert<-1580){
            delay(50);
            Serial.println("STOPPING");
            musicPlayer.stopPlaying();
            playMusicState  = STOPPED;
            if (myAlbum_1_Index>0){
              myAlbum_1_Index --;
              nowPlaying = myAlbum_1_Track[myAlbum_1_Index];
              printMP3Tracks();
              getLastTouch();
              delay(50);
              } else myAlbum_1_Index = 13; 
          }
        }
        if(horz>3350 && horz<3650){                                   //volume down
          if(vert>-1340 && vert<-840){
            Serial.println("volume down touched");
            musicPlayer.pausePlaying(true);
            delay(10);
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
              delay(50);
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
                delay(50);
              }
            musicPlayer.pausePlaying(false);
          }
        }
        if(horz>2900 && horz<3200){                                     //volume up
          if(vert>-1340 && vert<-840){
            Serial.println("volume up touched");
            musicPlayer.pausePlaying(true);
            delay(10);
            if (MP3volume > 50){
              MP3volume=MP3volume-2;
              tft.fillRoundRect(160, 202, 80, 25, 6, HX8357_BLACK);
              musicPlayer.setVolume(MP3volume,MP3volume);
              tft.setCursor(40, 208);
              tft.setTextColor(textColor);
              tft.setTextSize(2);
              tft.print("Volume:    ");
              tft.setTextColor(textColor);
              Serial.print("VOLUME IS NOW:   ");Serial.println(MP3volume);
              tft.print(MP3volume);                 //write volume
              getLastTouch();
              delay(50);
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
                delay(50);
              }
            musicPlayer.pausePlaying(false);
          }
        }
        if(horz>2430 && horz<2730){                                     //mute
          if(vert>-1340 && vert<-840){
            musicPlayer.pausePlaying(true);
            int lastMP3volume = MP3volume;
//              delay(50);
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
              delay(50);
          } else if(muted){
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
              delay(50);
            }
            musicPlayer.pausePlaying(false);
          }
        }
        if(horz>3370 && horz<3650){                                     //exit to main screen
          if(vert>-580 && vert<-110){
            delay(50);
            musicPlayer.stopPlaying();
            playMusicState  = STOPPED;
            ampOff();
            currentScreen = 3;
            getLastTouch();
            delay(50);
            startMP3Screen();
          }
        }
      }
    }
    }
  }

  void regDump() {                                                  //serial print M0 register
    
    while (! Serial) {}  // wait for serial monitor to attach
    ZeroRegOptions opts = { Serial, false };
    printZeroRegs(opts);
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
      currentSecond = now.second();         
    }


TS_Point getLastTouch(){    //In theory, this function should work without the ts.begin(). I'm
//    clearTSBuffer();      //leaving in as-is for future troubleshooting.
    TS_Point p;
    if (!ts.bufferEmpty()){
      p = ts.getPoint();
    }
    ts.begin();
    return p;
  }

//  TS_Point getLastTouch(){    //In theory, this function should work without the ts.begin(). I'm
//  //    clearTSBuffer();      //leaving in as-is for future troubleshooting.
//    
//    TS_Point p;
//    if (!ts.bufferEmpty()){
//      p = ts.getPoint();
//    }
//    ts.begin();
//    return p;
//    currentTouchMillis = millis();
//    while (millis() < currentTouchMillis + 50){
//      // wait for 50ms
//    } 
//  }  

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
    }
    return;    
  }

void loop() {
  if (millis() > timeNow + 500){
    timeNow = millis();
    Serial.println("IN MAIN LOOP");
  } 
  switch (currentScreen){
      case 3:
        controlMP3();
          
  }
}
