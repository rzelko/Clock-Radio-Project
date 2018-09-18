# Clock-Radio-Project
Adafruit Feather M0+TFT+MP3+WiFi Radio+Stereo

In this project, I am using:

Adafruit 3.5" 480x320 TFT FeatherWing https://www.adafruit.com/product/3651
Adafruit Feather M0 WiFi with ATWINC1500 https://www.adafruit.com/product/3010
Adafruit DS3231 RTC FeatherWing https://www.adafruit.com/product/3028
Adafruit Music Maker FeatherWing (no amp version) https://www.adafruit.com/product/3357
Adafruit TPA2016 Amp https://www.adafruit.com/product/1712 
SparkFun SI4703 FM Stereo Breakout https://www.sparkfun.com/products/11083


Essentially, my project is a touchscreen clock radio (FM Stereo) with MP3 player and eventually Internet Radio. I am using the TPA2016 amplifier to couple the audio signals from both the SI4703 FM Stereo module and the Music Maker FeatherWing.

HARDWARE MODIFICATIONS MADE:
  1. Due to conflicts with Music Maker FeatherWing, on the FeatherWing TFT:
      
   ```DC was jumpered to A1
      TCS was jumpered to A2
      RCS was jumpered to A3
      SCS was jumpered to A4```
  2. Due to conflicts with the M0 VBATPIN located on A7 (aka "D9"), on the Music Maker FeatherWing:
      ```DREQ on D9 was jumpered to D11```
