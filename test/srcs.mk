#
# srcs.mk All application source files.
#
# Copyright (C) 2010 Creytiv.com
#


#
# Test-cases:
#
TEST_SRCS	+= cmd.c
TEST_SRCS	+= contact.c
TEST_SRCS	+= ua.c
TEST_SRCS	+= cplusplus.c
TEST_SRCS	+= call.c
TEST_SRCS	+= mos.c
TEST_SRCS	+= net.c


#
# Mocks
#
TEST_SRCS	+= mock/dnssrv.c

TEST_SRCS	+= sip/aor.c
TEST_SRCS	+= sip/auth.c
TEST_SRCS	+= sip/domain.c
TEST_SRCS	+= sip/location.c
TEST_SRCS	+= sip/sipsrv.c
TEST_SRCS	+= sip/user.c

ifneq ($(USE_TLS),)
TEST_SRCS	+= mock/cert.c
endif

TEST_SRCS	+= mock/mock_ausrc.c


TEST_SRCS	+= test.c

TEST_SRCS	+= main.c
