include /ioc/tools/driver.makefile

BUILDCLASSES += Linux
USR_CFLAGS+=-DWITH_AAIO

SOURCES = regDev.c regDevSup.c regDevAaiAao.c regDevCopy.c simRegDev.c simRegDevAsync.c 
SOURCES_3.14 = regDevCalcout.c
DBDS_3.14 += regDevCalcout.dbd simRegDev.dbd simRegDevAsync.dbd
DBDS += regDev.dbd regDevAaiAao.dbd 
HEADERS = regDev.h
CMPLR = ANSI

test:
	make -C test test
