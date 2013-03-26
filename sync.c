#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/io.h>
#include "libusb-1.0/libusb.h"
#include "usb_frames.h"

static const uint8_t USB_IF_CLASS = 0xFF;
static const uint8_t USB_IF_SUBCLASS = 0x12;
static const uint8_t USB_IF_PROTOCOL = 0xEF;
static const uint8_t USB_IN_EPNUM = 0x80 | 1;
static const uint8_t USB_OUT_EPNUM = 2;

static bool containsUsbEndpoint(const struct libusb_interface_descriptor *if_desc, uint8_t addr) {
	for (int i = 0; i < if_desc->bNumEndpoints; i++) {
		if (if_desc->endpoint[i].bEndpointAddress == addr)
			return true;
	}
	return false;
}

static bool isArduino(libusb_device *dev) {
	struct libusb_device_descriptor dev_desc;
	int ret = libusb_get_device_descriptor(dev, &dev_desc);
	if (ret != 0)
		return false;

	if (dev_desc.bDeviceClass != 0xff)
		return false;

	struct libusb_config_descriptor *cfg_desc;
	ret = libusb_get_active_config_descriptor(dev, &cfg_desc);
	if (ret != 0)
		return false;

	bool found = false;
	for (int i=0; i < cfg_desc->bNumInterfaces; i++) {
		const struct libusb_interface *intf = cfg_desc->interface;
		for (int j=0; j < intf->num_altsetting; j++) {
			const struct libusb_interface_descriptor *if_desc = intf->altsetting;
			if (if_desc->bInterfaceClass != USB_IF_CLASS
				|| if_desc->bInterfaceSubClass != USB_IF_SUBCLASS
				|| if_desc->bInterfaceProtocol != USB_IF_PROTOCOL)
				continue;
			if (!containsUsbEndpoint(if_desc, USB_IN_EPNUM)
				|| !containsUsbEndpoint(if_desc, USB_OUT_EPNUM))
				continue;
			found = true;
			goto out;
		}
	}
out:
	libusb_free_config_descriptor(cfg_desc);

	return found;
}

struct libusb_device_handle *usb_connect(void) {
	int ret;

	ret = libusb_init(NULL);
	if (ret != 0) {
		fprintf(stderr, "Could not initialize libusb!\n");
		return NULL;
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
		if (isArduino(dev)) {
			found = dev;
			break;
		}
	}

	if (found == NULL) {
		ret = 1;
		fprintf(stderr, "No compatible USB device found!\n");
		goto out;
	}
	libusb_device_handle *handle;
	ret = libusb_open(found, &handle);
	if (ret != 0) {
		fprintf(stderr, "Could not open USB device!\n");
		goto out;
	}

	ret = libusb_claim_interface(handle, 0);
	if (ret != 0) {
		fprintf(stderr, "Could not open USB device!\n");
		goto out;
	}

out:
	libusb_free_device_list(list, 1);
	if (ret != 0)
		return NULL;
	else
		return handle;
}

#define REQ_SYNC_TIME 13
int sync_time_control(struct libusb_device_handle *dev_handle) {

	uint8_t buf[10]; // 64 + 16 bits = 10 B
	uint64_t *timebuf = (uint64_t *)&buf[0];
	uint16_t *framebuf = (uint16_t *)&buf[8];

	//while(true
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


		int ret = libusb_control_transfer(dev_handle,
								LIBUSB_ENDPOINT_OUT |  LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_DEVICE,
								REQ_SYNC_TIME,
								0, // value
								0, // index
								buf, // destination*
								sizeof(buf), // size
								50); // timeout [ms]
		printf("%s: sent time %" PRId64 " with result %d\n", __func__, time, ret);
	return ret;
}


int main(int argc, char **argv) {
	struct libusb_device_handle *handle = usb_connect();
	if (handle == NULL)
		return EXIT_FAILURE;

	sync_time_control(handle);
	//libusb_release_interface(NULL, 0);
	libusb_close(handle);
	libusb_exit(NULL);
}
