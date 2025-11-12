/* ----------------------------------------------------------------------------
remote control for OpenSoar/XCSoar, emulates a keyboard and mouse
hardware is just 3 pushbuttons connected between pins of an Arduino Leonardo
and GND and a joy stick button for arrow keys or mouse action and Enter

Button layout Stefly Remote (4 button type with joy stick) as shown on 
https://www.stefly.aero/product/stefly-leather-remote-stick (deeper on this side)
additional Speed to Fly switch or button between Arduino pin 7 and GND 
at the back side of the stick
PTT button for radio isn't connected to the Arduino

the code is started wit the OpenVario script 'Stick_Remote_Control_20170429.ino'
available at http://ftp.openvario.org/Stick_Remote_Control_20170429.ino (see:
https://www.openvario.org/doku.php?id=projects:remote_00:top)


the sketch uses libraries from
* https://github.com/thomasfredericks/Bounce2  - part of PushButton
* https://github.com/r89m/Button               - part of PushButton
* https://github.com/r89m/PushButton 
-----------------------------------------------------------------------------*/
// possible version types: VERSION_OLD (= v1.0), VERSION_NEW (= v2.0) or VERSION_EXT (= v2.1)
#define VERSION_OLD 0  // equivalent to the previous version 'Stick_Remote_Control_br'
                       // the differce is the use of Arduino IDE 2, and other libraries
                       // and a complete different code flow, so I expect a better 
                       // debounce behavior of the keys here
                       // Version: v1.0
#define VERSION_NEW 1  // previous version with added watchdog only and 
                       // (WatchDog-)Reset at long pressed Enter button (joy
                       // stick button) and with diagonal movement of the mouse
                       // and the activation of the serial interface
                       // Version: v2.0
#define VERSION_EXT 2  // extended version with WatchDog and additional key 'Q' (= quit)
                       // on long pressed X button (> 1sec,  < 4sec), 'X' (= shutdown) on very
                       // long pressed X button (> 4sec), 'E' (= btn_design) on long pressed.
                       // (> 1sec) Fn button
                       // Mouse moves at an increased speed after ~0.8 sec
                       // Version: v2.1
#define VERSION_SHIFT 3  // extended version with shift key 
                       // Version: v3.0
// -----------------------------------------------------------
#define VERSION_TYPE    VERSION_SHIFT  // OLD, NEW or EXT
// -----------------------------------------------------------
#include <Mouse.h>
#include <Keyboard.h>
#include <PushButton.h>

#if VERSION_TYPE >= VERSION_NEW
# include "avr/wdt.h"
# define WITH_WATCHDOG
// # define WITH_SERIAL
// # define EXTEND_SERIAL
#endif

#if VERSION_TYPE >= VERSION_EXT
# define MOUSE_ACCELERATOR
#endif

// disable the next define if you have a BUTTON instead a TOGGLE SWITCH: 
#ifdef VERSION_SHIFT
bool ShiftPressed = false;
unsigned long ShiftPressedTime = 0;
unsigned long DblClickTime = 0;
#endif  
#define USE_STF_TOGGLE

const char* Device  = "Stick Remote Control (SteFly)";
const char* Date    = __DATE__;  // = Compile Date: "2025-01-24";
#if VERSION_TYPE  == VERSION_OLD
  const char* Version = "v1.0";
#elif VERSION_TYPE  == VERSION_NEW
  const char* Version = "v2.0";
#elif VERSION_TYPE  == VERSION_EXT
  const char* Version = "v2.1";
#elif VERSION_TYPE  == VERSION_SHIFT
  const char* Version = "v3.0";
#else
  const char* Version = "unknown";
#endif

// check Linux delimiter and/or Window delimiter: 
const char* FileName  = (strrchr(__FILE__, '/') ?                     \
            strrchr(__FILE__, '/') + 1 : (strrchr(__FILE__, '\\') ?   \
            strrchr(__FILE__, '\\') + 1 : __FILE__));

// define timing for buttons etc.
const int joy_rebounce_interval = 3;
const int button_hold_threshold = 900;
const int enter_hold_threshold  = 2000;
const int button_long_hold      = 5000;  // = 5 seconds
const int joy_hold_threshold    = 80;
const int dblclick_threshold    = 500;
const unsigned long start_delay = 500;
const unsigned int start_wait   = 1000;  // 2000;

