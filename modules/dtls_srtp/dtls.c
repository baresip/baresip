/**
 * @file dtls.c DTLS functions
 *
 * Copyright (C) 2010 Creytiv.com
 */
#define OPENSSL_NO_KRB5 1
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <re.h>
#include <baresip.h>
#include "dtls_srtp.h"


#define DEBUG_MODULE "dtls_srtp"
#define DEBUG_LEVEL 5
#include <re_dbg.h>


/* note: shadow struct in libre's tls module */
struct tls {
	SSL_CTX *ctx;
	char *pass;  /* password for private key */
	/* ... */
	EVP_PKEY *key;
	X509 *x;
};


static void destructor(void *data)
{
	struct tls *tls = data;

	if (tls->ctx)
		SSL_CTX_free(tls->ctx);

	if (tls->x)
		X509_free(tls->x);
	if (tls->key)
		EVP_PKEY_free(tls->key);

	mem_deref(tls->pass);
}


static int cert_generate(X509 *x, EVP_PKEY *privkey, const char *aor,
			 int expire_days)
{
	X509_EXTENSION *ext;
	X509_NAME *subj;
	int ret;
	int err = ENOMEM;

	subj = X509_NAME_new();
	if (!subj)
		goto out;

	X509_set_version(x, 2);

	ASN1_INTEGER_set(X509_get_serialNumber(x), rand_u32());

	ret = X509_NAME_add_entry_by_txt(subj, "CN", MBSTRING_ASC,
					 (unsigned char *)aor,
					 (int)strlen(aor), -1, 0);
	if (!ret)
		goto out;

	if (!X509_set_issuer_name(x, subj) ||
	    !X509_set_subject_name(x, subj))
		goto out;

	X509_gmtime_adj(X509_get_notBefore(x), 0);
	X509_gmtime_adj(X509_get_notAfter(x), 60*60*24*expire_days);

	if (!X509_set_pubkey(x, privkey))
		goto out;

	ext = X509V3_EXT_conf_nid(NULL, NULL,
				  NID_basic_constraints, "CA:FALSE");
	if (1 != X509_add_ext(x, ext, -1))
		goto out;
	X509_EXTENSION_free(ext);

	err = 0;

 out:
	if (subj)
		X509_NAME_free(subj);

	return err;
}


static int tls_gen_selfsigned_cert(struct tls *tls, const char *aor)
{
	RSA *rsa;
	int err = ENOMEM;

	rsa = RSA_generate_key(1024, RSA_F4, NULL, NULL);
	if (!rsa)
		goto out;

	tls->key = EVP_PKEY_new();
	if (!tls->key)
		goto out;
	if (!EVP_PKEY_set1_RSA(tls->key, rsa))
		goto out;

	tls->x = X509_new();
	if (!tls->x)
		goto out;

	if (cert_generate(tls->x, tls->key, aor, 365))
		goto out;

	/* Sign the certificate */
	if (!X509_sign(tls->x, tls->key, EVP_sha1()))
		goto out;

	err = 0;

 out:
	if (rsa)
		RSA_free(rsa);

	return err;
}


int dtls_alloc_selfsigned(struct tls **tlsp, const char *aor,
			  const char *srtp_profiles)
{
	struct tls *tls;
	int r, err;

	if (!tlsp || !aor)
		return EINVAL;

	tls = mem_zalloc(sizeof(*tls), destructor);
	if (!tls)
		return ENOMEM;

	SSL_library_init();

	tls->ctx = SSL_CTX_new(DTLSv1_method());
	if (!tls->ctx) {
		err = ENOMEM;
		goto out;
	}

#if (OPENSSL_VERSION_NUMBER < 0x00905100L)
	SSL_CTX_set_verify_depth(tls->ctx, 1);
#endif

	SSL_CTX_set_read_ahead(tls->ctx, 1);

	/* Generate self-signed certificate */
	err = tls_gen_selfsigned_cert(tls, aor);
	if (err) {
		DEBUG_WARNING("failed to generate certificate (%s): %m\n",
			      aor, err);
		goto out;
	}

	r = SSL_CTX_use_certificate(tls->ctx, tls->x);
	if (r != 1) {
		err = EINVAL;
		goto out;
	}

	r = SSL_CTX_use_PrivateKey(tls->ctx, tls->key);
	if (r != 1) {
		err = EINVAL;
		goto out;
	}

	if (0 != SSL_CTX_set_tlsext_use_srtp(tls->ctx, srtp_profiles)) {
		DEBUG_WARNING("could not enable SRTP for profiles '%s'\n",
			      srtp_profiles);
		err = ENOSYS;
		goto out;
	}

	err = 0;
 out:
	if (err)
		mem_deref(tls);
	else
		*tlsp = tls;

	return err;
}


int dtls_print_sha1_fingerprint(struct re_printf *pf, const struct tls *tls)
{
	uint8_t md[64];
	unsigned int i, len;
	int err = 0;

	if (!pf || !tls)
		return EINVAL;

	len = sizeof(md);
	if (1 != X509_digest(tls->x, EVP_sha1(), md, &len))
		return ENOENT;

	for (i=0; i<len; i++) {
		err |= re_hprintf(pf, "%s%02x", i==0?"":":", md[i]);
	}

	return err;
}


int dtls_print_sha256_fingerprint(struct re_printf *pf, const struct tls *tls)
{
	uint8_t md[64];
	unsigned int i, len;
	int err = 0;

	if (!pf || !tls)
		return EINVAL;

	len = sizeof(md);
	if (1 != X509_digest(tls->x, EVP_sha256(), md, &len))
		return ENOENT;

	for (i=0; i<len; i++) {
		err |= re_hprintf(pf, "%s%02x", i==0?"":":", md[i]);
	}

	return err;
}
