# DUDE-Star RX
Software monitor of D-STAR, DMR, and Fusion YSF reflectors and repeaters/gateways over UDP

This software connects to D-STAR, DMR and Fusion reflectors and gateways/repeaters over UDP.  Decoding of the audio is done via the mbelib C library and DSDcc C++ library.  Mbelib can be found here:

https://github.com/szechyjs/mbelib

The relevent portions of the DSDcc library are included, with the modifications to allow decoding of internet data instead of RF data.

# Usage
Host/Mod: Select the desired host and module (for D-STAR) from the selections.

Callsign:  Enter your amateur radio callsign.  A valid license is required to use this software.  A valid DMR ID is required to connect to DMR servers.  Contact me via QRZ (AD8DP) if your DMR ID is not found.

TG:  For DMR, enter the talkgroup ID number.  A very active TG for testing functionality on Brandmeister is 91 (Brandmeister Worldwide)

Hit connect with these fields correctly populated and enjoy listening.

# Compiling on Linux
This software is written in C++ on Linux and requires mbelib and QT5, and natually the devel packages to build.  With these requirements met, run the following:
```
qmake
make
```
qmake may have a different name on your distribution i.e. on Fedora it's called qmake-qt5

# Builds
There is currently a 32-bit Windows executable available in the builds directory.  QT and mbelib are statically linked, no dependencies are required.
There is also an Android build called DROID-Star at the Play Store as a beta release.

# Todo
DMR and P25 support coming soon.
