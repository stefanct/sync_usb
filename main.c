/*
 * TODO:
 *		- get current time
 * 		- sync µc
 * 		- schedule event at precisely x ns
 * 		- toggle some pin
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/io.h>

#define uint unsigned int

int main(int argc, char **argv) {
	if (iopl(3) != 0) {
		fprintf(stderr, "ERROR: Could not get I/O privileges (%s).\n"
						"You need to be root.\n", strerror(errno));
		return 1;
	}
	for (uint i = 0; i <= 0xff; i++) {
		outb(i, 0x80);
		usleep(5000);
		printf("outb %03d\n", i);
		
	}
	return 0;
}