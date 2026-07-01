#pragma once

#include <EEPROM.h>
#include <avr/pgmspace.h>
#include <util/crc16.h>
#include <string.h>     // memcpy, memcmp
#include <stddef.h>     // offsetof

// ============================================================================
//  Layout persistence with dual-slot health-voting in EEPROM
//
//  Data managed here:
//    - btn_design[btnSize]            (declared in RemoteStick.h - RAM copy)
//    - all_layouts[NUM_LAYOUTS]       (declared in RemoteStick.h - flash presets)
//    - active_layout_index            (declared in RemoteStick.h)
//
//  Slot layout in EEPROM (per slot):
//    [0..3]                  LayoutBlockHeader (magic, sequence, presetIdx, reserved)
//    [4..4+payload-1]        payload (= sizeof(btn_design) = 60 bytes)
//    [4+payload..+payload+5] CRC-CCITT-16 (2 bytes)
//
//  On boot: loadLayoutFromEEPROM() picks the slot with the higher sequence
//  number; if one slot is corrupt, the other is healed from the surviving copy.
//  If both are corrupt, the caller must call loadPresetFromFlash(0).
// ============================================================================

// Size of the layout payload - derived from the real btn_design declaration
#define LAYOUT_PAYLOAD_SIZE   (sizeof(btn_design))

// EEPROM address map
//   0x000..0x013   SteFly serial number      (set elsewhere)
//   0x040..0x043   Reset-log block           (set elsewhere)
//   0x080..0x0CF   Layout slot A             (80 bytes reserved, 66 used)
//   0x0D0..0x11F   Layout slot B             (80 bytes reserved, 66 used)
//   0x120..0x123   Layout health/diagnostic  (4 bytes)
#define EE_LAYOUT_SLOT_A      0x080
#define EE_LAYOUT_SLOT_B      0x0D0
#define EE_LAYOUT_HEALTH      0x120

#define LAYOUT_MAGIC          0xAA55
#define HEALTH_MAGIC          0x5A5A
#define PRESET_INDEX_CUSTOM   0xFF   // marker: layout has been edited individually

// Persistent block header (4 bytes)
struct LayoutBlockHeader {
    uint16_t magic;        // 0xAA55, otherwise slot is considered invalid
    uint8_t  sequence;     // monotonically increasing (mod 256) write counter
    uint8_t  presetIdx;    // preset this layout was derived from (0xFF = custom)
};

// Offsets within a slot
#define EE_OFF_HEADER         0
#define EE_OFF_PAYLOAD        sizeof(LayoutBlockHeader)
#define EE_OFF_CRC            (EE_OFF_PAYLOAD + LAYOUT_PAYLOAD_SIZE)
#define EE_SLOT_TOTAL_SIZE    (EE_OFF_CRC + 2)

// Health/diagnostic block - long-term EEPROM tracking
struct LayoutHealth {
    uint16_t magic;             // 0x5A5A
    uint8_t  corruptionCount;   // how many times self-heal had to fire
    uint8_t  reserved;
};

// Last write sequence number, kept in RAM for incrementing on each write
static uint8_t lastLayoutSequence = 0;

extern void sendNMEA(const char set, const char* line);

// ---------------------------------------------------------------------------
// CRC over header + payload (everything in a slot up to the CRC bytes)
// ---------------------------------------------------------------------------
static uint16_t crc16_compute_slot(const LayoutBlockHeader& hdr, const void* payload) {
    uint16_t crc = 0xFFFF;
    const uint8_t* h = (const uint8_t*)&hdr;
    for (size_t i = 0; i < sizeof(hdr); i++) crc = _crc16_update(crc, h[i]);
    const uint8_t* p = (const uint8_t*)payload;
    for (size_t i = 0; i < LAYOUT_PAYLOAD_SIZE; i++) crc = _crc16_update(crc, p[i]);
    return crc;
}

// ---------------------------------------------------------------------------
// Read one slot. Returns true if header magic + CRC match.
// ---------------------------------------------------------------------------
static bool readSlot(uint16_t addr, LayoutBlockHeader& hdr, void* payload) {
    EEPROM.get(addr + EE_OFF_HEADER, hdr);
    if (hdr.magic != LAYOUT_MAGIC) return false;

    uint8_t* p = (uint8_t*)payload;
    for (size_t i = 0; i < LAYOUT_PAYLOAD_SIZE; i++) {
        p[i] = EEPROM.read(addr + EE_OFF_PAYLOAD + i);
    }

    uint16_t storedCRC;
    EEPROM.get(addr + EE_OFF_CRC, storedCRC);
    uint16_t calcCRC = crc16_compute_slot(hdr, payload);
    return (calcCRC == storedCRC);
}

