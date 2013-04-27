Jedi Academy with various changes to make it build/run on
more platforms including amd64/x86_64.

Currently only the single player code is built.

The game needs to be patched to 1.01 to work, the data in the
steam version is already patched.

	How to build:

mkdir build-sp && cd build-sp
cmake ../code/
make

copy jasp and jagame*.so to your game data directory

	Known issues:

When running windowed the mouse does not work in the menus.

Save games do not yet work on amd64.

The demo has various issues and does not work properly.
