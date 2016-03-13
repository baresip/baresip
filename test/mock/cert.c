/**
 * @file cert.c  TLS Certificate
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include "../test.h"


/**
 * Dummy certificate for testing.
 *
 *
 * It was generated like this:
 *
 *   $ openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem \
 *             -days 36500 -nodes
 *
 *
 * Dumping information:
 *
 *   $ openssl x509 -subject -dates -fingerprint -in cert.pem
 *   subject= /C=NO/ST=Retest/O=Retest AS/CN=Mr Retest/emailAddress=re@test.org
 *   notBefore=Nov 23 18:40:38 2014 GMT
 *   notAfter=Oct 30 18:40:38 2114 GMT
 *   Fingerprint=49:A4:E9:F4:80:3A:D4:38:84:F1:64:C3:B9:4B:F9:BB:80:F7:07:76
 */

const char test_certificate[] =

"-----BEGIN CERTIFICATE-----\r\n"
"MIIDmTCCAoGgAwIBAgIJAIt1/MAlTpB7MA0GCSqGSIb3DQEBCwUAMGIxCzAJBgNV\r\n"
"BAYTAk5PMQ8wDQYDVQQIDAZSZXRlc3QxEjAQBgNVBAoMCVJldGVzdCBBUzESMBAG\r\n"
"A1UEAwwJTXIgUmV0ZXN0MRowGAYJKoZIhvcNAQkBFgtyZUB0ZXN0Lm9yZzAgFw0x\r\n"
"NDExMjMxODQwMzhaGA8yMTE0MTAzMDE4NDAzOFowYjELMAkGA1UEBhMCTk8xDzAN\r\n"
"BgNVBAgMBlJldGVzdDESMBAGA1UECgwJUmV0ZXN0IEFTMRIwEAYDVQQDDAlNciBS\r\n"
"ZXRlc3QxGjAYBgkqhkiG9w0BCQEWC3JlQHRlc3Qub3JnMIIBIjANBgkqhkiG9w0B\r\n"
"AQEFAAOCAQ8AMIIBCgKCAQEAqnX4j6WK6tcN/X+C8C9ZSSlhVT2OdPB/lAPa3T3w\r\n"
"eB3wu2C9gnZcCSvekBhyFKSi4w0Az5HNjJWWQqpSeYW2MCEKI97DIu0hg/5qn2le\r\n"
"2sDjo4u/SNdH0CQHaLD2Xu0hhvZ/dTIulxpy5hLVmxs8/UZ8QKZ3vxDFE92p4LBL\r\n"
"tLYz6+TvWovmUqYL97J+2muXUMcZCCTbk8DQSGLBbsawXejVF8RgPiFHCvefybUQ\r\n"
"JCbtTDTfMykVnMEv3yMmfcXG/mwG8CLDRv7y8wh632aDdWfKN+g70gH0CFdjn070\r\n"
"eIZyJ3TyRi4b55RSC4FAdP2YKToOUH55N86wrbprnHb8swIDAQABo1AwTjAdBgNV\r\n"
"HQ4EFgQUcaZeVPUmMPFvKwYzn27b8BUJV3AwHwYDVR0jBBgwFoAUcaZeVPUmMPFv\r\n"
"KwYzn27b8BUJV3AwDAYDVR0TBAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAbsMt\r\n"
"zruNpBUZv08vdoWN9QWaJrmv8fvcx/RcuVLRuAaLYExEUJnoz3+dNFbR38BvncVC\r\n"
"LlDcSIK06JIHX6E7gJegWQdECoO/YgQGCwoIoQJtNCybxtZccb5uAGY/+qO3uOx0\r\n"
"Vx1NxrAMh5cpOIhZ8XiSYA2+JB71prW97diSQS+cU9xWCJxPU7UqQ10nV7PbfSmp\r\n"
"XTnj+togZPXYzJmSQR4RoM4Vqu27syo7xYQ90twoRKpRYTPdDTArpkTn6KuUuCJ1\r\n"
"t2v9LOkkxOvF11rLY6rLf0BG4XYkhnz4CLLt428wvAPykPcs95Q9TwpF/nKEwyfh\r\n"
"J+cC3FZTiBf/YmPPaA==\r\n"
"-----END CERTIFICATE-----\r\n"

