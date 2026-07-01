#pragma once

// ============================================================================
//  Serial / NMEA communication module
//
//  Supported NMEA-style commands (received via Serial.read in the main loop):
//    $PSRCI,R,<topic>     -> queryInfo() : Info  / Settings / Version / Device /
//                                          FileDate / FileName / Time
//    $PSRCC,S,<cmd>       -> execCommand(): Reboot
//    $PSRCS,S,Layout,N    -> select flash preset N (0..NUM_LAYOUTS-1)
//    $PSRCK,S,Key,btn,slot,ext,key  -> remap a single key entry
//                                       (slot: 0=Click, 1=LongClick, 2=DblClick)
//    $PSRCH,R,Health      -> dump layout health
//    $PSRCP,S,Pin,btn,pin -> SERVICE: assign new pin to a button (then reset)
//
//  Output frames are emitted via sendNMEA() with the field-tag passed by the
//  sender (e.g. 'I' = info, 'S' = setting, 'K' = key event, 'H' = health).
// ============================================================================

#include <EEPROM.h>
#include <avr/wdt.h>          // wdt_enable, WDTO_15MS - used by dispatchSerialCommand (PSRCP service)
#include "RemoteStick.h"
#include "RemoteEEprom.h"

// --- External symbols defined in the main .ino ------------------------------
extern const char *Version;
extern const char *Device;
extern const char *Date;
extern const char *FileName;
extern unsigned long time_now;
extern char serialNum[20];

// ============================================================================
//  Serial buffers
// ============================================================================
const int buffer_size = 0x40;
char send_buffer[buffer_size];
char nmea_buffer[buffer_size];
char send_info[buffer_size];

const unsigned int read_buffer_length = 0x100;
int read_buffer_pointer = 0;
char read_buffer[read_buffer_length];

// ============================================================================
//  NMEA checksum helpers
// ============================================================================
uint8_t NMEAChecksum(const char *src, int len) noexcept
{
    uint8_t checksum = 0;
    char *p = (char*)src;
    // skip the dollar sign (or exclamation mark used by CAI302) at the beginning
    if (p && *p && (*p == '$' || *p == '!')) { p++; len--; }
    while (*p) checksum ^= static_cast<uint8_t>(*p++);
    return checksum;
}

bool VerifyNMEAChecksum(const char *p)
{
    if (!*p) return false;
    const char *asterisk = strrchr(p, '*');
    if (asterisk == nullptr) return false;
    const char *checksum_string = asterisk + 1;
    char *endptr;
    unsigned long ReadCheckSum2 = strtoul(checksum_string, &endptr, 16);
    if (endptr == checksum_string || *endptr != 0 || ReadCheckSum2 >= 0x100)
        return false;
    uint8_t ReadCheckSum = (unsigned char)ReadCheckSum2;
    uint8_t CalcCheckSum = NMEAChecksum(p, asterisk - p);
    return CalcCheckSum == ReadCheckSum;
}

void AppendNMEAChecksum(char *p)
{
    if (!*p) return;
    const size_t length = strlen(p);
    sprintf(p + length, "*%02X", NMEAChecksum(p, length));
}

// ============================================================================
//  Frame transmission
// ============================================================================
void sendNMEA(const char set, const char* line) {
    // $PSRC<set>,A,<line>*<crc>
    //  PSCR: (S)-Stick, (R)-Remote, (C)-Control,
    // <set> = info / setting / key / health / ...
    snprintf_P(nmea_buffer, buffer_size, PSTR("$PSRC%c,A,%s"), set, line);
    AppendNMEAChecksum(nmea_buffer);
    Serial.println(nmea_buffer);
}

// ============================================================================
//  Send functions - emit a specific Info/Setting frame
// ============================================================================
void sendVersion() {
    snprintf_P(send_info, buffer_size, PSTR("Version,%s"), Version);
    sendNMEA('I', send_info);
}

void sendDevice() {
    snprintf_P(send_info, buffer_size, PSTR("Device,%s"), Device);
    sendNMEA('I', send_info);
}

