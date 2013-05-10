#include "stdlib.h"
#include "inttypes.h"
#include "time.h"
#include "time_funcs.h"
#include "j2a.h"

int print_time_devices(struct j2a_handle *comm[], unsigned int len) {
	for (uint i = 0; i < len; i++) {
		struct j2a_packet p;
		p.len = 0;
		if (j2a_send_by_name(comm[i], &p, "getTime") != 0)
			return EXIT_FAILURE;

		//j2a_packet_print(&p);
		uint64_t us = 0;
		for (unsigned int i = 0; i < 8; i++)
			us |= (uint64_t)p.msg[i] << (i * 8);

		//printf("us=%0"PRIx64"\n", us);
		//printf("us=%0"PRId64"\n", us);

		struct timespec ts;
		ts.tv_nsec = us % US_PER_SEC * 1000;
		ts.tv_sec = us / US_PER_SEC;
		//printf("sec=%0"PRId64"\n", ts.tv_sec);
		if(print_timespec(&ts, NULL) != 0)
			return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

#define NS_PER_SEC 1000000000L
#define US_PER_SEC	1000000L
int trigger_sync(struct j2a_handle *comm[], unsigned int len, uint64_t offset) {
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
		return EXIT_FAILURE;

	if (offset == 0)
		offset = 2;
	uint64_t time = ts.tv_sec * US_PER_SEC + ts.tv_nsec / 10000 + offset * US_PER_SEC;

	for (uint i = 0; i < len; i++) {
		struct j2a_packet p;
		p.len = 8;
		uint64_t *timebuf = (uint64_t *)&p.msg[0];

		*timebuf = time;

		//j2a_packet_print(&p);
		printf("setTrigger on %i ", i);
		if (j2a_send_by_name(comm[i], &p, "setTrigger") != 0) {
			 printf("failed\n");
			return EXIT_FAILURE;
		} else {
			 printf("succeeded\n");
			 //j2a_packet_print(&p);
		}
	}
	return EXIT_SUCCESS;
}

int connect(struct j2a_handle *comm[], unsigned int len) {
	for (uint i = 0; i < len; i++) {
		char nth[2];
		snprintf(nth, 2, "%i", i);
		comm[i] = j2a_connect(nth);
		if (comm[i] == NULL) {
			fprintf(stderr, "connect %s failed\n", nth);
			return EXIT_FAILURE;
		}
		printf("connected.\n");
		if (j2a_fetch_funcmap(comm[i]) != 0)
			return EXIT_FAILURE;
		
		//j2a_print_funcmap(comm[i], stdout);

		if (j2a_fetch_props(comm[i]) != 0)
			return EXIT_FAILURE;
		
		//j2a_print_propmap(comm[i], stdout);
	}
	return EXIT_SUCCESS;
}

void disconnect(struct j2a_handle *comm[], unsigned int len) {
	for (uint i = 0; i < len; i++)
		j2a_disconnect(comm[i]);
}

int main(int argc, char **argv) {
	if (j2a_init() != 0) {
		fprintf(stderr, "init failed\n");
		return EXIT_FAILURE;
	}

	int ret = EXIT_SUCCESS;
	//libusb_set_debug(NULL, 2);
	const char *nth_dev = NULL;
	if (argc > 1)
		nth_dev = argv[1];

	uint devs = 2;
	struct j2a_handle *comm[devs];
	if (connect(comm, devs) != 0) {
		fprintf(stderr, "connect failed\n");
		goto shutdown;
	}

	print_time_devices(comm, devs);


	uint64_t offset = 0;
	if (argc > 2)
		offset = strtoul(argv[2], NULL, 0);
	trigger_sync(comm, devs, offset);

	//if (argc > 1) {
		//struct j2a_packet p;
		//p.len = 0;
		//if ((ret = j2a_send_by_name(comm, &p, argv[1])) == 0)
			//j2a_packet_print(&p);
	//} else {
	//ret = print_time_device(comm);
	//}

	disconnect(comm, devs);
shutdown:
	j2a_shutdown();

	return ret;
}
