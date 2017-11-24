/**
 * @file rst.c MP3/ICY HTTP AV Source
 *
 * Copyright (C) 2011 Creytiv.com
 */

#include <string.h>
#include <re.h>
#include <rem.h>
#include <baresip.h>
#include "rst.h"


/**
 * @defgroup rst rst
 *
 * Audio and video source module using mpg123 as input
 *
 * The module 'rst' is using the mpg123 to play streaming
 * media (MP3) and provide this as an internal audio/video source.
 *
 * Example config:
 \verbatim
  audio_source        rst,http://relay.slayradio.org:8000/
  video_source        rst,http://relay.slayradio.org:8000/
 \endverbatim
 */


enum {
	RETRY_WAIT = 10000,
};


struct rst {
	const char *id;
	struct ausrc_st *ausrc_st;
	struct vidsrc_st *vidsrc_st;
	struct tmr tmr;
	struct dns_query *dnsq;
	struct tcp_conn *tc;
	struct mbuf *mb;
	char *host;
	char *path;
	char *name;
	char *meta;
	bool head_recv;
	size_t metaint;
	size_t metasz;
	size_t bytec;
	uint16_t port;
};


static int rst_connect(struct rst *rst);


static void destructor(void *arg)
{
	struct rst *rst = arg;

	tmr_cancel(&rst->tmr);
	mem_deref(rst->dnsq);
	mem_deref(rst->tc);
	mem_deref(rst->mb);
	mem_deref(rst->host);
	mem_deref(rst->path);
	mem_deref(rst->name);
	mem_deref(rst->meta);
}


static void reconnect(void *arg)
{
	struct rst *rst = arg;
	int err;

	rst->mb   = mem_deref(rst->mb);
	rst->name = mem_deref(rst->name);
	rst->meta = mem_deref(rst->meta);

	rst->head_recv = false;
	rst->metaint   = 0;
	rst->metasz    = 0;
	rst->bytec     = 0;

	err = rst_connect(rst);
	if (err)
		tmr_start(&rst->tmr, RETRY_WAIT, reconnect, rst);
}


static void recv_handler(struct mbuf *mb, void *arg)
{
	struct rst *rst = arg;
	size_t n;

	if (!rst->head_recv) {

		struct pl hdr, name, metaint, eoh;

		if (rst->mb) {
			size_t pos;
			int err;

			pos = rst->mb->pos;

			rst->mb->pos = rst->mb->end;

			err = mbuf_write_mem(rst->mb, mbuf_buf(mb),
					     mbuf_get_left(mb));
			if (err) {
				warning("rst: buffer write error: %m\n", err);
				rst->tc = mem_deref(rst->tc);
				tmr_start(&rst->tmr, RETRY_WAIT,
					  reconnect, rst);
				return;
			}

			rst->mb->pos = pos;
		}
		else {
			rst->mb = mem_ref(mb);
		}

		if (re_regex((const char *)mbuf_buf(rst->mb),
			     mbuf_get_left(rst->mb),
			     "[^\r\n]1\r\n\r\n", &eoh))
			return;

		rst->head_recv = true;

		hdr.p = (const char *)mbuf_buf(rst->mb);
		hdr.l = eoh.p + 5 - hdr.p;

		if (!re_regex(hdr.p, hdr.l, "icy-name:[ \t]*[^\r\n]+\r\n",
			      NULL, &name))
			(void)pl_strdup(&rst->name, &name);

		if (!re_regex(hdr.p, hdr.l, "icy-metaint:[ \t]*[0-9]+\r\n",
			      NULL, &metaint))
			rst->metaint = pl_u32(&metaint);

		if (rst->metaint == 0) {
			info("rst: icy meta interval not available\n");
			rst->tc = mem_deref(rst->tc);
			tmr_start(&rst->tmr, RETRY_WAIT, reconnect, rst);
			return;
		}

		rst_video_update(rst->vidsrc_st, rst->name, NULL);

		rst->mb->pos += hdr.l;

		info("rst: name='%s' metaint=%zu\n", rst->name, rst->metaint);

		if (rst->mb->pos >= rst->mb->end)
			return;

		mb = rst->mb;
	}

	while (mb->pos < mb->end) {

		if (rst->metasz > 0) {

			n = min(mbuf_get_left(mb), rst->metasz - rst->bytec);

			if (rst->meta)
				mbuf_read_mem(mb,
					     (uint8_t *)&rst->meta[rst->bytec],
					      n);
			else
				mb->pos += n;

			rst->bytec += n;
#if 0
			info("rst: metadata %zu bytes\n", n);
#endif
			if (rst->bytec >= rst->metasz) {
#if 0
				info("rst: metadata: [%s]\n", rst->meta);
#endif
				rst->metasz = 0;
				rst->bytec  = 0;

				rst_video_update(rst->vidsrc_st, rst->name,
						 rst->meta);
			}
		}
		else if (rst->bytec < rst->metaint) {

			n = min(mbuf_get_left(mb), rst->metaint - rst->bytec);

			rst_audio_feed(rst->ausrc_st, mbuf_buf(mb), n);

			rst->bytec += n;
			mb->pos    += n;
#if 0
			info("rst: mp3data %zu bytes\n", n);
#endif
		}
		else {
			rst->metasz = mbuf_read_u8(mb) * 16;
			rst->bytec  = 0;

			rst->meta = mem_deref(rst->meta);
			rst->meta = mem_zalloc(rst->metasz + 1, NULL);
#if 0
			info("rst: metalength %zu bytes\n", rst->metasz);
#endif
		}
	}
}


