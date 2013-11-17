#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <inttypes.h>
#include <time.h>
#include "time_funcs.h"
#include "j2a.h"

//#define fromArray(type, source, offset) \
	for (unsigned int i = offset; i < sizeof(type); i++)\
		us |= (type)p->msg[i] << (i * sizeof(type));

#define fromArray(type, source, offset) *(type*)(&(source)[offset])
#define toArray(type, source, destArray, offset) { type* ntoh_temp_var = (type*)(&(destArray)[offset]);ntoh_temp_var[0] = (source); }

int print_time_a2j(uint8_t *buf, size_t off) {
		//uint64_t us = 0;
		//for (unsigned int i = 0; i < 8; i++)
			//us |= (uint64_t)p->msg[i] << (i * 8);
		uint64_t us = fromArray(uint64_t, buf, off);

		//printf("us=%0"PRIx64"\n", us);
		//printf("us=%0"PRId64"\n", us);

		struct timespec ts;
		ts.tv_nsec = us % US_PER_SEC * 1000;
		ts.tv_sec = us / US_PER_SEC;
		//printf("sec=%0"PRId64"\n", ts.tv_sec);
		return print_timespec(&ts, NULL);
		
}

static int print_time_devices(j2a_handle *comm[], unsigned int len) {
	for (uint i = 0; i < len; i++) {
		j2a_packet p;
		p.len = 0;
		if (j2a_send_by_name(comm[i], &p, "getTime") != 0)
			return EXIT_FAILURE;

		if (print_time_a2j(p.msg, 0) != 0)
			return EXIT_FAILURE;
		printf("\n");
	}
	return EXIT_SUCCESS;
}

#define NS_PER_SEC 1000000000L
#define US_PER_SEC	1000000L
static int trigger_sync(j2a_handle *comm[], unsigned int len, uint64_t offset) {
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

		//j2a_print_packet(&p);
		printf("setTrigger on %i ", i);
		if (j2a_send_by_name(comm[i], &p, "setTrigger") != 0) {
			 printf("failed\n");
			return EXIT_FAILURE;
		} else {
			 printf("succeeded\n");
			 //j2a_print_packet(&p);
		}
	}
	return EXIT_SUCCESS;
}

int connect(j2a_handle *comm[], unsigned int len) {
	for (uint i = 0; i < len; i++) {
		char nth[2];
		snprintf(nth, 2, "%i", i);
		comm[i] = j2a_connect(nth);
		if (comm[i] == NULL) {
			fprintf(stderr, "connect %s failed\n", nth);
			return EXIT_FAILURE;
		}
		printf("connected %d.\n", i);
		if (j2a_fetch_funcmap(comm[i]) != 0)
			goto bail_out;
		
		//j2a_print_funcmap(comm[i], stdout);

		//if (j2a_fetch_props(comm[i]) != 0)
			//return EXIT_FAILURE;
		
		//j2a_print_propmap(comm[i], stdout);
	}
	return EXIT_SUCCESS;
bail_out:
	for (uint i = 0; i < len; i++) {
		j2a_disconnect(comm[i]);
	}
	return EXIT_FAILURE;
}

void disconnect(j2a_handle *comm[], unsigned int len) {
	for (uint i = 0; i < len; i++)
		j2a_disconnect(comm[i]);
}

void handle_sif_packet(struct j2a_sif_packet *sif) {
	j2a_handle *comm = sif->comm;
	j2a_packet *p = &sif->p;
	uint8_t seq = sif->seq;
	//printf("sif packet with sequence number %d received.\n", seq);
	//j2a_print_packet(p);
	print_time_a2j(p->msg, 0);
	printf(", ");
	int16_t quad = fromArray(int16_t, p->msg, sizeof(time_t));
	printf("quad value: %d, ", quad);

	int8_t pwm = (quad - 1024) * 100 / 1024;
	printf("pwm value: %d, ", pwm);
	toArray(int8_t, pwm, p->msg, 0);
	p->len = 1;
	int ret = j2a_send_by_name(comm, p, "a2j_set_servo");
	if (ret != 0)
		printf("j2a_send_by_name() returned %d\n", ret);
	else if (p->cmd != 0)
		printf("the RPC returned %d\n", p->cmd);
	else
		printf("ok\n");
}

static bool run = true;
static void handler(int sig) {
	(void) sig;
	//printf("Shutting down\n");
	run = false;
}

static int test_j2a(j2a_handle *comm) {
	int ret = 0;
	for (int i = 0; i <= 255; i++) {
		j2a_packet p = {0};
		for (int j = 0; j < i; j++) {
			p.msg[j] = j;
		}
		p.len = i;
		j2a_packet backup = p;
		if ((ret = j2a_send_by_name(comm, &p, "a2jEcho")) != 0) {
			printf("a2jEcho failed for i=%d with %d\n", i, ret);
			j2a_print_packet(&backup);
			ret = 1;
			break;
		}
	}
	return ret;
}

int main(int argc, char **argv) {
	if (j2a_init() != 0) {
		fprintf(stderr, "init failed\n");
		return EXIT_FAILURE;
	}

	int ret = EXIT_SUCCESS;
	//libusb_set_debug(NULL, 2);
	//const char *nth_dev = NULL;
	//if (argc > 1)
		//nth_dev = argv[1];

	struct sigaction sa;
	sa.sa_handler = &handler;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGHUP, &sa, NULL) != 0 || sigaction(SIGINT, &sa, NULL) != 0 || sigaction(SIGQUIT, &sa, NULL) != 0 || sigaction(SIGPIPE, &sa, NULL) != 0 || sigaction(SIGALRM, &sa, NULL) != 0 || sigaction(SIGTERM, &sa, NULL) != 0) {
		fprintf(stderr, "Could not set up signal handlers!");
		return EXIT_FAILURE;
	}
	/* ignore some other signals */
	sa.sa_handler = SIG_IGN;
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);


	uint devs = 1;
	j2a_handle *comm[devs];
	if (connect(comm, devs) != 0) {
		fprintf(stderr, "connect failed\n");
		ret = EXIT_FAILURE;
		goto shutdown;
	}

/*
	ret = test_j2a(comm[0]);
	goto disconnect;
	*/

	j2a_sif_handler h = {
			.cmd = 13,
			.handle = handle_sif_packet,
			.next = NULL
	};
	j2a_add_sif_handler(comm[0], &h);

///*
	if (print_time_devices(comm, devs)) {
		ret = EXIT_FAILURE;
		goto disconnect;
	}


	uint64_t offset = 0;
	if (argc > 2)
		offset = strtoul(argv[2], NULL, 0);
	trigger_sync(comm, devs, offset);

/*
	if (argc > 1) {
		struct j2a_packet p;
		p.len = 0;
		if ((ret = j2a_send_by_name(comm[0], &p, argv[1])) == 0)
			j2a_print_packet(&p);
	//} else {
		//ret = print_time_device(comm);
	}
*/

	while (run) {
		//int in;
		//if (fscanf(stdin, "%d", &in) == 1) {
			//printf("pwm value: %d\n", in);
			//j2a_packet pack = {0};
			//j2a_packet *p = &pack;
			//toArray(int16_t, in, p->msg, 0);
			//p->len = sizeof(int16_t);
			//ret = j2a_send_by_name(comm[0], p, "a2j_set_pwm");
			//printf("j2a_send_by_name() returned %d\n", ret);
		//}
		sleep(1);
	}
disconnect:
	disconnect(comm, devs);
shutdown:
	j2a_shutdown();

	return ret;
}