// ---------------------------------------------------------------------------
// Write one slot. Uses EEPROM.update() byte-by-byte to avoid wearing out
// unchanged cells.
// ---------------------------------------------------------------------------
static void writeSlot(uint16_t addr, const LayoutBlockHeader& hdr, const void* payload) {
    const uint8_t* h = (const uint8_t*)&hdr;
    for (size_t i = 0; i < sizeof(hdr); i++) {
        EEPROM.update(addr + EE_OFF_HEADER + i, h[i]);
    }
    const uint8_t* p = (const uint8_t*)payload;
    for (size_t i = 0; i < LAYOUT_PAYLOAD_SIZE; i++) {
        EEPROM.update(addr + EE_OFF_PAYLOAD + i, p[i]);
    }
    uint16_t crc = crc16_compute_slot(hdr, payload);
    EEPROM.put(addr + EE_OFF_CRC, crc);
}

// ---------------------------------------------------------------------------
// Bump the corruption counter (long-term hardware health tracking)
// ---------------------------------------------------------------------------
static void bumpCorruptionCounter() {
    LayoutHealth h;
    EEPROM.get(EE_LAYOUT_HEALTH, h);
    if (h.magic != HEALTH_MAGIC) {
        h.magic = HEALTH_MAGIC;
        h.corruptionCount = 1;
        h.reserved = 0;
    } else if (h.corruptionCount < 0xFF) {
        h.corruptionCount++;
    }
    EEPROM.put(EE_LAYOUT_HEALTH, h);
}

// ---------------------------------------------------------------------------
// Load btn_design from EEPROM. Returns true if a valid slot was found.
// On success: btn_design is populated, active_layout_index reflects the saved
// preset index, and self-heal is triggered if one slot was outdated/corrupt.
// ---------------------------------------------------------------------------
bool loadLayoutFromEEPROM() {
    LayoutBlockHeader hdrA, hdrB;
    key_struct payloadA[btnSize], payloadB[btnSize];

    bool aOk = readSlot(EE_LAYOUT_SLOT_A, hdrA, payloadA);
    bool bOk = readSlot(EE_LAYOUT_SLOT_B, hdrB, payloadB);

    LayoutBlockHeader* winnerHdr     = nullptr;
    key_struct*        winnerPayload = nullptr;
    bool               needSelfHeal  = false;

    if (aOk && bOk) {
        // Both valid: pick the newer one (signed 8-bit diff handles wrap-around)
        int8_t diff = (int8_t)(hdrA.sequence - hdrB.sequence);
        if (diff >= 0) { winnerHdr = &hdrA; winnerPayload = payloadA; }
        else           { winnerHdr = &hdrB; winnerPayload = payloadB; }
        if (diff != 0) needSelfHeal = true;
    } else if (aOk) {
        winnerHdr = &hdrA; winnerPayload = payloadA;
        needSelfHeal = true;     // B is missing/corrupt -> restore from A
    } else if (bOk) {
        winnerHdr = &hdrB; winnerPayload = payloadB;
        needSelfHeal = true;     // A is missing/corrupt -> restore from B
    } else {
        return false;            // both slots corrupt - caller must init default
    }

    memcpy(btn_design, winnerPayload, LAYOUT_PAYLOAD_SIZE);
    lastLayoutSequence  = winnerHdr->sequence;
    active_layout_index = winnerHdr->presetIdx;

    if (needSelfHeal) {
        bumpCorruptionCounter();
        if (winnerPayload == payloadA) writeSlot(EE_LAYOUT_SLOT_B, *winnerHdr, winnerPayload);
        else                           writeSlot(EE_LAYOUT_SLOT_A, *winnerHdr, winnerPayload);
        sendNMEA('I', "EEPROM self-heal");
    }
    return true;
}

// ---------------------------------------------------------------------------
// Write current btn_design to both EEPROM slots.
// ---------------------------------------------------------------------------
void writeLayoutToEEPROM(uint8_t presetIdx) {
    LayoutBlockHeader hdr;
    hdr.magic     = LAYOUT_MAGIC;
    hdr.sequence  = (uint8_t)(lastLayoutSequence + 1);
    hdr.presetIdx = presetIdx;

    // Write A first, then B. If power is lost in between, A is new (valid)
    // and B is old (also valid). Health-voting picks A via higher sequence.
    writeSlot(EE_LAYOUT_SLOT_A, hdr, btn_design);
    writeSlot(EE_LAYOUT_SLOT_B, hdr, btn_design);

    lastLayoutSequence  = hdr.sequence;
    active_layout_index = presetIdx;
}

// ---------------------------------------------------------------------------
// Load a flash preset into btn_design AND persist to EEPROM.
// Replaces the old loadLayout() from RemoteStick.h.
// ---------------------------------------------------------------------------
void loadPresetFromFlash(uint8_t presetIdx) {
    if (presetIdx >= NUM_LAYOUTS) presetIdx = 0;
    const void* src = (const void*)pgm_read_word(&all_layouts[presetIdx]);
    memcpy_P(btn_design, src, LAYOUT_PAYLOAD_SIZE);
    writeLayoutToEEPROM(presetIdx);
}

