deploy version.py (*.cmd)
-------------------------
* kopiert die build/arduino.avr.stefly_remote/StickRemote.ino.* Dateien  als StickRemote-SteFly-vXXX.* version in das Verzeichnis deploy/3.0/
* kopiert dann StickRemote-SteFly-vXXX.hex in das FlashRemoteStick/build/ verzeichnis - und ändert die flash.sh sowie flash.cmd entsprechend
* die alte Datei in FlashRemoteStick/build/ wird gelöscht...

programmer.py
-------------