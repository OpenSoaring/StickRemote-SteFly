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
// possible version types: VERSION_OLD (= v1.0), VERSION_2 (= v2.0) or VERSION_2_1 (= v2.1)
#define VERSION_OLD 0  // equivalent to the previous version 'Stick_Remote_Control_br'
                       // the differce is the use of Arduino IDE 2, and other libraries
                       // and a complete different code flow, so I expect a better 
                       // debounce behavior of the keys here
                       // Version: v1.0
#define VERSION_2   1  // previous version with added watchdog only and 
                       // (WatchDog-)Reset at long pressed Enter button (joy
                       // stick button) and with diagonal movement of the mouse
                       // and the activation of the serial interface
                       // Version: v2.0
#define VERSION_2_1 2  // extended version with WatchDog and additional key 'Q' (= quit)
                       // on long pressed X button (> 1sec,  < 4sec), 'X' (= shutdown) on very
                       // long pressed X button (> 4sec), 'E' (= btn_design) on long pressed.
                       // (> 1sec) Fn button
                       // Mouse moves at an increased speed after ~0.8 sec
                       // Version: v2.1
#define VERSION_3   3  // extended version with shift key, double click, serial communication
                       // watchdog send 0
                       // Version: v3.0
#define VERSION_4   4  // V3 + USB-Recovery (Stufe A+B)
                       //  - Frueher WDT-disable in .init3 (Caterina-Falle umgehen)
                       //  - WDT-Kick ohne Keyboard.write(0) (kein USB-Traffic noetig)
                       //  - MCUSR-Auswertung + Reset-Log im EEPROM + NMEA-Bootmsg
                       //  - Soft Re-Enumeration via USBDevice.detach/attach
                       //    bei USB-Suspend oder Send-Timeout (kein WDT-Reset noetig)
                       //  - exit(1) ersetzt durch sauberen wdt-Reset
                       // Version: v4.0
// -----------------------------------------------------------
/* /
#define VERSION_TYPE    VERSION_2_1  // OLD, NEW or EXT or SHIFT
/*/
#define VERSION_TYPE    VERSION_4
/**/
// -----------------------------------------------------------
#include <Mouse.h>
#include <Keyboard.h>
#include <PushButton.h>
#include <EEPROM.h>

#if VERSION_TYPE >= VERSION_2
# include "avr/wdt.h"
# define WITH_WATCHDOG
# define WITH_SERIAL
# define RESTART_VERSION1
#endif

#if VERSION_TYPE == VERSION_2_1
# define MOUSE_ACCELERATOR
# define RESTART_VERSION1
#endif

#if VERSION_TYPE >= VERSION_3
# define MOUSE_ACCELERATOR
# define MOUSE_WITH_STF_KEY
# define WITH_SERIAL_2
# define EXTEND_SERIAL
# define WAIT_FOR_ENTER
# define RESTART_VERSION2
# define WITH_DBLCLICK
#endif

#if VERSION_TYPE >= VERSION_4
// USB-Recovery-Paket. Wirkt zusammen mit (modifiziertem) Caterina-Bootloader,
// laeuft aber auch mit Original-Caterina - dann nur eben mit 8s Wartezeit nach
// einem WDT-Reset (was wir mit WITH_USB_REATTACH ja gerade vermeiden wollen).
# define WITH_EARLY_WDT_DISABLE   // .init3 Hook: MCUSR=0; wdt_disable() vor setup()
# define WITHOUT_USB_KEEPALIVE    // wdtReset() schickt kein Keyboard.write(0) mehr
# define WITH_RESET_LOG           // MCUSR + Zaehler ins EEPROM, NMEA-Bootmsg
# define WITH_USB_REATTACH        // sanftes Re-Enumerieren bei Suspend/Send-Stall
# define WITH_CLEAN_RESET         // exit(1) -> wdt_enable(WDTO_15MS); while(1){}
# define DEBUG_KEYBOARD
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
#elif VERSION_TYPE  == VERSION_4
  const char *Version = "v4.1";
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
const int dblclick_threshold    = 500;
const unsigned long start_delay = 500;
const unsigned int start_wait   = 2000;  // 2000;

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
       btnFn, btnMenu, btnX, btnSTF, btnPTT,
       joyUp, joyDown, joyLeft, joyRight,
       btnSize
};

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