const int MouseDistance = 4;     // mouse one step (-> mouse speed and accurancy)
const int mouse_speed_switch = 800;   // faster mouse after this time (in ms)
//                             KEY, MOUSE
const int joy_repeats[][2] = {{  5,   0},     // 1st reaction
                              {150, 120},     // pause after 1st press
#ifdef MOUSE_ACCELERATOR
                              { 20,   4},     // repeat interval (long press < 800ms)
                              { 20,   1}};    // accelerated repeat interval (long press > 800ms)
#else 
                              { 20,   4}};     // repeat interval (long press < 800ms)
#endif                              

// Create instances of PushButtons on digital pins
enum {
       btnEnter,
       btnFn, btnMenu, btnX, btnPTT,
       joyUp, joyDown, joyLeft, joyRight,
       btnSTF,  btnSize
};

// define variables
boolean mouse_active = false;
int first_pressed = 0;
int joy_active = 0;
int key_bounce = 0;
int joy_counter[btnSize]         = {0,0,0,0,0,0,0,0,0,0};
int repeat_mode[btnSize]         = {0,0,0,0,0,0,0,0,0,0};
unsigned long btn_timer[btnSize] = {0,0,0,0,0,0,0,0,0,0};
int x_repeat = 0;

unsigned long timeNow = 0;
#ifdef  WITH_WATCHDOG
unsigned long timeWDT = 0;
#endif
enum {PIN, JOY_KEY, JOY_X, JOY_Y};
enum {KEY_PIN, KEY_PRESSED, KEY_LONG, KEY_VERYLONG};
const int x = MouseDistance;
const int y = MouseDistance;

#if defined(WITH_SERIAL) && defined(EXTEND_SERIAL)
const unsigned int read_buffer_length = 0x400;
int read_buffer_pointer = 0;
int read_buffer_start = -1;
char read_buffer[read_buffer_length];
#endif  // Serial
/* -----------------------------------------------------------------
 define press (short press) and hold (long press) functions for each button
 settings in XCSoar default.xci are
 F1 QuickMenu
 F2 Analysis
 F3 Checklist
 F4 FlarmTraffic
 F5 GotoLookup (Select Waypoint)
 F6 Setup Alternates
 F7 Setup Task
 F8 Setup Basic (wingload,bugs, QNH)
 F9
    Status all
    Calculator (tasks)

 settings in openvario.xci
 V for vario mode
 S for speed to fly mode
 M for vario menu (audio)
 Q for quit
 X for shutdown
 E for btn_design (Pilot Event or something else)
------------------------------------------------------------------*/
const int btn_design[btnSize][4] = {
   // Pin,short pressed    >= 1 Second  very long (not used up to now) 
#if VERSION_TYPE  ==  VERSION_EXT
    { 6, KEY_RETURN,      0,           0},         // btnEnter: joystick button
    { 9, 0,               'E',         0},         // btnFn: mouse switch (and 'P')
    {14, KEY_F1,          'M',         0},         // btnMenu: QuickMenu and AudioMenue
    {15, KEY_ESC,         'Q',         'X'},       // btnX: ESC and 'Q'uit, or Shutdown
    { -1 },                                        // btnPTT - not connected here
#elif VERSION_TYPE  ==  VERSION_SHIFT
    { 6, KEY_RETURN,      0,           0},         // btnEnter: joystick button
    { 9, KEY_F5,          KEY_F6,      KEY_F9},    // btnFn: F5 (Goto) and F6 (Alternative) key
    {14, KEY_F1,          'M',         0},         // btnMenu: QuickMenu and AudioMenue
    {15, KEY_ESC,         'Q',         'X'},       // btnX: ESC and 'Q'uit, or Shutdown
    { -1 },                                        // btnPTT - not connected here
#else
    { 6, KEY_RETURN,      0,           0},         // btnEnter: joystick button
    { 9, 0,               0,           0},         // btnFn: mouse switch (and 'P')
    {14, KEY_F1,          'M',         0},         // btnMenu: QuickMenu and AudioMenue
    {15, KEY_ESC,         0,           0},         // btnX: ESC and 'Q'uit, or Shutdown
    { -1 },                                        // btnPTT - not connected here
#endif

//   Pin,JOY_KEY           JOY_X        JOY_Y
    { 2, KEY_UP_ARROW,    0,           -y},        // joyUp
    { 4, KEY_DOWN_ARROW,  0,           +y},        // joyDown
    { 5, KEY_LEFT_ARROW,  -x,          0},         // joyLeft
    { 3, KEY_RIGHT_ARROW, +x,          0},         // joyRight

  //  Pin,Switch On        Switch Off
    { 7, 'V',             'S',         0}          // btnSTF
};
char STF_key = 'V'; 
const uint8_t pullup = ENABLE_INTERNAL_PULLUP;
PushButton button[btnSize] = {
    {btn_design[btnEnter][PIN], pullup}, // joystick button
    {btn_design[btnFn]   [PIN], pullup}, // mouse switch (and 'P')
    {btn_design[btnMenu] [PIN], pullup}, // QuickMenu and AudioMenue
    {btn_design[btnX]    [PIN], pullup}, // ESC and 'Q'uit
    {btn_design[btnPTT]  [PIN], pullup}, // PTT -> not connected
    {btn_design[joyUp]   [PIN], pullup}, //
    {btn_design[joyDown] [PIN], pullup}, //
    {btn_design[joyLeft] [PIN], pullup}, //
    {btn_design[joyRight][PIN], pullup}, //
    {btn_design[btnSTF]  [PIN], pullup}  // Vario <-> Speed to Fly (STF)
};

