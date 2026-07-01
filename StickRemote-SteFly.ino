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

#include <Mouse.h>
#include <Keyboard.h>
#include <PushButton.h>
#include <EEPROM.h>
#include "RemoteStick.h"
#include "RemoteEEprom.h"        // dual-slot EEPROM persistence (uses btn_design + all_layouts)
#include "RemoteCommunication.h"
#include "avr/wdt.h"             // wdt_enable, wdt_disable, wdt_reset, WDTO_*

// Placement-new operator. Needed because the r89m/PushButton library does
// not expose Bounce::attach(), so we rebind buttons to their pins by
// re-constructing the PushButton object in-place at runtime (after the
// EEPROM-stored pin assignment is loaded in setup()).
inline void* operator new(size_t, void* ptr) noexcept { return ptr; }

// All feature flags are always active in v3.0:
#define RESTART_VERSION          // staggered boot path with serial 'Start' frame
#define WITHOUT_MOUSE            // no Mouse.* methods even if Mouse.h is included
#define WAIT_FOR_ENTER           // setup() waits for Enter to be released before continuing
#define WITH_DBLCLICK            // per-button DblClick handling
// USB recovery package - works with modified Caterina bootloader or stock one
// (the stock Caterina just adds an 8s wait on WDT reset, which WITH_USB_REATTACH
// avoids by re-enumerating via USBDevice.detach()/attach() instead).
#define WITH_EARLY_WDT_DISABLE   // .init3 hook: MCUSR=0; wdt_disable() before setup()
#define WITH_RESET_LOG           // MCUSR + reset counter logged to EEPROM, NMEA boot msg
#define WITH_USB_REATTACH        // soft re-enumerate on USB suspend / send stall
#define WITH_CLEAN_RESET         // exit(1) -> wdt_enable(WDTO_15MS); while(1){}

const char* Device  = "Stick Remote Control (SteFly)";
const char* Date    = __DATE__;
const char* Version = "v3.0";

// check Linux delimiter and/or Window delimiter: 
const char* FileName  = (strrchr(__FILE__, '/') ?                     \
            strrchr(__FILE__, '/') + 1 : (strrchr(__FILE__, '\\') ?   \
            strrchr(__FILE__, '\\') + 1 : __FILE__));

// define timing for buttons etc.
const int joy_rebounce_interval = 3;
const int button_hold_threshold = 900;
const int enter_hold_threshold  = 2000;
const int joy_hold_threshold    = 80;
const int dblclick_threshold    = 300;  // ms - shorter than 400 for snappier feel;
                                        // mechanical pushbuttons reliably do <300ms double-taps
const int strongclick_threshold = 200;  // ms - the 1st press time on a double click is very
                                        // short, after this threshold no double click test needed
const unsigned long start_delay = 500;  // ms
const unsigned int start_wait   = 2000;  // 2000;

const int mouse_speed_switch = 800;   // faster mouse after this time (in ms)
//                             KEY, MOUSE
const int joy_repeats[][2] = {{  5,   0},     // 1st reaction
                              {150, 120},     // pause after 1st press
                              { 20,   4}};     // repeat interval (long press < 800ms)

/*
// Create instances of PushButtons on digital pins
enum {
       btnFn, btnMenu, btnX, btnSTF,
       btnEnter,
       joyUp, joyDown, joyLeft, joyRight,
       btnPTT,
       btnSize
};
*/
// Pin assignment now lives in active_pin_config[] (declared in RemoteEEprom.h),
// loaded from EEPROM at boot (or from default_pin_config PROGMEM on first boot).
// The button[] array below is initialized with a harmless dummy pin (13 = LED_BUILTIN)
// and the real pins are attached in setup() after active_pin_config has been loaded.

// define variables
bool mouse_active = false;
int joy_active = 0;
int key_bounce = 0;
int joy_counter[btnSize]         = {0,0,0,0,0,0,0,0,0,0};
int repeat_mode[btnSize]         = {0,0,0,0,0,0,0,0,0,0};
unsigned long btn_timer[btnSize] = {0,0,0,0,0,0,0,0,0,0};
bool first_pressed[btnSize]      = {0,0,0,0,0,0,0,0,0,0};
int x_repeat = 0;

