/**
 * Project: Defuse a Bomb Teambuilding Activity
 * Author: Tom Blanchard 
 * Date: 10/10/2017
 * Version: 1
 * Revisions:
 */
#include <LiquidCrystal.h>
#include <Wire.h>
#include <SPI.h>
#include <SparkFunLSM9DS1.h>
#include <Keypad.h>
#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>
#include "defuse.h"

#define EEPROM_TIME_ADDR 0x00
#define EEPROM_SENSITIVITY_ADDR 0x01
// SDO_XM and SDO_G are both pulled high, so our addresses are:
#define LSM9DS1_M  0x1E // Would be 0x1C if SDO_M is LOW
#define LSM9DS1_AG  0x6B // Would be 0x6A if SDO_AG is LOW
//Neopixel pin
#define LED_PIN 11
#define BUZZ_PIN 13

#define DEFUSE_TIME 90

Adafruit_NeoPixel strip = Adafruit_NeoPixel(2, LED_PIN, NEO_GRB + NEO_KHZ800);
// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(9, 10, A0, A1, A2, A3); // A3, A2, A1, A0);

LSM9DS1 imu;


volatile boolean timer_tick = false;

double base_x = 0;
double base_y = 0;
double base_z = 0;

uint8_t code = 0;
uint8_t solution = 0;

byte rowPins[ROWS] = {8, 7, 6, 5}; //connect to the row pinouts of the kpd
byte colPins[COLS] = {4, 3, 2}; //connect to the column pinouts of the kpd
Keypad keypad = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );


uint8_t mask_index = 0;
uint8_t op_index = 0;
uint8_t beep_index = 0;

volatile long time_limit = 0;
long sensitivity = 0;
boolean beeping = false;
volatile uint8_t beep_count = 0;
volatile BeepMode bmode = NORMAL;

void setup() {
  //Set pins
  pinMode(BUZZ_PIN, OUTPUT);
  digitalWrite(BUZZ_PIN, LOW);

  // set up the LCD's number of columns and rows:
  Serial.begin(115200);

  lcd.begin(16, 2);
  lcd.clear();


  imu.settings.device.commInterface = IMU_MODE_I2C;
  imu.settings.device.mAddress = LSM9DS1_M;
  imu.settings.device.agAddress = LSM9DS1_AG;
  // The above lines will only take effect AFTER calling
  // imu.begin(), which verifies communication with the IMU
  // and turns it on

  if (!imu.begin())
  {
    lcd.print("IMU err");
    Serial.println("Failed to communicate with LSM9DS1.");
    while (1);
  }
  else {
    lcd.print("IMU init");
  }

  // Initialize all pixels to 'off'
  strip.begin();
  strip.show();

  //Show settings menu
  lcd.clear();
  lcd.print("* Settings");
  lcd.setCursor(0, 1);
  lcd.print("# Start");

  //Wait for user to press button
  while (1)
  {
    keypad.getKeys();
    if (keypad.isPressed('*'))
    {
      //Allow user to get their hands onto the buttons
      check_settings(true);
      break;
    }
    if ( keypad.isPressed('#'))
    {
      check_settings(false);
      break;
    }
  }

  //Seed the random number generator
  randomSeed(millis());
  code = (uint8_t)random(255);
  mask_index = random(sizeof(masks) / sizeof(mask_profile));
  op_index = random(sizeof(ops) / sizeof(op_profile));
  beep_index = random(sizeof(beeps) / sizeof(beep_profile));

  //Precalc the solution
  solution = process_code();

  //Show the code/solution
  lcd.clear();
  lcd.print("Num:");
  lcd.print(code);
  lcd.setCursor(0, 1);
  lcd.print("Passwd:");
  lcd.print(solution);
  lcd.print(" B:");
  lcd.print(beeps[beep_index].defuse_key);

  //Determine the interval for beeping
  beep_count = beeps[beep_index].interval - 1;

  //Show the led colors
  strip.setPixelColor(0, strip.Color(masks[mask_index].r, masks[mask_index].g, masks[mask_index].b));
  strip.setPixelColor(1, strip.Color(ops[op_index].r, ops[op_index].g, ops[op_index].b));
  strip.show();


  //Wait for # to continue
  delay(1000);
  while (1)
  {
    keypad.getKeys();
    if (keypad.isPressed('#'))
    {
      break;
    }
  }

  //Get calibration
  get_calibration();

  //Clear Leds
  setLedsToColor(0, 0, 0);

  //Start the timer and timer interrupts
  init_timer1();

}

char topbuff[17] = {'\0'};


