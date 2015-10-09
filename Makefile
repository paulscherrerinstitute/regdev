TOP=.

include $(TOP)/configure/CONFIG

regDev_DBD += base.dbd

# library
LIBRARY = regDev
LIB_SRCS += regDev_registerRecordDeviceDriver.cpp

INC += regDev.h

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

# stand alone application program

#PROD_DEFAULT = s7plcApp
#PROD_vxWorks = -nil-
#s7plcApp_SRCS += s7plcApp_registerRecordDeviceDriver.cpp
#s7plcApp_SRCS += appMain.cc
#s7plcApp_LIBS += s7plc
#s7plcApp_LIBS += $(EPICS_BASE_IOC_LIBS)
#s7plcApp_DBD += base.dbd
#s7plcApp_DBD += s7plc.dbd
#DBD += s7plcApp.dbd


include $(TOP)/configure/RULES