#ifdef WITH_DBLCLICK
unsigned long BtnClickTime[btnSize] = {0,0,0,0,0,0,0,0,0,0};
unsigned long BtnPressTime[btnSize] = {0,0,0,0,0,0,0,0,0,0};
unsigned long DblClickTime[btnSize] = {0,0,0,0,0,0,0,0,0,0};
#endif  

bool startBit = false;
unsigned long startTime = 0;
unsigned long time_now = 0;

unsigned long timeWDT = 0;

// const int x = MouseDistance;
// const int y = MouseDistance;

// Serial number lives here because setup() initializes it from EEPROM
// on first boot. The send buffers and read_buffer are defined inside
// RemoteCommunication.h (included at the top of this file).
char serialNum[20];
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
 V:       for vario mode
 S:       for speed to fly mode
 M:       for vario menu (audio)
 X:       for quit or shutdown
 1,2,3,4: for btn_design (Pilot Event or something else)
------------------------------------------------------------------*/

#define JoyKey Click
#define JoyX   LongClick
#define JoyY   DblClick

sKey STF_key = {0,'V'}; 

const uint8_t pullup = ENABLE_INTERNAL_PULLUP;
// Dummy pin 13 (= LED_BUILTIN) is harmless during the brief window between
// static initialization and the attach() calls in setup().
PushButton button[btnSize] = {
    {13, pullup}, {13, pullup}, {13, pullup}, {13, pullup}, {13, pullup},
    {13, pullup}, {13, pullup}, {13, pullup}, {13, pullup}, {13, pullup}
};

#ifdef WITH_EARLY_WDT_DISABLE
// ----------------------------------------------------------------------------
// Wird VOR setup() und VOR C++-Globalkonstruktion ausgefuehrt (Section .init3).
// Loescht MCUSR, deaktiviert den Watchdog sofort - damit kommt ein vorheriger
// WDT-Reset garantiert durch den Caterina-Bootloader. Auch wenn Caterina
// (unmodifiziert) anschliessend noch 8s wartet, kann er sich nicht erneut
// am WDT 'verschlucken'.
// mcusr_at_boot bleibt ueber den Reset hinweg verfuegbar (.noinit).
uint8_t mcusr_at_boot __attribute__((section(".noinit")));

void wdtEarlyDisable(void) __attribute__((naked, used, section(".init3")));
void wdtEarlyDisable(void) {
    mcusr_at_boot = MCUSR;
    MCUSR = 0;
    wdt_disable();
}
#endif  // WITH_EARLY_WDT_DISABLE

// ----------------------------------------------------------------------------
void watchdogInit() {
        timeWDT = time_now = millis();
        wdt_reset();
        WDTCSR |= (1 << WDCE) | (1 << WDE);  // set relevant bits to enter watchdog timer configuration mode
        WDTCSR = (1<< WDE) | WDTO_2S;        // turn on WatchDog timer
}
// ----------------------------------------------------------------------------
void wdtReset() {
    time_now = millis();  // unbedingt!
    if (time_now - timeWDT >= 450) {  // 2 in 1 second
        timeWDT = time_now;
        wdt_reset();
    }
}

#ifdef WITH_USB_REATTACH
// ----------------------------------------------------------------------------
// Anzahl bereits ausgefuehrter Soft-Reattaches seit Boot (NMEA-Diagnose).
static uint8_t       reattachCount       = 0;
// Zeitpunkt des letzten Soft-Reattach (Anti-Flapping).
static unsigned long lastReattachMillis  = 0;
// Minimal-Pause zwischen zwei Reattaches.
static const unsigned long REATTACH_MIN_INTERVAL = 5000;

// --- Stabilisierung (v3.0) ----------------------------------------------
// Zeitpunkt, an dem der USB-Zustand zum ersten Mal als 'abnormal' beobachtet
// wurde (isSuspended() oder !configured()). 0 = aktuell normal.
static unsigned long usbAbnormalSince    = 0;
// Mindestdauer, die der Abnormal-Zustand anhalten muss, bevor wir ein
// Reattach triggern. Schuetzt vor Spurious-Events durch elektrische Wackler
// (USBasp am ICSP-Header, ESD, EMV im Flug).
static const unsigned long USB_ABNORMAL_DEBOUNCE   = 500;
// Wenn der Stick laenger als so abnormal ist, *ohne* dass eine Taste
// gedrueckt wird, starten wir ein Reattach proaktiv aus dem Mainloop.
static const unsigned long USB_PREEMPTIVE_RECOVERY = 5000;

