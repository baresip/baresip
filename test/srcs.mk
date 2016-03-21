#
# srcs.mk All application source files.
#
# Copyright (C) 2010 Creytiv.com
#


#
# Test-cases:
#
TEST_SRCS	+= cmd.c
TEST_SRCS	+= ua.c
TEST_SRCS	+= cplusplus.c
TEST_SRCS	+= call.c
TEST_SRCS	+= mos.c


#
# Mocks
#
TEST_SRCS	+= mock/sipsrv.c
ifneq ($(USE_TLS),)
TEST_SRCS	+= mock/cert.c
endif


TEST_SRCS	+= test.c

TEST_SRCS	+= main.c
