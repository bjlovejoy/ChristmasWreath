#define ShiftRegisterPWM_DATA_PORT PORTD
#define ShiftRegisterPWM_DATA_MASK 128
#define ShiftRegisterPWM_CLOCK_PORT PORTE
#define ShiftRegisterPWM_CLOCK_MASK 64
#define ShiftRegisterPWM_LATCH_PORT PORTB
#define ShiftRegisterPWM_LATCH_MASK 16

#include <ShiftRegisterPWM.h>
#include <RTClib.h>

//TODO: to save battery power, put arduino into sleep and use button as interrupt
//      (may be able to keep existing code and turn OFF state into sleep/idle mode)

/*
 * Wiring: see https://timodenk.com/blog/shiftregister-pwm-library/
 * 
 * strand 1:    |   strand2:
 * red   = Q7   |   red   = Q4
 * green = Q6   |   green = Q3
 * blue  = Q5   |   blue  = Q2
 * 
 * (Q0 and Q1 are not used in this configuration)
 * For remainder of 74HC595, see setup below and link above
 * 
 * Several changes were made, both because pins 2 and 3 are SDA/SCL on
 * the Pro Micro and because the pins are backwards (shift register pin
 * 0 is actually 7, 1 is 6, etc.  So pins 2/3 will be used for I2C
 * instead of the usual A4/A5.
 * 
 * button = pin 5 (pull down configuration - HIGH reading means pushed)
 * RTC: data = pin 2 (SDA), clock = pin 3 (SCL)
 */

bool reverse = true;      //set true if using PNP BJTs, false for NPN
bool lights_set = false;  //for settting SOLID and MIXED light modes

ShiftRegisterPWM sr(1, 255);  //number of shift registers = 1, resolution = 0 to 255

//time objects
RTC_DS3231 rtc;
DateTime now, change_time;
int time_h;
int change_time_mins = 2;

//state machine and modes
enum light_modes   {OFF, SOLID, MIXED, RAINBOW};  //enum for state machine
enum colors        {RED, ORANGE, YELLOW, GREEN, AQUA, TEAL, BLUE, PINK, MAGENTA, PURPLE, WHITE, CLEAR};
enum rainbow_modes {CHANGE_ONE, CHANGE_TWO, RGB, REV_RGB, END};  //TODO: make more modes
int current_state = OFF;  //current place in state machine
int rainbow_mode;
int last_rgb_color;

//button and override variables
int button = 5;
bool button_override = false, override_set = false;
int override_hour;

//LED pins and values
int red1_pin   = 0, red1_val   = 0;
int green1_pin = 1, green1_val = 0;
int blue1_pin  = 2, blue1_val  = 0;
int red2_pin   = 3, red2_val   = 0;
int green2_pin = 4, green2_val = 0;
int blue2_pin  = 5, blue2_val  = 0;

//function prototypes
void read_button();
void check_time();
void set_led(int, int, int);
void set_color(int, int);
void rainbow_handle();
int color_ramp(int, int, int);
int dual_color_ramp(int, int, int, int);

void setup()
{
  Serial.begin(9600);

  pinMode(button, INPUT);  //timer override button
  pinMode(6, OUTPUT);      //sr data pin (ds)
  pinMode(7, OUTPUT);      //sr clock pin (SH-CP)
  pinMode(8, OUTPUT);      //sr latch pin (ST-CP)
  
  //use timer1 for frequent update
  sr.interrupt(ShiftRegisterPWM::UpdateFrequency::Fast);

  //setup the RTC
  if(!rtc.begin()) { Serial.println("Couldn't find RTC"); Serial.flush(); abort(); }
  if (rtc.lostPower()) { rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); }

  randomSeed(analogRead(0));

  set_color(1, CLEAR);
  set_color(2, CLEAR);
}

void loop()
{
  if(current_state == OFF)
  {
    if(!lights_set)
    {
      set_color(1, CLEAR);
      set_color(2, CLEAR);
      lights_set = true;
    }
    delay(100);
  }
  else if(current_state == SOLID)
  {
    if(!lights_set)
    {
      long random_color = random(CLEAR);
      set_color(1, random_color);
      set_color(2, random_color);
      lights_set = true;
    }
    else { delay(100); }
  }
  else if(current_state == MIXED)
  {
    if(!lights_set)
    {
      long random_color1 = random(CLEAR);
      long random_color2 = random(CLEAR);
      while(random_color1 == random_color2) { random_color2 = random(CLEAR); }
      set_color(1, random_color1);
      set_color(2, random_color2);
      lights_set = true;
    }
    else { delay(100); }
  }
  else if(current_state == RAINBOW)
  {
    if(!lights_set)
    {
      set_color(1, CLEAR);
      set_color(2, CLEAR);
      rainbow_mode = int(random(END));
      lights_set = true;
      if(rainbow_mode == RGB || rainbow_mode == REV_RGB)
      {
        last_rgb_color = 0;
        set_color(1, RED);
        set_color(2, RED);
      }
    }
    else
    {
      rainbow_handle();
    }
  }

  read_button();
  check_time();
}

