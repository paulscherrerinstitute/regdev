include /ioc/tools/driver.makefile

BUILDCLASSES += Linux

SOURCES += regDev.c
DBDS_3.14 += regDevCalcout.dbd
DBDS += regDev.dbd
HEADERS = regDev.h