const int x = MouseDistance;
const int y = MouseDistance;

#if defined(WITH_SERIAL) 
  const int buffer_size = 0x80;
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
struct key_struct {
    const int Pin;
    const int Click;
    const int LongClick;
          int DblClick;   
};
#define JoyKey Click
#define JoyX   LongClick
#define JoyY   DblClick

// const 
struct key_struct btn_design[btnSize]/* [4] */ = {
   // Pin,short pressed    >= 1 Second  very long (not used up to now) 
#if defined(DEBUG_KEYBOARD)
    { 6, KEY_RETURN,     0,      'E'},       // btnEnter: joystick button
    { 9, 0,       KEY_F4,  KEY_F5},    // btnFn: Mouse/Kbr, F4, F5   
    {14, KEY_F1,  KEY_F2,  KEY_F3},    // btnMenu: F1,  F2,  F3
    {15, KEY_ESC, KEY_F11, KEY_F12},   // btnX:    ESC, F11, F12
    { 7, 0,               0,           'V'}, // btnSTF
#elif defined(WITH_DBLCLICK)  // Standard Button Layout + extra: 1, 2, 3, 4, 5, E, X, managed by [events].xci 
/* joy */  { 6, KEY_RETURN, 0,  'E'},       // btnEnter: joystick button
/* Fn  */  { 9, '1',      '2',  '3'},       // btnFn: F5 (Goto) and F6 (Alternative) key
/* M   */  {14, KEY_F1,   'M',  '4'},       // btnMenu: QuickMenu and AudioMenue
/* X   */  {15, KEY_ESC,  'X',  '5'},       // btnX: ESC and 'X' for Shutdown (or Quit)
/* STF */  { 7,  0,         0,  'V'},       // btnSTF
#else
    { 6, KEY_RETURN,      0,           0},         // btnEnter: joystick button
    { 9, 0,               0,           0},         // btnFn: mouse switch (and 'P')
    {14, KEY_F1,          'M',         0},         // btnMenu: QuickMenu and AudioMenue
    {15, KEY_ESC,         0,           0},         // btnX: ESC only 
    { 7, 'V',             'S',         0},         // btnSTF
#endif
    { -1 },   // btnPTT - not connected here
    { 2, KEY_UP_ARROW,    0,           -y},        // joyUp
    { 4, KEY_DOWN_ARROW,  0,           +y},        // joyDown
    { 5, KEY_LEFT_ARROW,  -x,          0},         // joyLeft
    { 3, KEY_RIGHT_ARROW, +x,          0},         // joyRight

};
#ifdef USE_STF_TOGGLE
char STF_key = btn_design[btnSTF].DblClick; 
#endif
const uint8_t pullup = ENABLE_INTERNAL_PULLUP;
PushButton button[btnSize] = {
    {btn_design[btnEnter].Pin, pullup}, // joystick button
    {btn_design[btnFn]   .Pin, pullup}, // mouse switch (and 'P')
    {btn_design[btnMenu] .Pin, pullup}, // QuickMenu and AudioMenue
    {btn_design[btnX]    .Pin, pullup}, // ESC and 'Q'uit
    {btn_design[btnSTF]  .Pin, pullup}, // Vario <-> Speed to Fly (STF)
    {btn_design[btnPTT]  .Pin, pullup}, // PTT -> not connected
    {btn_design[joyUp]   .Pin, pullup}, //
    {btn_design[joyDown] .Pin, pullup}, //
    {btn_design[joyLeft] .Pin, pullup}, //
    {btn_design[joyRight].Pin, pullup}  //
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
#ifndef WITHOUT_USB_KEEPALIVE
        Keyboard.write(0); // only a \0  (alter Keepalive, kann USB blockieren!)
#endif
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

// --- Stabilisierung (v4.1) ----------------------------------------------
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

# ifdef WITH_SERIAL_2
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
void sendNMEA(const char set, const char* line) {
   // PSRC(%set%),R: (S)tick(R)emote(C)ontrol(%set%)field, (R)emote
    snprintf(nmea_buffer,buffer_size,"$PSRC%c,R,%s", set, line);
    AppendNMEAChecksum(nmea_buffer);
    Serial.println(nmea_buffer);
}
#endif // defined(WITH_SERIAL) 

// ----------------------------------------------------------------------------
void sendKey(const unsigned int key) {
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
    Keyboard.write(key);
#if defined(WITH_SERIAL)
    switch(key) {
      case KEY_F1: strcpy(send_buffer, "F1"); break;
      case KEY_F2: strcpy(send_buffer, "F2"); break;
      case KEY_F3: strcpy(send_buffer, "F3"); break;
      case KEY_F4: strcpy(send_buffer, "F4"); break;
      // case KEY_F5: strcpy(send_buffer, "F5"); break;
      case KEY_F6: strcpy(send_buffer, "F6"); break;
      case KEY_RETURN: strcpy(send_buffer, "Return"); break;
      case KEY_ESC: strcpy(send_buffer, "Esc"); break;
      case KEY_UP_ARROW: strcpy(send_buffer, "Up"); break;
      case KEY_DOWN_ARROW: strcpy(send_buffer, "Down"); break;
      case KEY_LEFT_ARROW: strcpy(send_buffer, "Left"); break;
      case KEY_RIGHT_ARROW: strcpy(send_buffer, "Right"); break;

      default: { 
        if (isalnum(key)) {
            send_buffer[0] =(char)key;
            send_buffer[1] = 0;
        } else send_buffer[0] = 0;
      }
      break;
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

#ifdef WAIT_FOR_ENTER
// ============================================= 
// ============================================= 
// ============================================= 
  // wait until button 'Enter' is released
  // pinMode( btn_design[btnEnter][PIN], INPUT); 
  // pinMode( btn_design[btnEnter][PIN], INPUT_PULLUP); 
  pinMode( btn_design[btnEnter].Pin, INPUT_PULLUP); 
/* */
#ifdef  WITH_WATCHDOG
//  while (!digitalRead(btn_design[btnEnter][PIN])) { // = isPressed
  while (!digitalRead(btn_design[btnEnter].Pin)) { // = isPressed
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
  strcpy(send_info, "zzzzzzzzzzzzzzzzzzzzzzzz");
  
#ifdef  WITH_WATCHDOG
//  watchdogInit();
#endif  

  // btnEnter:
#if VERSION_TYPE  >=  VERSION_2_0
  button[btnEnter].onRelease(0,enter_hold_threshold-1, onEnter);
  button[btnEnter].onHold(enter_hold_threshold, onReset);
#else
  button[btnEnter].onRelease(0,enter_hold_threshold-1, onEnter);
  button[btnEnter].onRelease(enter_hold_threshold,onReset);
#endif

  // joy stick arrow button
  for (int i=joyUp;i<=joyRight;i++) {
     button[i].onHoldRepeat(joy_hold_threshold, joy_rebounce_interval, onJoyArrow);
     button[i].onRelease(onJoyRelease);
     joy_counter[i] = 0;
  }

  // btnFn
#if VERSION_TYPE  ==  VERSION_2_1
  button[btnFn].onRelease(0,button_hold_threshold-1, onMouseMode);
  button[btnFn].onHold(button_hold_threshold, onButtonLong);
#elif defined (MOUSE_WITH_STF_KEY)  // no mouse mode anymore here:
  button[btnFn].onRelease(0,button_hold_threshold-1, onButtonClick);
  button[btnFn].onHold(button_hold_threshold, onButtonLong);
#else
  button[btnFn].onRelease(onMouseMode);
#endif

  // btnMenu
  button[btnMenu].onRelease(0,button_hold_threshold-1,onButtonClick);
  button[btnMenu].onHold(button_hold_threshold, onButtonLong);

  // btnX with Short, Long and VeryLong press
#if VERSION_TYPE  >=  VERSION_2_1
  button[btnX].onRelease(0,button_hold_threshold-1,onButtonClick);
  button[btnX].onHold(button_hold_threshold, onButtonLong);
#else
  button[btnX].onRelease(onXRelease);
#endif

  // btnSTF
#ifdef USE_STF_TOGGLE
  button[btnSTF].onRelease(0,button_hold_threshold-1,onSTF_switch);
#else
//  button[btnSTF].onRelease(0,button_hold_threshold-1,onSTF_switch);
  button[btnSTF].onPress(onSTF_switch);
  button[btnSTF].onRelease(onSTF_switch);
#endif


  Keyboard.begin();
  Mouse.begin();

  startTime = millis(); // overflow after ~50 days
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
void loop() {
  time_now = millis();

#if defined(WITH_DBLCLICK)
  for (int index = btnEnter; index < btnPTT; index++) {
    if (BtnClickTime[index] && time_now - BtnClickTime[index] > 500) { //-> SingleClick!
       // Serial.println(time_now - BtnClickTime[index]);  /* TEST */
       BtnClickTime[index] = 0;
       if (btn_design[index].Click) { // [KEY_PRESSED]) {
         sendKey(btn_design[index].Click);
       }
    }
  }
#endif // defined(WITH_DBLCLICK)

#if !defined(RESTART_VERSION2)
// #if VERSION_TYPE  <  VERSION_3
  for (int i=0;i<btnSize;i++)
    button[i].update();
#else // RESTART_VERSION2
 
//  if (!startBit && (time_now-startTime > start_wait)) {
  if (!startBit) {
      if (time_now-startTime > start_wait) {
         startBit = true;
# ifdef WITH_SERIAL_2
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
         // Reset-Statistik im EEPROM (Offset 64, ausserhalb der Seriennummer).
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
           else                              cause = "UNK";
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
# endif // RESTART_VERSION2
      }
  } else {
    for (int i=0;i<btnSize;i++)
      button[i].update();
  }
#endif

# ifdef MOUSE_WITH_STF_KEY
    mouse_active =  button[btnSTF].isPressed();
# endif  // MOUSE_WITH_STF_KEY

// #if VERSION_TYPE > VERSION_OLD
#ifdef  WITH_WATCHDOG
  wdtReset();
#endif

#ifdef WITH_USB_REATTACH
  // USB-Zustand beobachten: Stabilitaets-Debouncer und preemptive Recovery
  monitorUsbState();
#endif

#ifdef WITH_SERIAL

  // send_info[0] = 0;
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
          if (!strncmp(read_buffer + 1, "POPSQ,", 6))
             sendNMEA('S', read_buffer + 7);
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
      case 'v': sprintf(send_info, "Version,%s", Version); break;
      case 'd': sprintf(send_info, "Device,%s", Device);break;
      case 'u': sprintf(send_info, "FileDate,%s", Date);break;
      case 'f': sprintf(send_info, "FileName,%s", FileName);break;
      case 't': sprintf(send_info, "systime,%u", time_now);break;
      default:  
               // if (isprint(c)) sprintf(send_info, "Key,%c", c);
               break; 
    } 
    if (*send_info) {
      sendNMEA('S',send_info);
    }
  }
#endif  // WITH_SERIAL
}

// ----------------------------------------------------------------------------
void onEnter(PushButton& btn) {
    if (mouse_active) 
      Mouse.click(MOUSE_LEFT);
    else {
      /*/
      DblClickTest(btnEnter);        
      /*/
      if (btn_design[0].Click) {
         // sendKey(btn_design[0].Click);
         sendKey(btn_design[0].Click);
      }
    }
}

#ifndef MOUSE_WITH_STF_KEY
void onMouseMode(PushButton& btn){
    mouse_active = !mouse_active;
}
#endif

#ifdef WITH_DBLCLICK
bool DblClickTest(int index) {
  if (BtnClickTime[index] > 0) { //-> DoubleClick!
     Serial.println(time_now - BtnClickTime[index]);  /* TEST */
     DblClickTime[index] = time_now;
     BtnClickTime[index] = 0;
     if (btn_design[index].DblClick)
       // On btnSTF this is 0!
       sendKey(btn_design[index].DblClick);
     return true;
     
  } else {
    if (time_now - DblClickTime[index] > 1000) { //
      //if (btn_design[index].Click) {
        // Serial.println(time_now - BtnClickTime[index]);
        BtnClickTime[index] = time_now;
        // test_time = time_now;
      // }
    }
  }
  return false;
}
#endif

// ----------------------------------------------------------------------------
void onButtonClick(PushButton& btn){
  DblClickTest(&btn - button);
  //int index = &btn - button;
}

// ----------------------------------------------------------------------------
void onReset(PushButton& btn) {
#ifdef WITH_SERIAL_2
    sendNMEA('I', "Reset");
#endif  // WITH_SERIAL
//  if (btn_design[0].LongClick) {
//    sendKey(btn_design[0].LongClick);
//  }
  // if (!button[btnSTF].isPressed()) {
  //  if (millis()-startTime > start_delay) {
//  while(
#ifdef  WITH_WATCHDOG
//  while (!digitalRead(btn_design[btnEnter][PIN])) { // = isPressed
  while (!digitalRead(btn_design[btnEnter].Pin)) { // = isPressed
    wdtReset();
    delay(200);
    // sendNMEA('I', "x");
  }
#endif

#ifdef WITH_CLEAN_RESET
    // Sauberer Reset: kurz WDT scharf machen, dann busy-loop. Kein cli()
    // wie bei exit(), USB-Detach-Sequenz im Bootloader laeuft normal.
# ifdef WITH_SERIAL_2
    Serial.flush();
# endif
    wdt_enable(WDTO_15MS);
    while (1) { }
#else
//    delay(100);
    exit(1);
#endif
  // }
  // }
}

// ----------------------------------------------------------------------------
void onBtnHeldTopLeft(PushButton& btn){
  int index = &btn - button;
  sendKey(btn_design[index].LongClick);
}

// ----------------------------------------------------------------------------
void onButtonLong(PushButton& btn){
  int index = &btn - button;
  if (btn_design[index].LongClick) {
    BtnClickTime[index] = 0;
    sendKey(btn_design[index].LongClick);
  }
}
 
// ----------------------------------------------------------------------------
// void onButtonVeryLong(PushButton& btn){
//   int index = &btn - button;
//   if (btn_design[index].DblClick) {
//     sendKey(btn_design[index].DblClick);
//   } else if (btn_design[index].LongClick) {
//     sendKey(btn_design[index].LongClick);
//   }
// } 

// ----------------------------------------------------------------------------
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
        // Mouse.move(btn_design[index][JOY_X], btn_design[index][JOY_Y]);
        Mouse.move(btn_design[index].JoyX, btn_design[index].JoyY);
      } else { // mouse not active
        sendKey(btn_design[index].JoyKey);
      }
      switch(repeat_mode[index]) {
        case 0: 
        case 1: repeat_mode[index]++;break; 
#ifdef MOUSE_ACCELERATOR
        case 2: if (time_now - btn_timer[index] > mouse_speed_switch)
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

// ----------------------------------------------------------------------------
void onJoyRelease(PushButton& btn){
  int index = &btn - button;
  joy_counter[index] = 0;
  first_pressed[index] = false;
  btn_timer[index] = 0; //  millis();
}

// ----------------------------------------------------------------------------
#if VERSION_TYPE  >=  VERSION_2_1
void onXRepeat(PushButton& btn){
  
  if (!x_repeat++)
      btn_timer[btnX] = time_now;
}
#endif  // VERSION_TYPE  ==  VERSION_2_1

// ----------------------------------------------------------------------------
void onXRelease(PushButton& btn) {
#if  VERSION_TYPE  >=  VERSION_2_1
  unsigned long release_time = time_now - btn_timer[btnX];
  char key = 0;
  if (x_repeat < 3 ) 
      key = 0;
  if (x_repeat < 100)  // ca. 1sec
      key = btn_design[btnX].Click;
  else if (x_repeat < 400)   // ca. 4sec
     key = btn_design[btnX].LongClick; 
  else // x_repeat >= 200)
     key = btn_design[btnX].DblClick; 
#else
  char key = btn_design[btnX].Click;
#endif
  if (key) {
    sendKey(key); 
  }
  x_repeat = 0;
}

// ----------------------------------------------------------------------------
void onSTF_switch(PushButton& btn) {
#ifdef USE_STF_TOGGLE
# ifdef WITH_DBLCLICK
  if (!DblClickTest(btnSTF))
      return;
# endif
  // use a BUTTON to switch between Vario or Speed To Fly mode
  if (STF_key == 'S') //btn_design[btnSTF].LongClick) 
    STF_key = 'V'; // (btn_design[btnSTF].Click);
  else
    STF_key = 'S'; // (btn_design[btnSTF].LongClick); 
  btn_design[btnSTF].DblClick = STF_key;  //btn_design[btnSTF].LongClick;

#else
  if (button[btnSTF].isPressed()) 
    sendKey('S');
  else
    sendKey('V');
  // use a SWITCH to enable Vario or Speed To Fly mode
#endif

}
