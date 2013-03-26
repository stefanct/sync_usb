#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <sys/io.h>
#include <time.h>
#include "realtimeify.h"
#include "j2a.h"

#define uint unsigned int
#define NS_PER_SEC 1000000000L
#define WAKEUP_HEADROOM_NS 10000 /* number of ns to wake up before event */
#define PRECISION_NS 500 /* execute only if in interval [event-PRECISION_NS; event) */

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

int get_time(int clk_id, const char *clk_name) {
	const uint TIME_FMT = strlen("2012-12-31 12:59:59.123456789") + 1;
	char timestr[TIME_FMT];

	struct timespec ts, res;
	clock_getres(clk_id, &res);
	clock_gettime(clk_id, &ts);

	if (timespec2str(timestr, TIME_FMT, &ts) != 0) {
		printf("timespec2str failed!\n");
		return EXIT_FAILURE;
	} else {
		unsigned long resol = res.tv_sec * NS_PER_SEC + res.tv_nsec;
		printf("%s: res=%ld ns, time=%s\n", clk_name, resol, timestr);
		return EXIT_SUCCESS;
	}
}

int read_frame_number(const char *path) {
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		fprintf(stderr, "Unable to open '%s'.\n", path);
		return -1;
	}

	int new;
	int ret = fscanf(f, "%d", &new);
	if (ret != 1) {
		fprintf(stderr, "Unable to read frame number.\n");
		fclose(f);
		return -1;
	}

	fclose(f);
	return new;
}

int wait_for_new_frame_number(void) {
	const char *path = "/sys/devices/pci0000:00/0000:00:1d.0/usb2/frame_number";

	int old = read_frame_number(path);
	int new = -1;
	while (old >= 0) {
		new = read_frame_number(path);
		//printf("old = %d, new = %d\n", old, new);
		if (new != old)
			break;
		old = new;
	}
	return new;
}

int sync_time(struct j2a_handle *comm) {
	struct j2a_packet p = {0};
	p.len = 10; // 64 + 16 bits = 10 B
	uint64_t *timebuf = (uint64_t *)&p.msg[0];
	uint16_t *framebuf = (uint16_t *)&p.msg[8];

	while (true) {
		// wait for and get the new frame
		int fn = wait_for_new_frame_number();
		if (fn < 0)
			return 1;
		*framebuf = fn;
		// get timestamp
		struct timespec ts;
		if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
			return 2;
		uint64_t time = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
		*timebuf = time;
		p.len = 10;
		printf("sent timestamp=%" PRId64 " 0x%016" PRIx64 ", framenumber=%04" PRIx16 "\n", time, time, fn);
		//j2a_packet_print(&p);
		if (j2a_send_by_name(comm, &p, "syncTime") != 0)
			return 3;
		p.len=0;
		if (j2a_send_by_name(comm, &p, "getTime") != 0)
			return 4;
		printf("rcvd timestamp=%" PRId64 " 0x%016" PRIx64 " (fn=%04" PRIx16 "\n", *timebuf, *timebuf, *framebuf);
		sleep(5);
	}
	return 0;
}

int benchmark(int argc, char **argv) {

	if (j2a_init() != 0)
		return EXIT_FAILURE;

	//libusb_set_debug(NULL, 2);
	struct j2a_handle *comm = j2a_connect(NULL);
	if (comm == NULL)
		return EXIT_FAILURE;

	printf("connected.\n");
	if (j2a_fetch_funcmap(comm) != 0)
		return EXIT_FAILURE;
	
	j2a_print_funcmap(comm, stdout);

	if (j2a_fetch_props(comm) != 0)
		return EXIT_FAILURE;
	
	j2a_print_propmap(comm, stdout);

	if (argc > 1) {
		struct j2a_packet p;
		p.len = 0;
		if (j2a_send_by_name(comm, &p, argv[1]) == 0)
			j2a_packet_print(&p);
	} else
		sync_time(comm);

	j2a_disconnect(comm);
	j2a_shutdown();

	return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
	return realtimeify(benchmark, argc, argv);
	//return benchmark(argc, argv);
}
