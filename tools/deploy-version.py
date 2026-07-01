#!/usr/bin/env python3
"""
deploy-version.py
=================

Werkstatt-Skript zum Ausrollen einer frisch gebauten StickRemote-SteFly-
Firmware in den versionierten Deploy-Ordner und in das FlashRemoteStick-
Werkstatt-Tool.

Ablauf:
    1. User wird nach einer Version gefragt (z.B. "3.0.2").
    2. Alle Dateien 'StickRemote-SteFly.ino.*' aus
         build/arduino.avr.stefly_remote/
       werden nach
         deploy/<major.minor>/
       kopiert und dabei umbenannt in
         StickRemote-SteFly-v<version>.<extension>
    3. Die .hex-Datei wird zusaetzlich nach
         FlashRemoteStick/build/StickRemote-SteFly-v<version>.hex
       kopiert. Zuvor werden dort vorhandene aeltere
       'StickRemote-SteFly-v*.hex' geloescht.
    4. In den beiden Wrapper-Skripten
         FlashRemoteStick/flash.sh
         FlashRemoteStick/flash.cmd
       wird die BINARY=-Zeile auf die neue Datei aktualisiert.

Aufruf (aus dem Projekt-Wurzelordner):
    python tools/deploy-version.py
    python tools/deploy-version.py --version 3.0.2   # non-interactive
"""
from __future__ import annotations
import argparse
import re
import shutil
import sys
from pathlib import Path


# --- Konfiguration ---------------------------------------------------------
SOURCE_SUBDIR   = Path("build") / "arduino.avr.stefly_remote"
DEPLOY_ROOT     = Path("deploy")
FLASH_SUBDIR    = Path("FlashRemoteStick")
FLASH_BUILD     = FLASH_SUBDIR / "build"
SOURCE_STEM     = "StickRemote-SteFly.ino"
DEST_STEM_TPL   = "StickRemote-SteFly-v{version}"
BINARY_PATH_TPL = "build/StickRemote-SteFly-v{version}.hex"


# --- Utilities -------------------------------------------------------------
def prompt_version(default: str | None = None) -> str:
    """Fragt interaktiv nach einer Version. Akzeptiert 'N.M.P'-Format."""
    prompt = "Firmware-Version" + (f" [{default}]" if default else "") + " (z.B. 3.0.2): "
    while True:
        try:
            v = input(prompt).strip()
        except (EOFError, KeyboardInterrupt):
            print()
            sys.exit(1)
        if not v and default:
            v = default
        if re.match(r"^\d+\.\d+\.\d+$", v):
            return v
        print(f"  Ungueltig: '{v}'. Bitte im Format N.M.P (z.B. 3.0.2).")


def major_minor(version: str) -> str:
    m = re.match(r"^(\d+\.\d+)", version)
    if not m:
        raise ValueError(f"Version '{version}' nicht parsebar")
    return m.group(1)


def suffix_after_stem(name: str, stem: str) -> str | None:
    """Zieht die Extension nach '<stem>.' raus.
    'StickRemote-SteFly.ino.hex'                 -> 'hex'
    'StickRemote-SteFly.ino.with_bootloader.hex' -> 'with_bootloader.hex'
    """
    prefix = stem + "."
    if not name.startswith(prefix):
        return None
    return name[len(prefix):]


def update_binary_line(text: str, new_value: str, cmd_style: bool) -> tuple[str, int]:
    """Ersetzt BINARY=... in Wrapper-Skripten.
    cmd_style=True  -> 'set BINARY=<value>'
    cmd_style=False -> 'BINARY="<value>"'
    Rueckgabe: (neuer Text, Anzahl der Ersetzungen)
    """
    if cmd_style:
        pattern = re.compile(r"^\s*set\s+BINARY\s*=.*$", re.MULTILINE)
        replacement = f"set BINARY={new_value}"
    else:
        pattern = re.compile(r'^\s*BINARY\s*=.*$', re.MULTILINE)
        replacement = f'BINARY="{new_value}"'
    new_text, count = pattern.subn(replacement, text, count=1)
    return new_text, count


