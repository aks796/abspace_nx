Angry Birds Space for Nintendo Switch (abspace_nx)
===================================================

Runs the Android game Angry Birds Space HD on a Switch with Atmosphere. It
contains no game data: you supply the APK of a copy you own.

1. Copy switch/abspace_nx/ to the SD card.
2. Put your APK of Angry Birds Space HD 2.2.14 (com.rovio.angrybirdsspaceHD,
   armeabi-v7a) in sd:/switch/abspace_nx/. Any file name ending in .apk.
3. In sphaira: Homebrew > Angry Birds Space > Install Forwarder.
4. Launch the new Angry Birds Space icon on the HOME menu. The first start
   installs the game program for that icon, restarts, and unpacks the game's
   library from the APK (once).

Updating from an older build (folder sd:/switch/abspace): copy abspace_nx.nro
into that old folder and start the game from its usual icon. It updates
itself, then moves your APK, settings, saves, videos and mod into
sd:/switch/abspace_nx. The old AngryBirdsSpace.nro stays behind.

Controls: in a level, the left stick aims, A launches and uses the bird's
power, R switches to the hand cursor. In the menus the stick or D-pad moves
between buttons and the right stick brings out the hand cursor. The touch
screen works as on a phone. The full list is in README.md.

Angry Birds Orbital Escapade (optional), ShadowBird81's PC mod:
    https://gamejolt.com/games/OrbitalEscapade/1016788
Extract it and copy its data folder (or the whole extracted folder) into
sd:/switch/abspace_nx/. Its two worlds, Vege-toids and Omelettification,
appear as two more planets after Danger Zone. See orbital/README.txt.

Settings: sd:/switch/abspace_nx/config.ini (written at the first start).
Saves:    sd:/switch/abspace_nx/data/files/
Problems: sd:/switch/abspace_nx/debug.log and crash.log
