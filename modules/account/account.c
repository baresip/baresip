/**
 * @file account/account.c  Load SIP accounts from file
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>


static int account_write_template(const char *file)
{
	FILE *f = NULL;
	const char *login, *pass, *domain;

	info("account: creating accounts template %s\n", file);

	f = fopen(file, "w");
	if (!f)
		return errno;

	login = pass = sys_username();
	if (!login) {
		login = "user";
		pass = "pass";
	}

	domain = net_domain();
	if (!domain)
		domain = "domain";

	(void)re_fprintf(f,
			 "#\n"
			 "# SIP accounts - one account per line\n"
			 "#\n"
			 "# Displayname <sip:user:password@domain"
			 ";uri-params>;addr-params\n"
			 "#\n"
			 "#  uri-params:\n"
			 "#    ;transport={udp,tcp,tls}\n"
			 "#\n"
			 "#  addr-params:\n"
			 "#    ;answermode={manual,early,auto}\n"
			 "#    ;audio_codecs=speex/16000,pcma,...\n"
			 "#    ;auth_user=username\n"
			 "#    ;mediaenc={srtp,srtp-mand,srtp-mandf"
			 ",dtls_srtp,zrtp}\n"
			 "#    ;medianat={stun,turn,ice}\n"
			 "#    ;outbound=\"sip:primary.example.com"
			 ";transport=tcp\"\n"
			 "#    ;outbound2=sip:secondary.example.com\n"
			 "#    ;ptime={10,20,30,40,...}\n"
			 "#    ;regint=3600\n"
			 "#    ;pubint=0 (publishing off)\n"
			 "#    ;regq=0.5\n"
			 "#    ;rtpkeep={zero,stun,dyna,rtcp}\n"
			 "#    ;sipnat={outbound}\n"
			 "#    ;stunserver=stun:[user:pass]@host[:port]\n"
			 "#    ;video_codecs=h264,h263,...\n"
			 "#\n"
			 "# Examples:\n"
			 "#\n"
			 "#  <sip:user:secret@domain.com;transport=tcp>\n"
			 "#  <sip:user:secret@1.2.3.4;transport=tcp>\n"
			 "#  <sip:user:secret@"
			 "[2001:df8:0:16:216:6fff:fe91:614c]:5070"
			 ";transport=tcp>\n"
			 "#\n"
			 "<sip:%s:%s@%s>\n", login, pass, domain);

	if (f)
		(void)fclose(f);

	return 0;
}


/**
 * Add a User-Agent (UA)
 *
 * @param addr SIP Address string
 *
 * @return 0 if success, otherwise errorcode
 */
static int line_handler(const struct pl *addr)
{
	char buf[512];

	(void)pl_strcpy(addr, buf, sizeof(buf));

	return ua_alloc(NULL, buf);
}


/**
 * Read the SIP accounts from the ~/.baresip/accounts file
 *
 * @return 0 if success, otherwise errorcode
 */
static int account_read_file(void)
{
	char path[256] = "", file[256] = "";
	uint32_t n;
	int err;

	err = conf_path_get(path, sizeof(path));
	if (err) {
		warning("account: conf_path_get (%m)\n", err);
		return err;
	}

	if (re_snprintf(file, sizeof(file), "%s/accounts", path) < 0)
		return ENOMEM;

	if (!conf_fileexist(file)) {

		(void)fs_mkdir(path, 0700);

		err = account_write_template(file);
		if (err)
			return err;
	}

	err = conf_parse(file, line_handler);
	if (err)
		return err;

	n = list_count(uag_list());
	info("Populated %u account%s\n", n, 1==n ? "" : "s");

	if (list_isempty(uag_list())) {
		warning("account: No SIP accounts found"
			" -- check your config\n");
		return ENOENT;
	}

	return 0;
}


static int module_init(void)
{
	return account_read_file();
}


static int module_close(void)
{
	return 0;
}


const struct mod_export DECL_EXPORTS(account) = {
	"account",
	"application",
	module_init,
	module_close
};