/**************************************************************************************/
/***FUNCTIONS**************************************************************************/
/**************************************************************************************/

void read_button()
{
  if(digitalRead(button))
  {
    button_override = true;
    while(digitalRead(button)) { delay(10); }
    delay(20);
  }
}

void check_time()
{
  now = rtc.now();
  time_h = int(now.hour());
  
  if(button_override)  //handle button press
  {
    if(override_set) { override_set = false; }
    else
    {
      override_set = true;
      override_hour = time_h;
      current_state = OFF;
      lights_set = false;
    }
    button_override = false;
  }

  if(time_h > 18 && time_h < 21)  //within active hours (7-9 pm)
  {
    if(override_set && override_hour < 19) { override_set = false; }  //just entered active hours (with override set)

    if(!override_set)
    {
      active_handle:
      if(current_state == OFF)
      {
        ++current_state;
        lights_set = false;
        change_time = now + TimeSpan(0, 0, change_time_mins, 0);
      }
      else if(now > change_time)
      {
        change_time = now + TimeSpan(0, 0, change_time_mins, 0);
        if(current_state == RAINBOW) { current_state = SOLID; lights_set = false; }
        else { ++current_state; lights_set = false; }
      }
    }
  }
  
  else  //outside active hours
  {
    if(override_set && override_hour > 18 && override_hour < 21) { override_set = false; }  //just left active hours (with override set)

    if(override_set) { goto active_handle; }
    else
    {
      if(current_state != OFF) { current_state = OFF; lights_set = false; }
    }
  }
}


void set_led(int pin, int new_level, int level_storage)
{
  if(reverse) { level_storage = map(new_level, 0, 255, 255, 0); }
  sr.set(pin, level_storage);
}

void set_color(int strand, int color)
{
  int red_level = 0, green_level = 0, blue_level = 0;
  if(color == RED)
  {
    red_level = 255;
  }
  else if(color == ORANGE)
  {
    red_level = 255;
    green_level = 50;
  }
  else if(color == YELLOW)
  {
    red_level = 255;
    green_level = 255;
  }
  else if(color == GREEN)
  {
    green_level = 255;
  }
  else if(color == AQUA)
  {
    green_level = 255;
    blue_level = 255;
  }
  else if(color == TEAL)
  {
    green_level = 255;
    blue_level = 50;
  }
  else if(color == BLUE)
  {
    blue_level = 255;
  }
  else if(color == PINK)
  {
    red_level = 255;
    blue_level = 50;
  }
  else if(color == MAGENTA)
  {
    red_level = 255;
    blue_level = 100;
  }
  else if(color == PURPLE)
  {
    red_level = 255;
    blue_level = 200;
  }
  else if(color == WHITE)
  {
    red_level = 255;
    green_level = 255;
    blue_level = 255;
  }

  if(strand == 1)
  {
    if(reverse)
    {
      red1_val   = map(red_level,   0, 255, 255, 0);
      green1_val = map(green_level, 0, 255, 255, 0);
      blue1_val  = map(blue_level,  0, 255, 255, 0);
    }
    else
    {
      red1_val   = red_level;
      green1_val = green_level;
      blue1_val  = blue_level;
    }
    sr.set(red1_pin, red1_val);
    sr.set(green1_pin, green1_val);
    sr.set(blue1_pin, blue1_val);
  }
  else if(strand == 2)
  {
    if(reverse)
    {
      red2_val   = map(red_level,   0, 255, 255, 0);
      green2_val = map(green_level, 0, 255, 255, 0);
      blue2_val  = map(blue_level,  0, 255, 255, 0);
    }
    else
    {
      red2_val   = red_level;
      green2_val = green_level;
      blue2_val  = blue_level;
    }
    sr.set(red2_pin, red2_val);
    sr.set(green2_pin, green2_val);
    sr.set(blue2_pin, blue2_val);
  }
}