// Aktueller USB-Zustand 'verdaechtig'?
static inline bool usbAbnormalNow() {
    return USBDevice.isSuspended() || !USBDevice.configured();
}

// Hat der Abnormal-Zustand schon laenger als Debounce-Threshold angedauert?
static inline bool usbAbnormalStable() {
    return (usbAbnormalSince != 0) &&
           (time_now - usbAbnormalSince >= USB_ABNORMAL_DEBOUNCE);
}

// Wird im Mainloop einmal pro Iteration gerufen. Verfolgt den Zeitpunkt,
// an dem die Schnittstelle zum ersten Mal abnormal wurde, und triggert ein
// preemptives Reattach, wenn es zu lange so bleibt.
static void monitorUsbState() {
    if (usbAbnormalNow()) {
        if (usbAbnormalSince == 0) {
            usbAbnormalSince = time_now;
        } else if (time_now - usbAbnormalSince >= USB_PREEMPTIVE_RECOVERY) {
            softReattach("idle-recover");
            // softReattach hat eigenes Anti-Flapping (REATTACH_MIN_INTERVAL);
            // Marker zuruecksetzen, damit naechste Episode wieder
            // debounct gemessen wird.
            usbAbnormalSince = 0;
        }
    } else {
        usbAbnormalSince = 0;
    }
}

// Versucht ein sanftes USB-Re-Enumerate, ohne den Chip zurueckzusetzen.
// Vorteil gegenueber WDT-Reset: keine Bootloader-Phase, < 1s Recovery,
// laufender Zustand bleibt erhalten (Tastenstatus, EEPROM-Zaehler usw.).
void softReattach(const char *reason) {
    unsigned long now = millis();
    if (lastReattachMillis != 0 && (now - lastReattachMillis) < REATTACH_MIN_INTERVAL) {
        return;
    }
    lastReattachMillis = now;
    if (reattachCount < 255) reattachCount++;

    char buf[48];
    snprintf(buf, sizeof(buf), "Reattach,%s,n=%u", reason, (unsigned)reattachCount);
    sendNMEA('I', buf);
    Serial.flush();

    USBDevice.detach();
    // Wartezeit ohne den Mainloop zu verlieren: WDT mitkicken.
    for (uint8_t i = 0; i < 25; i++) {  // ~250 ms detach
        wdt_reset();
        delay(10);
    }
    USBDevice.attach();
    // Bis zu ~300 ms warten, bis Host wieder enumeriert hat.
    for (uint8_t i = 0; i < 30; i++) {
        wdt_reset();
        delay(10);
        if (USBDevice.configured()) break;
    }
}
#endif  // WITH_USB_REATTACH

// NMEA helpers (NMEAChecksum, VerifyNMEAChecksum, AppendNMEAChecksum,
// sendNMEA) live in RemoteCommunication.h - included at the top of this file.

// ----------------------------------------------------------------------------
void sendKey(const sKey key) {

#ifdef WITH_USB_REATTACH
    // Falls Host die Schnittstelle suspendiert hat: erst Remote-Wakeup
    // anfordern, dann kurz warten. Wenn das nicht hilft UND der Zustand
    // ist bereits laenger stabil abnormal (Debounce gegen Spurious-Events,
    // z.B. durch angeschlossenes USBasp oder kurze EMV-Stoerungen) -> Soft-Reattach.
    if (USBDevice.isSuspended()) {
        bool woken = USBDevice.wakeupHost();
        for (uint8_t i = 0; i < 20 && USBDevice.isSuspended(); i++) {
            wdt_reset();
            delay(5);
        }
        if (USBDevice.isSuspended() && usbAbnormalStable()) {
            softReattach(woken ? "wake-fail" : "no-wake");
        }
    } else if (!USBDevice.configured() && usbAbnormalStable()) {
        // Schnittstelle nicht konfiguriert UND laenger stabil so -> Reattach.
        softReattach("not-cfg");
    }
#endif  // WITH_USB_REATTACH
    if (key.ext) {
        switch (key.key) {
          case MOUSE_SWITCH:    switchMouse(); break;
          case APP_SWITCH:      switchApp(); break;
          case APP_SWITCH2:     switchApp_doubleTAB(); break;
          case APP_HIDE:        hideApp(); break;
          case TOOGLE_STF_VARIO:toggleSpeedToFly_Vario(); break;
          default: break;
        }
      // keyEvent(key);
      // sprintf(send_buffer+strlen(send_buffer), "%02X.%02X", key.ext, key.key);
    } else {
      Keyboard.write(key.key);
      char *keycode = key_code(key);
      if (keycode) strcpy(send_buffer, keycode);
      else         send_buffer[0] = 0;
    }
    if (*send_buffer) {
      sendNMEA('K', send_buffer);
      send_buffer[0] = 0;
    }
}

