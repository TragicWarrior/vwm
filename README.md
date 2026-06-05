REQUIREMENTS
============

CMake
ncursesw 5.4+
libviper 3.0.0+  - https://github.com/TragicWarrior/libviper
libgpm (optional)
libvterm 9.0+ - https://github.com/TragicWarrior/libvterm
FreeType         (for the screen-capture module; "make all")
libcups2-dev     (for the print module; "make all")

INSTALLATION
============

By default, build system tries to install plugins (shared libraries) in the
/usr/local/lib/ directory.  

For a simple installation run the following make commands as root:

cmake CMakeList.txt
make
sudo make install

CONFIGURATION
=============

To add "programs" to the menu, edit the JSON configuration file located at
~/.config/vwm/config.json (created with sane defaults on first run).

A sample configuration file is located at samples/config.json which can
easily be edited to support your binary paths.

Enjoy!