void sendFileDate() {
    snprintf_P(send_info, buffer_size, PSTR("FileDate,%s"), Date);
    sendNMEA('I', send_info);
}

void sendFileName() {
    snprintf_P(send_info, buffer_size, PSTR("FileName,%s"), FileName);
    sendNMEA('I', send_info);
}

void sendTime() {
    snprintf_P(send_info, buffer_size, PSTR("systime,%u"), time_now);
    sendNMEA('I', send_info);
}

void sendLayout() {
    snprintf_P(send_info, buffer_size, PSTR("Layout,%u"), active_layout_index);
    sendNMEA('S', send_info);
}

void sendSerialNumber() {
    snprintf_P(send_info, buffer_size, PSTR("S/N,%s"), serialNum);
    sendNMEA('I', send_info);
}

void sendSettingsBlock() {
    sendLayout();
    sendNMEA('S', "Ready");
}

void sendInfoBlock() {
    sendDevice();
    sendVersion();
    sendFileName();
    sendSerialNumber();
    sendNMEA('I', "Ready");
}

// ============================================================================
//  Reset command (triggered via $PSRCC,R,Reboot)
// ============================================================================
//  resetCmd() lives in the main .ino - it is sketch-level behaviour
//  (clean WDT reset). Declared extern here so execCommand() can dispatch it.
extern void resetCmd();

// ============================================================================
//  $PSRCI / $PSRCC dispatchers
// ============================================================================
// $PSRCI,R,<topic>  - return true if topic recognized
bool queryInfo(const char* cmd) {
         if (!strcmp_P(cmd, PSTR("Info")))     { sendInfoBlock();     return true; }
    else if (!strcmp_P(cmd, PSTR("Settings"))) { sendSettingsBlock(); return true; }
    else if (!strcmp_P(cmd, PSTR("Version")))  { sendVersion();       return true; }
    else if (!strcmp_P(cmd, PSTR("Device")))   { sendDevice();        return true; }
    else if (!strcmp_P(cmd, PSTR("FileDate"))) { sendFileDate();      return true; }
    else if (!strcmp_P(cmd, PSTR("FileName"))) { sendFileName();      return true; }
    else if (!strcmp_P(cmd, PSTR("Time")))     { sendTime();          return true; }
    else                                       {                      return false; }
}

// $PSRCC,R,<cmd>  - return true if cmd recognized
bool execCommand(const char* cmd) {
         if (!strcmp_P(cmd, PSTR("Reboot")))   { resetCmd();          return true; }
    else                                       {                      return false; }
}

// $PSRCS,S,Layout,N  - select flash preset N, persist to EEPROM
bool selectLayout(const char* args) {
    uint8_t idx = atoi(args);
    loadPresetFromFlash(idx);
    snprintf_P(send_info, buffer_size, PSTR("Layout,%u"), idx);
    sendNMEA('I', send_info);
    return true;
}

// $PSRCK,S,Key,btn,slot,ext,key  - remap a single key entry
// $PSRCK,S,Key,1,2,0,67 - no hex number please
bool updateKey(const char* args) {
    uint8_t btn, slot, ext, key;
    if (sscanf(args, "%hhu,%hhu,%hhu,%hhu", &btn, &slot, &ext, &key) != 4) {
        sendNMEA('I', "PSRCK parse error");
        return true;
    }
    if (updateKeySlot(btn, slot, ext, key))
        snprintf_P(send_info, buffer_size, PSTR("Key updated:[%u,%u],{%u,%u}"), btn, slot, ext, key);
    else
        snprintf_P(send_info, buffer_size, PSTR("Invalid btn/slot index:[%u,%u],{%u,%u}"), btn, slot, ext, key);
    sendNMEA('I', send_info);
    return true;
}