// ----------------------------------------------------------------------------
// Extended Key Functions:
// ----------------------------------------------------------------------------
void switchMouse() {
    mouse_active = !mouse_active;
    sprintf_P(send_buffer+strlen(send_buffer), mouse_active ? PSTR("Mouse") :PSTR("Arrow"));
}

// ----------------------------------------------------------------------------
void toggleSpeedToFly_Vario() {
  // use a SWITCH to enable Vario or Speed To Fly mode
  STF_key.key = (STF_key.key == 'V') ? 'S' : 'V';
  sendKey(STF_key);
}

// ----------------------------------------------------------------------------
// Switch to the previous app (Alt+Tab style).
//
// Cross-platform timing notes:
//   * Windows / Linux: switch happens immediately on ALT+TAB press,
//     ALT-release just confirms. Total time doesn't matter much.
//   * Android 13: switcher is shown while ALT is held, app is committed
//     on ALT-release. Total of ~300ms feels responsive.
//   * Android 11: requires ALT to be held BEFORE TAB is pressed for the
//     gesture to register as "app switch" (not "alt typed, then tab typed").
//     Pre-ALT hold of >=50ms greatly improves reliability there.
//
// IMPORTANT: this implementation sends a SINGLE TAB while ALT is held.
// On desktop OSes that moves one step in the MRU stack (= previous app).
// A double-TAB inside the same ALT-hold would move TWO steps on Windows/Linux
// (= switch to a different app than intended). Only on certain Android versions
// might rapid double-TAB stabilize the switch - not portable.
// If you want to experiment with double-TAB on Android-only devices, swap to
// switchApp_doubleTAB() defined below and observe behavior.
void switchApp() {
       Keyboard.press(KEY_LEFT_ALT);
       delay(60);                       // pre-hold: let Android 11 register the modifier
       Keyboard.press(KEY_TAB);
       delay(50);                       // TAB press: long enough to register on all platforms
       Keyboard.release(KEY_TAB);
       // Post-hold: keep ALT down so the switcher stays open while the
       // OS commits to the target app. WDT-kick during the wait.
       for (uint8_t i = 0; i < 50; i++) {
       // for (uint8_t i = 0; i < 28; i++) {
         wdt_reset();
         delay(10);
       }
       Keyboard.release(KEY_LEFT_ALT);
}

// ----------------------------------------------------------------------------
// EXPERIMENTAL variant: double-TAB inside the same ALT-hold.
// On Windows/Linux this switches to the THIRD most recent app (skips one).
// On some Android versions this gives a more reliable switch to the previous
// app than single-TAB. Untested across the full Android version matrix.
// Currently NOT wired up - to try it, call this instead of switchApp() in
// the APP_SWITCH dispatch (see line ~364).
void switchApp_doubleTAB() {
       Keyboard.press(KEY_LEFT_ALT);
       delay(60);
       Keyboard.press(KEY_TAB);
       delay(50);
       Keyboard.release(KEY_TAB);
       delay(100);                       // inter-TAB gap
       Keyboard.press(KEY_TAB);
       delay(50);
       Keyboard.release(KEY_TAB);
       for (uint8_t i = 0; i < 25; i++) {
         wdt_reset();
         delay(10);
       }
       Keyboard.release(KEY_LEFT_ALT);
}

