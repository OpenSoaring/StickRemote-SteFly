## 2026-05-19: Version v4.1 (USB-Recovery, stabilisiert)
sketch: **StickRemote-SteFly.ino** (Schalter: `#define VERSION_TYPE VERSION_4`)
#### Layout unverändert gegenüber v3/v4.0
#### Was sich gegenüber v4.0 ändert
Hintergrund: in der Werkstatt war zu beobachten, dass das v4.0-Reattach
gelegentlich ungewollt feuerte, wenn der USBasp am ICSP-Header steckte —
elektrisches Loading auf PB1/PB3 (Pin 15/14, btnX/btnMenu) erzeugte
Spurious-Button-Events, die `sendKey()` und damit `softReattach()` in
einem ungünstigen Moment auslösten und den 1200-Baud-Reset-Pfad sabotierten.
Dieselbe Mechanik kann im Flug auch durch EMV-Glitches passieren.

  * **Debounce der Abnormal-Erkennung**: `softReattach()` wird jetzt nur
    aus `sendKey()` heraus aufgerufen, wenn `USBDevice.isSuspended()` oder
    `!USBDevice.configured()` **mindestens 500 ms stabil** anliegen.
    Kurze Glitches werden ignoriert.
  * **Preemptive Recovery**: ein neues `monitorUsbState()` läuft in jedem
    Mainloop-Durchgang. Wenn der Abnormal-Zustand 5 s anhält *ohne* dass
    eine Taste gedrückt wird, fährt automatisch ein Reattach. Schützt
    Sticks, die im Flug länger nicht angefasst werden.
  * **Diagnose**: Reattach-Gründe in NMEA werden ergänzt um `idle-recover`
    (preemptive aus Mainloop) zusätzlich zu `wake-fail`/`no-wake`/`not-cfg`.

#### Werkstatt-Hinweis
USBasp während des 1200-Baud-Reset-Tests *immer* vom Stick trennen.
Verbindung zur ICSP-Buchse nur während des einmaligen Bootloader-Flash.

## 2026-05-18: Version v4.0 (USB-Recovery)
sketch: **StickRemote-SteFly.ino** (Schalter: `#define VERSION_TYPE VERSION_4`)
#### Layout unverändert gegenüber v3
#### Hintergrund
Im Feld (Flugzeug-Einbau) wurden vereinzelt Hänger beobachtet, bei denen der
Stick nach einer Weile keine Tastendrücke mehr an den Host schickt. Re-Plug
hilft, ist aber im eingebauten Zustand nicht möglich. Stufe A+B Patches dazu:

#### Stufe A — Robuster gegen den Caterina-Bootloader-Trap
  * **Früher WDT-Disable** in `.init3` (vor `setup()` und vor C++-Globalkonstruktion):
    `MCUSR` wird in `mcusr_at_boot` (Section `.noinit`) gesichert, dann `MCUSR = 0; wdt_disable();`.
    Damit kommt jeder WDT-Reset garantiert durch den Bootloader.
  * **WDT-Kick entkoppelt** vom USB-Stack: `wdtReset()` ruft kein `Keyboard.write(0)`
    mehr auf (`#define WITHOUT_USB_KEEPALIVE`). Vermeidet die selbstgemachte
    Hängequelle, in der das Keepalive selbst im suspendierten Endpoint blockiert.
  * **Reset-Log im EEPROM** (Offset 64..67): Magic `'R'`, uint16 Reset-Zähler,
    letzter MCUSR. Beim Boot wird per NMEA `$PSRCI,R,Reset=WDT|BOR|EXT|POR,MCUSR=0xHH,n=NN` gemeldet.
  * **`exit(1)` ersetzt** durch sauberen `wdt_enable(WDTO_15MS); while(1){}`.

#### Stufe B — Sanftes USB-Re-Enumerate statt Reset
  * Vor jedem `sendKey()`: Wenn `USBDevice.isSuspended()` → erst
    `USBDevice.wakeupHost()`, kurz warten, sonst Soft-Reattach.
  * `softReattach()`: `USBDevice.detach(); ~250 ms warten (WDT mitgekickt);
    USBDevice.attach();` und bis zu ~300 ms auf `USBDevice.configured()` warten.
    Anti-Flapping: max. 1× alle 5 s.
  * NMEA-Diagnose: `$PSRCI,R,Reattach,<grund>,n=NN` bei jeder Recovery.
  * Effekt: < 1 s Recovery ohne Bootloader-Beteiligung, laufender Zustand
    bleibt erhalten.

#### Stufe D — Modifizierter Caterina-Bootloader (separat, einmaliger ISP-Flash)
  * Caterina-Variante, die bei WDT-Reset (MCUSR.WDRF) die 8-Sekunden-DFU-Wartezeit
    überspringt. USB-Upload via Arduino-IDE bleibt voll erhalten, weil der
    Auto-Reset der IDE (1200-Baud-Geste) einen *externen* Reset auslöst (EXTRF).
  * Patch siehe Repo-Notizen / Service-Dokumentation, Flash via USBasp oder
    zweitem Arduino als ISP. Einmalig in der Werkstatt, nicht im Flug.

