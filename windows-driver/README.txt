SteFly Stick Remote - Driver Installation
=========================================

This driver package makes Windows display the SteFly Stick Remote with the
correct name in Device Manager ("SteFly Stick Remote" instead of "USB Serial
Device"). The actual driver used is the Windows built-in usbser.sys - no
third-party kernel driver is installed.

Installation (once per PC):
---------------------------

  1) install-cert.cmd
       Right-click -> "Run as administrator"
       Adds the SteFly signing certificate to the trusted publishers list.

  2) install-driver.cmd
       Right-click -> "Run as administrator"
       Installs the INF system-wide into the Windows driver store.

  3) Plug in the stick (or unplug and replug it if it was already connected).

  4) Device Manager will now show the stick as "SteFly Stick Remote (COMx)"
     under "Ports (COM & LPT)".

Troubleshooting
---------------

  * "Run as administrator" is mandatory - a regular double-click is NOT enough
    because the Windows driver store requires elevated permissions.

  * If install-driver.cmd reports "INF not signed": install-cert.cmd was not
    executed first, or did not successfully populate both stores (Root and
    TrustedPublisher).

  * The stick works WITHOUT this driver installation as well - Windows will
    just use the generic name "USB Serial Device (COMx)", but the HID keyboard
    and mouse functions will operate normally.

Support
-------

SteFly.aero - contact details at https://www.stefly.aero
