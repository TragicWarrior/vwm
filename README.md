REQUIRMENTS
===========

CMake
ncurses 5.4+
protothread - https://github.com/LarryRuane/protothread
libviper 2.1.5+  - https://github.com/TragicWarrior/libviper
libconfig
libgpm (optional)
libvterm 3.1+ - https://github.com/TragicWarrior/libvterm

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

To add "programs" to the menu, it must by done by editing the vwmrc file
which is located in ~/.vwm/

An sample configuration file is located at samples/vwmrc which can easily be
editted to support your binary paths.

Enjoy!
