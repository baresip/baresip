/**
 * @file account/account.c  Load SIP accounts from file
 *
 * Copyright (C) 2010 - 2015 Creytiv.com
 */
#include <re.h>
#include <baresip.h>


/**
 * @defgroup account account
 *
 * Load SIP accounts from a file
 *
 * This module is loading SIP accounts from file ~/.baresip/accounts.
 * If the file exist and is readable, all SIP accounts will be populated
 * from this file. If the file does not exist, a template file will be
 * created.
 *
 * Examples:
 \verbatim
  "User 1 with password prompt" <sip:user@domain.com>
  "User 2 with stored password" <sip:user@domain.com>;auth_pass=pass
  "User 2 with ICE" <sip:user@1.2.3.4;transport=tcp>;medianat=ice
  "User 3 with IPv6" <sip:user@[2001:df8:0:16:216:6fff:fe91:614c]:5070>
 \endverbatim
 */


static int account_write_template(const char *file)
{
	FILE *f = NULL;
	const char *login, *pass, *domain;
	int r, err = 0;

	info("account: creating accounts template %s\n", file);

	f = fopen(file, "w");
	if (!f)
		return errno;

	login = sys_username();
	if (!login) {
		login = "user";
	}

	pass = "PASSWORD";

	domain = net_domain(baresip_network());
	if (!domain)
		domain = "domain";

	r = re_fprintf(f,
			 "#\n"
			 "# SIP accounts - one account per line\n"
			 "#\n"
			 "# Displayname <sip:user@domain"
			 ";uri-params>;addr-params\n"
			 "#\n"
			 "#  uri-params:\n"
			 "#    ;transport={udp,tcp,tls}\n"
			 "#\n"
			 "#  addr-params:\n"
			 "#    ;answermode={manual,early,auto}\n"
			 "#    ;audio_codecs=opus/48000/2,pcma,...\n"
			 "#    ;auth_user=username\n"
			 "#    ;auth_pass=password\n"
			 "#    ;call_transfer=no\n"
			 "#    ;mediaenc={srtp,srtp-mand,srtp-mandf"
			 ",dtls_srtp,zrtp}\n"
			 "#    ;medianat={stun,turn,ice}\n"
			 "#    ;mwi=no\n"
			 "#    ;outbound=\"sip:primary.example.com"
			 ";transport=tcp\"\n"
			 "#    ;outbound2=sip:secondary.example.com\n"
			 "#    ;ptime={10,20,30,40,...}\n"
			 "#    ;regint=3600\n"
			 "#    ;pubint=0 (publishing off)\n"
			 "#    ;regq=0.5\n"
			 "#    ;sipnat={outbound}\n"
			 "#    ;stunuser=STUN/TURN/ICE-username\n"
			 "#    ;stunpass=STUN/TURN/ICE-password\n"
			 "#    ;stunserver=stun:[user:pass]@host[:port]\n"
			 "#    ;video_codecs=h264,h263,...\n"
			 "#\n"
			 "# Examples:\n"
			 "#\n"
			 "#  <sip:user@domain.com;transport=tcp>"
		         ";auth_pass=secret\n"
			 "#  <sip:user@1.2.3.4;transport=tcp>"
		         ";auth_pass=secret\n"
			 "#  <sip:user@"
			 "[2001:df8:0:16:216:6fff:fe91:614c]:5070"
			 ";transport=tcp>;auth_pass=secret\n"
			 "#\n"
		       "#<sip:%s@%s>;auth_pass=%s\n", login, domain, pass);
	if (r < 0)
		err = ENOMEM;

	if (f)
		(void)fclose(f);

	return err;
}


/**
 * Add a User-Agent (UA)
 *
 * @param addr SIP Address string
 * @param arg  Handler argument (unused)
 *
 * @return 0 if success, otherwise errorcode
 */
static int line_handler(const struct pl *addr, void *arg)
{
	char buf[512];
	struct ua *ua;
	struct account *acc;
	int err;
	(void)arg;

	(void)pl_strcpy(addr, buf, sizeof(buf));

	err = ua_alloc(&ua, buf);
	if (err)
		return err;

	acc = ua_account(ua);
	if (!acc) {
		warning("account: no account for this ua\n");
		return ENOENT;
	}

	if (account_regint(acc) != 0) {
		int e;

		e = ua_register(ua);
		if (e) {
			warning("account: failed to register ua"
				" '%s' (%m)\n", ua_aor(ua), e);
		}
	}

	/* optional password prompt */
	if (!str_isset(account_auth_pass(acc))) {
		char *pass = NULL;

		(void)re_printf("Please enter password for %s: ",
				account_aor(acc));

		err = ui_password_prompt(&pass);
		if (err)
			goto out;

		err = account_set_auth_pass(acc, pass);

		mem_deref(pass);
	}

 out:
	return err;
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

	err = conf_parse(file, line_handler, NULL);
	if (err)
		return err;

	n = list_count(uag_list());
	info("Populated %u account%s\n", n, 1==n ? "" : "s");

	if (list_isempty(uag_list())) {
		info("account: No SIP accounts found\n"
			" -- check your config "
			"or add an account using 'uanew' command\n");
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
