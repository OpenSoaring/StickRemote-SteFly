#!/usr/bin/env python3
"""
programmer.py
=============

Cross-platform Flash-Tool fuer den SteFly Stick Remote (Arduino-Leonardo-basiert).

Findet automatisch:
- avrdude und avrdude.conf in den ueblichen Installationspfaden
  (Arduino IDE 1.x, Arduino IDE 2 / Boards Manager, System-PATH)
- den Firmware-COM-Port des laufenden Sticks (via VID/PID)
- nach 1200-Baud-Reset den Bootloader-COM-Port

und ruft avrdude mit den richtigen Argumenten auf.

Lauffaehig auf:
    Windows  (COMx)
    Linux    (/dev/ttyACM*)
    macOS    (/dev/cu.usbmodem*)

Beispiel:
    python tools/programmer.py --firmware build/arduino.avr.leonardo/StickRemote-SteFly.ino.hex
    python tools/programmer.py -f build/StickRemote-SteFly.hex --verbose

Erfordert: pyserial  (pip install pyserial)
"""
from __future__ import annotations
import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

# find_stick_ports.py liegt im selben Ordner
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from find_stick_ports import (
    find_firmware_port,
    find_bootloader_port,
    trigger_bootloader,
    _scan,
    BOOTLOADER_IDS,
)


# -- avrdude und avrdude.conf finden -------------------------------------

def _arduino_packages_root() -> Path | None:
    """Wurzelpfad der Boards-Manager-Installation (IDE 2 / arduino-cli)."""
    if sys.platform == "win32":
        base = Path(os.environ.get("LOCALAPPDATA", "")) / "Arduino15"
    elif sys.platform == "darwin":
        base = Path.home() / "Library" / "Arduino15"
    else:  # linux
        base = Path.home() / ".arduino15"
    return base if base.exists() else None


def find_avrdude() -> str | None:
    """Sucht eine avrdude-Binary. Reihenfolge: mitgelieferte Toolchain ->
    System-PATH -> Arduino-IDE 2 -> Arduino-IDE 1.x. Gibt None zurueck,
    wenn nichts gefunden."""
    # 0) Mitgelieferte Toolchain im Projektordner (portable, ohne IDE-Install)
    here = Path(__file__).resolve().parent
    for bundled in (
        here / "avrdude" / ("avrdude.exe" if sys.platform == "win32" else "avrdude"),
        here / "avrdude" / "bin" / ("avrdude.exe" if sys.platform == "win32" else "avrdude"),
    ):
        if bundled.exists():
            return str(bundled)

    # 1) System-PATH
    on_path = shutil.which("avrdude")
    if on_path:
        return on_path

    # 2) Boards-Manager (IDE 2 / arduino-cli)
    candidates: list[Path] = []
    pkg_root = _arduino_packages_root()
    if pkg_root:
        avrdude_dir = pkg_root / "packages" / "arduino" / "tools" / "avrdude"
        if avrdude_dir.exists():
            for vdir in sorted(avrdude_dir.iterdir(), reverse=True):
                exe = vdir / "bin" / ("avrdude.exe" if sys.platform == "win32" else "avrdude")
                if exe.exists():
                    candidates.append(exe)

    # 3) Arduino IDE 1.x klassisch
    if sys.platform == "win32":
        for prefix in (r"C:\Program Files (x86)\Arduino", r"C:\Program Files\Arduino"):
            exe = Path(prefix) / "hardware" / "tools" / "avr" / "bin" / "avrdude.exe"
            if exe.exists():
                candidates.append(exe)
    elif sys.platform == "darwin":
        for prefix in ("/Applications/Arduino.app/Contents/Java/hardware/tools/avr/bin",
                       "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources"):
            exe = Path(prefix) / "avrdude"
            if exe.exists():
                candidates.append(exe)

    return str(candidates[0]) if candidates else None


