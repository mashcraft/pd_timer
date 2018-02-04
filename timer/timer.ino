/*================================================================================*
   Pinewood Derby Timer                                Version 2.50 - 10 Mar 2013
   www.miscjunk.org/mj/pg_pdt.html

   Flexible and affordable Pinewood Derby timer that interfaces with the 
   following software:
     - PD Test/Tune/Track Utility
     - Grand Prix Race Manager software

   Refer to the "PDT_MANUAL.PDF" file for setup and usage instructions.


   Copyright (C) 2011-2013 David Gadberry

   This work is licensed under the Creative Commons Attribution-NonCommercial-
   ShareAlike 3.0 Unported License. To view a copy of this license, visit 
   http://creativecommons.org/licenses/by-nc-sa/3.0/ or send a letter to 
   Creative Commons, 444 Castro Street, Suite 900, Mountain View, California, 
   94041, USA.
 *================================================================================*/

/*-----------------------------------------*
  - TIMER CONFIGURATION -
 *-----------------------------------------*/
#define NUM_LANES    1                 // number of lanes

//#define LED_DISPLAY  1                 // Enable lane place/time displays
#define SHOW_PLACE   1                 // Show place mode
#define PLACE_DELAY  3                 // Delay (secs) when displaying time/place
#define MIN_BRIGHT   0                 // minimum display brightness (0-15)
#define MAX_BRIGHT   15                // maximum display brightness (0-15)

#define GATE_RESET   0                 // Enable closing start gate to reset timer
/*-----------------------------------------*
  - END -
 *-----------------------------------------*/


#ifdef LED_DISPLAY                     // LED control libraries
#include "Wire.h"
#include "Adafruit_LEDBackpack.h"
#include "Adafruit_GFX.h"
#endif

/*-----------------------------------------*
  - static definitions -
 *-----------------------------------------*/
#define PDT_VERSION  "2.50"            // software version
#define MAX_LANES    6                 // maximum number of lanes (Uno)

#define mREADY       0                 // program modes
#define mRACING      1
#define mFINISH      2

#define START_TRIP   LOW               // start switch trip condition
#define NULL_TIME    99.999            // null (non-finish) time
#define NUM_DIGIT    4                 // timer resolution (# of decimals)
#define DISP_DIGIT   4                 // total number of display digits

#define PWM_LED_ON   220
#define PWM_LED_OFF  255
#define char2int(c) (c - '0') 

#define RACE_MASK    0
#define READY_MASK   64

//
// serial messages                        <- to timer
//                                        -> from timer
//
#define SMSG_ACKNW   '.'               // -> acknowledge message

#define SMSG_POWER   'P'               // -> start-up (power on or hard reset)

#define SMSG_CGATE   'G'               // <- check gate
#define SMSG_GOPEN   'O'               // -> gate open

#define SMSG_RESET   'R'               // <- reset
#define SMSG_READY   'K'               // -> ready

#define SMSG_SOLEN   'S'               // <- start solenoid
#define SMSG_START   'B'               // -> race started
#define SMSG_FORCE   'F'               // <- force end

#define SMSG_LMASK   'M'               // <- mask lane
#define SMSG_UMASK   'U'               // <- unmask all lanes

#define SMSG_GVERS   'V'               // <- request timer version
#define SMSG_DEBUG   'D'               // <- toggle debug on/off
#define SMSG_GNUML   'N'               // <- request number of lanes

/*-----------------------------------------*
  - pin assignments -
 *-----------------------------------------*/
byte BRIGHT_LEV   = A0;                // brightness level
byte RESET_SWITCH =  8;                // reset switch
byte STATUS_LED_R =  9;                // status LED (red)
byte STATUS_LED_B = 10;                // status LED (blue)
byte STATUS_LED_G = 11;                // status LED (green)
byte START_GATE   = 12;                // start gate switch
byte START_SOL    = 13;                // start solenoid

