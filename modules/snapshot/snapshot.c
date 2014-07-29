/**
 * @file snapshot.c  Snapshot Video-Filter
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <time.h>
#include "snapshot.h"
#include "png_vf.h"
#include "jpg_vf.h"

static bool flag_enc, flag_dec;
static bool flag_enc_j, flag_dec_j;
static bool flag_preview_j, flag_preview;

static char filename_buf[128];
static char previewfn_buf[128];

static int make_preview(const struct vidframe **preview,const struct vidframe *vf)
{
	int err = 0;
	struct vidsz ns;
	struct vidframe *f2 = NULL;
	
	ns.w = 160;
	ns.h = 120;
	
	err |= vidframe_alloc(&f2, VID_FMT_RGB32, &ns);
	if (err) return err;
	vidconv(f2, vf, NULL);

	*preview = f2;

	return 0;
}

static int encode(struct vidfilt_enc_st *st, struct vidframe *frame)
{
	(void)st;

	struct vidframe *preview;
	
	if (!frame)
		return 0;

	if (flag_enc) 
	{
		flag_enc = false;
		make_filename("snapshot-send",filename_buf,sizeof(filename_buf),"png");
		png_save_vidframe(frame, filename_buf);
		filename_buf[0]=0;
	}
	
	if (flag_enc_j) 
	{
		flag_enc_j = false;
		make_filename("snapshot-send",filename_buf,sizeof(filename_buf),"jpg");
		jpg_save_vidframe(frame, filename_buf);
		filename_buf[0]=0;
	}

	return 0;
}


static int decode(struct vidfilt_dec_st *st, struct vidframe *frame)
{
	(void)st;

	struct vidframe *preview;

	if (!frame)
		return 0;

	if (flag_dec) 
	{
		flag_dec = false;

		if (!filename_buf[0]) 
			make_filename("snapshot-recv",filename_buf,sizeof(filename_buf),"png");

		png_save_vidframe(frame, filename_buf);

		if (flag_preview)
		{
			if (make_preview(&preview, frame)==0)
			{
				png_save_vidframe(preview, previewfn_buf);			
				mem_deref(preview);
			}
			flag_preview = false;
		}
		    

		filename_buf[0]=0;
	}

	if (flag_dec_j) 
	{
		flag_dec_j = false;
		if (!filename_buf[0]) 
			make_filename("snapshot-recv",filename_buf,sizeof(filename_buf),"jpg");

		jpg_save_vidframe(frame, filename_buf);

		if (flag_preview_j)
		{
			if (make_preview(&preview, frame)==0)
			{
				jpg_save_vidframe(preview, previewfn_buf);
				mem_deref(preview);
			}
			flag_preview_j = false;
		}

		filename_buf[0]=0;
	}

	return 0;
}

struct snapshot_arg
{
	char	*filename;
	char	*preview_fn;
	char	fmt;   // 'o' || 'p'
	char	com2;  // 0 = nothing 1 = preview
};

static int do_snapshot(struct re_printf *pf, struct cmd_arg *arg)
{
	(void)pf;

	/* NOTE: not re-entrant */
	flag_enc = flag_dec = true;
	filename_buf[0]=0;
	info("PNG snapshot request");
	if (pf->arg!=NULL)
	{
		struct snapshot_arg * sarg = (struct snapshot_arg *) pf->arg;

		info(" into: ");info(sarg->filename);
		if (sarg->com2==1)
		{
			info(" (preview ");info(sarg->preview_fn);
			info(")");
		}
		info("\n");					

		strncpy(filename_buf,sarg->filename,128);
		strncpy(previewfn_buf,sarg->preview_fn,128);
		
		flag_preview = (sarg->com2==1);
		// only one of them
		flag_enc = false;
	}
		
	return 0;
}

static int do_snapshot_j(struct re_printf *pf,  struct cmd_arg *arg)
{
	(void)pf;

	/* NOTE: not re-entrant */
	flag_enc_j = flag_dec_j = true;
	filename_buf[0]=0;
	info("JPG snapshot request");
	if (pf->arg!=NULL)
	{
		struct snapshot_arg * sarg = (struct snapshot_arg *) pf->arg;

		info(" into: ");info(sarg->filename);
		if (sarg->com2==1)
		{
			info(" (preview ");info(sarg->preview_fn);
			info(")");
		}
		info("\n");					

		strncpy(filename_buf,sarg->filename,128);
		strncpy(previewfn_buf,sarg->preview_fn,128);

		flag_preview_j = (sarg->com2==1);
		// only one of them
		flag_enc_j = false;
	}

	return 0;
}

static struct vidfilt snapshot = {
	LE_INIT, "snapshot", NULL, encode, NULL, decode,
};

static const struct cmd cmdv[] = {
	{'o', 0, "Take png video snapshot", do_snapshot },
	{'p', 0, "Take jpg video snapshot", do_snapshot_j },
};


static int module_init(void)
{
	vidfilt_register(&snapshot);
	info("Snapshot: PNG JPG\n");
	return  cmd_register(cmdv, ARRAY_SIZE(cmdv));
}


static int module_close(void)
{
	vidfilt_unregister(&snapshot);
	cmd_unregister(cmdv);
	return 0;
}

static char *make_filename(const char *name,
			  char *buf, unsigned int length, const char *ext)
{
	/*
	 * -2013-03-03-15-22-56.jpg - 24 chars
	 */
	if (strlen(name) + 24 >= length) {
		buf[0] = '\0';
		return buf;
	}

	time_t tnow = time(NULL);
	const struct tm *tmx = localtime(&tnow);
	
	sprintf(buf, (tmx->tm_mon < 9 ? "%s-%d-0%d" : "%s-%d-%d"), name,
		1900 + tmx->tm_year, tmx->tm_mon + 1);

	sprintf(buf + strlen(buf), (tmx->tm_mday < 10 ? "-0%d" : "-%d"),
		tmx->tm_mday);

	sprintf(buf + strlen(buf), (tmx->tm_hour < 10 ? "-0%d" : "-%d"),
		tmx->tm_hour);

	sprintf(buf + strlen(buf), (tmx->tm_min < 10 ? "-0%d" : "-%d"),
		tmx->tm_min);

	sprintf(buf + strlen(buf), (tmx->tm_sec < 10 ? "-0%d.%s" : "-%d.%s"),
		tmx->tm_sec,ext);

	return buf;
}

EXPORT_SYM const struct mod_export DECL_EXPORTS(snapshot) = {
	"snapshot",
	"vidfilt",
	module_init,
	module_close
};

