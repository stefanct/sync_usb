#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <sys/io.h>
#include <time.h>
#include "time_funcs.h"
#include "realtimeify.h"

/* Toggles the highest pin of the 0x80 "POST" I/O port. */
void toggle_pin(void) {
	static int val = 0x88;
	static const uint8_t mask = 0x80;
	outb(val, 0x80);
	val ^= mask;
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
		toggle_pin();

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
}