// ----------------------------------------------------------------------------
// Hide / minimize the current app (WIN+ENTER style).
//
// Same timing strategy as switchApp(): pre-hold of the GUI modifier for
// 60ms so Android 11 reliably registers the modifier before ENTER comes,
// 50ms ENTER press for guaranteed key-down delivery, then 200ms post-hold
// to let the OS commit the action before the GUI key is released.
// Total ~310ms - faster than the previous ~225ms (which itself was already
// short, but the missing pre-hold caused intermittent failures on Android 11).
void hideApp() {
       Keyboard.press(KEY_LEFT_GUI);
       delay(60);                       // pre-hold: let Android 11 register the modifier
       Keyboard.press(KEY_RETURN);
       delay(50);                       // ENTER press: long enough on all platforms
       Keyboard.release(KEY_RETURN);
       for (uint8_t i = 0; i < 20; i++) {
         wdt_reset();
         delay(10);
       }
       Keyboard.release(KEY_LEFT_GUI);
}

// ----------------------------------------------------------------------------
// ============================================= 
// ============================================= 
// ============================================= 

// ----------------------------------------------------------------------------
void setup() {
  Serial.begin(9600);

  // Load pin assignment (hardware mapping) from EEPROM. On first boot or
  // corruption, falls back to factory default and persists it. Must happen
  // before attach() calls below and before WAIT_FOR_ENTER reads pin state.
  loadPinConfig();

  // Bind each PushButton to its actual MCU pin. Because PushButton has no
  // attach() method exposed, we use placement-new to re-construct the object
  // in-place with the correct pin. Unconnected buttons (pin 0xFF) keep their
  // dummy-pin object from static init - the mainloop ignores them anyway via
  // the layout's Click/LongClick/DblClick == 0 check.
  for (uint8_t i = 0; i < btnSize; i++) {
      if (active_pin_config[i] != 0xFF) {
          new (&button[i]) PushButton(active_pin_config[i], pullup);
      }
  }

  // Restore layout from EEPROM with dual-slot health-voting + self-heal.
  // If both slots are corrupt or never initialized, fall back to flash preset 0.
  if (!loadLayoutFromEEPROM()) {
    loadPresetFromFlash(0);
  }

#ifdef WAIT_FOR_ENTER
// =============================================
// =============================================
// =============================================
  // wait until button 'Enter' is released
  pinMode(active_pin_config[btnEnter], INPUT_PULLUP);
  while (!digitalRead(active_pin_config[btnEnter])) { // = isPressed
    wdtReset();
    delay(200);
  }
  button[btnEnter].update();
  
/*  */

// ============================================= 
// ============================================= 
// ============================================= 
#endif

/* TODO(August2111): This is a (bad) workaround for strcpy: the first cmd 
   in source code - not in calling behaviour - have a bit failure inside...:*/
  // strncpy_P(send_info, PSTR("zzzzzzzzzzzzzzzzzzzzzzzz"), buffer_size);
  memset(send_info, 'z', buffer_size);
  send_info[buffer_size-1] = 0;
  
  // normal key buttons:
  for (int i = 0;i < btnEnter; i++) {
    button[i].onRelease(0,button_hold_threshold-1, onButtonClick);
    button[i].onHold(button_hold_threshold, onLongClick);
  }
  // Enter has a different threshold:
  button[btnEnter].onRelease(0,enter_hold_threshold-1, onButtonClick);
  button[btnEnter].onHold(enter_hold_threshold, onLongClick);
  // joy stick arrow button
  for (int i = joyUp;i <= joyRight; i++) {
     button[i].onHoldRepeat(joy_hold_threshold, joy_rebounce_interval, onJoyArrow);
     button[i].onRelease(onJoyRelease);
     joy_counter[i] = 0;
  }

  Keyboard.begin();
  Mouse.begin();

  startTime = millis();  // overflow after ~50 days
}

// ----------------------------------------------------------------------------
// All send/dispatch functions (sendVersion, sendDevice, sendFileDate,
// sendFileName, sendTime, sendLayout, sendSerialNumber, sendSettingsBlock,
// sendInfoBlock, queryInfo, execCommand) live in RemoteCommunication.h -
// included at the top of this file. Only resetCmd() stays here since it is
// sketch-level behaviour (clean WDT-driven reboot).
// ----------------------------------------------------------------------------
void resetCmd() {
    sendNMEA('I', "Reset(Serial)");
    Serial.flush();
    wdt_enable(WDTO_15MS);    // clean reset via watchdog
    while (1) { }
}

