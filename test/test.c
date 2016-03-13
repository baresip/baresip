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


static void signal_handler(int sig)
{
	re_fprintf(stderr, "test interrupted by signal %d\n", sig);
	re_cancel();
}


int re_main_timeout(uint32_t timeout_ms)
{
	struct tmr tmr;
	int err = 0;

	tmr_init(&tmr);

	tmr_start(&tmr, timeout_ms, timeout_handler, &err);
	re_main(signal_handler);

	tmr_cancel(&tmr);
	return err;
}