// ---------------------------------------------------------------------------
// Update a single key slot (used by NMEA $PSRCK,S command).
// slotIdx: 0 = Click, 1 = LongClick, 2 = DblClick
// Returns true on success, false if indices are out of range.
// ---------------------------------------------------------------------------
bool updateKeySlot(uint8_t btnIdx, uint8_t slotIdx, uint8_t ext, uint8_t key) {
    if (btnIdx >= btnSize || slotIdx > 2) return false;

    sKey newKey = { ext, key };
    switch (slotIdx) {
        case 0: btn_design[btnIdx].Click     = newKey; break;
        case 1: btn_design[btnIdx].LongClick = newKey; break;
        case 2: btn_design[btnIdx].DblClick  = newKey; break;
    }
    // Layout no longer matches a pure preset - mark as custom
    writeLayoutToEEPROM(PRESET_INDEX_CUSTOM);
    return true;
}

// ---------------------------------------------------------------------------
sKey askKeySlot(uint8_t btnIdx, uint8_t slotIdx) {
    if (btnIdx >= btnSize || slotIdx > 2) return {-1, -1};
    

    switch (slotIdx) {
        case 0: return btn_design[btnIdx].Click;
        case 1: return btn_design[btnIdx].LongClick;
        case 2: return btn_design[btnIdx].DblClick;
    }
}

// ---------------------------------------------------------------------------
// Send health/diagnostic info via NMEA ($PSRCH,R command response)
// ---------------------------------------------------------------------------
void sendLayoutHealth() {
    LayoutHealth h;
    EEPROM.get(EE_LAYOUT_HEALTH, h);
    uint8_t corruptions = (h.magic == HEALTH_MAGIC) ? h.corruptionCount : 0;

    char buf[48];
    snprintf_P(buf, sizeof(buf), PSTR("Corruptions=%u,Seq=%u,Preset=%u"),
               corruptions, lastLayoutSequence, active_layout_index);
    sendNMEA('H', buf);
}

// ============================================================================
//  Pin configuration block (hardware mapping of buttons to MCU pins)
//
//  This is a service-level setting: written once at first boot (or via ISP
//  pre-loaded .eep file), changed only rarely via the $PSRCP,S service command
//  if the assembled hardware uses a different pin assignment than the factory
//  default. Not exposed to the end user.
//
//  The board-layout defaults live in PROGMEM (default_pin_config) and are
//  copied into the EEPROM on first boot. From then on, the EEPROM is the
//  authoritative source and is loaded into active_pin_config at every boot.
// ============================================================================

#define EE_PIN_CONFIG     0x130        // right after the layout health block
#define PINCFG_MAGIC      0xCC55

// PushButton<->pin mapping. 0xFF = button not connected.
// Order matches the enum {btnFn, btnMenu, btnX, btnSTF, btnEnter,
//                        joyUp, joyDown, joyLeft, joyRight, btnPTT}
const PROGMEM uint8_t default_pin_config[btnSize] = {
    9, 14, 15, 7, 6, 2, 4, 5, 3, 0xFF
};

// RAM working copy - filled at boot from EEPROM (or default_pin_config)
uint8_t active_pin_config[btnSize];

struct PinConfigBlock {
    uint16_t magic;             // 0xCC55
    uint8_t  pins[btnSize];     // one byte per button (0xFF = not connected)
    uint16_t crc;               // CRC-CCITT-16 over magic + pins
};

static uint16_t crc16_pinconfig(const PinConfigBlock& blk) {
    uint16_t crc = 0xFFFF;
    crc = _crc16_update(crc, blk.magic & 0xFF);
    crc = _crc16_update(crc, blk.magic >> 8);
    for (uint8_t i = 0; i < btnSize; i++) crc = _crc16_update(crc, blk.pins[i]);
    return crc;
}

// Returns true if a valid pin config was loaded from EEPROM.
// Caller passes the destination array (typically active_pin_config).
bool loadPinConfigFromEEPROM(uint8_t pins_out[btnSize]) {
    PinConfigBlock blk;
    EEPROM.get(EE_PIN_CONFIG, blk);
    if (blk.magic != PINCFG_MAGIC) return false;
    if (crc16_pinconfig(blk) != blk.crc) return false;
    memcpy(pins_out, blk.pins, btnSize);
    return true;
}

// Persist the supplied pin mapping into the EEPROM block.
// Single-slot (no dual-bank) - this is a one-shot service write, not a
// frequently-changing value.
void savePinConfigToEEPROM(const uint8_t pins_in[btnSize]) {
    PinConfigBlock blk;
    blk.magic = PINCFG_MAGIC;
    memcpy(blk.pins, pins_in, btnSize);
    blk.crc = crc16_pinconfig(blk);

    const uint8_t* p = (const uint8_t*)&blk;
    for (size_t i = 0; i < sizeof(blk); i++) {
        EEPROM.update(EE_PIN_CONFIG + i, p[i]);
    }
}

// Loads pin config into active_pin_config. If the EEPROM block is empty or
// corrupt, falls back to the PROGMEM default and persists it. Call early in
// setup() - before the button[] objects are attached to their pins.
void loadPinConfig() {
    if (!loadPinConfigFromEEPROM(active_pin_config)) {
        memcpy_P(active_pin_config, default_pin_config, btnSize);
        savePinConfigToEEPROM(active_pin_config);
        sendNMEA('I', "PinConfig initialized from factory default");
    }
}