#ifdef  WITH_WATCHDOG
void watchdogInit() {                                         
        timeWDT = timeNow = millis();
        wdt_reset();
        WDTCSR |= (1 << WDCE) | (1 << WDE);  // set relevant bits to enter watchdog timer configuration mode
        WDTCSR = (1<< WDE) | WDTO_2S;        // turn on WatchDog timer
}
void wdtReset() {
    timeNow = millis();
    if (timeNow - timeWDT >= 450) {  // 2 in 1 second
        timeWDT = timeNow;
        wdt_reset();
    }
}
#endif  // WITH_WATCHDOG

bool startBit = false;
unsigned long startTime = 0;


// ============================================= 
// ============================================= 
// ============================================= 
uint8_t
NMEAChecksum(const char *src, int len) noexcept
{
  uint8_t checksum = 0;
  char *p = src;

  /* skip the dollar sign at the beginning (the exclamation mark is
     used by CAI302 */
  if (p && *p && (*p == '$' || *p == '!'))
    { p++; len--;}  // remove_prefix(1);

  // for (char ch : src)
  while (*p)  //  && (len-- > 0))
    checksum ^= static_cast<uint8_t>(*p++);

  return checksum;
}

bool
VerifyNMEAChecksum(const char *p)
{
  //assert(p != nullptr);
  if (!*p)
    return false;

  const char *asterisk = strrchr(p, '*');
  if (asterisk == nullptr)
    return false;

  const char *checksum_string = asterisk + 1;
  char *endptr;
  unsigned long ReadCheckSum2 = strtoul(checksum_string, &endptr, 16);
  if (endptr == checksum_string || *endptr != 0 || ReadCheckSum2 >= 0x100)
    return false;

  uint8_t ReadCheckSum = (unsigned char)ReadCheckSum2;
  uint8_t CalcCheckSum = NMEAChecksum(p, asterisk - p);

  return CalcCheckSum == ReadCheckSum;
}

void
AppendNMEAChecksum(char *p)
{
  //assert(p != nullptr);
  if (!*p)
    return false;

  const size_t length = strlen(p);
  sprintf(p + length, "*%02X", NMEAChecksum(p, length));
}

void sendNMEA(const char* line) {
    char buffer[0x100];
   // PSRCI,R: (S)tick(R)emote(C)ontrol(I)nfo, (R)emote
    strncpy(buffer, "$PSRCI,R,",  0x100);
    strncat(buffer, line,  0x100);
    AppendNMEAChecksum(buffer);
    Serial.println(buffer);
}

