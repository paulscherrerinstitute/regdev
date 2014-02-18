include /ioc/tools/driver.makefile

BUILDCLASSES += Linux
USR_CFLAGS+=-DWITH_AAIO

SOURCES += regDev.c
SOURCES += regDevSup.c
SOURCES += regDevAaiAao.c
SOURCES += regDevCopy.c
SOURCES += simRegDev.c

SOURCES_3.14 = regDevCalcout.c
DBDS_3.14 += regDevCalcout.dbd simRegDev.dbd
DBDS += regDev.dbd regDevAaiAao.dbd 
HEADERS = regDev.h
CMPLR = ANSI

test:
	make -C test test

copytest: regDevCopy.c
	gcc -o copytest regDevCopy.c -DTESTCASE -I /usr/local/epics/base/include -I /usr/local/epics/base/include/os/Linux
	./copytest
	rm copytest
