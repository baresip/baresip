/**
 * @file media.h
 *
 * For more information see:
 * https://www.onvif.org/ver10/media/wsdl/media.wsdl
 * https://www.onvif.org/ver20/media/wsdl/media.wsdl
 *
 * Copyright (C) 2019 commend.com - Christoph Huber
 */

#ifndef _ONVIF_MEDIA_H_
#define _ONVIF_MEDIA_H_

#define MAXMEDIAPROFILE 10

/* Only if this is a null ptr */
extern struct profile *std_profile;

/* List which hold all the existing configurations */
extern struct list profile_l;
extern struct list vs_l;
extern struct list ve_l;
extern struct list as_l;
extern struct list ae_l;
extern struct list ao_l;
extern struct list ad_l;

struct soap_fault;

/* TODO 1: we should save the configuration made by the init_media function.
 * VMS (atleast XProtect) works with the config and source/ouput tokens
 * currently they are random generated and will change after each restart
 * generate it only once and save it in a file.
 * later: write config files -> pars config files
 * -> generate tokens -> write back
 **/

/* #__attribute__ ((aligned (8))) depending on the target */
struct media_config {
	struct le le;

	char token [65];          /* unique identifier <max 64 chars + '\0'> */
	char name  [65];          /* readable name <max 64 char + '\0'>      */
	uint8_t  usecount;        /* use count                               */

	union {
		struct {
			int8_t maxprofile;      /* Maximal number of profiles*/
			char *viewmodes;        /* RO viewmode as defined in
						tt:viewModes */
			char sourcetoken [64];  /* reference to the physical
						   input */
			float framerate;        /* framerate of the source */
			struct instances {
				uint8_t jpeg_i;      /* Guaranteed JPEG Encoder
							of this source */
				uint8_t h264_i;      /* Guaranteed H264 Encoder
							of this source */
				uint8_t mpeg4_i;     /* Guaranteed MPEG4 Enc.
							of this source */
			} i;
			struct bounds {
				int x;
				int y;
				int w;
				int h;
			} b;                    /* bounds */
		} vs;

		struct {
			bool gfr;               /* guaranteed frame rate */
			int govlen;             /* group of video frame len */
			enum venc {
				JPEG,
				MPEG4,
				H264,
			} enc;                  /* video encoding enum */
			char *encstring;        /* encoder string */
			struct resolution {
				int w;
				int h;
			} res;                  /* resolution */
			float quality;          /* video quantizers high ->
						   high quality */
			struct ratecontrol {
				bool cbr;       /* enforce constant bit
						   rate */
				int frl;        /* framerate limit */
				int ei;         /* encoding interval */
				int brl;        /* bitrate limit */
			} ratec;                /* rate control */
			struct {
				struct {
					int type;       /* address type */
					struct sa addr; /* ipv4address */
				} addr;             /* multicast address */
				int      ttl;       /* multicast ttl */
				bool     autostart; /* RO signal if streaming
						       is persistant */
			} multicast;            /* multicast information
						   (must be present) */
			uint8_t st;             /* session timeout number
						   <'PT%dS'> */
		} ve;

		struct {
			char sourcetoken [64];  /* reference to the physical
						   input */
			uint8_t ch;             /* channels */
		} as;

		struct {
			enum aenc {
				G711,
				G726,
				AAC,
			} enc;                  /* audio encoding enum */
			char *encstring;        /* encoder string */
			int br;                 /* bitrate */
			int sr;                 /* samplerate */
			struct {
				struct {
					int type;       /* address type */
					struct sa addr; /* ipv4address */
				} addr;             /* multicast address */
				int      ttl;       /* multicast ttl */
				bool     autostart; /* RO signal if streaming
						       is persistant */
			} multicast;            /* multicast information
						   (must be present) */
			uint8_t st;             /* session timeout number
						   <'PT%dS'> */
		} ae;

		struct {
			enum sendprimacy {
				HALF_DUPLEX_CLIENT,
				HALF_DUPLEX_SERVER,
				HALF_DUPLEX_AUTO,
			} sp;                   /* send primacy enum */
			char outputtoken [64];  /* reference to the physical
						   output */
			int outputlevel;        /* output volume */
		} ao;

