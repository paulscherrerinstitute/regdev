include /ioc/tools/driver.makefile

ifeq (${EPICS_BASETYPE},3.14)
CROSS_COMPILER_TARGET_ARCHS += linux-crisv10
endif

BUILDCLASSES += Linux

SOURCES += regDev.c
SOURCES_Linux += mmapDev.c
DBDS += regDev.dbd
DBDS_Linux += mmapDev.dbd
