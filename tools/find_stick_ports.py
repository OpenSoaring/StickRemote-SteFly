#!/usr/bin/env python3
"""
find_stick_ports.py
====================

Hilfsmodul zum automatischen Erkennen der COM-Ports eines SteFly Stick Remote
(bzw. eines kompatiblen Arduino-Leonardo-basierten HID-Geraets), waehrend
der Sketch laeuft *und* nachdem ueber den 1200-Baud-Reset in den Caterina-
Bootloader gewechselt wurde.

Wird ohne Argumente aufgerufen, gibt es beide Ports auf stdout aus
- den aktuellen Firmware-Port und (per 1200-Baud-Reset getriggert) den
Bootloader-Port - sodass es sich in einem Shell-Skript einfach via
`for /f` oder Backticks auswerten laesst.

Erfordert: pyserial  (pip install pyserial)
"""
from __future__ import annotations
import argparse
import os
import sys
import time
from dataclasses import dataclass
from typing import Iterable

# Lokale, mitgelieferte python-deps (z.B. pyserial) bevorzugen, falls vorhanden.
# Wird durch tools/bootstrap.ps1 eingerichtet, damit das Werkstatt-Setup
# unabhaengig von einer System-Python-Installation funktioniert.
_local_deps = os.path.join(os.path.dirname(os.path.abspath(__file__)), "python-deps")
if os.path.isdir(_local_deps) and _local_deps not in sys.path:
    sys.path.insert(0, _local_deps)

try:
    import serial
    from serial.tools.list_ports import comports
except ImportError:
    sys.stderr.write(
        "Fehler: 'pyserial' ist nicht installiert.\n"
        "Loesungen:\n"
        "  - lokal (empfohlen): tools\\bootstrap.ps1 ausfuehren\n"
        "  - global:            pip install pyserial\n"
    )
    sys.exit(2)


# -- Bekannte VID/PID-Kombinationen --------------------------------------
# Firmware-Port: SteFly Stick Remote (pid.codes 0x8500) oder kompatible
# Arduino-Leonardo-Varianten mit laufendem Sketch.
FIRMWARE_IDS: list[tuple[int, int]] = [
    (0x1209, 0x8500),  # SteFly Stick Remote (pid.codes)
    (0x2341, 0x8036),  # Arduino Leonardo (Standard Arduino LLC)
    (0x2A03, 0x8036),  # Arduino Leonardo (alternate Arduino VID)
    (0x2341, 0x8037),  # Arduino Micro (selbe MCU/Bootloader)
    (0x2A03, 0x8037),
]

# Bootloader-Port: Caterina im DFU-Wartemodus.
# Achtung: solange der Caterina-Bootloader nicht selbst auf VID/PID 1209:85xx
# umgebaut ist, meldet er sich weiter unter der Standard-Arduino-VID.
BOOTLOADER_IDS: list[tuple[int, int]] = [
    (0x2341, 0x0036),  # Caterina-Leonardo
    (0x2A03, 0x0036),
    (0x2341, 0x0037),  # Caterina-Micro
    (0x2A03, 0x0037),
    (0x1209, 0x8501),  # falls man irgendwann eine separate Bootloader-PID nimmt
]


@dataclass
class StickPort:
    device: str          # z.B. "COM5" oder "/dev/ttyACM0"
    vid: int
    pid: int
    manufacturer: str | None
    product: str | None
    serial_number: str | None

    def __str__(self) -> str:
        return self.device


def _scan(allowed: Iterable[tuple[int, int]]) -> list[StickPort]:
    """Listet alle aktuell sichtbaren Ports mit passender VID/PID auf."""
    allow = set(allowed)
    hits: list[StickPort] = []
    for p in comports():
        if p.vid is None or p.pid is None:
            continue
        if (p.vid, p.pid) in allow:
            hits.append(StickPort(
                device=p.device,
                vid=p.vid,
                pid=p.pid,
                manufacturer=p.manufacturer,
                product=p.product,
                serial_number=p.serial_number,
            ))
    return hits


def find_firmware_port(serial_number: str | None = None) -> StickPort | None:
    """Liefert den COM-Port des laufenden Sketches.

    Wenn `serial_number` gesetzt ist, wird zusaetzlich danach gefiltert -
    nuetzlich, wenn mehrere Sticks gleichzeitig am Host haengen.
    """
    for s in _scan(FIRMWARE_IDS):
        if serial_number is None or s.serial_number == serial_number:
            return s
    return None


def find_bootloader_port(
    timeout: float = 6.0,
    poll_interval: float = 0.1,
    exclude_devices: set[str] | None = None,
) -> StickPort | None:
    """Wartet bis zu `timeout` Sekunden auf das Auftauchen eines Bootloader-Ports.

    `exclude_devices` erlaubt es, bekannte (nicht gemeinte) Sticks/Ports
    auszuschliessen - der frisch erschienene Port wird so eindeutig identifiziert.
    """
    exclude_devices = exclude_devices or set()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for s in _scan(BOOTLOADER_IDS):
            if s.device not in exclude_devices:
                return s
        time.sleep(poll_interval)
    return None


