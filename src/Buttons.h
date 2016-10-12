// Key matrix
/*

  Rows are set to input_pullup and cols are pulled low one by one
*/
const byte ROWS = 5;
const byte COLS = 5; 

#ifdef BRAINS_FEB
  byte row_pins[ROWS] = {6,7,8,9,10}; //connect to the row pinouts of the keypad
  byte col_pins[COLS] = {20,2,3,4,5}; //connect to the column pinouts of the keypad
#endif
#ifdef BRAINS_AUG
  byte row_pins[ROWS] = {5,7,9,11,33}; 
  byte col_pins[COLS] = {4,6,8,10,12};
#endif
#ifdef BRAINS_SEP
  byte col_pins[COLS] = {2,14,7,8,6}; 
  byte row_pins[ROWS] = {10,13,11,12,28};
#endif
// Enumeration of the keys that are present
enum BUTTONS {
  DUMMY_KEY,
  KEYB_0,  KEYB_1,  KEYB_2,  KEYB_3,  KEYB_4,  KEYB_5,  KEYB_6,  KEYB_7,  KEYB_8,  KEYB_9,
  STEP_0,  STEP_1,  STEP_2,  STEP_3,  STEP_4,  STEP_5,  STEP_6,  STEP_7,
  OCT_DOWN, OCT_UP,
  DBL_SPEED, SEQ_RANDOM,
  SEQ_START
};

// // Key matrix hookup
char keys[ROWS][COLS] = {
  { DUMMY_KEY, STEP_7, DUMMY_KEY, STEP_0, DUMMY_KEY },
  { STEP_6,    STEP_5, SEQ_START, STEP_1, STEP_2 },
  { OCT_DOWN,  STEP_4, DUMMY_KEY, STEP_3, OCT_UP },
  { KEYB_0,    KEYB_1, KEYB_2,    KEYB_3, KEYB_4 },
  { KEYB_5,    KEYB_6, KEYB_7,    KEYB_8, KEYB_9 }
};
// char keys[ROWS][COLS] = {
//   { DUMMY_KEY, STEP_6, OCT_DOWN, KEYB_0, KEYB_5 },
//   { STEP_7, STEP_5, STEP_4, KEYB_1, KEYB_6 },
//   { DUMMY_KEY, SEQ_START, DUMMY_KEY, KEYB_2, KEYB_7 },
//   { STEP_0, STEP_1, STEP_3, KEYB_3, KEYB_8 },
//   { DUMMY_KEY, STEP_2, OCT_UP, KEYB_4, KEYB_9 }
// };

Keypad keypad = Keypad( makeKeymap(keys), row_pins, col_pins, ROWS, COLS );

void keys_init();


void keys_init() {
    keypad.setDebounceTime(15);
}