//
//                    Lane #    1     2     3     4     5     6
//
int  DISP_ADD [MAX_LANES] = {0x70, 0x71, 0x72, 0x73, 0x74, 0x75};    // display I2C addresses
byte LANE_DET [MAX_LANES] = {   2,    3,    4,    5,    6,    7};    // finish detection pins

/*-----------------------------------------*
  - global variables -
 *-----------------------------------------*/
boolean       fDebug = false;          // debug flag
boolean       ready_first;             // first pass in ready state flag
boolean       finish_first;            // first pass in finish state flag

unsigned long start_time;              // race start time (microseconds)
unsigned long lane_time  [MAX_LANES];  // lane finish time (microseconds)
int           lane_place [MAX_LANES];  // lane finish place
boolean       lane_mask  [MAX_LANES];  // lane mask status

int           serial_data;             // serial data
byte          mode;                    // current program mode

#ifdef LED_DISPLAY                     // LED display control
Adafruit_7segment  disp_mat[MAX_LANES];
#endif

void initialize(boolean powerup=false);
void dbg(int, char * msg, int val=-999);
void smsg(char msg, boolean crlf=true);
void smsg_str(char * msg, boolean crlf=true);

/*================================================================================*
  SETUP TIMER
 *================================================================================*/
void setup()
{  
/*-----------------------------------------*
  - hardware setup -
 *-----------------------------------------*/
  pinMode(STATUS_LED_R, OUTPUT);
  pinMode(STATUS_LED_B, OUTPUT);
  pinMode(STATUS_LED_G, OUTPUT);
  pinMode(START_SOL,    OUTPUT);
  pinMode(RESET_SWITCH, INPUT);
  pinMode(START_GATE,   INPUT);
  pinMode(BRIGHT_LEV,   INPUT);
  
  digitalWrite(RESET_SWITCH, HIGH);    // enable pull-up resistor
  digitalWrite(START_GATE,   HIGH);    // enable pull-up resistor

  for (int n=0; n<NUM_LANES; n++)
  {
#ifdef LED_DISPLAY
    disp_mat[n] = Adafruit_7segment();
    disp_mat[n].begin(DISP_ADD[n]);
    disp_mat[n].clear();
    disp_mat[n].drawColon(false);
    disp_mat[n].writeDisplay();
#endif
    pinMode(LANE_DET[n], INPUT);

    digitalWrite(LANE_DET[n], HIGH);   // enable pull-up resistor
  }
  set_led_bright();

/*-----------------------------------------*
  - software setup -
 *-----------------------------------------*/
  Serial.begin(9600);
  smsg(SMSG_POWER);

  initialize(true);
  unmask_all_lanes();
}


/*================================================================================*
  MAIN LOOP
 *================================================================================*/
void loop()
{
  process_general_msgs();

  switch (mode)
  {
    case mREADY:
      ready_state();
      break;
    case mRACING:
      racing_state();
      break;
    case mFINISH:
      finished_state();
      break;
  }

}


/*================================================================================*
  TIMER READY STATE
 *================================================================================*/
void ready_state()
{
  if (ready_first)
  {
    set_status_led();
    clear_led_display(false);
    ready_first = false;
  }

  if (serial_data == int(SMSG_SOLEN))    // activate start solenoid
  {
    digitalWrite(START_SOL, HIGH);
    smsg(SMSG_ACKNW);
  }
  
  if (digitalRead(START_GATE) == START_TRIP)    // timer start
  {
    start_time = micros();

    digitalWrite(START_SOL, LOW);

    smsg(SMSG_START);
    delay(100); 

    mode = mRACING; 
  }

  return;
}
  

/*================================================================================*
  TIMER RACING STATE
 *================================================================================*/
