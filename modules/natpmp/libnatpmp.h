/**
 * @file libnatpmp.h Interface to NAT-PMP Client library
 *
 * Copyright (C) 2010 Creytiv.com
 */


enum {
	NATPMP_VERSION =    0,
	NATPMP_PORT    = 5351,
};

enum natpmp_op {
	NATPMP_OP_EXTERNAL    = 0,
	NATPMP_OP_MAPPING_UDP = 1,
	NATPMP_OP_MAPPING_TCP = 2,
};

enum natpmp_result {
	NATPMP_SUCCESS          = 0,
	NATPMP_UNSUP_VERSION    = 1,
	NATPMP_REFUSED          = 2,
	NATPMP_NETWORK_FAILURE  = 3,
	NATPMP_OUT_OF_RESOURCES = 4,
	NATPMP_UNSUP_OPCODE     = 5
};

struct natpmp_resp {
	uint8_t vers;
	uint8_t op;
	uint16_t result;
	uint32_t epoch;

	union {
		uint32_t ext_addr;
		struct {
			uint16_t int_port;
			uint16_t ext_port;
			uint32_t lifetime;
		} map;
	} u;
};

struct natpmp_req;

typedef void (natpmp_resp_h)(int err, const struct natpmp_resp *resp,
			     void *arg);

int natpmp_external_request(struct natpmp_req **npp, const struct sa *srv,
			    natpmp_resp_h *h, void *arg);
int natpmp_mapping_request(struct natpmp_req **natpmpp, const struct sa *srv,
			   uint16_t int_port, uint16_t ext_port,
			   uint32_t lifetime, natpmp_resp_h *resph, void *arg);
