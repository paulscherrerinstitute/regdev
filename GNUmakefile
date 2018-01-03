ifeq ($(wildcard /ioc/tools/driver.makefile),)
$(warning It seems you do not have the PSI build environment. Remove GNUmakefile.)
include Makefile
else
include /ioc/tools/driver.makefile

BUILDCLASSES += Linux

SOURCES += regDev.c
SOURCES += regDevSup.c
SOURCES += regDevAaiAao.c
SOURCES += regDevCopy.c
SOURCES += simRegDev.c

SOURCES_3.14 = regDevCalcout.c
DBDS_3.14 += regDevCalcout.dbd simRegDev.dbd
DBDS += regDevBase.dbd regDevAaiAao.dbd 
HEADERS = regDev.h

ifneq ($(wildcard ${EPICS_BASE}/include/int64inRecord.h),)
SOURCES += regDevInt64.c
DBDS += regDevInt64.dbd 
endif

test:
	make -C test test

copytest: regDevCopy.c
	gcc -o copytest regDevCopy.c -DTESTCASE -I /usr/local/epics/base/include -I /usr/local/epics/base/include/os/Linux
	./copytest
	rm copytest
endif

