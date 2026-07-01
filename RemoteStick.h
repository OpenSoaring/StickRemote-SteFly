#pragma once

struct sKey {
  uint8_t ext;
  uint8_t key;
};

#define KEY_STANDARD        0
#define KEY_EXTEND          1

#define MOUSE_SWITCH        1
#define APP_SWITCH          2
#define APP_SWITCH2         3
#define APP_HIDE            4
#define TOOGLE_STF_VARIO    5
#define STICK_RESET         6

#define KEY_MOUSE_SWITCH {KEY_EXTEND, MOUSE_SWITCH}
#define KEY_APP_SWITCH   {KEY_EXTEND, APP_SWITCH}
#define KEY_APP_SWITCH2  {KEY_EXTEND, APP_SWITCH2}
#define KEY_APP_HIDE     {KEY_EXTEND, APP_HIDE}
#define KEY_TOGGLE_S_V   {KEY_EXTEND, TOOGLE_STF_VARIO}
#define KEY_STICK_RESET  {KEY_EXTEND, STICK_RESET}

struct key_struct {
    sKey Click;
    sKey LongClick;
    sKey DblClick;   
};

uint8_t *
key_code(sKey key) {
    static uint8_t buffer[8];
    if (key.ext) {
      snprintf_P(buffer, sizeof(buffer), PSTR("{%02X-%02X}"), key.ext, key.key );
    } else switch(key.key) {
      case KEY_F1: strncpy_P(buffer, PSTR("F1"), sizeof(buffer)); break;
      case KEY_F2: strncpy_P(buffer, PSTR("F2"), sizeof(buffer)); break;
      case KEY_F3: strncpy_P(buffer, PSTR("F3"), sizeof(buffer)); break;
      case KEY_F4: strncpy_P(buffer, PSTR("F4"), sizeof(buffer)); break;
      case KEY_F5: strncpy_P(buffer, PSTR("F5"), sizeof(buffer)); break;
      case KEY_F6: strncpy_P(buffer, PSTR("F6"), sizeof(buffer)); break;
      case KEY_F7: strncpy_P(buffer, PSTR("F7"), sizeof(buffer)); break;
      case KEY_F8: strncpy_P(buffer, PSTR("F8"), sizeof(buffer)); break;
      case KEY_F9: strncpy_P(buffer, PSTR("F9"), sizeof(buffer)); break;
      case KEY_F10: strncpy_P(buffer, PSTR("F10"), sizeof(buffer)); break;
      case KEY_F11: strncpy_P(buffer, PSTR("F11"), sizeof(buffer)); break;
      case KEY_F12: strncpy_P(buffer, PSTR("F12"), sizeof(buffer)); break;
      case KEY_RETURN: strncpy_P(buffer, PSTR("Return"), sizeof(buffer)); break;
      case KEY_ESC: strncpy_P(buffer, PSTR("Esc"), sizeof(buffer)); break;
      case KEY_UP_ARROW: strncpy_P(buffer, PSTR("Up"), sizeof(buffer)); break;
      case KEY_DOWN_ARROW: strncpy_P(buffer, PSTR("Down"), sizeof(buffer)); break;
      case KEY_LEFT_ARROW: strncpy_P(buffer, PSTR("Left"), sizeof(buffer)); break;
      case KEY_RIGHT_ARROW: strncpy_P(buffer, PSTR("Right"), sizeof(buffer)); break;
    default:
      if (isalnum(key.key)) {
        buffer[0] = key.key;
        buffer[1] = 0;
      }
      else
        //return nullptr;
        strncpy_P(buffer, PSTR("(null)"), sizeof(buffer)); 
      break;
    }
    return buffer;
}

// Anzahl Tasten und Slots pro Eintrag aus deinem Sketch
// #define NUM_KEYS    10

// Create instances of PushButtons on digital pins
enum {
       btnFn, btnMenu, btnX, btnSTF,
       btnEnter,
       joyUp, joyDown, joyLeft, joyRight,
       btnPTT,
       btnSize
};

// --- Flash-residente Layouts ------------------------------------------------
#include "layouts/basic-layout.h"
#include "layouts/advanced-layout.h"
#include "layouts/android-layout.h"
#include "layouts/android2-layout.h"
#include "layouts/starterkey-layout.h"
#include "layouts/starterkey2-layout.h"
#include "layouts/test-layout.h"

// Pointer-Tabelle to alle layouts (Pointer itself also in flash)
const PROGMEM void* const all_layouts[] = {
    layout_basic,
    layout_advanced,   // w/ some dbl clicks and slow click behavior
    layout_android,    // w/o dbl click
    layout_android2,   // w/ dbl click
    layout_starterkey, // w/o dbl click
    layout_starterkey2,// w/ dbl click
    layout_test,       // test layout
};
const uint8_t NUM_LAYOUTS = sizeof(all_layouts) / sizeof(all_layouts[0]);

// --- RAM working copy -------------------------------------------------------
// Both 'btn_design' and 'active_layout_index' are read/written by the mainloop
// and by the EEPROM persistence layer in RemoteEEprom.h. Include order matters:
// RemoteStick.h must be included BEFORE RemoteEEprom.h in the main .ino.
key_struct btn_design[btnSize];

uint8_t active_layout_index = 0;

// Layout loading is handled by RemoteEEprom.h:
//   loadLayoutFromEEPROM()        - boot-time restore from EEPROM
//   loadPresetFromFlash(idx)      - select a flash preset and persist
//   writeLayoutToEEPROM(idx)      - persist current btn_design
//   updateKeySlot(btn, slot, ...) - modify a single key
//   sendKeySlot(btn, slot, ...)   - ask for a single key
//   sendLayoutHealth()            - NMEA $PSRCH,R diagnostic dump