def trigger_bootloader(port: str) -> None:
    """1200-Baud-Geste: oeffnet den Port kurz bei 1200 Baud und schliesst wieder.

    Caterina/USBCore-Sketch sehen das `SET_LINE_CODING(baud=1200)`, schreiben
    den Magic-Key ins RAM und triggern einen WDT-Reset in den DFU-Modus.

    Achtung: manche OS-Treiber-Kombinationen senden `SET_LINE_CODING` erst beim
    Close oder erst nach einer DTR/RTS-Aenderung. Deshalb hier explizit DTR
    und RTS togglen, bevor wir wieder schliessen. Das ist exakt das, was
    arduino-cli und die Arduino-IDE intern auch tun.
    """
    try:
        # do not use context manager - we want fine-grained control
        s = serial.Serial(write_timeout=0.5)
        s.port = port
        s.baudrate = 1200
        s.dtr = False           # vor open setzen, damit es beim open angewendet wird
        s.rts = False
        s.open()
        # kurze Pause, damit der Host die LineCoding-Aenderung ueber den Bus schickt
        time.sleep(0.05)
        # einige Stacks (Windows usbser) senden SET_LINE_CODING erst durch DTR-Toggle
        try:
            s.dtr = True
            time.sleep(0.05)
            s.dtr = False
        except (serial.SerialException, OSError):
            pass
        time.sleep(0.05)
        s.close()
    except (serial.SerialException, OSError):
        # Geraet kann waehrend des Vorgangs verschwinden - genau das ist das Ziel.
        pass
    # dem Host noch einen Moment Zeit geben, den Bus zu re-enumerieren
    time.sleep(0.2)


def find_both_ports(
    timeout: float = 6.0,
    serial_number: str | None = None,
) -> tuple[StickPort | None, StickPort | None]:
    """Komplettlauf: Firmware-Port finden, Reset triggern, Bootloader-Port warten.

    Rueckgabe: (firmware_port_before_reset, bootloader_port_after_reset).
    Beide koennen None sein, wenn nichts gefunden wurde.
    """
    fw = find_firmware_port(serial_number=serial_number)
    if fw is None:
        return None, None

    # Welche Bootloader-Ports sind JETZT bereits sichtbar?
    # (z.B. weil der Stick zufaellig gerade im DFU-Fenster steckt)
    pre_existing = {s.device for s in _scan(BOOTLOADER_IDS)}

    trigger_bootloader(fw.device)
    bl = find_bootloader_port(timeout=timeout, exclude_devices=pre_existing)
    return fw, bl


def _cli() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--firmware-only", action="store_true",
                    help="Nur den Firmware-Port suchen (kein Reset triggern)")
    ap.add_argument("--bootloader-only", action="store_true",
                    help="Nur einen aktuell vorhandenen Bootloader-Port suchen")
    ap.add_argument("--timeout", type=float, default=6.0,
                    help="Wartezeit (Sekunden) auf den Bootloader-Port (Default 6)")
    ap.add_argument("--serial", default=None,
                    help="iSerialNumber-Filter (falls mehrere Sticks angesteckt sind)")
    ap.add_argument("--quiet", "-q", action="store_true",
                    help="Nur Geraetenamen ausgeben, eine Zeile pro Port")
    args = ap.parse_args()

    if args.bootloader_only:
        bl_candidates = _scan(BOOTLOADER_IDS)
        if not bl_candidates:
            sys.stderr.write("kein Bootloader-Port sichtbar\n")
            return 1
        for s in bl_candidates:
            print(s.device if args.quiet else f"bootloader: {s.device}  ({s.product})")
        return 0

    if args.firmware_only:
        fw = find_firmware_port(serial_number=args.serial)
        if fw is None:
            sys.stderr.write("kein Firmware-Port gefunden\n")
            return 1
        print(fw.device if args.quiet else f"firmware: {fw.device}  ({fw.product})")
        return 0

    fw, bl = find_both_ports(timeout=args.timeout, serial_number=args.serial)
    if fw is None:
        sys.stderr.write("kein Firmware-Port gefunden - Stick angeschlossen?\n")
        return 1
    if bl is None:
        sys.stderr.write(
            f"firmware-Port: {fw.device}, aber kein Bootloader-Port "
            f"innerhalb {args.timeout}s erschienen\n"
        )
        return 2

    if args.quiet:
        print(fw.device)
        print(bl.device)
    else:
        print(f"firmware:   {fw.device}  ({fw.product})  VID={fw.vid:#06x} PID={fw.pid:#06x}")
        print(f"bootloader: {bl.device}  ({bl.product})  VID={bl.vid:#06x} PID={bl.pid:#06x}")
    return 0


if __name__ == "__main__":
    sys.exit(_cli())
