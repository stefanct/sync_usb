#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "time_funcs.h"

int print_timespec(struct timespec *ts, struct timespec *res) {
	const unsigned int TIME_FMT = strlen("2012-12-31 12:59:59.123456789") + 1;
	char timestr[TIME_FMT];
	if (timespec2str(timestr, TIME_FMT, ts) != 0) {
		printf("timespec2str failed!\n");
		return EXIT_FAILURE;
	} else {
		unsigned long resol = (res != NULL) ? resol = res->tv_sec * NS_PER_SEC + res->tv_nsec : 0;
		printf("res=%ld ns, time=%s\n", resol, timestr);
		return EXIT_SUCCESS;
	}
}

/* buf needs to be able to store a maximum of 30 characters. */
int timespec2str(char *buf, uint len, struct timespec *ts) {
	int ret;
	struct tm t;

	tzset();
	if (localtime_r(&(ts->tv_sec), &t) == NULL)
		return 1;

	/* Let's assume that no system has more than 30 years of uptime...
	 * NB: tm_year is since 1900 */
	if (t.tm_year < 100) {
		uint s = (ts->tv_sec) % 60;
		uint m = (ts->tv_sec / 60) % 60;
		uint h = (ts->tv_sec / (60*60));
		ret = snprintf(buf, len, "%02d:%02d:%02d.%09ld", h, m, s, ts->tv_nsec);
		if (ret >= len)
			return 2;
	} else {
		ret = strftime(buf, len, "%F %T", &t);
		if (ret == 0)
			return 3;
		len -= ret - 1;
		

		ret = snprintf(&buf[strlen(buf)], len, ".%09ld", ts->tv_nsec);
		if (ret >= len)
			return 4;
	}

	return 0;
}

int print_time_host(int clk_id) {

	struct timespec ts, res;
	clock_getres(clk_id, &res);
	clock_gettime(clk_id, &ts);
	return print_timespec(&ts, &res);
}
