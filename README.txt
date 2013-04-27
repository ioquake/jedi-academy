Jedi Academy with various changes to make it build/run on
more platforms including amd64/x86_64.

Currently only the single player code runs on amd64.

The game needs to be patched to 1.01 to work, the data in the
steam version is already patched.

	How to build single player:

mkdir build-sp && cd build-sp
cmake ../code/
make

copy jasp and jagame*.so to the directory containing base or demo

	How to build multiplayer:

mkdir build-mp && cd build-mp
cmake ../codemp/
make

copy jamp and jampded to the directory containing base
copy *.so to your base directory

	Known issues:

When running windowed the mouse does not work in the menus.

Save games do not yet work on amd64.

The demo has various issues and does not work properly.

multiplayer has to be run with "+set sv_pure 0" for now
