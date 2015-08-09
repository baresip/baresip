#include <re.h>
#include <baresip.h>
#include "test.h"


static void timeout_handler(void *arg)
{
	int *err = arg;

	warning("selftest: re_main() loop timed out -- test hung..\n");

	*err = ETIMEDOUT;

	re_cancel();
}


int re_main_timeout(uint32_t timeout)
{
	struct tmr tmr;
	int err = 0;

	tmr_init(&tmr);

	tmr_start(&tmr, timeout * 1000, timeout_handler, &err);
	re_main(NULL);

	tmr_cancel(&tmr);
	return err;
}
