include /ioc/tools/driver.makefile

BUILDCLASSES += Linux

SOURCES += regDev.c
SOURCES += mmapDrv.c
DBDS += regDev.dbd
DBDS += mmapDrv.dbd
HEADERS = regDev.h
