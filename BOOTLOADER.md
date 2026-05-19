# Modifizierter Caterina-Bootloader (Stufe D)

Dieser Schritt ist optional, aber empfohlen, sobald Stufe A+B (in
`StickRemote-SteFly.ino` Version 4) sich im Feld bewährt hat und der
Watchdog-Reset trotzdem noch gelegentlich auftritt.

## Zweck

Der Original-`Caterina-Leonardo`-Bootloader des Arduino Leonardo wartet nach
*jedem* Reset 8 Sekunden auf einen Firmware-Upload via USB-CDC. Wenn der
Watchdog im Sketch zugeschlagen hat, ist das verschwendete Zeit — und in
Einzelfällen bleibt der Bootloader in dieser Wartezeit hängen, sodass der
Stick „tot" erscheint, bis das USB-Kabel gezogen wird.

Die hier beschriebene modifizierte Caterina-Variante prüft beim Boot die
Reset-Ursache (`MCUSR`):

- **EXTRF** (externer Reset, inkl. Auto-Reset per 1200-Baud-Geste der
  Arduino-IDE) → Bootloader wartet wie immer 8 s auf Upload. **Upload-Workflow
  bleibt unverändert.**
- **WDRF** (Watchdog-Reset aus dem laufenden Sketch) → Bootloader springt
  sofort in den Sketch. Recovery in < 100 ms statt 8 s.
- **PORF** (Power-on-Reset) → Springt sofort in den Sketch, wenn einer
  installiert ist.

> Hinweis: Die Original-Caterina-Quelle hat diese Logik bereits ansatzweise,
> ist aber wegen des `MAGIC_BOOT_KEY`-Checks in `.noinit`-RAM unzuverlässig
> (uninitialisiertes RAM nach Brown-out kann fälschlich als „DFU gewünscht"
> interpretiert werden). Die Modifikation macht den Pfad robuster.

## Source-Patch für Caterina.c

Die Caterina-Quelle liegt in deiner Arduino-IDE-Installation unter:

- Windows: `…\Arduino15\packages\arduino\hardware\avr\<version>\bootloaders\caterina\Caterina.c`
- macOS/Linux: `~/.arduino15/packages/arduino/hardware/avr/<version>/bootloaders/caterina/Caterina.c`

Suche in `main()` den Block, der `MCUSR` prüft (steht ganz am Anfang von
`main()`), und ersetze ihn durch die robuste Variante:

```c
int main(void) {
    /* Save the value of the boot key memory before it is overwritten */
    uint16_t bootKeyPtrVal = *bootKeyPtr;
    *bootKeyPtr = 0;

    /* Check the reason for the reset so we can act accordingly */
    uint8_t mcusr_state = MCUSR;
    MCUSR = 0;
    wdt_disable();

    /* === SteFly-Patch: WDT-Reset niemals als 'Upload gewünscht' werten === */
    if (mcusr_state & (1 << WDRF)) {
        /* Watchdog-Reset aus dem Sketch -> sofort in den Sketch zurück,
           unabhängig vom Magic-Key (RAM nach Brown-out unzuverlässig). */
        if (pgm_read_word(0) != 0xFFFF) {
            StartSketch();
        }
    } else if (mcusr_state & (1 << EXTRF)) {
        /* Externer Reset (Reset-Pin, 1200-Baud-Geste der IDE) ->
           DFU-Modus für Upload, wie bei Original-Caterina. */
        /* fall through to bootloader */
    } else if ((mcusr_state & (1 << PORF)) && (pgm_read_word(0) != 0xFFFF)) {
        /* Power-on und Sketch vorhanden -> Sketch starten. */
        StartSketch();
    } else if ((bootKeyPtrVal == MAGIC_BOOT_KEY) && (pgm_read_word(0) != 0xFFFF)) {
        /* Programmatischer Bootloader-Eintritt via Magic-Key vom Sketch. */
        /* fall through to bootloader */
    } else if (pgm_read_word(0) != 0xFFFF) {
        /* Kein bekannter Grund für Bootloader-Eintritt -> Sketch starten. */
        StartSketch();
    }
    /* === Ende SteFly-Patch === */

    /* ... Rest von Caterina main() bleibt unverändert ... */
```

## Build

Im selben Verzeichnis wie `Caterina.c` liegt ein `Makefile`. Build mit:

```bash
cd <…>/bootloaders/caterina
make MCU=atmega32u4 BOARD=LEONARDO
```

Ergebnis: `Caterina-Leonardo.hex` (etwa 4 KB). Optional umbenennen in
`Caterina-Leonardo-SteFly.hex`, damit klar ist, welche Version drauf ist.

## Flashen via ISP-Programmer

Benötigt: USBasp (oder zweiter Arduino als ISP), 6-pol-ISP-Kabel, gelötet auf
den 6-Pin ICSP-Header des Leonardo (auf dem Stick-Board: ICSP-Pad in der Mitte
neben dem 32U4).

1. ISP-Programmer mit dem Stick verbinden, Stick **nicht** an USB.
2. Fuses prüfen (sollte unverändert bleiben):
   ```bash
   avrdude -c usbasp -p atmega32u4 -U lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h
   ```
   Erwartete Werte für Leonardo: lfuse `0xFF`, hfuse `0xD8`, efuse `0xCB`.
3. Bootloader flashen:
   ```bash
   avrdude -c usbasp -p atmega32u4 \
           -U flash:w:Caterina-Leonardo-SteFly.hex:i \
           -U lock:w:0x2F:m
   ```
4. ISP-Kabel ab, Stick an USB. Es sollte sich wie gewohnt als „Arduino
   Leonardo" enumerieren.

## Verifikation

- Sketch v4.0 (mit `WITH_RESET_LOG`) flashen wie gewohnt aus der Arduino-IDE.
  Wenn das normal funktioniert, ist der modifizierte Bootloader EXTRF-kompatibel.
- Im Sketch einen Test-WDT-Reset auslösen (z. B. langes Enter-Drücken → ruft
  jetzt sauberen WDT-Reset auf). Die nächste NMEA-Meldung sollte
  `$PSRCI,R,Reset=WDT,...` innerhalb < 1 s (statt > 8 s) zeigen.
- Reset-Zähler im EEPROM mit jedem WDT-Test um 1 hochzählen.

## Rollback

Original-Caterina ist in jeder Arduino-IDE-Installation als
`Caterina-Leonardo.hex` mitgeliefert. Per ISP zurückspielen mit gleichem
`avrdude`-Befehl wie oben, nur mit Original-HEX.

## Service-Notiz

Beim Bau eines Sticks immer notieren, welcher Bootloader drauf ist
(z. B. Aufkleber auf der Platinen-Unterseite, oder im EEPROM ein Byte
„BL-Type" mit-schreiben). Im NMEA-Strom kann das mit ausgegeben werden,
damit man im Feld eindeutig die Geräte unterscheidet.