static void estab_handler(void *arg)
{
	struct rst *rst = arg;
	struct mbuf *mb;
	int err;

	info("rst: connection established\n");

	mb = mbuf_alloc(512);
	if (!mb) {
		err = ENOMEM;
		goto out;
	}

	err = mbuf_printf(mb,
			  "GET %s HTTP/1.0\r\n"
			  "Icy-MetaData: 1\r\n"
			  "\r\n",
			  rst->path);
	if (err)
		goto out;

	mb->pos = 0;

	err = tcp_send(rst->tc, mb);
	if (err)
		goto out;

 out:
	if (err) {
		warning("rst: error sending HTTP request: %m\n", err);
	}

	mem_deref(mb);
}


static void close_handler(int err, void *arg)
{
	struct rst *rst = arg;

	info("rst: tcp closed: %m\n", err);

	rst->tc = mem_deref(rst->tc);

	tmr_start(&rst->tmr, RETRY_WAIT, reconnect, rst);
}


static void dns_handler(int err, const struct dnshdr *hdr, struct list *ansl,
			struct list *authl, struct list *addl, void *arg)
{
	struct rst *rst = arg;
	struct dnsrr *rr;
	struct sa srv;

	(void)err;
	(void)hdr;
	(void)authl;
	(void)addl;

	rr = dns_rrlist_find(ansl, rst->host, DNS_TYPE_A, DNS_CLASS_IN, true);
	if (!rr) {
		warning("rst: unable to resolve: %s\n", rst->host);
		tmr_start(&rst->tmr, RETRY_WAIT, reconnect, rst);
		return;
	}

	sa_set_in(&srv, rr->rdata.a.addr, rst->port);

	err = tcp_connect(&rst->tc, &srv, estab_handler, recv_handler,
			  close_handler, rst);
	if (err) {
		warning("rst: tcp connect error: %m\n", err);
		tmr_start(&rst->tmr, RETRY_WAIT, reconnect, rst);
		return;
	}
}


static int rst_connect(struct rst *rst)
{
	struct sa srv;
	int err;

	if (!sa_set_str(&srv, rst->host, rst->port)) {

		err = tcp_connect(&rst->tc, &srv, estab_handler, recv_handler,
				  close_handler, rst);
		if (err) {
			warning("rst: tcp connect error: %m\n", err);
		}
	}
	else {
		err = dnsc_query(&rst->dnsq, net_dnsc(baresip_network()),
				 rst->host, DNS_TYPE_A,
				 DNS_CLASS_IN, true, dns_handler, rst);
		if (err) {
			warning("rst: dns query error: %m\n", err);
		}
	}

	return err;
}


int rst_alloc(struct rst **rstp, const char *dev)
{
	struct pl host, port, path;
	struct rst *rst;
	int err;

	if (!rstp || !dev)
		return EINVAL;

	if (re_regex(dev, strlen(dev), "http://[^:/]+[:]*[0-9]*[^]+",
		     &host, NULL, &port, &path)) {
		warning("rst: bad http url: %s\n", dev);
		return EBADMSG;
	}

	rst = mem_zalloc(sizeof(*rst), destructor);
	if (!rst)
		return ENOMEM;

	rst->id = "rst";

	err = pl_strdup(&rst->host, &host);
	if (err)
		goto out;

	err = pl_strdup(&rst->path, &path);
	if (err)
		goto out;

	rst->port = pl_u32(&port);
	rst->port = rst->port ? rst->port : 80;

	err = rst_connect(rst);
	if (err)
		goto out;

 out:
	if (err)
		mem_deref(rst);
	else
		*rstp = rst;

	return err;
}


void rst_set_audio(struct rst *rst, struct ausrc_st *st)
{
	if (!rst)
		return;

	rst->ausrc_st = st;
}


void rst_set_video(struct rst *rst, struct vidsrc_st *st)
{
	if (!rst)
		return;

	rst->vidsrc_st = st;
}


static int module_init(void)
{
	int err;

	err = rst_audio_init();
	if (err)
		goto out;

	err = rst_video_init();
	if (err)
		goto out;

 out:
	if (err) {
		rst_audio_close();
		rst_video_close();
	}

	return err;
}


static int module_close(void)
{
	rst_audio_close();
	rst_video_close();

	return 0;
}


EXPORT_SYM const struct mod_export DECL_EXPORTS(rst) = {
	"rst",
	"avsrc",
	module_init,
	module_close
};