def find_avrdude_conf(avrdude_path: str | None) -> str | None:
    """Sucht avrdude.conf. Erst neben/unter der avrdude-Binary, dann systemweit."""
    if avrdude_path:
        bin_dir = Path(avrdude_path).resolve().parent
        for c in (bin_dir / "avrdude.conf",
                  bin_dir.parent / "etc" / "avrdude.conf"):
            if c.exists():
                return str(c)
    for c in ("/etc/avrdude.conf",
              "/usr/local/etc/avrdude.conf",
              "/opt/homebrew/etc/avrdude.conf"):
        if Path(c).exists():
            return c
    return None


# -- Default-Suche fuer die Firmware-HEX -----------------------------------

def find_default_firmware(project_root: Path) -> Path | None:
    """Sucht eine sinnvolle .hex-Datei im Projekt, wenn der User keine angibt."""
    for rel in (
        "build/StickRemote-SteFly.hex",
        "build/arduino.avr.leonardo/StickRemote-SteFly.ino.hex",
    ):
        p = project_root / rel
        if p.exists():
            return p
    # Fallback: irgendein neuestes .hex im build-Tree
    builds = list(project_root.glob("build/**/StickRemote-SteFly.ino.hex"))
    if builds:
        builds.sort(key=lambda p: p.stat().st_mtime, reverse=True)
        return builds[0]
    return None


# -- Hauptablauf ---------------------------------------------------------

def flash(
    firmware_hex: Path,
    avrdude: str,
    avrdude_conf: str | None,
    mcu: str = "atmega32u4",
    programmer: str = "avr109",
    baud: int = 57600,
    reset_timeout: float = 8.0,
    verbose: bool = False,
    dry_run: bool = False,
    erase: bool = False,
    skip_reset: bool = False,
    serial_filter: str | None = None,
) -> int:
    """Komplett: Firmware-Port suchen, Reset, Bootloader-Port abwarten, avrdude.
    Rueckgabe: Exit-Code (0 = OK)."""

    # 1) Firmware-Port finden (falls Reset noetig)
    pre_bl = {s.device for s in _scan(BOOTLOADER_IDS)}
    bl_port = None

    if skip_reset:
        # Annahme: Stick steht bereits im DFU-Modus
        print("[programmer] --skip-reset: kein 1200-Baud-Reset, suche direkt Bootloader-Port")
        bl = find_bootloader_port(timeout=reset_timeout)
        if bl is None:
            print("[programmer] FEHLER: kein Bootloader-Port gefunden", file=sys.stderr)
            return 3
        bl_port = bl.device
    else:
        fw = find_firmware_port(serial_number=serial_filter)
        if fw is None:
            # Vielleicht steht der Stick schon im Bootloader
            bl_now = _scan(BOOTLOADER_IDS)
            if bl_now:
                print(f"[programmer] Stick scheint schon im Bootloader-Modus zu sein: {bl_now[0].device}")
                bl_port = bl_now[0].device
            else:
                print("[programmer] FEHLER: weder Firmware- noch Bootloader-Port gefunden", file=sys.stderr)
                print("[programmer]   Stick angeschlossen? Treiber installiert? Aktuelle Ports:", file=sys.stderr)
                for p in __import__("serial.tools.list_ports", fromlist=("comports",)).comports():
                    print(f"[programmer]     {p.device:20s}  VID={p.vid}  PID={p.pid}  {p.product}", file=sys.stderr)
                return 1
        else:
            print(f"[programmer] Firmware-Port: {fw.device}  ({fw.product})")
            print(f"[programmer] Triggere 1200-Baud-Reset...")
            trigger_bootloader(fw.device)
            bl = find_bootloader_port(timeout=reset_timeout, exclude_devices=pre_bl)
            if bl is None:
                print(
                    f"[programmer] FEHLER: nach {reset_timeout}s ist kein Bootloader-Port erschienen",
                    file=sys.stderr,
                )
                return 2
            bl_port = bl.device

    print(f"[programmer] Bootloader-Port: {bl_port}")

    # 2) avrdude-Kommandozeile zusammenbauen
    cmd = [avrdude]
    if avrdude_conf:
        cmd += ["-C", avrdude_conf]
    cmd += ["-p", mcu,
            "-c", programmer,
            "-P", bl_port,
            "-b", str(baud)]
    if not erase:
        # avr109/Caterina macht sowieso einen Page-Erase pro Page, ein extra -e
        # ist nicht noetig und triggert sonst eine unnoetige Vollloeschung
        cmd += ["-D"]
    else:
        cmd += ["-e"]
    if verbose:
        cmd += ["-v"]
    cmd += ["-U", f"flash:w:{firmware_hex}:i"]

    print("[programmer] $", " ".join(f'"{c}"' if " " in str(c) else str(c) for c in cmd))
    if dry_run:
        print("[programmer] --dry-run: avrdude wurde nicht ausgefuehrt")
        return 0

    # 3) avrdude ausfuehren - direkt durchschleifen, damit man den Fortschritt sieht
    try:
        result = subprocess.run(cmd)
        return result.returncode
    except FileNotFoundError:
        print(f"[programmer] FEHLER: avrdude nicht ausfuehrbar: {avrdude}", file=sys.stderr)
        return 4


