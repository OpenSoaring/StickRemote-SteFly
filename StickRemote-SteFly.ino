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

// examle seral communications:
// * $POPSL,N - select flash preset N as active layout (0 - 2)
// * $POPSK,1,2,0,67 - update btnMenu, DblClick, 0, 'C'
// * $POPSH - request layout health dump
// * $POPSQ,    readPOPSQ(read_buffer + 7);
//    * "Version"  sendVersion();  
//    * "Device"   sendDevice();  
//    * "FileDate" sendFileDate();  
//    * "FileName" sendFileName();  
//    * "Time"     sendTime();  
//    * "Reboot"   resetCmd();  

the sketch uses libraries from
* https://github.com/thomasfredericks/Bounce2  - part of PushButton
* https://github.com/r89m/Button               - part of PushButton
* https://github.com/r89m/PushButton 
-----------------------------------------------------------------------------*/
// possible version types: VERSION_OLD (= v1.0), VERSION_2 (= v2.0) or VERSION_2_1 (= v2.1)
#define VERSION_OLD 0  // equivalent to the previous version 'Stick_Remote_Control_br'
                       // the differce is the use of Arduino IDE 2, and other libraries
                       // and a complete different code flow, so I expect a better 
                       // debounce behavior of the keys here
                       // Version: v1.0
#define VERSION_2   20000  // previous version with added watchdog only and 
                       // (WatchDog-)Reset at long pressed Enter button (joy
                       // stick button) and with diagonal movement of the mouse
                       // and the activation of the serial interface
                       // Version: v2.0
#define VERSION_2_1 20100  // extended version with WatchDog and additional key 'Q' (= quit)
                       // on long pressed X button (> 1sec,  < 4sec), 'X' (= shutdown) on very
                       // long pressed X button (> 4sec), 'E' (= btn_design) on long pressed.
                       // (> 1sec) Fn button
                       // Mouse moves at an increased speed after ~0.8 sec
                       // Version: v2.1
#define VERSION_3   30000  // extended version with shift key, double click, serial communication
                       // + USB-Recovery (level A+B) 
                       // watchdog send 0
                       //  - early WDT-disable in .init3 (Caterina-Falle umgehen)
                       //  - WDT click w/o Keyboard.write(0) (no USB-Traffic necessary)
                       //  - MCUSR analysis + Reset-Log in EEPROM + NMEA-Bootmsg
                       //  - Soft Re-Enumeration via USBDevice.detach/attach
                       //    at USB-Suspend or Send-Timeout (no WDT-Reset necessary)
                       //  - exit(1) replaced w/ clean wdt-Reset
#define VERSION_3_1 30100  // Version: v3.1
#define VERSION_3_2 30200  // Version: v3.2
// -----------------------------------------------------------
#define VERSION_TYPE    VERSION_3
// -----------------------------------------------------------
#include <Mouse.h>
#include <Keyboard.h>
#include <PushButton.h>
#include <EEPROM.h>
#include "RemoteStick.h"
#include "RemoteEEprom.h"   // dual-slot EEPROM persistence (uses btn_design + all_layouts)

// Placement-new operator. Needed because the r89m/PushButton library does
// not expose Bounce::attach(), so we rebind buttons to their pins by
// re-constructing the PushButton object in-place at runtime (after the
// EEPROM-stored pin assignment is loaded in setup()).
inline void* operator new(size_t, void* ptr) noexcept { return ptr; }

#if VERSION_TYPE >= VERSION_2
# include "avr/wdt.h"
# define WITH_WATCHDOG
# define WITH_SERIAL
# define RESTART_VERSION
#endif

#if VERSION_TYPE >= VERSION_3
# define WITHOUT_MOUSE
# define EXTEND_SERIAL
# define WAIT_FOR_ENTER
# define WITH_DBLCLICK
#endif

#if VERSION_TYPE >= VERSION_3   // (Claude changes)
// USB-Recovery-Paket. Wirkt zusammen mit (modifiziertem) Caterina-Bootloader,
// laeuft aber auch mit Original-Caterina - dann nur eben mit 8s Wartezeit nach
// einem WDT-Reset (was wir mit WITH_USB_REATTACH ja gerade vermeiden wollen).
# define WITH_EARLY_WDT_DISABLE   // .init3 Hook: MCUSR=0; wdt_disable() vor setup()
# define WITH_RESET_LOG           // MCUSR + Zaehler ins EEPROM, NMEA-Bootmsg
# define WITH_USB_REATTACH        // sanftes Re-Enumerieren bei Suspend/Send-Stall
# define WITH_CLEAN_RESET         // exit(1) -> wdt_enable(WDTO_15MS); while(1){}
#endif

