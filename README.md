# armcommlib

Architecture:

	- AArch64cryptolib: library of cryptographic functions of ARM A-class cores.
	- armcommlib: original library
	- test: main.c and client-server sockets system

Compilation:

	- make = compile AArch64cryptolib
	- make all = compile armcommlib
	- In tests/ --> gcc -o main main.c ../armcommlib/libarmcommlib.so ../AArch64cryptolib/libAArch64crypto.a

Execution:

	- LD_LIBRARY_PATH="home/pi/armcommlib;$LD_LIBRARY_PATH" ./main  
