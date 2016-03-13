/**
 * @file gst/dump.c  Gstreamer playbin pipeline - dump utilities
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#include <gst/gst.h>
#include "gst.h"


void gst_dump_props(GstElement *g)
{
	uint64_t u64;
	gchar *strval;
	double volume;
	int n;

	debug("Gst properties:\n");

	g_object_get(g, "delay", &u64, NULL);
	debug(" delay:           %lu ns\n", u64);

	g_object_get(g, "uri", &strval, NULL);
	debug(" uri:             %s\n", strval);
	g_free(strval);

	g_object_get(g, "suburi", &strval, NULL);
	debug(" suburi:          %s\n", strval);
	g_free(strval);

	g_object_get(g, "queue-size", &u64, NULL);
	debug(" queue-size:      %lu ns\n", u64);

	g_object_get(g, "queue-threshold", &u64, NULL);
	debug(" queue-threshold: %lu ns\n", u64);

	g_object_get(g, "nstreams", &n, NULL);
	debug(" nstreams:        %d\n", n);

	g_object_get(g, "volume", &volume, NULL);
	debug(" Volume:          %f\n", volume);
}


void gst_dump_caps(const GstCaps *caps)
{
	GstStructure *s;
	int rate, channels, width;

	if (!caps)
		return;

	if (!gst_caps_get_size(caps))
		return;

	s = gst_caps_get_structure(caps, 0);

	gst_structure_get_int(s, "rate",     &rate);
	gst_structure_get_int(s, "channels", &channels);
	gst_structure_get_int(s, "width",    &width);

	info("gst: caps dump: %d Hz, %d channels, width=%d\n",
	     rate, channels, width);
}