void rainbow_handle()
{
  //pick a strand (1 or 2) and change to a color other than it's current color
  if(rainbow_mode == CHANGE_ONE)
  {
    long strand = random(1, 3);
    long color = random(3);
    
    if(strand == 1)
    {
      if(color == 0)  //red
      {
        red1_val = color_ramp(red1_pin, red1_val, 10);
      }
      else if(color == 1)  //green
      {
        green1_val = color_ramp(green1_pin, green1_val, 10);
      }
      else if(color == 2)  //blue
      {
        blue1_val = color_ramp(blue1_pin, blue1_val, 10);
      }
    }
    else if(strand == 2)
    {
      if(color == 0)  //red
      {
        red2_val = color_ramp(red2_pin, red2_val, 10);
      }
      else if(color == 1)  //green
      {
        green2_val = color_ramp(green2_pin, green2_val, 10);
      }
      else if(color == 2)  //blue
      {
        blue2_val = color_ramp(blue2_pin, blue2_val, 10);
      }
    }
  }

  //change both strands to a color other than their current color
  else if(rainbow_mode == CHANGE_TWO)
  {
    long color1 = random(3);
    long color2 = random(3);

    //strand 1
    if(color1 == 0)  //red
    {
      red1_val = color_ramp(red1_pin, red1_val, 10);
    }
    else if(color1 == 1)  //green
    {
      green1_val = color_ramp(green1_pin, green1_val, 10);
    }
    else if(color1 == 2)  //blue
    {
      blue1_val = color_ramp(blue1_pin, blue1_val, 10);
    }

    //strand 2
    if(color2 == 0)  //red
    {
      red2_val = color_ramp(red2_pin, red2_val, 10);
    }
    else if(color2 == 1)  //green
    {
      green2_val = color_ramp(green2_pin, green2_val, 10);
    }
    else if(color2 == 2)  //blue
    {
      blue2_val = color_ramp(blue2_pin, blue2_val, 10);
    }
  }

  //change both strands according to ROYGBV color pattern
  else if(rainbow_mode == RGB)
  {
    if(last_rgb_color = 0)  //red
    {
      green1_val = dual_color_ramp(green1_pin, green2_pin, green1_val, 10);
      green2_val = green1_val;
      red1_val   = dual_color_ramp(red1_pin, red2_pin, red1_val, 10);
      red2_val   = red1_val;

      last_rgb_color = 1;
    }
    else if(last_rgb_color = 1)  //green
    {
      blue1_val  = dual_color_ramp(blue1_pin, blue2_pin, blue1_val, 10);
      blue2_val  = blue1_val;
      green1_val = dual_color_ramp(green1_pin, green2_pin, green1_val, 10);
      green2_val = green1_val;

      last_rgb_color = 2;
    }
    else if(last_rgb_color = 2)  //blue
    {
      red1_val  = dual_color_ramp(red1_pin, red2_pin, red1_val, 10);
      red2_val  = red1_val;
      blue1_val = dual_color_ramp(blue1_pin, blue2_pin, blue1_val, 10);
      blue2_val = blue1_val;

      last_rgb_color = 0;
    }
  }

  //similar to RGB, but change colors in the reverse direction
  else if(rainbow_mode == REV_RGB)
  {
    if(last_rgb_color = 0)  //red
    {
      blue1_val = dual_color_ramp(blue1_pin, blue2_pin, blue1_val, 10);
      blue2_val = blue1_val;
      red1_val  = dual_color_ramp(red1_pin, red2_pin, red1_val, 10);
      red2_val  = red1_val;

      last_rgb_color = 2;
    }
    else if(last_rgb_color = 1)  //green
    {
      red1_val  = dual_color_ramp(red1_pin, red2_pin, red1_val, 10);
      red2_val  = red1_val;
      green1_val = dual_color_ramp(green1_pin, green2_pin, green1_val, 10);
      green2_val = green1_val;

      last_rgb_color = 0;
    }
    else if(last_rgb_color = 2)  //blue
    {
      green1_val = dual_color_ramp(green1_pin, green2_pin, green1_val, 10);
      green2_val = green1_val;
      blue1_val  = dual_color_ramp(blue1_pin, blue2_pin, blue1_val, 10);
      blue2_val  = blue1_val;

      last_rgb_color = 1;
    }
  }

  //I removed RANDOM mode, since I don't have a use for it rn
}

//ramps the color for the selected pin in the opposite direction, regardless of "reverse"
int color_ramp(int pin, int current_color_val, int interval)
{
  int new_color_val = map(current_color_val, 0, 255, 255, 0);  //target color val

  if(new_color_val == 0)
  {
    for(int i = current_color_val; i > new_color_val; --i)
    {
      sr.set(pin, i);
      delay(interval);
    }
  }
  else
  {
    for(int i = current_color_val; i < new_color_val; ++i)
    {
      sr.set(pin, i);
      delay(interval);
    }
  }
  
  return new_color_val;
}

int dual_color_ramp(int pin1, int pin2, int current_color_val, int interval)
{
  int new_color_val = map(current_color_val, 0, 255, 255, 0);  //target color val

  if(new_color_val == 0)
  {
    for(int i = current_color_val; i > new_color_val; --i)
    {
      sr.set(pin1, i);
      sr.set(pin2, i);
      delay(interval);
    }
  }
  else
  {
    for(int i = current_color_val; i < new_color_val; ++i)
    {
      sr.set(pin1, i);
      sr.set(pin2, i);
      delay(interval);
    }
  }
  
  return new_color_val;
}
