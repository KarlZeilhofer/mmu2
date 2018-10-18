Changelog for DIY MMU 2.0
=========================



# 2018-10-18
* refactored repository
* restructured folders and filenames
* add platformio.ini
* add wrapper class Application for compatibility with
  Arduino IDE and Platformio with QtCreator.
  Tested compiling with Arduino IDE 1.8.5
* reduced RAM usage using progmem macro F() from 139% to 45%
  for Arduino Leonardo
* add config.h
* fixed all compiler warnings. Some were critical with integer overflow!
  (works with all config flags in config.h enabled or disabled)