"-----BEGIN PRIVATE KEY-----\r\n"
"MIIEvwIBADANBgkqhkiG9w0BAQEFAASCBKkwggSlAgEAAoIBAQCqdfiPpYrq1w39\r\n"
"f4LwL1lJKWFVPY508H+UA9rdPfB4HfC7YL2CdlwJK96QGHIUpKLjDQDPkc2MlZZC\r\n"
"qlJ5hbYwIQoj3sMi7SGD/mqfaV7awOOji79I10fQJAdosPZe7SGG9n91Mi6XGnLm\r\n"
"EtWbGzz9RnxApne/EMUT3angsEu0tjPr5O9ai+ZSpgv3sn7aa5dQxxkIJNuTwNBI\r\n"
"YsFuxrBd6NUXxGA+IUcK95/JtRAkJu1MNN8zKRWcwS/fIyZ9xcb+bAbwIsNG/vLz\r\n"
"CHrfZoN1Z8o36DvSAfQIV2OfTvR4hnIndPJGLhvnlFILgUB0/ZgpOg5Qfnk3zrCt\r\n"
"umucdvyzAgMBAAECggEAC1xxhKFz8NMEi7DD+V4uhUHMyvGfXQvqdOMM41INhPP5\r\n"
"54M7HkblO3dBDjmS4O1YLenf8/WzzXrq2OahOJhA3FRXaKygNOO5KCL82EMdn1bb\r\n"
"1TqrNR+kGatNEx04TntfkK89L4J4uHl6zvrSYdQe7IKWJXjy4jkr6XcMq30Ujqa6\r\n"
"Val03Cr40VL0ZSYXnwTf/P65GtQTdyTOemYkUbMH9qRoxHmE7ZsPRpbXU4k30JWh\r\n"
"6VYy+7h5XmjrX+VdIiia6sMjy0mbtsxJb6DOo19ro4DfF5JmFZpXnBhsJGhMxiEM\r\n"
"94QEC5Tv6b0hWomFpOm3I5jOnktavCFQ1NsNHUspgQKBgQDUQm2+F06YdC6Pgt+9\r\n"
"COnd9rz2lB2TjGRIJvis6MW7EfQ7XHkH9y/sDGLzINBVn6DkaHmQZDrIg77Mey+H\r\n"
"LG0QL6+7WK7c1X/Zga6LKvkLlmcMWq6i1uu+Q3UODu7XcFh6f8kDThP+BubfUWpw\r\n"
"rCRq74gF1JzQISoKqmyW/AXMZQKBgQDNln0jzgh/ySWRjJbuh6GqPQunDQsh3K3I\r\n"
"4WDHK40NHFgIzPomKO+pqsOu3V/X81NARqfUyoIp/455YRheLHBUJg7maLfxrBq/\r\n"
"qQPEUwSAI6lheMz1WNWri65GvwBlVENajVuMh/xfmKVL1KVV+LXGO5L1UClWITCM\r\n"
"VSC0QT8XNwKBgQC7bYUWS+JdAIp0su36MDrCgzPM0HFlbpzGkZMYq9qeG3Z8TGWb\r\n"
"QQyR9UYSxjDwyqn5xr9BXyABG0SJr2UCiZosps8YMXEHE4d3eumzfdi4ALEx2YlH\r\n"
"xVwZf9uG9Gy21D9svBW102YX8+Q94diJcZgezTBhZaKqrf4/uMl2cUh1eQKBgQDB\r\n"
"heZYXOqdN0A5CUlOUbg5YutkHaAcCPpBvP33niRRchvgdOsIHsKzSL6ZDWPaCP+V\r\n"
"4qy7XsE2PYzk7yQcCeLXI1glRe/Y+3PWdIfKN4dmA6u+yBLO5QeFSqALkmISAEbC\r\n"
"p4vE9oD3j94RSqM0EUEy0ANfDk1K+UUU5FE7vKth8wKBgQCoKvPNfC3FmyhLK7EK\r\n"
"zgfIYAcoi52EFu6xQ9PvZHg60ptYvq1L+H6LR8cxYfcrPTt3aDbFf41RahPkokNh\r\n"
"2HDxiHd4HwWkAGiqZXCTA2znb+rnJ9fheI6g/Wb3p+oGeCFHMcdUcsl1qWBFDtax\r\n"
"ygS/tEFgy1z2dVMLbLKqEUscsA==\r\n"
"-----END PRIVATE KEY-----\r\n"
;
