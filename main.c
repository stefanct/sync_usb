#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <sys/io.h>
#include <time.h>
#include "realtimeify.h"
#include "usb_frames.h"
#include "j2a.h"

#define uint unsigned int
#define NS_PER_SEC 1000000000L
#define US_PER_SEC    1000000L

int port80(void) {
	while(true) {
		uint i;
		if (scanf("%x", &i) != 1)
			break;
		
		outb(i, 0x80);
	}
	//for (uint i = 0; i <= 0xff; i++) {
		//usleep(5000);
		//printf("outb %03d\n", inb(0x80));
		//
	//}
	return 0;
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

int print_timespec(struct timespec *ts, struct timespec *res) {
	const uint TIME_FMT = strlen("2012-12-31 12:59:59.123456789") + 1;
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

int print_time_device(struct j2a_handle *comm) {
	struct j2a_packet p;
	p.len = 0;
	if (j2a_send_by_name(comm, &p, "getTime") != 0)
		return EXIT_FAILURE;

	j2a_packet_print(&p);
	uint64_t us = 0;
	for (unsigned int i = 0; i < 8; i++)
		us |= (uint64_t)p.msg[i] << (i * 8);

	printf("us=%0"PRIx64"\n", us);
	printf("us=%0"PRId64"\n", us);

	struct timespec ts;
	ts.tv_nsec = us % US_PER_SEC * 1000;
	ts.tv_sec = us / US_PER_SEC;
	printf("sec=%0"PRId64"\n", ts.tv_sec);
	return print_timespec(&ts, NULL);
	
	//return EXIT_SUCCESS;
}

int print_time_host(int clk_id, const char *clk_name) {

	struct timespec ts, res;
	clock_getres(clk_id, &res);
	clock_gettime(clk_id, &ts);
	return print_timespec(&ts, &res);
}

#define WAKEUP_HEADROOM_NS 10000 /* number of ns to wake up before event */
#define PRECISION_NS 500 /* execute only if in interval [event-PRECISION_NS; event) */

int benchmark(int argc, char **argv) {
	struct timespec sync_ts, ts;
	clock_gettime(CLOCK_REALTIME, &sync_ts);
	sync_ts.tv_nsec = NS_PER_SEC - 10000;
	for (unsigned int i = 0; i < 10; i++) {
		sync_ts.tv_sec++;
		clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &sync_ts, NULL);
		while (true) {
			clock_gettime(CLOCK_REALTIME, &ts);
			if (ts.tv_nsec >= NS_PER_SEC - PRECISION_NS)
				break;
			if (ts.tv_nsec < sync_ts.tv_nsec) {
				sync_ts.tv_sec++;
				clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &sync_ts, NULL);
			}
		}
		char buf[30];
		timespec2str(buf, sizeof(buf), &ts);
		printf("%s\n", buf);
	}
	return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
	//return realtimeify(benchmark, argc, argv);
	if (j2a_init() != 0) {
		fprintf(stderr, "init failed\n");
		return EXIT_FAILURE;
	}

	//libusb_set_debug(NULL, 2);
	struct j2a_handle *comm = j2a_connect(NULL);
	if (comm == NULL) {
		fprintf(stderr, "connect failed\n");
		return EXIT_FAILURE;
	}
	printf("connected.\n");
	if (j2a_fetch_funcmap(comm) != 0)
		return EXIT_FAILURE;
	
	j2a_print_funcmap(comm, stdout);

	if (j2a_fetch_props(comm) != 0)
		return EXIT_FAILURE;
	
	j2a_print_propmap(comm, stdout);

	int ret = EXIT_SUCCESS;
	if (argc > 1) {
		struct j2a_packet p;
		p.len = 0;
		if ((ret = j2a_send_by_name(comm, &p, argv[1])) == 0)
			j2a_packet_print(&p);
	} else {
		ret = print_time_device(comm);
	}

	j2a_disconnect(comm);
	j2a_shutdown();

	return ret;
}