		struct {
			enum aenc dec;
			int br;                 /* bitrate */
			int sr;                 /* samplerate */
			uint8_t ch;             /* channels */
		} ad;
	} t;
};


struct profile {
	struct le le;

	char             token [65];    /* unique identifier of the profile
					   <0-255> */
	char             name  [65];    /* name of the profile */
	bool             fixed;         /* fixed profile */

	struct media_config *vsc;    /* videosource config */
	struct media_config *vec;    /* videoencoder config */

	struct media_config *asc;    /* audiosource config */
	struct media_config *aec;    /* audioencoder config */

	struct media_config *aoc;    /* audiooutput config */
	struct media_config *adc;    /* audiodecoder config */

	/* analytics config */
	/* ptz config */
	/* metadata config */
};


int media_init(void);
void media_deinit(void);

int media_GetProfiles_h(const struct soap_msg *msg, struct soap_msg **prtresp);
int media_GetProfile_h(const struct soap_msg *msg, struct soap_msg **prtresp,
	struct soap_fault *f);
int media_GetVSCS_h(const struct soap_msg *msg, struct soap_msg **prtresp);
int media_GetVSC_h(const struct soap_msg *msg, struct soap_msg **prtresp,
	struct soap_fault *f);
int media_GetVECS_h(const struct soap_msg *msg, struct soap_msg **prtresp);
int media_GetVEC_h(const struct soap_msg *msg, struct soap_msg **prtresp,
	struct soap_fault *f);
int media_GetASCS_h(const struct soap_msg *msg, struct soap_msg **prtresp);
int media_GetASC_h(const struct soap_msg *msg, struct soap_msg **prtresp,
	struct soap_fault *f);
int media_GetAECS_h(const struct soap_msg *msg, struct soap_msg **prtresp);
int media_GetAEC_h(const struct soap_msg *msg, struct soap_msg **prtresp,
	struct soap_fault *f);
int media_GetAOCS_h(const struct soap_msg *msg, struct soap_msg **prtresp);
int media_GetAOC_h(const struct soap_msg *msg, struct soap_msg **prtresp,
	struct soap_fault *f);
int media_GetADCS_h(const struct soap_msg *msg, struct soap_msg **prtresp);
int media_GetADC_h(const struct soap_msg *msg, struct soap_msg **prtresp,
	struct soap_fault *f);
int media_GetStreamUri_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_GetVideoSources_h(const struct soap_msg *msg,
	struct soap_msg **prtresp);
int media_GetAudioSources_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_GetMetadataConfigurations_h(const struct soap_msg *msg,
	struct soap_msg **prtresp);
int media_GetAudioEncoderConfigurationOptions_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_GetVideoEncoderConfigurationOptions_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_GetAudioDecoderConfigurationOptions_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_CreateProfile_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_DeleteProfile_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_AddVideoSourceConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_AddAudioSourceConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_AddVideoEncoderConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_AddAudioEncoderConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_AddAudioOutputConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_AddAudioDecoderConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_RemoveVideoSourceConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_RemoveAudioSourceConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_RemoveVideoEncoderConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_RemoveAudioEncoderConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_RemoveAudioOutputConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_RemoveAudioDecoderConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_GetAudioOutputs_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_GetAudioOutputConfigurationOptions_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_GetCompVideoEncoderConfigs_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_GetCompAudioEncoderConfigs_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_GetGuaranteedNumberOfVEInstances_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_GetCompVideoSourceConfigs_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_GetVideoSourceConfigurationOptions_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_GetCompAudioSourceConfigs_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_GetAudioSourceConfigurationOptions_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_GetCompAudioOutputConfigs_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_GetCompAudioDecoderConfigs_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_SetVideoSourceConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_SetAudioSourceConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_SetVideoEncoderConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_SetAudioEncoderConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);
int media_SetAudioOutputConfiguration_h(const struct soap_msg *msg,
	struct soap_msg **prtresp, struct soap_fault *f);


#endif /* _ONVIF_MEDIA_H */

