/**
 * @file magic.h  Interface to magic macros
 *
 * Copyright (C) 2010 Creytiv.com
 */


#ifndef RELEASE

#ifndef MAGIC
#error "macro MAGIC must be defined"
#endif


/*
 * Any C compiler conforming to C99 or later MUST support __func__
 */
#if __STDC_VERSION__ >= 199901L
#define __MAGIC_FUNC__ (const char *)__func__
#else
#define __MAGIC_FUNC__ __FUNCTION__
#endif


/** Check magic number */
#define MAGIC_DECL uint32_t magic;
#define MAGIC_INIT(s) (s)->magic = MAGIC
#define MAGIC_CHECK(s) \
	if (MAGIC != s->magic) {					\
		warning("%s: wrong magic struct=%p (magic=0x%08x)\n",	\
			__MAGIC_FUNC__, s, s->magic);			\
		BREAKPOINT;						\
	}
#else
#define MAGIC_DECL
#define MAGIC_INIT(s)
#define MAGIC_CHECK(s) do {(void)(s);} while (0);
#endif