// ----------------------------------------------------------------------------
void SingleClick(int index) {
  if (btn_design[index].Click.key) { // [KEY_PRESSED]) {
    if (mouse_active && index == btnEnter) {
      Mouse.release(MOUSE_LEFT);
      // Mouse.press(MOUSE_LEFT);
    } else
      sendKey(btn_design[index].Click);
  }
}
// ----------------------------------------------------------------------------
void loop() {
  time_now = millis();

#if defined(WITH_DBLCLICK)
  for (int index = 0; index <= btnEnter; index++) {
////////////////////////////////////////////////
    if  (!digitalRead(active_pin_config[index])  // = isPressed
    // if (button[index].isPressed()
        && (BtnPressTime[index] == 0))
        BtnPressTime[index] = time_now;
////////////////////////////////////////////////
    int click_time = time_now - BtnClickTime[index];
    if (BtnClickTime[index] && (click_time > dblclick_threshold)) { //-> SingleClick!
       // Serial.println(time_now - BtnClickTime[index]);  /* TEST */
       BtnClickTime[index] = 0;
       SingleClick(index);
    }
  }
#endif // defined(WITH_DBLCLICK)

#if !defined(RESTART_VERSION)
  for (int i=0;i<btnSize;i++)
    button[i].update();
#else // RESTART_VERSION
 
  if (!startBit) {
      if (time_now-startTime > start_wait) {
         startBit = true;
         sendNMEA('I',"Start");
         serialNum[0] = EEPROM.read(0);
         if (serialNum[0] == 'S') {
             // maybe a valid serial SteFly number
            EEPROM.get(0, serialNum);
         } else {
            strcpy(serialNum, "SF-SRC:00002");  // Stick Remote Control
            EEPROM.put(0, serialNum);
            Serial.println("Rite EEProm");
         }
         sendNMEA('I',serialNum);

# ifdef WITH_RESET_LOG
         // Reset-Statistik im EEPROM (Offset 64, outside serial number).
         // Layout: [64]='R' magic, [65..66]=uint16 resetCount, [67]=last MCUSR.
         {
           const int RL_BASE = 64;
           const uint8_t RL_MAGIC = 'R';
           uint16_t resetCount = 0;
           uint8_t  lastMcusr  = mcusr_at_boot;
           if (EEPROM.read(RL_BASE) != RL_MAGIC) {
             EEPROM.write(RL_BASE, RL_MAGIC);
             resetCount = 1;
             EEPROM.put(RL_BASE + 1, resetCount);
           } else {
             EEPROM.get(RL_BASE + 1, resetCount);
             if (resetCount < 0xFFFF) resetCount++;
             EEPROM.put(RL_BASE + 1, resetCount);
           }
           EEPROM.write(RL_BASE + 3, lastMcusr);

           const char *cause;
           if      (lastMcusr & (1<<WDRF))  cause = "WDT";
           else if (lastMcusr & (1<<BORF))  cause = "BOR";
           else if (lastMcusr & (1<<EXTRF)) cause = "EXT";
           else if (lastMcusr & (1<<PORF))  cause = "POR";
           else                             cause = "UNK";
           char buf[60];
           snprintf(buf, sizeof(buf),
                    "Reset=%s,MCUSR=0x%02X,n=%u",
                    cause, lastMcusr, (unsigned)resetCount);
           sendNMEA('I', buf);
         }
# endif  // WITH_RESET_LOG
        // Enabling after start_wait time
        watchdogInit();
      }
  } else {
    for (int i=0;i<btnSize;i++)
      button[i].update();
  }
#endif

  wdtReset();

#ifdef WITH_USB_REATTACH
  // USB-Zustand beobachten: Stabilitaets-Debouncer und preemptive Recovery
  monitorUsbState();
#endif

  // Stream-based NMEA reader + dispatcher lives in RemoteCommunication.h.
  // processSerialChar() accumulates chars into read_buffer and dispatches a
  // complete '$PSRC...' frame on newline via dispatchSerialCommand().
  //
  // The legacy single-char shortcuts (v, d, u, f, t, 0..2) have been retired -
  // same functionality is reachable via:
  //   v    -> $PSRCI,R,Version
  //   d    -> $PSRCI,R,Device
  //   u    -> $PSRCI,R,FileDate
  //   f    -> $PSRCI,R,FileName
  //   t    -> $PSRCI,R,Time
  //   0..2 -> $PSRCS,S,Layout,0..2
  int c = Serial.read();
  if (isprint(c) || c == '\n') {
    processSerialChar(c);
  }
}