void sendKey(const unsigned char key) {
    char buffer[0x100];

    Keyboard.write(key);
   // PSRCI,R: (S)tick(R)emote(C)ontrol(K)ey, (R)emote
    strncpy(buffer, "$PSRCK,R,",  0x100);
    int len = strlen(buffer); 
    switch(key) {
      case KEY_F1: strncat(buffer, "F1", 0x100); break;
      case KEY_F2: strncat(buffer, "F2", 0x100); break;
      case KEY_F3: strncat(buffer, "F3", 0x100); break;
      case KEY_F4: strncat(buffer, "F4", 0x100); break;
      case KEY_F5: strncat(buffer, "F5", 0x100); break;
      case KEY_F6: strncat(buffer, "F6", 0x100); break;
      case KEY_RETURN: strncat(buffer, "Return", 0x100); break;
      case KEY_ESC: strncat(buffer, "Esc", 0x100); break;
      case KEY_UP_ARROW: strncat(buffer, "Up", 0x100); break;
      case KEY_DOWN_ARROW: strncat(buffer, "Down", 0x100); break;
      case KEY_LEFT_ARROW: strncat(buffer, "Left", 0x100); break;
      case KEY_RIGHT_ARROW: strncat(buffer, "Right", 0x100); break;

      default: { 
        if (isalnum(key)) strncat(buffer, &key,  1);
        else *buffer = 0;
      }
      break;
    }
    if (*buffer) {
      AppendNMEAChecksum(buffer);
      Serial.println(buffer);
    }
}
// ============================================= 
// ============================================= 
// ============================================= 

void setup() {
#ifdef WITH_SERIAL
  Serial.begin(9600);
#endif  // WITH_SERIAL

// ============================================= 
// ============================================= 
// ============================================= 
  // wait until button 'Enter' is released
  // pinMode( btn_design[btnEnter][PIN], INPUT); 
  pinMode( btn_design[btnEnter][PIN], INPUT_PULLUP); 
/* */
  while (!digitalRead(btn_design[btnEnter][PIN])) { // = isPressed
    wdtReset();
    delay(200);
  }

  button[btnEnter].update();
  
/*  */

// ============================================= 
// ============================================= 
// ============================================= 
#ifdef  WITH_WATCHDOG
  watchdogInit();
#endif  

  // btnEnter:
/*/
  button[btnEnter].onRelease(0,enter_hold_threshold-1,onEnter);
  button[btnEnter].onRelease(enter_hold_threshold,onReset);
 /*/
  button[btnEnter].onRelease(0,enter_hold_threshold-1,onEnter);
  button[btnEnter].onHold(enter_hold_threshold, onReset);
/**/  
  // joy stick arrow button
  for (int i=joyUp;i<=joyRight;i++) {
     button[i].onHoldRepeat(joy_hold_threshold, joy_rebounce_interval, onJoyArrow);
     button[i].onRelease(onJoyRelease);
     joy_counter[i] = 0;
  }

  // btnFn
#if VERSION_TYPE  ==  VERSION_EXT
  button[btnFn].onRelease(0,button_hold_threshold-1, onMouseMode);
  button[btnFn].onHold(button_hold_threshold, onButtonLong);
#elif VERSION_TYPE  ==  VERSION_SHIFT
  button[btnFn].onRelease(0,button_hold_threshold-1, onButtonReleased);
  button[btnFn].onHold(button_hold_threshold, onButtonLong);
#else
  button[btnFn].onRelease(onMouseMode);
#endif

  // btnMenu
  button[btnMenu].onRelease(0,button_hold_threshold-1,onButtonReleased);
  button[btnMenu].onHold(button_hold_threshold, onButtonLong);

  // btnX with Short, Long and VeryLong press
#if VERSION_TYPE  >=  VERSION_EXT
  button[btnX].onHoldRepeat(80, 10, onXRepeat);
#endif
  button[btnX].onRelease(onXRelease);

  // btnSTF
  button[btnSTF].onPress(onSTF_switch);
#ifdef USE_STF_TOGGLE
  button[btnSTF].onRelease(onSTF_switch);
#endif

  Keyboard.begin();
  Mouse.begin();

  startTime = millis(); // overflow after ~50 days
}

unsigned long test_time = 0;

