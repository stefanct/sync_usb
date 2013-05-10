#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <sys/io.h>
#include <time.h>
#include "time_funcs.h"
#include "realtimeify.h"
#include "usb_frames.h"

#define uint unsigned int

int toggle_pin(void) {
	static int val = 0x88;
	static const uint8_t mask = 0x80;
	outb(val, 0x80);
	val ^= mask;
	return 0;
}

#define WAKEUP_HEADROOM_NS 20000 /* number of ns to wake up before event */
#define PRECISION_NS 500 /* execute only if in interval [event-PRECISION_NS; event) */

int benchmark(unsigned int times) {
	struct timespec sync_ts, ts;
	clock_gettime(CLOCK_REALTIME, &sync_ts);
	sync_ts.tv_nsec = NS_PER_SEC - WAKEUP_HEADROOM_NS;
	for (unsigned int i = 0; i < times; i++) {
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
		if (toggle_pin() != 0)
			return EXIT_FAILURE;

		char buf[30];
		timespec2str(buf, sizeof(buf), &ts);
		printf("%s\n", buf);
	}
	return EXIT_SUCCESS;
}

int parse_and_benchmark(int argc, char **argv) {
	int times = 10;
	if (argc >= 2) {
		times = atoi(argv[1]);
		if (times <= 0) {
			printf("%d times is not valid\n", times);
			return EXIT_FAILURE;
		}
	}
	if (iopl(3) != 0) {
		printf("You need to be root!\n");
		return EXIT_FAILURE;
	}

	return benchmark(times);
}

int main(int argc, char **argv) {
	return realtimeify(parse_and_benchmark, argc, argv);
/*	if (j2a_init() != 0) {
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
*/
}
