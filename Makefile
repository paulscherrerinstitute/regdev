TOP=.

include $(TOP)/configure/CONFIG

regDev_DBD += base.dbd

# library
LIBRARY = regDev
LIB_SRCS += regDev_registerRecordDeviceDriver.cpp

INC += regDev.h
DBDS = regDev.dbd

LIB_SRCS += regDev.c
LIB_SRCS += regDevCopy.c
LIB_SRCS += regDevSup.c
regDev_DBD += regDevBase.dbd

LIB_SRCS += regDevAaiAao.c
regDev_DBD += regDevAaiAao.dbd

LIB_SRCS += regDevCalcout.c
regDev_DBD += regDevCalcout.dbd

LIB_SRCS += simRegDev.c
regDev_DBD += simRegDev.dbd

LIB_LIBS += $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
