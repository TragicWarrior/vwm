REQUIRMENTS

CMake
ncurses 5.4+
protothread - https://github.com/LarryRuane/protothread
libviper - https://github.com/TragicWarrior/libviper
libconfig
libgpm (optional)
libvterm - https://github.com/TragicWarrior/libvterm


INSTALLATION

By default, build system tries to install plugins (shared libraries) in the
/usr/local/lib/ directory.  

For a simple installation run the following make commands as root:

cmake CMakeList.txt
make
sudo make install

Enjoy!