void loop() {

  //Get the time at the start of the loop
  unsigned long loop_start = millis();


  //******************************
  //DETECT IF OUT OF TIME
  if (time_limit <= 0)
  {
    boom(TIME);
  }

  //*******************************
  //DETECT IF DEFUSED
  //Get all keypresses
  if (keypad.getKeys())
  {
    for (int i = 0; i < LIST_MAX; i++) // Scan the whole key list.
    {
      //check it is in a pressed state
      if (keypad.key[i].kstate == PRESSED)
      {
        //see if it is the correct button
        if (keypad.key[i].kchar == beeps[beep_index].defuse_key)
        {
          //Move on to defusing
          defuse();
        }
        else {

          sprintf(topbuff, "%s", "INCORRECT BUTTON");
          if (time_limit > 10)
          {
            time_limit = 10;
          }
          digitalWrite(BUZZ_PIN, HIGH);
        }

      }
    }
  }


  //****************************
  //DETECT GONE OFF
  //Get more accelerometer data
  if ( imu.accelAvailable() )
  {
    imu.readAccel();

    //Correct based on calibration
    int err_x = abs(base_x - imu.ax);
    int err_y = abs(base_y - imu.ay);
    int err_z = abs(base_z - imu.az);

    //TODO: find decent threshold
    if ( (err_x > sensitivity) || (err_y > sensitivity) || (err_z > 2 * sensitivity) )
    {
      Serial.print(err_x);
      Serial.print(", ");
      Serial.print(err_y);
      Serial.print(", ");
      Serial.println(err_z);
      boom(MOTION);
    }
  }

  if (timer_tick)
  {
    do_time();
  }

  //keep the loop to approx 20 msec
  unsigned long loop_end = millis();
  unsigned long diff = loop_end - loop_start;

  if (diff > 20)
  {
    diff = 20;
  }
  delay(20 - diff);
}

/**
   Processes the code generated and returns the solution the user must type in to defuse the bomb
*/
uint8_t process_code()
{
  CodeOperator op = ops[op_index].op;
  uint8_t mask = masks[mask_index].mask;

  uint8_t processed_code = 0;
  Serial.println(code);

  //Perform operation using mask
  switch (op)
  {
    case NOT:
      Serial.println("NOT");
      processed_code  = ~code;// | mask;
      break;
    case AND:
      Serial.println("AND");
      processed_code  = code & mask;
      break;
    case OR:
      Serial.println("OR");
      processed_code  = code | mask;
      break;
    case XOR:
      Serial.println("XOR");
      processed_code  = code ^ mask;
      break;
    default:
      Serial.println("Err in processing code");
  }
  Serial.println(mask, BIN);
  Serial.println(processed_code);
  return processed_code;
}

void defuse()
{
  bmode = TIMEBASED;
  beep_count = 0;
  topbuff[0] = '\0';
  digitalWrite(BUZZ_PIN, LOW);
  strip.setPixelColor(0, strip.Color(masks[mask_index].r, masks[mask_index].g, masks[mask_index].b));
  strip.setPixelColor(1, strip.Color(ops[op_index].r, ops[op_index].g, ops[op_index].b));
  strip.show();


  time_limit = DEFUSE_TIME;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("MOTION DISABLED");

  delay(1000);

  lcd.clear();
  uint8_t pos = 0;

  while (1) {
    if (time_limit < 0)
    {
      boom(DEFUSEFAIL);
    }
    if (timer_tick)
    {
      do_time();
    }
    char key = keypad.getKey();

    if (key == '*')
    {
      topbuff[pos] = '\0';
      long entered_code = atol(topbuff);
      if (entered_code > 255)
      {
        Serial.println("Value larger than uint8_t");
      }


      if ((long)solution == entered_code)
      {
        Serial.println("Welldone");
        boom_success();
      }
      else
      {
        Serial.println("Woops");
        Serial.println(solution);
        Serial.println(entered_code);
        lcd.clear();
        lcd.setCursor(0,1);
        lcd.println("     WRONG!     ");
        pos = 0;
        topbuff[pos] = '\0';
        delay(2000);
      }
      do_time();
      continue;
    }

    if (key == '#')
    {
      pos = 0;
      topbuff[pos] = '\0';
      do_time();
      continue;
    }

    if (key) {
      topbuff[pos] = key;
      pos++;
      topbuff[pos] = '\0';

      if (pos >= 10)
      {
        pos = 0;
      }
      do_time();
    }

  }
}

void boom_success()
{
  bmode = STOP;
  setLedsToColor(0, 255, 0);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("  -=YOU WIN!=-");
  lcd.setCursor(0, 1);
  lcd.print(" -=WELL DONE!=-");

  while (1);

  //TODO: something fun here? rgb lights and a tune?
}

void boom(BoomReason br)
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("    -=BOOM=-");
  lcd.setCursor(0, 1);
  lcd.print("  GAME IS OVER");
  BeepMode prev_bmode = bmode;
  bmode = STOP;
  digitalWrite(BUZZ_PIN, HIGH);


  setLedsToColor(255, 0, 0);
  boolean booming = true;
  while (booming)
  {
    keypad.getKeys();

    if ( (time_limit > 0 ) && (br == MOTION) && (keypad.isPressed('#')) ) {
      bmode = prev_bmode;
      digitalWrite(BUZZ_PIN, LOW);
      setLedsToColor(0, 0, 0);
      get_calibration();
      break;
    }
    if (keypad.isPressed('#'))
    {
      digitalWrite(BUZZ_PIN, LOW);
    }

  }
}

