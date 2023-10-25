/**
 * @file cert.c  TLS Certificate
 *
 * Copyright (C) 2010 Alfred E. Heggestad
 */
#include <re.h>
#include <rem.h>
#include "../test.h"


/**
 * X509/PEM certificate with ECDSA keypair
 *
 *  $ openssl ecparam -out ec_key.pem -name prime256v1 -genkey
 *  $ openssl req -new -key ec_key.pem -x509 -nodes -days 3650 -out cert.pem
 */

const char test_certificate[] =

"-----BEGIN CERTIFICATE-----\r\n"
"MIICBzCCAa2gAwIBAgIUZy0UqzsDq7fGUsZh6QxkXgCa030wCgYIKoZIzj0EAwIw\r\n"
"WTELMAkGA1UEBhMCTk8xEzARBgNVBAgMClNvbWUtU3RhdGUxITAfBgNVBAoMGElu\r\n"
"dGVybmV0IFdpZGdpdHMgUHR5IEx0ZDESMBAGA1UEAwwJMTI3LjAuMC4xMB4XDTE5\r\n"
"MDUyNDE5NTM0OFoXDTI5MDUyMTE5NTM0OFowWTELMAkGA1UEBhMCTk8xEzARBgNV\r\n"
"BAgMClNvbWUtU3RhdGUxITAfBgNVBAoMGEludGVybmV0IFdpZGdpdHMgUHR5IEx0\r\n"
"ZDESMBAGA1UEAwwJMTI3LjAuMC4xMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE\r\n"
"inP/oBEqBbXRxDzyk7sbh8rRJbfbXBRG2uJl2g6YhSkYZkifGyEueJ7+A9D9LfBh\r\n"
"b5+lKXuJc02XQW5IwUmToqNTMFEwHQYDVR0OBBYEFH1vSH2IBZvKYNDPfPOk41Dw\r\n"
"hyTWMB8GA1UdIwQYMBaAFH1vSH2IBZvKYNDPfPOk41DwhyTWMA8GA1UdEwEB/wQF\r\n"
"MAMBAf8wCgYIKoZIzj0EAwIDSAAwRQIhAOm79QetPxioy/S0Rk9lhPgfBslgM6f4\r\n"
"tihVBSpe0FdJAiAC6Usj7p3H8dvu9Oa1gtOXSJkh1MT6pkfW21YseRWP4A==\r\n"
"-----END CERTIFICATE-----\r\n"
"-----BEGIN EC PARAMETERS-----\r\n"
"BggqhkjOPQMBBw==\r\n"
"-----END EC PARAMETERS-----\r\n"
"-----BEGIN EC PRIVATE KEY-----\r\n"
"MHcCAQEEIMWTO9/z24fiq13MM5UF1CVD3yJjVXRe0qpTCmmZU5ppoAoGCCqGSM49\r\n"
"AwEHoUQDQgAEinP/oBEqBbXRxDzyk7sbh8rRJbfbXBRG2uJl2g6YhSkYZkifGyEu\r\n"
"eJ7+A9D9LfBhb5+lKXuJc02XQW5IwUmTog==\r\n"
"-----END EC PRIVATE KEY-----\r\n"
;