def _cli() -> int:
    here = Path(__file__).resolve().parent
    project_root = here.parent

    ap = argparse.ArgumentParser(
        description=__doc__.splitlines()[1],
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("-f", "--firmware", type=Path,
                    help="Pfad zur .hex-Datei (Default: auto-Suche unter build/)")
    ap.add_argument("--avrdude", help="Pfad zu avrdude (Default: auto-Suche)")
    ap.add_argument("--avrdude-conf", help="Pfad zu avrdude.conf (Default: auto-Suche)")
    ap.add_argument("-v", "--verbose", action="store_true", help="avrdude verbose")
    ap.add_argument("--dry-run", action="store_true",
                    help="Kommandozeile anzeigen, aber avrdude nicht ausfuehren")
    ap.add_argument("--erase", action="store_true",
                    help="vor dem Flash eine Vollloeschung machen (selten noetig)")
    ap.add_argument("--skip-reset", action="store_true",
                    help="kein 1200-Baud-Reset (Stick muss schon im Bootloader sein)")
    ap.add_argument("--reset-timeout", type=float, default=8.0,
                    help="Wartezeit (s) auf Erscheinen des Bootloader-Ports (Default 8)")
    ap.add_argument("--serial", help="iSerialNumber-Filter, falls mehrere Sticks angesteckt sind")
    args = ap.parse_args()

    # Firmware
    firmware = args.firmware or find_default_firmware(project_root)
    fw = str(firmware).strip()
    # match firmware:
    if (len(fw) < 10):
        print('FW = ',fw)
        firmware = Path('deploy/'+fw+'/Stick_Remote_Control_SteFly_v'+fw+'.ino.hex')
    print('firmware = ', firmware)
    if firmware is None or not firmware.exists():
        print("[programmer] FEHLER: keine .hex gefunden. -f explizit angeben.", file=sys.stderr)
        return 5

    # avrdude
    avrdude = args.avrdude or find_avrdude()
    if not avrdude:
        print("[programmer] FEHLER: avrdude nicht gefunden. --avrdude angeben oder PATH ergaenzen.",
              file=sys.stderr)
        return 6

    avrdude_conf = args.avrdude_conf or find_avrdude_conf(avrdude)
    # avrdude.conf kann auch optional sein (avrdude findet selber eine), wir warnen aber
    if not avrdude_conf:
        print("[programmer] Warnung: keine avrdude.conf gefunden, avrdude muss sie selbst finden")

    print(f"[programmer] Firmware:     {firmware}")
    print(f"[programmer] avrdude:      {avrdude}")
    print(f"[programmer] avrdude.conf: {avrdude_conf or '(none)'}")

    return flash(
        firmware_hex=firmware,
        avrdude=avrdude,
        avrdude_conf=avrdude_conf,
        verbose=args.verbose,
        dry_run=args.dry_run,
        erase=args.erase,
        skip_reset=args.skip_reset,
        reset_timeout=args.reset_timeout,
        serial_filter=args.serial,
    )


if __name__ == "__main__":
    sys.exit(_cli())
