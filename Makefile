TOP=.

include $(TOP)/configure/CONFIG

regDev_DBD += base.dbd

# library
LIBRARY = regDev

INC += regDev.h
DBDS = regDev.dbd

LIB_SRCS += regDev.c
LIB_SRCS += regDevCopy.c
LIB_SRCS += regDevSup.c
regDev_DBD += regDevBase.dbd

# Check EPICS base version for available record types
ifeq ($(BASE_3_14),YES)
ifeq ($(shell $(PERL) -e 'print $(EPICS_MODIFICATION)>=12'),1)
WITH_AAIO=YES
endif
ifeq ($(shell $(PERL) -e 'print $(EPICS_MODIFICATION)>=5'),1)
WITH_CALCOUT=YES
endif
endif

ifdef BASE_3_15
WITH_CALCOUT=YES
WITH_AAIO=YES
endif

ifdef WITH_AAIO
LIB_SRCS += regDevAaiAao.c
regDev_DBD += regDevAaiAao.dbd
endif

ifdef WITH_CALCOUT
LIB_SRCS += regDevCalcout.c
regDev_DBD += regDevCalcout.dbd
endif

ifneq ($(wildcard ${EPICS_BASE}/include/lsiRecord.h),)
LIB_SRCS += regDevLsiLso.c
regDev_DBD += regDevLsiLso.dbd
endif

ifneq ($(wildcard ${EPICS_BASE}/include/int64inRecord.h),)
LIB_SRCS += regDevInt64.c
regDev_DBD += regDevInt64.dbd
endif

LIB_SRCS += simRegDev.c
regDev_DBD += simRegDev.dbd

LIB_LIBS += memDisplay
LIB_LIBS += $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
