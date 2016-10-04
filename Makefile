all: duptest.exe

clean:
	del duptest.exe

duptest.exe: duptest.cpp
	cl /Zi /EHsc /oduptest.exe ws2_32.lib duptest.cpp