// disable the next define if you have a BUTTON instead a TOGGLE SWITCH: 
#define USE_STF_TOGGLE

const char* Device  = "Stick Remote Control (SteFly)";
const char* Date    = __DATE__;  // = Compile Date: "2025-01-24";
#if VERSION_TYPE  == VERSION_OLD
  const char* Version = "v1.0.1";
#elif VERSION_TYPE  == VERSION_2
  const char* Version = "v2.0.1";
#elif VERSION_TYPE  == VERSION_2_1
  const char* Version = "v2.1.1";
#elif VERSION_TYPE  == VERSION_3
  const char *Version = "v3.0";
#elif VERSION_TYPE  == VERSION_3_1
  const char *Version = "v3.1";
#elif VERSION_TYPE  == VERSION_3_2
  const char *Version = "v3.2";
#else
  const char* Version = "unknown";
#endif

const char *VersionStr = "Version,";

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
const int dblclick_threshold    = 400;  // previous: 500;  // ms
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
unsigned long DblClickTime[btnSize] = {0,0,0,0,0,0,0,0,0,0};
#endif  

bool startBit = false;
unsigned long startTime = 0;
unsigned long time_now = 0;

#ifdef  WITH_WATCHDOG
unsigned long timeWDT = 0;
#endif

// const int x = MouseDistance;
// const int y = MouseDistance;

#if defined(WITH_SERIAL) 
  const int buffer_size = 0x40;
  char send_buffer[buffer_size];
  char nmea_buffer[buffer_size];
  char send_info[buffer_size];
# if defined(EXTEND_SERIAL)
    const unsigned int read_buffer_length = 0x100;
    int read_buffer_pointer = 0;
    char read_buffer[read_buffer_length];
# endif  // EXTEND_SERIAL
#endif  // WITH_SERIAL
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

#ifdef USE_STF_TOGGLE
char STF_key = btn_design[btnSTF].DblClick.key; 
#endif

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

#ifdef  WITH_WATCHDOG
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
#endif  // WITH_WATCHDOG

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

# ifdef WITH_SERIAL
    char buf[48];
    snprintf(buf, sizeof(buf), "Reattach,%s,n=%u", reason, (unsigned)reattachCount);
    sendNMEA('I', buf);
    Serial.flush();
# endif

    USBDevice.detach();
    // Wartezeit ohne den Mainloop zu verlieren: WDT mitkicken.
    for (uint8_t i = 0; i < 25; i++) {  // ~250 ms detach
#  ifdef WITH_WATCHDOG
        wdt_reset();
#  endif
        delay(10);
    }
    USBDevice.attach();
    // Bis zu ~300 ms warten, bis Host wieder enumeriert hat.
    for (uint8_t i = 0; i < 30; i++) {
#  ifdef WITH_WATCHDOG
        wdt_reset();
#  endif
        delay(10);
        if (USBDevice.configured()) break;
    }
}
#endif  // WITH_USB_REATTACH

// ============================================= 
// ============================================= 
// ============================================= 
#if defined(WITH_SERIAL) 
// ----------------------------------------------------------------------------
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

// ----------------------------------------------------------------------------
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

// ----------------------------------------------------------------------------
void
AppendNMEAChecksum(char *p)
{
  //assert(p != nullptr);
  if (!*p)
    return;

  const size_t length = strlen(p);
  sprintf(p + length, "*%02X", NMEAChecksum(p, length));
}

// ----------------------------------------------------------------------------
// Extended Key Functions:
// ----------------------------------------------------------------------------
void switchMouse() {
    mouse_active = !mouse_active;
    sprintf_P(send_buffer+strlen(send_buffer), mouse_active ? PSTR("Mouse") :PSTR("Arrow"));
}

// ----------------------------------------------------------------------------
void switchApp() {
       Keyboard.press(KEY_LEFT_ALT);
       delay(5);
       Keyboard.press(KEY_TAB);
       delay(20);
       Keyboard.release(KEY_TAB);
       for (int i=0;i<6;i++) {
#ifdef  WITH_WATCHDOG
         wdtReset();
#endif
         delay(100);
       }
       Keyboard.release(KEY_LEFT_ALT);
}