# --- Hauptablauf -----------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--version", "-v", help="Firmware-Version (N.M.P), sonst interaktive Abfrage")
    ap.add_argument("--project-root", type=Path,
                    help="Projekt-Wurzel (Default: Elternordner dieses Skripts)")
    ap.add_argument("--dry-run", action="store_true",
                    help="Nur anzeigen was passieren wuerde, nichts schreiben")
    args = ap.parse_args()

    project_root = args.project_root
    if project_root is None:
        project_root = Path(__file__).resolve().parent.parent
    project_root = project_root.resolve()

    source_dir  = project_root / SOURCE_SUBDIR
    flash_dir   = project_root / FLASH_SUBDIR
    flash_build = project_root / FLASH_BUILD

    if not source_dir.exists():
        print(f"FEHLER: Build-Ordner nicht gefunden:\n    {source_dir}", file=sys.stderr)
        print("  -> Erst 'Export Compiled Binary' in der Arduino IDE ausfuehren.", file=sys.stderr)
        return 2

    version = args.version or prompt_version()
    try:
        mm = major_minor(version)
    except ValueError as e:
        print(f"FEHLER: {e}", file=sys.stderr)
        return 3

    deploy_dir = project_root / DEPLOY_ROOT / mm
    print(f"\nProjekt-Wurzel:   {project_root}")
    print(f"Quelle:           {source_dir}")
    print(f"Deploy-Ziel:      {deploy_dir}")
    print(f"Flash-Build:      {flash_build}")
    print(f"Version:          v{version}")
    print()

    # --- 1) Quell-Dateien sammeln ----------------------------------------
    source_files = sorted(
        p for p in source_dir.iterdir()
        if p.is_file() and p.name.startswith(SOURCE_STEM + ".")
    )
    if not source_files:
        print(f"FEHLER: Keine '{SOURCE_STEM}.*' im Build-Ordner gefunden.", file=sys.stderr)
        return 4

    dest_stem = DEST_STEM_TPL.format(version=version)

    # --- 2) Deploy-Kopieren ----------------------------------------------
    if not args.dry_run:
        deploy_dir.mkdir(parents=True, exist_ok=True)

    print(f"Kopiere nach {deploy_dir}:")
    hex_deploy_path: Path | None = None
    for src in source_files:
        ext = suffix_after_stem(src.name, SOURCE_STEM)
        if ext is None:
            continue
        new_name = f"{dest_stem}.{ext}"
        dst = deploy_dir / new_name
        print(f"  {src.name}  ->  {new_name}")
        if not args.dry_run:
            shutil.copy2(src, dst)
        if ext == "hex":
            hex_deploy_path = dst

    if hex_deploy_path is None:
        print(f"WARNUNG: Keine .hex-Datei unter den Quellen gefunden - "
              "FlashRemoteStick wird nicht aktualisiert.", file=sys.stderr)
        return 0

    # --- 3) Alte HEX in FlashRemoteStick/build/ loeschen -----------------
    print(f"\nAlte HEX in {flash_build} loeschen:")
    if flash_build.exists():
        existing = sorted(flash_build.glob("StickRemote-SteFly-v*.hex"))
        if existing:
            for old in existing:
                print(f"  loesche  {old.name}")
                if not args.dry_run:
                    old.unlink()
        else:
            print("  (keine alte HEX vorhanden)")
    else:
        print(f"  (Ordner {flash_build} existiert noch nicht - wird angelegt)")
        if not args.dry_run:
            flash_build.mkdir(parents=True, exist_ok=True)

    # --- 4) Neue HEX in FlashRemoteStick/build/ kopieren -----------------
    hex_new_name = f"{dest_stem}.hex"
    flash_hex_dst = flash_build / hex_new_name
    print(f"\nKopiere {hex_new_name} nach {flash_build}")
    if not args.dry_run:
        shutil.copy2(hex_deploy_path, flash_hex_dst)

    # --- 5) BINARY= in Wrapper-Skripten patchen --------------------------
    binary_value = BINARY_PATH_TPL.format(version=version)
    print(f"\nBINARY= aktualisieren auf '{binary_value}':")
    for name, cmd_style in (("flash.sh", False), ("flash.cmd", True)):
        wrapper = flash_dir / name
        if not wrapper.exists():
            print(f"  WARNUNG: {name} nicht gefunden - uebersprungen")
            continue
        text = wrapper.read_text(encoding="utf-8")
        new_text, count = update_binary_line(text, binary_value, cmd_style)
        if count == 0:
            print(f"  WARNUNG: keine BINARY-Zeile in {name} gefunden - unveraendert")
        else:
            print(f"  {name}: BINARY-Zeile aktualisiert")
            if not args.dry_run:
                wrapper.write_text(new_text, encoding="utf-8")

    if args.dry_run:
        print("\n(Dry-Run: es wurde nichts geschrieben.)")
    else:
        print(f"\nFertig. Version v{version} deployed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