void racing_state()
{
  int lanes_left, finish_order, lane_status[NUM_LANES];
  unsigned long current_time, last_finish_time;


  set_status_led();
  clear_led_display(true);

  finish_order = 0;
  last_finish_time = 0;

  lanes_left = NUM_LANES;
  for (int n=0; n<NUM_LANES; n++)
  {
    if (lane_mask[n]) lanes_left--;
  }

  while (lanes_left)
  {
    current_time = micros();

    for (int n=0; n<NUM_LANES; n++) lane_status[n] = bitRead(PIND, LANE_DET[n]);    // read status of all lanes

    for (int n=0; n<NUM_LANES; n++)
    {
      if (lane_time[n] == 0 && lane_status[n] == HIGH && !lane_mask[n])    // car has crossed finish line
      {
        lanes_left--;

        lane_time[n] = current_time - start_time;

        if (lane_time[n] > last_finish_time)
        {
          finish_order++;
          last_finish_time = lane_time[n];
        }
        lane_place[n] = finish_order;

        set_led_display(n, lane_place[n], lane_time[n], SHOW_PLACE);
      }
    }
    
    serial_data = get_serial_data();

    if (serial_data == int(SMSG_FORCE) || serial_data == int(SMSG_RESET) || digitalRead(RESET_SWITCH) == LOW)    // force race to end
    {
      lanes_left = 0;
      smsg(SMSG_ACKNW);
    }
  }
    
  send_race_results();

  mode = mFINISH;

  return;
}


/*================================================================================*
  TIMER FINISHED STATE
 *================================================================================*/
void finished_state()
{
  if (finish_first)
  {
    set_status_led();
    finish_first = false;
  }

  if (GATE_RESET && digitalRead(START_GATE) != START_TRIP)    // gate closed
  {
    delay(500);    // ignore any switch bounce

    if (digitalRead(START_GATE) != START_TRIP)    // gate still closed
    {
      initialize();    // reset timer
    } 
  } 

  set_led_bright();
  display_place_time();

  return;
}


/*================================================================================*
  PROCESS GENERAL SERIAL MESSAGES
 *================================================================================*/
void process_general_msgs()
{
  int lane;
  char tmps[50];


  serial_data = get_serial_data();

  if (serial_data == int(SMSG_GVERS))    // get software version
  {
      sprintf(tmps, "vert=%s", PDT_VERSION);
      smsg_str(tmps);
  } 

  else if (serial_data == int(SMSG_GNUML))    // get number of lanes
  {
      sprintf(tmps, "numl=%d", NUM_LANES);
      smsg_str(tmps);
  } 

  else if (serial_data == int(SMSG_DEBUG))    // toggle debug
  {
    fDebug = !fDebug;
    dbg(true, "toggle debug = ", fDebug);
  } 

  else if (serial_data == int(SMSG_CGATE))    // check start gate
  {
    if (digitalRead(START_GATE) == START_TRIP)    // gate open
    {
      smsg(SMSG_GOPEN);
    } 
    else
    {
      smsg(SMSG_ACKNW);
    } 
  } 

  else if (serial_data == int(SMSG_RESET) || digitalRead(RESET_SWITCH) == LOW)    // timer reset
  {
    if (digitalRead(START_GATE) != START_TRIP)    // only reset if gate closed
    {
      initialize();
    } 
    else
    {
      smsg(SMSG_GOPEN);
    } 
  } 

  else if (serial_data == int(SMSG_LMASK))    // lane mask
  {
    delay(100);
    serial_data = get_serial_data();

    lane = serial_data - 48;
    if (lane >= 1 && lane <= NUM_LANES)
    {
      lane_mask[lane-1] = true;

      dbg(fDebug, "set mask on lane = ", lane);
    }
    smsg(SMSG_ACKNW);
  }

  else if (serial_data == int(SMSG_UMASK))    // unmask all lanes
  {
    unmask_all_lanes();
    smsg(SMSG_ACKNW);
  }

  return;
}


/*================================================================================*
  SEND RACE RESULTS TO COMPUTER
 *================================================================================*/
