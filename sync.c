#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/io.h>
#include "libusb-1.0/libusb.h"
#include "da.h"
#include "usb_frames.h"

#define NS_PER_SEC 1000000000L
#define US_PER_SEC    1000000L

static bool containsUsbEndpoint(const struct libusb_interface_descriptor *if_desc, uint8_t addr) {
	for (int i = 0; i < if_desc->bNumEndpoints; i++) {
		if (if_desc->endpoint[i].bEndpointAddress == addr)
			return true;
	}
	return false;
}

static int getSyncIF(libusb_device *dev) {
	struct libusb_device_descriptor dev_desc;
	int ret = libusb_get_device_descriptor(dev, &dev_desc);
	if (ret != 0)
		return -1;

	printf("dev class: %d, subclass: %d, prot: %d\n", dev_desc.bDeviceClass, dev_desc.bDeviceSubClass, dev_desc.bDeviceProtocol);
	if (dev_desc.bDeviceClass != 0xff)
		return -1;

	struct libusb_config_descriptor *cfg_desc;
	ret = libusb_get_active_config_descriptor(dev, &cfg_desc);
	if (ret != 0)
		return -1;

	int if_num = -1;
	//int cfg_num = -1;
	printf("num if: %d\n", cfg_desc->bNumInterfaces);
	for (int i = 0; i < cfg_desc->bNumInterfaces; i++) {
		const struct libusb_interface *intf = cfg_desc->interface;
		printf("num_altsetting: %d\n", intf->num_altsetting);
		for (int j = 0; j < intf->num_altsetting; j++) {
			const struct libusb_interface_descriptor *if_desc = intf->altsetting + j;
			printf("int class: %d, subclass: %d, prot: %d\n", if_desc->bInterfaceClass, if_desc->bInterfaceSubClass, if_desc->bInterfaceProtocol);
			if (if_desc->bInterfaceClass != RTCSYNC_IF_CLASS
				|| if_desc->bInterfaceSubClass != RTCSYNC_IF_SUBCLASS
				|| if_desc->bInterfaceProtocol != RTCSYNC_IF_PROTOCOL)
				continue;
			if (!containsUsbEndpoint(if_desc, RTCSYNC_EP_ADDR))
				continue;
			if_num = i;
			//cfg_num = j;
			goto out;
		}
	}
out:
	libusb_free_config_descriptor(cfg_desc);

	return if_num;
}

int usb_connect(struct libusb_device_handle **handlep, int *if_num) {
	int ret;

	ret = libusb_init(NULL);
	if (ret != 0) {
		fprintf(stderr, "Could not initialize libusb!\n");
		return 1;
	}

	libusb_device **list;
	libusb_device *found = NULL;
	ssize_t cnt = libusb_get_device_list(NULL, &list);
	if (cnt < 0) {
		ret = 1;
		fprintf(stderr, "Could not get USB device list!\n");
		goto out;
	}

	for (ssize_t i = 0; i < cnt; i++) {
		libusb_device *dev = list[i];
		if ((*if_num = getSyncIF(dev)) >= 0) {
			found = dev;
			break;
		}
	}

	if (found == NULL) {
		ret = 1;
		fprintf(stderr, "No compatible USB device found!\n");
		goto out;
	}
	struct libusb_device_handle *handle;
	ret = libusb_open(found, &handle);
	if (ret != 0) {
		fprintf(stderr, "Could not open USB device!\n");
		goto out;
	}
	*handlep = handle;
	ret = libusb_claim_interface(handle, *if_num);
	if (ret != 0) {
		fprintf(stderr, "Could not claim USB device!\n");
		goto out;
	}

out:
	libusb_free_device_list(list, 1);
	if (ret != 0)
		return 1;
	else
		return 0;
}

#define REQ_SYNC_TIME 13
int send_sync(struct libusb_device_handle *dev_handle) {

	uint8_t buf[10]; // 64 + 16 bits = 10 B
	uint64_t *timebuf = (uint64_t *)&buf[0];
	uint16_t *framebuf = (uint16_t *)&buf[8];

	int fn = wait_for_new_frame_number();
	if (fn < 0)
		return 1;
	*framebuf = fn;
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
		return 2;
	uint64_t time = ts.tv_sec * US_PER_SEC + ts.tv_nsec / 1000;
	*timebuf = time;

	printf("%s: sending time %" PRId64 " = 0x%" PRIx64 " (frame number %4d).\n", __func__, *timebuf, *timebuf, fn);
	int transferred;
	int ret = libusb_interrupt_transfer(dev_handle,
							LIBUSB_ENDPOINT_OUT | 3,
							buf,
							sizeof(buf), // size
							&transferred,
							100); // timeout [ms]
	if (ret != 0) {
		fprintf(stderr, "%s\n", libusb_error_name(ret));
	}
	return ret;
}


int main(int argc, char **argv) {
	struct libusb_device_handle *handle = NULL;
	int int_if;
	if (usb_connect(&handle, &int_if) != 0)
		return EXIT_FAILURE;

	int secs = 1;
	if (argc >= 2)
		secs = atoi(argv[1]);
	if (secs < 1 || secs > 86400) /* 24 hrs max */
		secs = 1;

	int ret;
	while ((ret = send_sync(handle)) == 0) {
		sleep(secs);
	}

	libusb_release_interface(NULL, int_if);
	libusb_close(handle);
	libusb_exit(NULL);

	return EXIT_FAILURE;
}
