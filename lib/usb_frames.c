#include <stdio.h>

static inline int read_frame_number(const char *path) {
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

