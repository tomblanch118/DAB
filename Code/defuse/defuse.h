/**
 * Project: Defuse a Bomb Teambuilding Activity
 * Author: Tom Blanchard 
 * Date: 10/10/2017
 * Version: 1
 * Revisions:
 */
 
enum CodeOperator {NOT, AND, OR, XOR};
enum BoomReason {TIME, MOTION, DEFUSEFAIL};
enum BeepMode {NORMAL, TIMEBASED,STOP};

typedef struct beep_profile
{
  uint8_t interval;
  char defuse_key;
};

typedef struct mask_profile
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t mask;
};

typedef struct op_profile
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
  CodeOperator op;
};

//TODO:time beeps and make sure the timings are good
beep_profile beeps[] =
{
  {10, '*'},
  {15, '7'},
  {20, '0'},
  {25, '3'}
};

mask_profile masks[] =
{
  {255, 0, 0, 0xAA},
  {0, 255, 0, 0x55},
  {0, 0, 255, 0xF8},
  {200, 0, 200, 0x1B}
};

op_profile ops[] =
{
  //{255, 0, 0, NOT},
  {0, 255, 0, AND},
  {0, 0, 255, OR},
  {200, 0, 200, XOR}
};

const byte ROWS = 4; //four rows
const byte COLS = 3; //three columns
char keys[ROWS][COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};