void loop() {
  unsigned long time = millis();
  if (!startBit && (time-startTime > start_wait)) {
    startBit = true;
#ifdef WITH_SERIAL
    // Serial.println("$PSRDI,R,Start");
    sendNMEA("Start");
    // /* TEST */ sendKey('Z');
#endif  // WITH_SERIAL
  } else {
    for (int i=0;i<btnSize;i++)
      button[i].update();
  }

#if VERSION_TYPE > VERSION_OLD
  wdtReset();
  /*
  if (time - test_time > 1000) {
    test_time = time;
    Serial.println(Date);
  }
  */
#endif

#ifdef WITH_SERIAL

  char c = Serial.read();
  if (isprint(c)) {

    char buffer[0x100];
#if defined(EXTEND_SERIAL)
  read_buffer[read_buffer_pointer++] = c;
  read_buffer_pointer %= read_buffer_length;

  switch (c) {  // one char allowed only
    case '\n': {
       int i = 0;
       if ((read_buffer_start >= 0) &&
        read_buffer[read_buffer_start] == '$')
        for (int p=read_buffer_start; p!=read_buffer_pointer; ) {
           buffer[i++] = read_buffer[p++];
          p %= read_buffer_length;
        }
        if (i>0 && i < 0x100) {
          buffer[i] == 0;
          buffer[i-3] == 0; /* TEST!!!*/     
          sendNMEA(buffer+6);
        }
        read_buffer_start = -1;
    }
    break; 
    case '$': read_buffer_start = read_buffer_pointer > 0 ?
                  read_buffer_pointer -1 : read_buffer_length-1;
    break;
    default:  break;
  } 

  if (read_buffer_start >= 0)
    return; 

#endif  // defined(EXTEND_SERIAL)

// char buffer[0x100];
buffer[0] = 0;
  switch (c) {  // one char allowed only
    case 'v': sprintf(buffer, "Version,%s", Version);break;
    case 'd': sprintf(buffer, "Device,%s", Device);break;
    case 'u': sprintf(buffer, "Date,%s", Date);break;
    case 'f': sprintf(buffer, "Filename,%s", FileName);break;
    default: 
             // if (isprint(c)) sprintf(buffer, "Key,%c", c);
          break;
  } 
  if (*buffer) 
    sendNMEA(buffer);

  }
#endif  // WITH_SERIAL
}

void onBtnPress(PushButton& btn) {
  int index = &btn - button;
  btn_timer[index] = millis();
}
void onEnter(PushButton& btn){
    if (mouse_active) 
      Mouse.click(MOUSE_LEFT);
    else {
      if (btn_design[0][KEY_PRESSED]) {
        // sendKey(btn_design[0][KEY_PRESSED]);
        sendKey(btn_design[0][KEY_PRESSED]);
      }
    }
}

#if VERSION_TYPE  !=  VERSION_SHIFT
void onMouseMode(PushButton& btn){
    mouse_active = !mouse_active;
}
#endif

void onButtonReleased(PushButton& btn){
  int index = &btn - button;
  if (btn_design[index][KEY_PRESSED]) {
    sendKey(btn_design[index][KEY_PRESSED]);
    
  }
}

void onReset(PushButton& btn){
  if (btn_design[0][KEY_LONG]) {
    sendKey(btn_design[0][KEY_LONG]);
  }
  if (!button[btnSTF].isPressed()) {
    if (millis()-startTime > start_delay) {
#ifdef WITH_SERIAL
    sendNMEA("Reset");
    // /* TEST */ sendKey('R');
#endif  // WITH_SERIAL
    delay(100);
    exit(1);
  }
  }
} 

void onBtnHeldTopLeft(PushButton& btn){
  int index = &btn - button;
  sendKey(btn_design[index][KEY_LONG]);
}

void onButtonLong(PushButton& btn){
  int index = &btn - button;
  if (btn_design[index][KEY_LONG]) {
    sendKey(btn_design[index][KEY_LONG]);
  }
}
 
void onButtonVeryLong(PushButton& btn){
  int index = &btn - button;
  if (btn_design[index][KEY_VERYLONG]) {
    sendKey(btn_design[index][KEY_VERYLONG]);
  } else if (btn_design[index][KEY_LONG]) {
    sendKey(btn_design[index][KEY_LONG]);
  }
} 

