#
# module.mk
#
# Copyright (C) 2010 Alfred E. Heggestad
#

MOD		:= hisilicon
$(MOD)_SRCS	+= hisi.c hisi_src.c hisi_play.c
$(MOD)_LFLAGS	+= -L$(HISI_SDK_DIR)/mpp/lib
$(MOD)_LFLAGS	+= -lmpi -lVoiceEngine -lsecurec -lupvqe -ldnvqe
$(MOD)_CFLAGS	+= -I-L$(HISI_SDK_DIR)/mpp/include

include mk/mod.mk