void check_settings(boolean change)
{

  //Read set time in minutes and sensitivity from eeprom
  time_limit = EEPROM.read(EEPROM_TIME_ADDR);
  sensitivity = EEPROM.read(EEPROM_SENSITIVITY_ADDR);


  if (change)
  {

    Serial.println("Settings Menu");

    Serial.println(time_limit);
    Serial.println(sensitivity);
    uint8_t ptl = 0;
    uint8_t psn = 0;

    uint8_t sn = sensitivity;
    uint8_t tl = time_limit;

    while (!keypad.isPressed('#'))
    {
      keypad.getKeys();

      if (keypad.isPressed('1'))
      {
        tl--;
      }
      if (keypad.isPressed('3'))
      {
        tl++;
      }
      if (keypad.isPressed('4'))
      {
        sn--;
      }
      if (keypad.isPressed('6'))
      {
        sn++;
      }

      if ( (ptl != tl) || (psn != sn))  {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Time:");
        lcd.print(tl);
        lcd.print("mins");
        lcd.setCursor(0, 1);
        lcd.print("Sensivity:");
        lcd.print(sn);
      }
      ptl = tl;
      psn = sn;
      delay(50);
    }
    Serial.println("New Settings");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Saved");
    EEPROM.write(EEPROM_TIME_ADDR, tl);
    EEPROM.write(EEPROM_SENSITIVITY_ADDR, sn);
    time_limit = tl;
    sensitivity = sn;
    delay(1000);
  }

  time_limit = time_limit * 60; //scale to seconds
  sensitivity  = sensitivity * 10; //scale up to better range
}

void get_calibration()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Calibrating...");
  lcd.setCursor(0, 1);
  lcd.print("Keep Still");
  delay(1000);
  for (int i = 0; i < 50; i++)
  {
    if ( imu.accelAvailable() )
    {
      imu.readAccel();
      base_x = ( 0.5f * base_x ) + (0.5f * imu.ax);
      base_y = ( 0.5f * base_y ) + (0.5f * imu.ay);
      base_z = ( 0.5f * base_z ) + (0.5f * imu.az);
    }
    delay(20);
  }

  Serial.println(base_x);
  Serial.println(base_y);
  Serial.println(base_z);
}

static void setLedsToColor(uint8_t r, uint8_t g, uint8_t b)
{

  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  strip.show();
}

static inline void init_timer1()
{
  //Set counter to 0
  TCNT1 = 0;

  //Set to CTC mode, 64 prescaler
  TCCR1A = 0x00;
  TCCR1B = _BV(WGM12) | _BV(CS11) | _BV(CS10);

  //16,000,000 MHz / 64 = 2500,00
  //Set output compare for 1/10 second interrupt this is scaled to 1Hz by the interrupt handler
  OCR1A = 2500;//15625;
  //This means we can do things (like beep annoyingly) at 4/5Hz


  //turn on output compare 1 A interrupt
  TIMSK1 |= _BV(OCIE1A);

  //Make sure interrupts are on
  sei();
}


ISR(TIMER1_COMPA_vect)
{
  static uint8_t time_speed = 100;
  time_speed--;
  if (time_speed == 0)
  {
    time_speed = 100;
    timer_tick = true;

    time_limit--;

    //This is the regular beep method
    if (bmode == NORMAL) {
      //If it is time to beep
      if (beep_count == 0) {
        //If we are already beeping, stop and reset
        if (beeping) {
          beep_count = beeps[beep_index].interval - 1;
          beeping = false;
          digitalWrite(BUZZ_PIN, LOW);
        }
        else {//otherwise start beeping
          beeping = true;
          digitalWrite(BUZZ_PIN, HIGH);
        }
      }
      else {
        beep_count--;
      }
    }
  }

  if (bmode == TIMEBASED) {
    if (beep_count == 0) {
      //If we are already beeping, stop and reset
      if (beeping) {
        long working_time = time_limit;
        if (working_time >= DEFUSE_TIME) {
          working_time = DEFUSE_TIME;
        }

        beep_count = map(working_time, DEFUSE_TIME, 0, 100, 0);

        beeping = false;
        digitalWrite(BUZZ_PIN, LOW);
      }
      else {//otherwise start beeping
        beeping = true;
        digitalWrite(BUZZ_PIN, HIGH);
      }
    }
    else {
      beep_count--;
    }
  }
}

static void do_time()
{
  timer_tick = false;

  lcd.clear();
  lcd.setCursor(0, 1);
  lcd.print(topbuff);
  lcd.setCursor(6, 0);
  if ((time_limit / 60) < 10)
  {
    lcd.print("0");
  }
  lcd.print((time_limit / 60));
  lcd.print(":");
  if ((time_limit % 60) < 10)
  {
    lcd.print("0");
  }
  lcd.print((time_limit % 60));

}

