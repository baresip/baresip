#
# module.mk
#
# Copyright (C) 2010 Creytiv.com
#

# opencore-amr source directory
AMR_PATH ?= ../amr

MOD		:= amr
$(MOD)_SRCS	+= amr.c sdp.c


ifneq ($(shell [ -d $(SYSROOT)/include/opencore-amrnb ] && echo 1 ),)
$(MOD)_CFLAGS	+= -DAMR_NB=1 -I$(SYSROOT)/include/opencore-amrnb
$(MOD)_LFLAGS	+= -lopencore-amrnb
else
ifneq ($(shell [ -d $(SYSROOT_LOCAL)/include/opencore-amrnb ] && echo 1 ),)
$(MOD)_CFLAGS	+= -DAMR_NB=1 -I$(SYSROOT_LOCAL)/include/opencore-amrnb
$(MOD)_LFLAGS	+= -lopencore-amrnb
else
ifneq ($(shell [ -d $(SYSROOT_ALT)/include/opencore-amrnb ] && echo 1 ),)
$(MOD)_CFLAGS	+= -DAMR_NB=1 -I$(SYSROOT_ALT)/include/opencore-amrnb
$(MOD)_LFLAGS	+= -lopencore-amrnb
else
ifneq ($(shell [ -d $(SYSROOT)/local/include/amrnb ] && echo 1),)
$(MOD)_CFLAGS	+= -DAMR_NB=1 -I$(SYSROOT)/local/include/amrnb
$(MOD)_LFLAGS	+= -lamrnb
else
ifneq ($(shell [ -d $(SYSROOT)/include/amrnb ] && echo 1),)
$(MOD)_CFLAGS	+= -DAMR_NB=1 -I$(SYSROOT)/include/amrnb
$(MOD)_LFLAGS	+= -lamrnb
else
ifneq ($(shell [ -d $(AMR_PATH)/include/opencore-amrnb ] && echo 1),)
$(MOD)_CFLAGS	+= -DAMR_NB=1 -I$(AMR_PATH)/include/opencore-amrnb
$(MOD)_LFLAGS	+= -lamrnb
endif
endif
endif
endif
endif
endif


ifneq ($(shell [ -f $(SYSROOT_ALT)/include/opencore-amrwb/enc_if.h ] && \
	echo 1 ),)
$(MOD)_CFLAGS	+= -DAMR_WB=1 -I$(SYSROOT_ALT)/include/opencore-amrwb
$(MOD)_LFLAGS	+= -lopencore-amrwb
else
ifneq ($(shell [ -f $(SYSROOT_LOCAL)/include/opencore-amrwb/enc_if.h ] && \
	echo 1 ),)
$(MOD)_CFLAGS	+= -DAMR_WB=1 -I$(SYSROOT_LOCAL)/include/opencore-amrwb
$(MOD)_LFLAGS	+= -lopencore-amrwb
else
ifneq ($(shell [ -f $(SYSROOT)/local/include/amrwb/enc_if.h ] && echo 1),)
$(MOD)_CFLAGS	+= -DAMR_WB=1 -I$(SYSROOT)/local/include/amrwb
$(MOD)_LFLAGS	+= -lamrwb
else
ifneq ($(shell [ -f $(SYSROOT)/include/amrwb/enc_if.h ] && echo 1),)
$(MOD)_CFLAGS	+= -DAMR_WB=1 -I$(SYSROOT)/include/amrwb
$(MOD)_LFLAGS	+= -lamrwb
else
ifneq ($(shell [ -f $(SYSROOT)/include/vo-amrwbenc/enc_if.h ] && echo 1),)
$(MOD)_CFLAGS	+= -DAMR_WB=1 -I$(SYSROOT)/include/vo-amrwbenc
$(MOD)_LFLAGS	+= -lvo-amrwbenc
endif
endif
endif
endif
endif


# extra for decoder
ifneq ($(shell [ -f $(SYSROOT)/include/opencore-amrwb/dec_if.h ] && echo 1 ),)
$(MOD)_CFLAGS	+= -I$(SYSROOT)/include/opencore-amrwb
$(MOD)_LFLAGS	+= -lopencore-amrwb
endif


$(MOD)_LFLAGS	+= -lm


include mk/mod.mk
