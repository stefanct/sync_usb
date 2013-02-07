/*
 * TODO:
 *		- get current time
 * 		- sync µc
 * 		- schedule event at precisely x ns
 * 			# scheduling
 * 			# cpu affinity
 * 		- toggle some pin
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/io.h>
#include <sched.h>

#define uint unsigned int
#define THIS_PROC	0

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

int set_affin(int cpu) {
	cpu_set_t mask;

	// FIXME: input validation

	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);

	if(sched_setaffinity(THIS_PROC, sizeof(cpu_set_t), &mask) != 0) {
		fprintf(stderr, "sched_setaffinity error %d: %s\n", errno, strerror(errno));
		return EXIT_FAILURE;
	}

	printf("Running on CPU #%d now.\n", cpu);
	return EXIT_SUCCESS;
}

int time(void) {
	//CLOCK_MONOTONIC_RAW
	return EXIT_SUCCESS;
}

int set_sched(int policy) {
	struct sched_param param;

	if(sched_getparam(THIS_PROC, &param) < 0) {
		fprintf(stderr, "sched_get_priority_min error %d: %s\n", errno, strerror(errno));
		return EXIT_FAILURE;
	}

	param.sched_priority = sched_get_priority_min(policy);

	if(param.sched_priority < 0) {
		fprintf(stderr, "sched_get_priority_min error %d: %s\n", errno, strerror(errno));
		return EXIT_FAILURE;
	}

	if(sched_setscheduler(THIS_PROC, policy, &param) != 0) {
		fprintf(stderr, "sched_setscheduler error %d: %s\n", errno, strerror(errno));
		return EXIT_FAILURE;
	}

	printf("Running with policy %d at priority %d now.\n", policy, param.sched_priority);
	return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
	if(iopl(3) != 0) {
		fprintf(stderr, "ERROR: Could not get I/O privileges (%s).\n"
						"You need to be root.\n", strerror(errno));
		return 1;
	}

	
	set_affin(sysconf(_SC_NPROCESSORS_ONLN)/2);
	set_sched(SCHED_RR);
	return EXIT_SUCCESS;
}