## 2025-01-26: Version v2.1
sketch: **Stick_Remote_Control_SteFly_2.1.ino**
#### Layout see below at '(2018-01-01): Version BR'
  * 1 : switches between mouse and keyboard emulation, **a long pressure ( > 1 Sec) sends a 'E' - could be managed as PilotEvent**
      in the XCSoar/OpenSoar - or to another user defined event in the xci-file
  * 2 : call the QuickMenu ('F1'), long pressure (> 1 sec) Audio/Vario-Menu ('M')
  * 3 : is ESC, **a long pressure calls the Quit program command ('Q'), and a very long pressure ( > 4 sec) sends a 'X' - can be used as exit command or for a shutdown event**
  * 4 : is an additional button connected to the PTT (not managed by the RemoteControl itself)
  * 5 (up), 6 (down), 7 (left), 8 (right) :  are arrow keys **with an accelerated movement after 1 second of continuous pressure**
  * 0 : is Enter, a long pressure (> 1 sec) should reset the RemoteStick!
  * 9 (out of this layout): is a switch (backside of stick) between Vario and STF (Speed-to-Fly) mode

#### Benefits
  * WatchDog, manually triggered reset, diagonal mouse run and better debouncing like 2.0
  * additional button assignments with long press
    * Fn: sending **'E'** after 1 sec (f.e. to use as a PilotEvent)
    * X:  sending **'Q'** after 1 sec (f.e. to close the OpenSoar)<BR/>
          sending **'X'** after 4 sec (f.e. to shutdown the OpenVario)
  * **Mouse** has an **accelerated** mode

## 2025-01-26: Version v2.0
sketch: **Stick_Remote_Control_SteFly_2.0.ino**
#### Layout see below at '(2018-01-01): Version BR'
  * 1 : switches between mouse and keyboard emulation
  * 2 : call the QuickMenu ('F1'), long pressure (> 1 sec) Audio/Vario-Menu ('M')
  * 3 : is ESC, a long pressure (> 1 sec) calls the Quit program command ('Q')
  * 4 : is an additional button connected to the PTT (not managed by the RemoteControl itself)
  * 5 (up), 6 (down), 7 (left), 8 (right) :  are arrow keys
  * 0 : is Enter, ****a long pressure (> 1 sec) should reset the RemoteStick!***
  * 9 (out of this layout): is a switch (backside of stick) between Vario and STF (Speed-to-Fly) mode

#### Benefits
  * WatchDog is resetting the stick if the program stops (in case of outside radio disturbance)
  * Manually triggering a reset is also possible by long pressing the joystick button ('Enter') - this means that a possible communication problem between the stick and OpenVario can be remedied by re-registering the stick 

## 2025-01-26: Version v1.0
sketch: **Stick_Remote_Control_SteFly_1.0.ino**
#### Layout see below at '(2018-01-01): Version BR'
  * 1 : switches between mouse and keyboard emulation
  * 2 : call the QuickMenu ('F1'), long pressure (> 1 sec) Audio/Vario-Menu ('M')
  * 3 : is ESC, a long pressure (> 1 sec) calls the Quit program command ('Q')
  * 4 : is an additional button connected to the PTT (not managed by the RemoteControl itself)
  * 5 (up), 6 (down), 7 (left), 8 (right) :  are arrow keys
  * 0 : is Enter, ****a long pressure (> 1 sec) should reset the RemoteStick!***
  * 9 (out of this layout): is a switch (backside of stick) between Vario and STF (Speed-to-Fly) mode
Button assignment similar to Stick_Remote_Control_br
#### Differences/Benefits
  * Better debounce behavior
  * Mouse cursor has a (intuitive) diagonal run possibility

## (2018-01-01): Version BR
sketch: **Stick_Remote_Control_br.ino**
  * You can see photos of the stick and button layout here:: https://www.stefly.aero/product/stefly-leather-remote-stick
  * The SteFly layout - see below

### (SteFly-)Layout
```
           1
                2
         5
       7 0 8  
         6      3
         
          4 
```
  * 1 : switches between mouse and keyboard emulation
  * 2 : call the QuickMenu ('F1'), long pressure > 1 Sec Audio/Vario-Menu ('M')
  * 3 : is ESC
  * 5 (up), 6 (down), 7 (left), 8 (right) :  are arrow keys
  * 0 : is Enter
  * 4 : is an additional button connected to the PTT (not managed by the RemoteControl itself)
  * 9 (out of this layout): is a switch (backside of stick) between Vario and STF (Speed-to-Fly) mode
 

## 2017-06-29: new sketch from OpenVario with better debounce behavior 
  * library Bounce2 instead of Bounce, both developed from Thomas Fredericks 
  * Button layout: OpenVario (not new SteFly layout)

## 2014-01-06: 1st sketch from OpenVario
  remote control for XCSoar, emulates a keyboard and mouse
  hardware is just pushbuttons connected between pins of an Arduino Leonardo and Gnd
  for each button pressure a keystroke or mouse action is sent
  code is a wild mix of various snippets found on the net mixed up by an software illiterate 
  I started from http://forum.arduino.cc/index.php?topic=80913.msg1077713#msg1077713 and 
  modified by copy & paste trial and error. Kudos to Paul for the great starting point! 
 
  in my remote buttons are arranged in the following pattern
 
### (OV-)Layout
```
             1  2  3
         0   4  5  6
             7  8  9
```
  * 0 switches between mouse and keyboard emulation
  * 2, 4, 6, 8 are arrow keys
  * 7 sends a (keyboard) ESC, 
  * 9 sends a (keyboard) Enter
  * 1, 3, 5 call screens wanted to access directly during flight