void send_race_results()
{
  float lane_time_sec;


  for (int n=0; n<NUM_LANES; n++)    // send times to computer
  {
    lane_time_sec = (float)(lane_time[n] / 1000000.0);    // elapsed time (seconds)

    if (lane_time_sec == 0)    // did not finish
    {
      lane_time_sec = NULL_TIME;
    }

    Serial.print(n+1);
    Serial.print(" - ");
    Serial.println(lane_time_sec, NUM_DIGIT);  // numbers are rounded to NUM_DIGIT
                                               // digits by println function
  }

  return;
}


/*================================================================================*
  RACE FINISHED - DISPLAY PLACE / TIME FOR ALL LANES
 *================================================================================*/
void display_place_time()
{
  unsigned long now;
  static boolean display_mode;
  static unsigned long last_display_update = 0;


  if (!SHOW_PLACE) return;

  now = millis();

  if (last_display_update == 0)  // first cycle
  {
    last_display_update = now;
    display_mode = false;
  }

  if ((now - last_display_update) > (unsigned long)(PLACE_DELAY * 1000))
  {
    dbg(fDebug, "display_place_time");

    for (int n=0; n<NUM_LANES; n++)
    {
      set_led_display(n, lane_place[n], lane_time[n], display_mode);
    }

    display_mode = !display_mode;
    last_display_update = now;
  }

  return;
}


/*================================================================================*
  SET LANE PLACE/TIME DISPLAY
 *================================================================================*/
void set_led_display(int lane, int display_place, unsigned long display_time, int display_mode)
{
  int c;
  char cnum[25];
  double display_time_sec;
  long time_int, time_dec;

//  dbg(fDebug, "led: lane = ", lane);
//  dbg(fDebug, "led: plce = ", display_place);
//  dbg(fDebug, "led: time = ", display_time);

#ifdef LED_DISPLAY
  disp_mat[lane].clear();

  if (display_mode)
  {
    if (display_place > 0)
    {
      sprintf(cnum,"%1d", display_place);

      disp_mat[lane].writeDigitNum(3, char2int(cnum[0]), false);
    }
    else  // did not finish
    {
      disp_mat[lane].writeDigitRaw(3, READY_MASK);
    }
    disp_mat[lane].drawColon(false);
    disp_mat[lane].writeDisplay();
  }
  else
  {
    display_time_sec = (double)(display_time / (double)1000000.0);    // elapsed time (seconds)

    // work around for Arduino sprintf - float limitation
    time_int = (long)display_time_sec;
    time_dec = (long)((display_time_sec - (double)time_int)*(double)1000000.0);
    time_dec += (long)50;    // round to 4th decimal
    sprintf(cnum, "%01ld.%06ld", time_int, time_dec);

    c = 0;
    for (int d=0; d<DISP_DIGIT; d++)
    {
      if (display_time > 0)
      {
        disp_mat[lane].writeDigitNum(d+int(d/2), char2int(cnum[c]), (cnum[c+1] == '.'));    // time

        c++; if (cnum[c] == '.') c++;
      }
      else  // did not finish
      {
        disp_mat[lane].writeDigitRaw(d+int(d/2), READY_MASK);
      }
    }
    disp_mat[lane].drawColon(false);
    disp_mat[lane].writeDisplay();
  }
#endif

  return;
}


/*================================================================================*
  CLEAR LANE PLACE/TIME DISPLAY
 *================================================================================*/
void clear_led_display(boolean status)
{
  int mask;

  dbg(fDebug, "led: CLEAR");

#ifdef LED_DISPLAY
  if (status)
  {
    mask = RACE_MASK;     // racing
  }
  else
  {
    mask = READY_MASK;    // ready
  }

  for (int n=0; n<NUM_LANES; n++)
  {
    disp_mat[n].clear();

    for (int d=0; d<DISP_DIGIT; d++)
    {
      disp_mat[n].writeDigitRaw(d+int(d/2), mask);
    }

    disp_mat[n].drawColon(false);
    disp_mat[n].writeDisplay();
  }
#endif

  return;
}


