/**
 * @file realtime.c  Real-Time scheduling
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <re.h>
#include <baresip.h>
#ifdef DARWIN
#include <sys/types.h>
#include <sys/sysctl.h>
#include <stdio.h>
#include <mach/mach.h>
#ifdef __APPLE__
#include "TargetConditionals.h"
#endif
#endif


#ifdef DARWIN
static int set_realtime(int period, int computation, int constraint)
{
	struct thread_time_constraint_policy ttcpolicy;
	int ret;

	ttcpolicy.period      = period;      /* HZ/160  */
	ttcpolicy.computation = computation; /* HZ/3300 */
	ttcpolicy.constraint  = constraint;  /* HZ/2200 */
	ttcpolicy.preemptible = 1;

	ret = thread_policy_set(mach_thread_self(),
				THREAD_TIME_CONSTRAINT_POLICY,
				(thread_policy_t)&ttcpolicy,
				THREAD_TIME_CONSTRAINT_POLICY_COUNT);
	if (ret != KERN_SUCCESS)
		return ENOSYS;

	return 0;
}
#endif


/**
 * Enable real-time scheduling (for selected platforms)
 *
 * @param enable True to enable, false to disable
 * @param fps    Wanted video framerate
 *
 * @return 0 if success, otherwise errorcode
 */
int realtime_enable(bool enable, int fps)
{
#ifdef DARWIN
	if (enable) {
#if TARGET_OS_IPHONE
		int bus_speed = 100000000;
#else
		int ret, bus_speed;
		int mib[2] = { CTL_HW, HW_BUS_FREQ };
		size_t len;

		len = sizeof(bus_speed);
		ret = sysctl (mib, 2, &bus_speed, &len, NULL, 0);
		if (ret < 0) {
			return ENOSYS;
		}

		info("realtime: fps=%d bus_speed=%d\n", fps, bus_speed);
#endif

		return set_realtime(bus_speed / fps,
				    bus_speed / 3300, bus_speed / 2200);
	}
	else {
		kern_return_t ret;
		thread_standard_policy_data_t pt;
		mach_msg_type_number_t cnt = THREAD_STANDARD_POLICY_COUNT;
		boolean_t get_default = TRUE;

		ret = thread_policy_get(mach_thread_self(),
					THREAD_STANDARD_POLICY,
				        (thread_policy_t)&pt,
					&cnt, &get_default);
		if (KERN_SUCCESS != ret)
			return ENOSYS;

		ret = thread_policy_set(mach_thread_self(),
					THREAD_STANDARD_POLICY,
					(thread_policy_t)&pt,
					THREAD_STANDARD_POLICY_COUNT);
		if (KERN_SUCCESS != ret)
			return ENOSYS;

		return 0;
	}
#else
	(void)enable;
	(void)fps;
	return ENOSYS;
#endif
}