// $PSRCK,R,Key,btn,slot  - ask a single key entry
// $PSRCK,R,Key,1,2
bool askKey(const char* args) {
    uint8_t btn, slot;
    if (sscanf(args, "%hhu,%hhu", &btn, &slot ) != 2) {
        snprintf_P(send_info, buffer_size, PSTR("PSRCK parse error:[%u,%u]?"), btn, slot);
        sendNMEA('I', send_info);
        return true;
     }

     sKey key = askKeySlot(btn, slot);
     snprintf_P(send_info, buffer_size, PSTR("KeySlot[%u,%u],'%s'"), btn, slot, key_code(key));
     sendNMEA('I', send_info);

    return true;
}

// $PSRCH,R,Health  - dump current layout health/diagnostic
bool dumpHealth(const char* /*args*/) {
    sendLayoutHealth();
    return true;
}

// $PSRCP,S,Pin,btn,pin  - SERVICE command: assign new pin, then reset
bool setPinAssignment(const char* args) {
    uint8_t btn, pin;
    if (sscanf(args, "%hhu,%hhu", &btn, &pin) != 2 || btn >= btnSize) {
        sendNMEA('I', "PSRCP,S parse error or btn out of range");
        return true;
    }
    uint8_t pins[btnSize];
    if (!loadPinConfigFromEEPROM(pins))
        memcpy_P(pins, default_pin_config, btnSize);
    pins[btn] = pin;
    savePinConfigToEEPROM(pins);
    sendNMEA('I', "PinConfig updated, restarting");
    Serial.flush();
    wdt_enable(WDTO_15MS);
    while (1) { }     // never returns
    return true;      // unreachable, but keeps the symmetry
}

// ============================================================================
//  Top-level dispatch for incoming '$PSRC...' frames.
//  Called by the main loop after a full line has been read into read_buffer.
//  Each branch delegates to a single-purpose handler and returns directly.
//  Returns true if a known command was handled, false otherwise.
// ============================================================================
bool dispatchSerialCommand(char *buf) {
    if (buf[0] != '$') return false;

         if (!strncmp_P(buf + 1, PSTR("PSRCI,R,"),        8)) return queryInfo       (buf +  9);
    else if (!strncmp_P(buf + 1, PSTR("PSRCC,S,"),        8)) return execCommand     (buf +  9);
    else if (!strncmp_P(buf + 1, PSTR("PSRCS,S,Layout,"),15)) return selectLayout    (buf + 16);
    else if (!strncmp_P(buf + 1, PSTR("PSRCK,R,Key,"),   12)) return askKey          (buf + 13);
    else if (!strncmp_P(buf + 1, PSTR("PSRCK,S,Key,"),   12)) return updateKey       (buf + 13);
    else if (!strncmp_P(buf + 1, PSTR("PSRCH,R,Health"), 14)) return dumpHealth      (buf + 15);
    else if (!strncmp_P(buf + 1, PSTR("PSRCP,S,Pin,"),   12)) return setPinAssignment(buf + 13);
    else                                                      return false;
}

// ============================================================================
//  Stream reader - called from the main loop on each Serial.read() that
//  yielded a printable character or newline. Accumulates into read_buffer
//  and dispatches a complete line on '\n'. Returns true if the loop should
//  exit early (line consumed / waiting for more input).
// ============================================================================
bool processSerialChar(int c) {
    switch (c) {
        case '\r':
            return true;
        case '\n': {
            if ((read_buffer_pointer > 0) && read_buffer[0] == '$') {
                int l = read_buffer_pointer;
                read_buffer[l] = 0;
                // strip "*HH" checksum suffix if present (we don't verify)
                if (l >= 3 && read_buffer[l - 3] == '*') read_buffer[l - 3] = 0;
                if (!dispatchSerialCommand(read_buffer)) {
                    Serial.println(read_buffer);   // unknown - echo for debug
                }
            }
            read_buffer_pointer = 0;
            return true;
        }
        case '$':
            read_buffer[0] = (char)c;
            read_buffer_pointer = 1;
            return true;
        default:
            if (read_buffer_pointer > 0
                && read_buffer_pointer < (int)read_buffer_length - 1) {
                read_buffer[read_buffer_pointer++] = (char)c;
            }
            return true;
    }
}