// ----------------------------------------------------------------------------
#ifdef WITH_DBLCLICK
// Called on every button release. Decides:
//   a) If this is the second click within dblclick_threshold and the button
//      has a DblClick action configured -> fire DblClick now, suppress the
//      pending Click that was deferred from the first release.
//   b) If the button has NO DblClick action -> fire Click IMMEDIATELY,
//      no latency penalty. Most buttons fall into this category.
//   c) Otherwise: first click on a DblClick-capable button -> defer the
//      Click until the timeout in loop() decides "no second click coming".
bool DblClickTest(int index) {
////////////////////////////////////////////////
  if (BtnPressTime[index] != 0) {
    int press_time = time_now - BtnPressTime[index];
    BtnPressTime[index] = 0;  // reset time
    if (press_time > strongclick_threshold) {  // threshold for strong click
           SingleClick(index);
         /*
         char buf[48];
         snprintf_P(buf, sizeof(buf), PSTR("StrongClick: t=%u"), press_time);
         sendNMEA('I', buf);
         */
           BtnClickTime[index] = 0;
           return true;
    }
  }
////////////////////////////////////////////////
  if (BtnClickTime[index] > 0) {
     // --- (a) Second click within window -> DblClick ---
     int click_time = time_now - BtnClickTime[index];
     DblClickTime[index] = time_now;
     BtnClickTime[index] = 0;             // cancel the pending single-Click
     if (btn_design[index].DblClick.key)
       sendKey(btn_design[index].DblClick);
     return true;
  }

  // --- (b) No DblClick configured -> eager Click, no waiting ---
  if (btn_design[index].DblClick.key == 0) {
    if (btn_design[index].Click.key) {
      if (mouse_active && index == btnEnter) {
        Mouse.click(MOUSE_LEFT);
      } else {
        sendKey(btn_design[index].Click);
      }
    }
    DblClickTime[index] = time_now;       // bookkeeping, not used for trigger
    return false;
  }

  // --- (c) DblClick is configured -> defer the Click for now ---
  if (time_now - DblClickTime[index] > 1000) {
      BtnClickTime[index] = time_now;
      if (mouse_active && index == btnEnter) {
         Mouse.press(MOUSE_LEFT);
      }
  }
  return false;
}
#endif

// ----------------------------------------------------------------------------
void onButtonClick(PushButton& btn){

#ifdef WITH_DBLCLICK
  DblClickTest(&btn - button);
#endif  // WITH_DBLCLICK
}

// ----------------------------------------------------------------------------
void onReset(PushButton& btn) {
    sendNMEA('I', "Reset(Manually)");

  while (!digitalRead(active_pin_config[btnEnter])) { // = isPressed
    wdtReset();
    delay(200);
  }

#ifdef WITH_CLEAN_RESET
    // Sauberer Reset: kurz WDT scharf machen, dann busy-loop. Kein cli()
    // wie bei exit(), USB-Detach-Sequenz im Bootloader laeuft normal.
    Serial.flush();
    wdt_enable(WDTO_15MS); // much faster...
    while (1) { }
#else
    exit(1);
#endif
}

// ----------------------------------------------------------------------------
void onBtnHeldTopLeft(PushButton& btn){
  int index = &btn - button;
  sendKey(btn_design[index].LongClick);
}

// ----------------------------------------------------------------------------
void onLongClick(PushButton& btn){
  int index = &btn - button;
  BtnPressTime[index] = 0;
  if (index == btnEnter) {
    onReset(btn);
  } else {
    if (btn_design[index].LongClick.key) {
#ifdef WITH_DBLCLICK
      BtnClickTime[index] = 0;
#endif  // WITH_DBLCLICK
      sendKey(btn_design[index].LongClick);
    }
  }
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
