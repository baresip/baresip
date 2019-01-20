/**
 * @file mctrl.c  Media Control
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include "core.h"


/*
 * RFC 5168 XML Schema for Media Control
 * note: deprecated, use RTCP FIR instead
 *
 *
 * Example XML Document:
 *
 * <pre>

   <?xml version="1.0" encoding="utf-8"?>
     <media_control>
       <vc_primitive>
         <to_encoder>
           <picture_fast_update>
	   </picture_fast_update>
	 </to_encoder>
       </vc_primitive>
     </media_control>

  </pre>
 */
int mctrl_handle_media_control(struct pl *body, bool *pfu)
{
	if (!body)
		return EINVAL;

	/* Poor-mans XML parsing */
	if (0 == re_regex(body->p, body->l, "picture_fast_update")) {
		if (pfu)
			*pfu = true;
	}

	return 0;
}