// ----------------------------------------------------------------------------
void hideApp() {
       Keyboard.press(KEY_LEFT_GUI);
       delay(5);
       Keyboard.press(KEY_RETURN);
       delay(20);
       Keyboard.release(KEY_RETURN);
       for (int i=0;i<2;i++) {
#ifdef  WITH_WATCHDOG
         wdtReset();
#endif
         delay(100);
       }
       Keyboard.release(KEY_LEFT_GUI);
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
void sendNMEA(const char set, const char* line) {
   // PSRC(%set%),R: (S)tick(R)emote(C)ontrol(%set%)field, (R)emote
    
    snprintf_P(nmea_buffer,buffer_size,PSTR("$PSRC%c,R,%s"), set, line);
    AppendNMEAChecksum(nmea_buffer);
    Serial.println(nmea_buffer);
}
#endif // defined(WITH_SERIAL) 

// ----------------------------------------------------------------------------
void sendKey(const sKey key, const unsigned int info=0) {

#ifdef WITH_USB_REATTACH
    // Falls Host die Schnittstelle suspendiert hat: erst Remote-Wakeup
    // anfordern, dann kurz warten. Wenn das nicht hilft UND der Zustand
    // ist bereits laenger stabil abnormal (Debounce gegen Spurious-Events,
    // z.B. durch angeschlossenes USBasp oder kurze EMV-Stoerungen) -> Soft-Reattach.
    if (USBDevice.isSuspended()) {
        bool woken = USBDevice.wakeupHost();
        for (uint8_t i = 0; i < 20 && USBDevice.isSuspended(); i++) {
# ifdef WITH_WATCHDOG
            wdt_reset();
# endif
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
          case MOUSE_SWITCH: switchMouse(); break;
          case APP_SWITCH:   switchApp(); break;
          case APP_HIDE:     hideApp(); break;
          default: break;
        }
      // keyEvent(key);
      // sprintf(send_buffer+strlen(send_buffer), "%02X.%02X", key.ext, key.key);
    } else {
      Keyboard.write(key.key);
#if defined(WITH_SERIAL)
     char *keycode = key_code(key);
     if (keycode)
       strcpy(send_buffer, keycode);       
     else
       send_buffer[0] = 0;
#endif // defined(WITH_SERIAL) 
    }
#if defined(WITH_SERIAL)
    if (info > 0) {
       sprintf(send_buffer+strlen(send_buffer)," (%dms)++++", info);
    }
    if (*send_buffer) {
      sendNMEA('K', send_buffer);
      send_buffer[0] = 0;
    }
#endif // defined(WITH_SERIAL2) 
}
// ============================================= 
// ============================================= 
// ============================================= 

// ----------------------------------------------------------------------------
void setup() {
#ifdef WITH_SERIAL
  Serial.begin(9600);
#endif  // WITH_SERIAL

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
#ifdef  WITH_WATCHDOG
  while (!digitalRead(active_pin_config[btnEnter])) { // = isPressed
    wdtReset();
    delay(200);
  }
#endif
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
  for (int i = 0;i <= btnEnter; i++) {
    button[i].onRelease(0,button_hold_threshold-1, onButtonClick);
    button[i].onHold(button_hold_threshold, onDblClick);
  }
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
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// SendCommands:
/*
      case 'v': snprintf_P(send_info, buffer_size, PSTR("Version,%s"), Version); break;
      case 'd': snprintf_P(send_info, buffer_size, PSTR("Device,%s"), Device);break;
      case 'u': snprintf_P(send_info, buffer_size, PSTR("FileDate,%s"), Date);break;
      case 'f': snprintf_P(send_info, buffer_size, PSTR("FileName,%s"), FileName);break;  
      case 't': snprintf_P(send_info, buffer_size, PSTR("systime,%u"), time_now);break;
*/
void sendVersion() {
    snprintf_P(send_info, buffer_size, PSTR("Version,%s"), Version);
    sendNMEA('I',send_info);
}
// ----------------------------------------------------------------------------
void sendDevice() {
    snprintf_P(send_info, buffer_size, PSTR("Device,%s"), Device);
    sendNMEA('I',send_info);
}
// ----------------------------------------------------------------------------
void sendFileDate() {
    snprintf_P(send_info, buffer_size, PSTR("FileDate,%s"), Date);
    sendNMEA('I',send_info);
}
// ----------------------------------------------------------------------------
void sendFileName() {
    snprintf_P(send_info, buffer_size, PSTR("FileName,%s"), FileName);
    sendNMEA('I',send_info);
}
// ----------------------------------------------------------------------------
void sendTime() {
    snprintf_P(send_info, buffer_size, PSTR("systime,%u"), time_now);
    sendNMEA('I',send_info);
}
// ----------------------------------------------------------------------------
void resetCmd() {
#ifdef WITH_SERIAL
    sendNMEA('I', "Reset(Serial)");
    Serial.flush();
# endif

    wdt_enable(WDTO_15MS); // much faster...
    while (1) { }
}
// ----------------------------------------------------------------------------
void readPOPSQ(const char* cmd) {
       if (!strcmp_P(cmd, PSTR("Version")))  sendVersion();  
  else if (!strcmp_P(cmd, PSTR("Device")))   sendDevice();  
  else if (!strcmp_P(cmd, PSTR("FileDate"))) sendFileDate();  
  else if (!strcmp_P(cmd, PSTR("FileName"))) sendFileName();  
  else if (!strcmp_P(cmd, PSTR("Time")))     sendTime();  
  else if (!strcmp_P(cmd, PSTR("Reboot")))   resetCmd();  
}
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
void loop() {
  time_now = millis();

#if defined(WITH_DBLCLICK)
  for (int index = 0; index <= btnEnter; index++) {
    int click_time = time_now - BtnClickTime[index];
    if (BtnClickTime[index] && (click_time > dblclick_threshold)) { //-> SingleClick!
       // Serial.println(time_now - BtnClickTime[index]);  /* TEST */
       BtnClickTime[index] = 0;
       if (btn_design[index].Click.key) { // [KEY_PRESSED]) {
         sendKey(btn_design[index].Click);
       }
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
# ifdef WITH_SERIAL
         sendNMEA('I',"Start");
         char serialNum[20];
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

#  ifdef WITH_RESET_LOG
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
#  endif  // WITH_RESET_LOG
# endif  // WITH_SERIAL
# ifdef  WITH_WATCHDOG
        // Enabling after start_wait time
        watchdogInit();
# endif // RESTART_VERSION
      }
  } else {
    for (int i=0;i<btnSize;i++)
      button[i].update();
  }
#endif

#ifdef  WITH_WATCHDOG
  wdtReset();
#endif

#ifdef WITH_USB_REATTACH
  // USB-Zustand beobachten: Stabilitaets-Debouncer und preemptive Recovery
  monitorUsbState();
#endif

#ifdef WITH_SERIAL
  int c = Serial.read();
  if (isprint(c) || (c=='\n')) {
#if defined(EXTEND_SERIAL)
  // KEEP IT SIMPLE! Without interrupt handler for serial input you don't need 
  // a ring buffer
  switch (c) {  // one char allowed only
    case '\r': return; // break;
    case '\n': {
        if ((read_buffer_pointer > 0) && read_buffer[0] == '$') {
          int l = read_buffer_pointer;
          //size_t l = strlen(read_buffer);
          read_buffer[l] = 0;
          if (read_buffer[l - 3] == '*') read_buffer[l - 3] = 0;
          if (!strncmp(read_buffer + 1, "POPSQ,", 6)) {
              readPOPSQ(read_buffer + 7);
             // sendNMEA('S', read_buffer + 7);
          }
          // $POPSL,N - select flash preset N as active layout
          else if (!strncmp(read_buffer + 1, "POPSL,", 6)) {
              uint8_t idx = atoi(read_buffer + 7);
              loadPresetFromFlash(idx);
              snprintf_P(send_info, buffer_size, PSTR("Preset loaded,%u"), idx);
              sendNMEA('I', send_info);
          }
          // $POPSK,btn,slot,ext,key - update single key entry (slot: 0=Click,1=LongClick,2=DblClick)
          // example: $POPSK,1,2,0,67 - update btMenu, DblClick, 0, 'C'
          else if (!strncmp(read_buffer + 1, "POPSK,", 6)) {
              uint8_t btn, slot, ext, key;
              if (sscanf(read_buffer + 7, "%hhu,%hhu,%hhu,%hhu",
                         &btn, &slot, &ext, &key) == 4) {
                  if (updateKeySlot(btn, slot, ext, key))
                      sendNMEA('I', "Key updated");
                  else
                      sendNMEA('I', "Invalid btn/slot index");
              } else {
                  sendNMEA('I', "POPSK parse error");
              }
          }
          // $POPSH - request layout health dump
          else if (!strncmp(read_buffer + 1, "POPSH", 5)) {
              sendLayoutHealth();
          }
          // $POPSPS,btn,pin - SERVICE command: assign a new pin to a button.
          // Triggers a clean reset so the new pin assignment takes effect.
          // Use sparingly - this is for hardware reconfiguration, not user use.
          else if (!strncmp(read_buffer + 1, "POPSPS,", 7)) {
              uint8_t btn, pin;
              if (sscanf(read_buffer + 8, "%hhu,%hhu", &btn, &pin) == 2
                  && btn < btnSize) {
                  uint8_t pins[btnSize];
                  if (!loadPinConfigFromEEPROM(pins)) {
                      memcpy_P(pins, default_pin_config, btnSize);
                  }
                  pins[btn] = pin;
                  savePinConfigToEEPROM(pins);
                  sendNMEA('I', "PinConfig updated, restarting");
                  Serial.flush();
                  wdt_enable(WDTO_15MS);
                  while (1) { }   // clean reset, new config takes effect on reboot
              } else {
                  sendNMEA('I', "POPSPS parse error or btn out of range");
              }
          }
          else
             Serial.println(read_buffer); /*TEST */
        } 
      }
      read_buffer_pointer = 0;
      return; // break;
    case '$': // starts the reading
          read_buffer[0] = (char) c;
          read_buffer_pointer = 1;
       break;
    default:
          if (read_buffer_pointer > 0)
              read_buffer[read_buffer_pointer++] = (char) c;
       break;
  }

  if (read_buffer_pointer != 0)
    return; 
#endif  // defined(EXTEND_SERIAL)
    switch (c | 0x20) {  // one char allowed only
      // case 'v': snprintf_P(send_info, buffer_size, PSTR("Version,%s"), Version); break;
      case 'v': sendVersion();return;
      case 'd': snprintf_P(send_info, buffer_size, PSTR("Device,%s"), Device);break;
      case 'u': snprintf_P(send_info, buffer_size, PSTR("FileDate,%s"), Date);break;
      case 'f': snprintf_P(send_info, buffer_size, PSTR("FileName,%s"), FileName);break;  
      case 't': snprintf_P(send_info, buffer_size, PSTR("systime,%u"), time_now);break;
      case '0':
      case '1':
      case '2':
      {
        char l = c-'0';
         loadPresetFromFlash(l);   // load + persist (was: loadLayout)
         snprintf_P(send_info, buffer_size, PSTR("keyLayout %d (%d): "), l,
                    sizeof(btn_design));
        strcat(send_info, key_code(btn_design[0].Click));
        strcat(send_info, "-");
        strcat(send_info, key_code(btn_design[0].LongClick));
        strcat(send_info, "-");
        strcat(send_info, key_code(btn_design[0].DblClick));
         // sendNMEA('X',send_info);
         // sprintf(send_info, "keyLayout, %c", c);
      }
      break;

      default:  
               // if (isprint(c)) snprintf_P(send_info, buffer_size, PSTR("Key,%c"), c);
               break; 
    } 
    if (*send_info) {
      sendNMEA('S',send_info);
    }
  }
#endif  // WITH_SERIAL
}

// ----------------------------------------------------------------------------
#ifdef WITH_DBLCLICK
bool DblClickTest(int index) {
  if (BtnClickTime[index] > 0) { //-> DoubleClick!
     int click_time = time_now - BtnClickTime[index];
     DblClickTime[index] = time_now;
     BtnClickTime[index] = 0;
     if (btn_design[index].DblClick.key)
       // On btnSTF this is 0!
       sendKey(btn_design[index].DblClick, click_time);
     return true;
     
  } else {
    if (time_now - DblClickTime[index] > 1000) { //
        BtnClickTime[index] = time_now;
    }
  }
  return false;
}
#endif

// ----------------------------------------------------------------------------
void onButtonClick(PushButton& btn){
#ifdef WITH_DBLCLICK
  DblClickTest(&btn - button);
  //int index = &btn - button;
#endif  // WITH_DBLCLICK
}

// ----------------------------------------------------------------------------
void onReset(PushButton& btn) {
#ifdef WITH_SERIAL
    sendNMEA('I', "Reset(Manually)");
#endif  // WITH_SERIAL

#ifdef  WITH_WATCHDOG
  while (!digitalRead(active_pin_config[btnEnter])) { // = isPressed
    wdtReset();
    delay(200);
  }
#endif

#ifdef WITH_CLEAN_RESET
    // Sauberer Reset: kurz WDT scharf machen, dann busy-loop. Kein cli()
    // wie bei exit(), USB-Detach-Sequenz im Bootloader laeuft normal.
# ifdef WITH_SERIAL
    Serial.flush();
# endif
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
void onDblClick(PushButton& btn){
  int index = &btn - button;
  if (index==btnEnter) {
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
