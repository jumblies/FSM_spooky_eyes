#include <Arduino.h>
#define FASTLED_ALLOW_INTERRUPTS 0 // Fixes glitchingon WS2811
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <FastLED.h>
#include "secrets.h"
FASTLED_USING_NAMESPACE
#if defined(FASTLED_VERSION) && (FASTLED_VERSION < 3001000)
#warning "Requires FastLED 3.1 or later; check github for latest code."
#endif

#define LED_TYPE WS2811
#define COLOR_ORDER RGB
#define NUM_LEDS 50
#define DATA_PIN D4
#define BRIGHTNESS 100
#define FRAMES_PER_SECOND 30
CRGB leds[NUM_LEDS];

const int maxEyes = 25; // maximum number of concurrently active blinkers
// const int minEyes = 20; // minimum number of concurrently active blinkers

// dead-time between lighting of a range of pixels
const int deadTimeMin = 10;
const int deadTimeMax = 100; //default was 300;

// interval between blink starts - independent of position
const int intervalMin = 10;
const int intervalMax = 300; //Default 300

const int stepInterval = 10;
long lastStep = 0;




/*****************************************************************************
Blinker Class

Implements a state machine which generates a blink of random duration and color.
The blink uses two adjacent pixels and ramps the intensity up, then down, with 
a random repeat now and again.
*****************************************************************************/

class blinker
{
  public:
  
  boolean m_active;  // blinker is in use.
  int m_deadTime;  // don't re-use this pair immediately
  
  int m_pos;  // position of the 'left' eye.  the 'right' eye is m_pos + 1
  
  int m_red;  // RGB components of the color
  int m_green;
  int m_blue;
  
  int m_increment;  // ramp increment - determines blink speed
  int m_repeats;  // not used
  int m_intensity;  // current ramp intensity
  
  public:
  // Constructor - start as inactive
  blinker()
  {
    m_active = false;
  }
  
  // Initiate a blink at the specified pixel position
  // All other blink parameters are randomly generated
  void StartBlink(int pos)
  {
    m_pos = pos;
    
    // Pick a random color - skew toward red/orange/yellow part of the spectrum for extra creepyness
    m_red = random(150, 255);
    // m_blue = 0;
    m_blue = random(0, 25);
    m_green = random(0, 50);
    
    m_repeats += random(1, 3);
    
    // set blink speed and deadtime between blinks
    m_increment = random(1, 6);
    m_deadTime = random(deadTimeMin, deadTimeMax);

    // Mark as active and start at intensity zero
    m_active = true;
    m_intensity = 0;
  }
  
  // Step the state machine:
  void step()
  {
    if (!m_active)
    { 
      // count down the dead-time when the blink is done
      if (m_deadTime > 0)
      {
        m_deadTime--;
      }
      return;
    }
    
    // Increment the intensity
    m_intensity += m_increment;
    if (m_intensity >= 75)  // max out at 75 - then start counting down
    {
      m_increment = -m_increment;
      m_intensity += m_increment;
    }
    if (m_intensity <= 0)
    {
        // make sure pixels all are off
      //strip.setPixelColor(m_pos, Color(0,0,0));
      leds[m_pos]= CRGB::Black;
      //strip.setPixelColor(m_pos+1, Color(0,0,0));
      leds[m_pos+1]= CRGB::Black;
      
      if (--m_repeats <= 0)      // Are we done?
      {
         m_active = false;
      }
      else // no - start to ramp up again
      {
          m_increment = random(1, 5);
      }
      return;
    }
    
    // Generate the color at the current intensity level
    int r =  map(m_red, 0, 255, 0, m_intensity);
    int g =  map(m_green, 0, 255, 0, m_intensity);
    int b =  map(m_blue, 0, 255, 0, m_intensity);
    //uint32_t color = Color(r, g, b);  //changed by Chemdoc77

    CRGB color = CRGB( r, g, b);
    
    // Write to both 'eyes'
    leds[m_pos]= color;
   // strip.setPixelColor(m_pos +1, color);
   leds[m_pos+1]= color;
  }
};

// An array of blinkers - this is the maximum number of concurrently active blinks
blinker blinkers[maxEyes];

// A delay between starting new blinks
int countdown;

void setup()
{  countdown = 0;
  delay(3000); // 3 second delay for recovery
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.config(ip, gateway, subnet);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.mode(WIFI_STA);
  // tell FastLED about the LED strip configuration
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  //FastLED.addLeds<LED_TYPE,DATA_PIN,CLK_PIN,COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);

  // set master brightness control
  FastLED.setBrightness(BRIGHTNESS);
  ArduinoOTA.setHostname("ESPWEB");
  // Delay necessary to get ack from upload
  ArduinoOTA.onStart([]() {});
  ArduinoOTA.onEnd([]() { delay(500); });

  ArduinoOTA.onError([](ota_error_t error) {
    (void)error;
    ESP.restart();
  });
  /* setup the OTA server */
  ArduinoOTA.begin();
}

void loop()
{
if (millis() - lastStep > stepInterval)
  {
    lastStep = millis();
    --countdown;
    for(int i = 0; i < maxEyes; i++)
    {
      // Only start a blink if the countdown is expired and there is an available blinker
      if ((countdown <= 0) && (blinkers[i].m_active == false))
      {
        int newPos = random(0,NUM_LEDS/2) * 2;
            
        for(int j = 0; j < maxEyes; j++)
        {
          // avoid active or recently active pixels
          if ((blinkers[j].m_deadTime > 0) && (abs(newPos - blinkers[j].m_pos) < 4))
          {
            Serial.print("-");
            Serial.print(newPos);
            newPos = -1;  // collision - do not start
            break;
          }
        }
  
        if (newPos >= 0)  // if we have a valid pixel to start with...
        {
         Serial.print(i);
         Serial.print(" Activate - ");
         Serial.println(newPos);
         blinkers[i].StartBlink(newPos);  
         countdown = random(intervalMin, intervalMax);  // random delay to next start
        }
      }
      // step all the state machines
       blinkers[i].step();
    }
   
    FastLED.show();
  }
  ArduinoOTA.handle();
}