/*================================================================================*
  SET LANE DISPLAY BRIGHTNESS
 *================================================================================*/
void set_led_bright()
{
  static float current_level = -1.0;
  float new_level;

#ifdef LED_DISPLAY
  new_level = long(1023 - analogRead(BRIGHT_LEV)) / 1023.0F * 15.0F;
  new_level = min(new_level, (float)MAX_BRIGHT);
  new_level = max(new_level, (float)MIN_BRIGHT);

  if (fabs(new_level - current_level) > 0.3F)    // deadband to prevent flickering 
  {                                              // between levels
    dbg(fDebug, "led: BRIGHT");

    current_level = new_level;

    for (int n=0; n<NUM_LANES; n++)
    {
      disp_mat[n].setBrightness((int)current_level);
    }
  }
#endif

  return;
}


/*================================================================================*
  SET TIMER STATUS LED
 *================================================================================*/
void set_status_led()
{
  int r_lev, b_lev, g_lev;

  dbg(fDebug, "status led = ", mode);

  r_lev = PWM_LED_OFF;
  b_lev = PWM_LED_OFF;
  g_lev = PWM_LED_OFF;

  if (mode == mREADY)         // blue
  {
    b_lev = PWM_LED_ON;
  }
  else if (mode == mRACING)  // green
  {
    g_lev = PWM_LED_ON;
  }
  else if (mode == mFINISH)  // red
  {
    r_lev = PWM_LED_ON;
  }

  analogWrite(STATUS_LED_R,  r_lev);
  analogWrite(STATUS_LED_B,  b_lev);
  analogWrite(STATUS_LED_G,  g_lev);

  return;
}


/*================================================================================*
  READ SERIAL DATA FROM COMPUTER
 *================================================================================*/
int get_serial_data()
{  
  int data = 0;
  
  if (Serial.available() > 0)
  {
    data = Serial.read();
    dbg(fDebug, "ser rec = ", data);
  }

  return data;
}  


/*================================================================================*
  INITIALIZE TIMER
 *================================================================================*/
void initialize(boolean powerup)
{  
  for (int n=0; n<NUM_LANES; n++)
  {
    lane_time[n] = 0;
    lane_place[n] = 0;
  }

  start_time = 0;
  set_status_led();
  digitalWrite(START_SOL, LOW);

  // if power up and gate is open -> goto FINISH state
  if (powerup && digitalRead(START_GATE) == START_TRIP) 
  {
    mode = mFINISH;
  }
  else
  {
    mode = mREADY;

    smsg(SMSG_READY);
    delay(100);
  }
  Serial.flush();

  ready_first  = true;
  finish_first  = true;

  return;
}


/*================================================================================*
  UNMASK ALL LANES
 *================================================================================*/
void unmask_all_lanes()
{  
  dbg(fDebug, "unmask all lanes");

  for (int n=0; n<NUM_LANES; n++)
  {
    lane_mask[n] = false;
  }  

  return;
}  


/*================================================================================*
  SEND DEBUG TO COMPUTER
 *================================================================================*/
void dbg(int flag, char * msg, int val)
{  
  char tmps[50];


  if (!flag) return;

  smsg_str("dbg: ", false);
  smsg_str(msg, false);

  if (val != -999)
  {
    sprintf(tmps, "%d", val);
    smsg_str(tmps);
  }
  else
  {
    smsg_str("");
  }

  return;
}


/*================================================================================*
  SEND SERIAL MESSAGE (CHAR) TO COMPUTER
 *================================================================================*/
void smsg(char msg, boolean crlf)
{  
  if (crlf)
  {
    Serial.println(msg);
  }
  else
  {
    Serial.print(msg);
  }

  return;
}


/*================================================================================*
  SEND SERIAL MESSAGE (STRING) TO COMPUTER
 *================================================================================*/
void smsg_str(char * msg, boolean crlf)
{  
  if (crlf)
  {
    Serial.println(msg);
  }
  else
  {
    Serial.print(msg);
  }

  return;
}
