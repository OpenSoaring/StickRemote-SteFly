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
// Single version line - history of earlier versions (VERSION_OLD, VERSION_2,
// VERSION_2_1, VERSION_3, ...) and the feature additions per version are
// preserved in chronic.txt. The key-mapping split that used to motivate the
// VERSION_TYPE macro is now handled at runtime via the EEPROM-backed layout
// system, so no compile-time switching is needed anymore.
//
// Everything that is not specific to the joystick hardware - serial protocol,
// layout framework and EEPROM, watchdog, reset log, USB recovery,
// Click/LongClick/DblClick handling - lives in the SteFlyRemoteCore library,
// which the Rotary Panel uses as well. That keeps both devices behaving the
// same; this sketch adds the joystick and its own layouts.

#include <Mouse.h>
#include <Keyboard.h>
#include <PushButton.h>
#include <EEPROM.h>
// All ten layout rows are push buttons on the stick, the joystick contacts
// included, so REMOTE_PUSHBUTTON_COUNT keeps its default (btnSize).
#include <SteFlyRemoteCore.h>

const char* Device  = "Stick Remote Control (SteFly)";
const char* Date    = __DATE__;
const char* Version = "v3.0";

// check Linux delimiter and/or Window delimiter: 
const char* FileName  = (strrchr(__FILE__, '/') ?                     \
            strrchr(__FILE__, '/') + 1 : (strrchr(__FILE__, '\\') ?   \
            strrchr(__FILE__, '\\') + 1 : __FILE__));

// Written to a blank EEPROM on the first start.
const char DefaultSerialNumber[] = "SF-SRC:00002";  // Stick Remote Control

// Factory wiring of the stick, see RemoteEEprom.h (pin config block).
// Order: btnFn, btnMenu, btnX, btnSTF, btnEnter, joyUp, joyDown, joyLeft,
//        joyRight, btnPTT (0xFF = not connected)
const PROGMEM uint8_t default_pin_config[btnSize] = {
    9, 14, 15, 7, 6, 2, 4, 5, 3, 0xFF
};

// --- Layouts of the stick --------------------------------------------------
// The basic layout comes from the library and is the same on all SteFly
// devices; the others are tuned for the stick and may differ from those of
// the Rotary Panel. The index is what $PSRCS,S,Layout,N selects and what the
// EEPROM stores, so layout_basic stays first and new layouts are appended.
#include "layouts/advanced-layout.h"
#include "layouts/android-layout.h"
#include "layouts/android2-layout.h"
#include "layouts/starterkey-layout.h"
#include "layouts/starterkey2-layout.h"
#include "layouts/test-layout.h"

const PROGMEM void* const all_layouts[] = {
    layout_basic,      // from SteFlyRemoteCore, same on all devices
    layout_advanced,   // w/ some dbl clicks and slow click behavior
    layout_android,    // w/o dbl click
    layout_android2,   // w/ dbl click
    layout_starterkey, // w/o dbl click
    layout_starterkey2,// w/ dbl click
    layout_test,       // test layout
};
const uint8_t NUM_LAYOUTS = sizeof(all_layouts) / sizeof(all_layouts[0]);

// define timing for the joystick
const int joy_rebounce_interval = 3;
const int joy_hold_threshold    = 80;

const int mouse_speed_switch = 800;   // faster mouse after this time (in ms)
//                             KEY, MOUSE
const int joy_repeats[][2] = {{  5,   0},     // 1st reaction
                              {150, 120},     // pause after 1st press
                              { 20,   4}};     // repeat interval (long press < 800ms)

// joystick state
int joy_active = 0;
int key_bounce = 0;
int joy_counter[btnSize]         = {0,0,0,0,0,0,0,0,0,0};
int repeat_mode[btnSize]         = {0,0,0,0,0,0,0,0,0,0};
unsigned long btn_timer[btnSize] = {0,0,0,0,0,0,0,0,0,0};
bool first_pressed[btnSize]      = {0,0,0,0,0,0,0,0,0,0};
int x_repeat = 0;

// ----------------------------------------------------------------------------
void setup() {
  coreSetup();
}

// ----------------------------------------------------------------------------
void loop() {
  coreLoop();
}

// ----------------------------------------------------------------------------
// Called by coreSetup() after the push buttons are bound to their pins.
void remoteDeviceSetup() {
  // joy stick arrow button
  for (int i = joyUp;i <= joyRight; i++) {
     button[i].onHoldRepeat(joy_hold_threshold, joy_rebounce_interval, onJoyArrow);
     button[i].onRelease(onJoyRelease);
     joy_counter[i] = 0;
  }
}

// ----------------------------------------------------------------------------
// The joystick contacts are push buttons and are updated by coreLoop()
// together with the other buttons, so there is nothing extra to poll.
void remoteDeviceUpdate() {
}

// ----------------------------------------------------------------------------
void onJoyArrow(PushButton& btn) {
  int index = &btn - button;
  // Diagonal movement is always allowed (was disabled in the old v1.0
  // 'Stick_Remote_Control_br' code path - retired with the version split).
  if (!button[btnEnter].isPressed()) {
  // unsigned time_now = millis();
  bool first = !first_pressed[index];
  // first_pressed[index] = time_now - btn_timer[index] > 150;
  first_pressed[index] = true;
    if (first) {
      btn_timer[index] = time_now;
      repeat_mode[index] = 0;
      key_bounce = joy_repeats[0][mouse_active];  // immediately / smooth
      joy_counter[index] = 0;
    }
    if (joy_counter[index] >= key_bounce) {
      if (mouse_active) {
        Mouse.move(btn_design[index].JoyX.key, btn_design[index].JoyY.key);
      } else { // mouse not active
        sendKey(btn_design[index].JoyKey);
      }
      switch(repeat_mode[index]) {
        case 0: 
        case 1: repeat_mode[index]++;break; 
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

// ----------------------------------------------------------------------------
void onJoyRelease(PushButton& btn){
  int index = &btn - button;
  joy_counter[index] = 0;
  first_pressed[index] = false;
  btn_timer[index] = 0; //  millis();
}
// ----------------------------------------------------------------------------