void onJoyArrow(PushButton& btn) { //
  int index = &btn - button;
#if VERSION_TYPE == VERSION_OLD
  // without diagonal movement of the mouse like the old sketch
  // 'Stick_Remote_Control_br.ino'
  for (int i=joyUp;i<=joyRight;i++) {
     if (index != i)
       if (btn_timer[i] > 0) // an other direction is used
           return;
  }
#endif  // VERSION_TYPE == VERSION_OLD
  if (!button[btnEnter].isPressed()) {
  unsigned current_time = millis();
  bool first = !(first_pressed & (1 << index));
  // first_pressed |= (1 << index) && (current_time - btn_timer[index] > 150);
  first_pressed |= (1 << index);
    if (first) {
      btn_timer[index] = current_time;
      repeat_mode[index] = 0;
      key_bounce = joy_repeats[0][mouse_active];  // immediately / smooth
      joy_counter[index] = 0;
    }
    if (joy_counter[index] >= key_bounce) {
      if (mouse_active) {
        Mouse.move(btn_design[index][JOY_X], btn_design[index][JOY_Y]);
      } else { // mouse not active
        sendKey(btn_design[index][JOY_KEY]);
      }
      switch(repeat_mode[index]) {
        case 0: 
        case 1: repeat_mode[index]++;break; 
#ifdef MOUSE_ACCELERATOR
        case 2: if (current_time - btn_timer[index] > mouse_speed_switch)
                   repeat_mode[index]++;break; 
#endif                              
        default: break;
      }
      key_bounce = joy_repeats[repeat_mode[index]][mouse_active];
      joy_counter[index] = 0;
    } else { 
      joy_counter[index]++;
    }
  } else {  // Btn Enter is pressed!
  }
}

void onJoyRelease(PushButton& btn){
  int index = &btn - button;
  joy_counter[index] = 0;
  first_pressed &= ~(1 << index);
  btn_timer[index] = 0; //  millis();
}

#if VERSION_TYPE  >=  VERSION_EXT
void onXRepeat(PushButton& btn){
  
  if (!x_repeat++)
      btn_timer[btnX] = millis();
}
#endif  // VERSION_TYPE  ==  VERSION_EXT

void onXRelease(PushButton& btn) {
#if  VERSION_TYPE  >=  VERSION_EXT
  unsigned long release_time = millis() - btn_timer[btnX];
  char key = 0;
  if (x_repeat < 3 ) 
      key = 0;
  if (x_repeat < 100)  // ca. 1sec
      key = btn_design[btnX][KEY_PRESSED];
  else if (x_repeat < 400)   // ca. 4sec
     key = btn_design[btnX][KEY_LONG]; 
  else // x_repeat >= 200)
     key = btn_design[btnX][KEY_VERYLONG]; 
#else
  char key = btn_design[btnX][KEY_PRESSED];
#endif
  if (key) {
    sendKey(key); 
  }
  x_repeat = 0;
}

void onSTF_switch(PushButton& btn) {

#ifdef VERSION_SHIFT
  unsigned long time = millis();
  int ShiftTime = (ShiftPressedTime > 0) ? time - ShiftPressedTime :
    dblclick_threshold + 1;
 
  bool released = ShiftTime >= dblclick_threshold;
  ShiftPressed = button[btnSTF].isPressed();
  mouse_active = ShiftPressed;
  if (!ShiftPressed) {
      // /* TEST */ sendKey('1');
    return;
  }
  if (released) {
    ShiftPressedTime = time;
    // /* TEST */ sendKey('x');
    // /* TEST */ Serial.println(ShiftPressedTime);
    return;
  }   
   /* No DblClick inside 1 second*/
   if (time-DblClickTime < 1000) {
      // /* TEST */ Serial.println("No DblClick");
      return;
   } 
   DblClickTime = time;
  // /* TEST */ sendKey('d');
  // /* TEST */ Serial.println("DblClick");
 #endif

#ifdef USE_STF_TOGGLE
  // use a BUTTON to switch between Vario or Speed To Fly mode
  if (STF_key == btn_design[btnSTF][KEY_LONG]) 
#else
  // use a SWITCH to enable Vario or Speed To Fly mode
  if (button[btnSTF].isPressed()) 
#endif
    STF_key = (btn_design[btnSTF][KEY_PRESSED]);
  else
    STF_key = (btn_design[btnSTF][KEY_LONG]); 
  sendKey(STF_key);
}
