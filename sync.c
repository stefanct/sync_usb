/** @file */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <sys/io.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libgen.h>
#include <stddef.h>
#include <errno.h>
#include <pwd.h>
#include <syslog.h>
#include "libusb-1.0/libusb.h"
#include "da.h"
#include "realtimeify.h"
#include "usb_frames.h"

#define NS_PER_SEC 1000000000L
#define US_PER_SEC	1000000L
#define DAEMON_NAME "SyncUSB"
#define SOCKET_NAME "/tmp/"DAEMON_NAME

#ifdef LOG_PERROR
	#define LOG_OPTS	LOG_PID | LOG_PERROR
#else
	#define LOG_OPTS	LOG_PID
#endif

struct sync_dev {
	struct libusb_device_handle *handle;
	int if_num;
	struct sync_dev *next;
};
typedef struct sync_dev sync_dev;

static sync_dev *root_dev = NULL;
static libusb_context *libusb_ctx = NULL;
static int socket_fd = -1;
static bool isdaemon = false;
static volatile bool run = true;

struct sync_addr {
	int16_t busnum;
	int16_t devaddr;
};

static void remove_sync_dev(sync_dev **list, sync_dev *dev) {
	sync_dev *cur_dev = *list;
	sync_dev *prev_dev = NULL;

	while (cur_dev != NULL) {
		if (cur_dev == dev) {
			if (prev_dev == NULL) {
				/* new root */
				*list = cur_dev->next;
			} else {
				prev_dev->next = cur_dev->next;
			}
			libusb_release_interface(cur_dev->handle, cur_dev->if_num);
			libusb_close(cur_dev->handle);
			free(cur_dev);
			return;
		}
		
		prev_dev = cur_dev;
		cur_dev = cur_dev->next;
	}
}

sync_dev *init_sync_dev(int16_t bus_num, int16_t dev_addr) {
	struct libusb_device_handle *handle = NULL;
	int if_num = -1;
	int ret;

	libusb_device **list;
	ssize_t cnt = libusb_get_device_list(libusb_ctx, &list);
	if (cnt < 0) {
		syslog(LOG_ERR, "Could not get USB device list!");
		return NULL;
	}

	/* search SyncUSB interface within active configurations of all devices */
	for (ssize_t i = 0; i < cnt; i++) {
		libusb_device *dev = list[i];
		/* if the bus number/device address is valid, it has to match.
		 * if not then we will use the first compatible device. */
		int16_t cur_bus_num = libusb_get_bus_number(dev);
		int16_t cur_dev_addr = libusb_get_device_address(dev);
		if (bus_num >= 0) {
			if (bus_num != cur_bus_num)
				continue;
		}

		if (dev_addr >= 0) {
			if (dev_addr != cur_dev_addr)
				continue;
		}

		struct libusb_config_descriptor *cfg_desc;
		ret = libusb_get_active_config_descriptor(dev, &cfg_desc);
		if (ret == LIBUSB_ERROR_NOT_FOUND) {
			/* not configured */
			continue;
		}
		if (ret != 0) {
			syslog(LOG_ERR, "Could not get configuration descriptor!");
			continue;
		}

		for (int i = 0; i < cfg_desc->bNumInterfaces; i++) {
			const struct libusb_interface *intf = &cfg_desc->interface[i];
			if (intf->num_altsetting > 1)
				continue;

			const struct libusb_interface_descriptor *if_desc = intf->altsetting;
			if (if_desc->bInterfaceClass != RTCSYNC_IF_CLASS
				|| if_desc->bInterfaceSubClass != RTCSYNC_IF_SUBCLASS
				|| if_desc->bInterfaceProtocol != RTCSYNC_IF_PROTOCOL)
				continue;

			ret = libusb_open(dev, &handle);
			if (ret != 0) {
				syslog(LOG_ERR, "Could not open USB device!");
				break;
			}

			if (if_desc->iInterface == 0) {
				syslog(LOG_WARNING, "No interface string!");
				continue;
			}
			char name[strlen(RTCSYNC_IF_STRING) + 1];
			if (libusb_get_string_descriptor_ascii(handle, if_desc->iInterface, (unsigned char*)name, sizeof(name)) < 0) {
				syslog(LOG_WARNING, "Could not retrieve string descriptor!");
				continue;
			}

			if (strcmp(RTCSYNC_IF_STRING, name) != 0) {
				syslog(LOG_WARNING, "Interface name (%s) does not match (%s).", name, RTCSYNC_IF_STRING);
				continue;
			}

			if_num = i;
			ret = libusb_claim_interface(handle, if_num);
			if (ret != 0) {
				syslog(LOG_ERR, "Could not claim USB interface!");
				continue;
			}

			libusb_free_config_descriptor(cfg_desc);
			libusb_free_device_list(list, 1);
			sync_dev *dev = malloc(sizeof(sync_dev));
			if (dev == NULL) {
				syslog(LOG_CRIT, "Out of memory.");
				return NULL;
			}

			dev->if_num = if_num;
			dev->handle = handle;
			dev->next = NULL;
			syslog(LOG_NOTICE, "Starting to sync USB device on bus number %d and device address %d.", cur_bus_num, cur_dev_addr);
			return dev;
		}
		libusb_free_config_descriptor(cfg_desc);
	}

	libusb_free_device_list(list, 1);
	syslog(LOG_WARNING, "No compatible USB device found on bus number %d and device address %d!", bus_num, dev_addr);
	syslog(LOG_INFO, "If the search was triggered by udev then the device probably disappeared in the meantime.");
	return NULL;

}

int get_syncbuf(uint8_t *buf, unsigned int len) {
	if (len < 10)
		return 1;

	uint64_t *timebuf = (uint64_t *)&buf[0];
	uint16_t *framebuf = (uint16_t *)&buf[8];

	int fn = wait_for_new_frame_number();
	if (fn < 0)
		return 1;
	*framebuf = fn;
	struct timespec ts;
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
		return 1;
	uint64_t time = ts.tv_sec * US_PER_SEC + ts.tv_nsec / 1000;
	*timebuf = time;

	syslog(LOG_DEBUG, "Filling sync buffer with time %" PRId64 " = 0x%" PRIx64 " (frame number %4d).", *timebuf, *timebuf, fn);
	return 0;
}

static void cleanup(void) {
	sync_dev *cur_dev = root_dev;
	root_dev = NULL;
	while (cur_dev != NULL) {
		libusb_release_interface(cur_dev->handle, cur_dev->if_num);
		libusb_close(cur_dev->handle);
		cur_dev = cur_dev->next;
	}
	if (libusb_ctx != NULL)
		libusb_exit(libusb_ctx);

	if (socket_fd >= 0) {
		if (isdaemon) {
			struct stat fs = {0};
			if (fstat(socket_fd, &fs) != 0 || !S_ISSOCK(fs.st_mode) || unlink(SOCKET_NAME) < 0)
				syslog(LOG_ERR, "Could not remove FIFO.");
		} else
			close(socket_fd);
	}
	syslog(LOG_NOTICE, DAEMON_NAME" exits.");
	closelog();
}

static void bailout(void) {
	cleanup();
	exit(EXIT_FAILURE);
}

static void handler(int sig) {
	run = false;
}

/** Connect or create a singleton socket.
 *  Tries to create a UNIX socket with path \a name and sets \a socket_fd to the resulting file descriptor.
 *  \returns
 *  	-1 on errors,
 *  	 0 on successful server bindings,
 *  	 1 on successful client connects.
 */
int singleton_connect(const char *name, int * const socket_fd) {
	int len, tmpd;
	struct sockaddr_un addr = {0};

	if ((tmpd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0) {
		syslog(LOG_CRIT, "Could not create socket: '%s'.", strerror(errno));
		return -1;
	}

	// fill in socket address structure
	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, name);
	len = offsetof(struct sockaddr_un, sun_path) + strlen(name);

	int ret;
	unsigned int retries = 1; // at least one to start with abandoned socket
	do {
		// bind the name to the descriptor
		ret = bind(tmpd, (struct sockaddr *)&addr, len);
		*socket_fd = tmpd;
		// if this succeeds there was no daemon before
		if (ret == 0) {
			return 0;
		}
		if (errno == EADDRINUSE) {
			ret = connect(tmpd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un));
			if (ret != 0) {
				if (errno == ECONNREFUSED) {
					syslog(LOG_WARNING, "Could not connect to socket - assuming daemon died.");
					unlink(name);
					continue;
				}
				syslog(LOG_ERR, "Could not connect to socket: '%s'.", strerror(errno));
				continue;
			}
			syslog(LOG_NOTICE, "Daemon is already running.");
			return 1;
		}
		syslog(LOG_ERR, "Could not bind to socket: '%s'.", strerror(errno));
	} while (retries-- > 0);

	syslog(LOG_ERR, "Could neither connect to an existing daemon nor become one.");
	close(tmpd);
	return -1;
}

static int drop_root(const char *username) {
	struct passwd *pw = getpwnam(username);
	if (pw == NULL) {
		syslog(LOG_WARNING, "No such user '%s'", username);
		return 1;
	}

	uid_t usr_id = pw->pw_uid;
	uid_t grp_id = pw->pw_gid;

	if (setgid(grp_id) != 0)
		return 1;
	if (setuid(usr_id) != 0)
		return 1;

	return 0;
}

void usage(const char *name) {
	printf("Usage: %s [-d] [-b <bus> -d <dev>] [-D] [-i <secs>] [-u username]\n\n", name);

	printf(" -b | --busnum <bus>                use device on USB port <bus> (default: any)\n"
	       " -d | --device <dev>                use USB device with address <dev> (default: any)\n"
	       " -D | --daemon                      daemonize (default: do not)\n"
	       " -i | --interval <secs>             sleep <secs> seconds between syncs (default: 1)\n"
	       " -u | --username <username>         if run as root, change to user <username> (default: 'nobody')\n"
	       " -v | --verbose                     print verbose output (default: do not)\n"
	      );
}

static pid_t daemonize(void) {
	/* already a daemon? */
	if (getppid() == 1) {
		syslog(LOG_WARNING, "Tried to daemonize twice.");
		return -1;
	}

	pid_t pid;

	pid = fork();
	switch(pid) {
		case -1:
			syslog(LOG_ERR, "Could not fork: %s", strerror(errno));
			return -1;
		case 0: break; /* child succeeds normally */
		default: return pid; /* parent can end here */
	}

	if (setsid() < 0) {
		syslog(LOG_ERR, "Could not create new session: %s", strerror(errno));
		return -1;
	}

	umask(077);

	if (chdir("/") != 0) {
		syslog(LOG_ERR, "Unable to change directory to '/': %s", strerror(errno));
		return -1;
	}

	if (freopen("/dev/zero", "r", stdin) == NULL) {
		syslog(LOG_ERR, "Could not reopen stdin: %s", strerror(errno));
		return -1;
	}

	if (freopen("/dev/null", "w", stdout) == NULL) {
		syslog(LOG_ERR, "Could not reopen stdout: %s", strerror(errno));
		return -1;
	}

	if (freopen("/dev/null", "w", stderr) == NULL) {
		syslog(LOG_ERR, "Could not reopen stderr: %s", strerror(errno));
		return -1;
	}
	return pid;
}

int sync_devices (uint8_t *buf, unsigned int len) {
	int errcnt = 0;
	sync_dev *cur_dev = root_dev;
	while (run && cur_dev != NULL) {
		sync_dev *nxt_dev = cur_dev->next;

		int transferred;
		int ret = libusb_interrupt_transfer(cur_dev->handle,
								RTCSYNC_EP_ADDR,
								buf,
								len,
								&transferred,
								100); // timeout [ms]
		libusb_device *dev = libusb_get_device(cur_dev->handle);
		if (ret == 0 && transferred == len) {
			syslog(LOG_DEBUG, "Device on bus %d, address %d synced.", libusb_get_bus_number(dev), libusb_get_device_address(dev));
		} else if (ret == LIBUSB_ERROR_NO_DEVICE || ret == LIBUSB_ERROR_OTHER) {
			syslog(LOG_NOTICE, "Removal detected of device on bus %d address %d.", libusb_get_bus_number(dev), libusb_get_device_address(dev));
			remove_sync_dev(&root_dev, cur_dev);
		} else {
			syslog(LOG_ERR, "Could not send sync to device on bus %d address %d (sent %d/%dB): %s", libusb_get_bus_number(dev), libusb_get_device_address(dev), transferred, len, (ret == 0) ? "incomplete" : libusb_error_name(ret));
			ret = libusb_reset_device(cur_dev->handle);
			if (ret != 0) {
				syslog(LOG_ERR, "Tried to reset device on bus %d address %d, but even that failed: %s", libusb_get_bus_number(dev), libusb_get_device_address(dev), libusb_error_name(ret));
				remove_sync_dev(&root_dev, cur_dev);
			}
			errcnt++;
		}
		cur_dev = nxt_dev;
	}
	return errcnt;
}

int main(int argc, char **argv) {
	char *exe_name = strdup(argv[0]);
	openlog(basename(exe_name), LOG_OPTS, LOG_DAEMON);

	static const char optstring[] = "b:d:Dhi:ru:v";
	static const struct option long_options[] = {
		{"bus",		1, NULL, 'b'},
		{"device",		1, NULL, 'd'},
		{"daemon",		0, NULL, 'D'},
		{"help",		0, NULL, 'h'},
		{"interval",	1, NULL, 'i'},
		{"realtimeify",	0, NULL, 'r'},
		{"username",	1, NULL, 'u'},
		{"verbose",		0, NULL, 'v'},
		
		{NULL,			0, NULL, 0},
	};

	bool daemonize_it = false;
	bool realtimeify_it = false;
	int16_t bus_num = -1;
	int16_t dev_addr = -1;
	unsigned int interval = 1;
	const char *username = "nobody";
	setlogmask(LOG_MASK(LOG_EMERG) | LOG_MASK(LOG_ALERT) | LOG_MASK(LOG_CRIT) | LOG_MASK(LOG_ERR) | LOG_MASK(LOG_WARNING) | LOG_MASK(LOG_NOTICE)); // default for daemon
	int logmask = setlogmask(0);

	int opt;
	int option_index;
	opterr = 0;
	while ((opt = getopt_long(argc, argv, optstring,
				  long_options, &option_index)) != EOF) {
		switch (opt) {
		case 'b': {
			int temp = 0;
			char *endptr;
			temp = strtol(optarg, &endptr, 10);
			if (*endptr || temp < 0 || temp > 255) {
				syslog(LOG_CRIT, "Illegal bus number: '%s'!", optarg);
				cleanup();
				return EXIT_FAILURE;
			}
			bus_num = (int16_t)temp;
			break;
		}
		case 'd': {
			int temp = 0;
			char *endptr;
			temp = strtol(optarg, &endptr, 10);
			if (*endptr || temp < 0 || temp > 127) {
				syslog(LOG_CRIT, "Illegal device number: '%s'!", optarg);
				bailout();
			}
			dev_addr = (int16_t)temp;
			break;
		}
		case 'D':
			daemonize_it = true;
			break;
		case 'h':
			usage(exe_name);
			cleanup();
			return EXIT_SUCCESS;
		case 'i':
			interval = atoi(optarg);
			break;
		case 'r':
			realtimeify_it = true;
			break;
		case 'u':
			username = optarg;
			break;
		case 'v':
			logmask |= LOG_MASK(LOG_INFO) | LOG_MASK(LOG_DEBUG);
			break;
		default:
			syslog(LOG_CRIT, "Error: Unknown parameter(s) found!");
			bailout();
			break;
		}
	}

	if (optind < argc) {
		syslog(LOG_CRIT, "Error: Extra parameter(s) found!");
		bailout();
	}

	if (interval < 1) {
		syslog(LOG_WARNING, "Interval of %d out of bounds, using 1.", interval);
		interval = 1;
	}

	/* 24 hrs max */
	if (interval > 86400) {
		syslog(LOG_WARNING, "Interval of %d out of bounds, using 86400.", interval);
		interval = 86400;
	}

	setlogmask(logmask);

	if (realtimeify_it) {
		if (getuid() != 0) {
			syslog(LOG_CRIT, "You need to be root to realtimeify!");
			bailout();
		}
		if (soft_realtimeify() != EXIT_SUCCESS) {
			syslog(LOG_CRIT, "Could not realtimeify!");
			bailout();
		}
	}

	if (getuid() == 0) {
		if (drop_root(username) != 0) {
			syslog(LOG_WARNING, "Cannot drop root privileges and change to '%s'%s%s.",
				   username, (errno != 0) ? ": " : "", (errno != 0) ? strerror(errno) : "");
		}
	}

	
	int ret;
	switch (singleton_connect(SOCKET_NAME, &socket_fd)) {
		case 0: { /* Daemon */
			isdaemon = true;
			if (daemonize_it) {
				pid_t pid = daemonize();
				if (pid < 0) {
					syslog(LOG_CRIT, "Could not daemonize!");
					bailout();
				}
				if (pid > 0) {
					fprintf(stderr, DAEMON_NAME" daemon started as pid %d.\n", pid);
					exit(EXIT_SUCCESS);
				}
			}

			/* set terminating signal handlers */
			struct sigaction sa;
			sa.sa_handler = &handler;
			sigemptyset(&sa.sa_mask);
			if (sigaction(SIGHUP, &sa, NULL) != 0 || sigaction(SIGINT, &sa, NULL) != 0 || sigaction(SIGQUIT, &sa, NULL) != 0 || sigaction(SIGPIPE, &sa, NULL) != 0 || sigaction(SIGALRM, &sa, NULL) != 0 || sigaction(SIGTERM, &sa, NULL) != 0) {
				syslog(LOG_CRIT, "Could not set up signal handlers!");
				bailout();
			}

			/* ignore some other signals */
			sa.sa_handler = SIG_IGN;
			sigaction(SIGUSR1, &sa, NULL);
			sigaction(SIGUSR2, &sa, NULL);

			ret = libusb_init(&libusb_ctx);
			if (ret != 0) {
				syslog(LOG_ERR, "Could not initialize libusb!");
				bailout();
			}

			syslog(LOG_NOTICE, DAEMON_NAME" started.");

			root_dev = init_sync_dev(bus_num, dev_addr);
			/* even without a valid device (address) we continue as a daemon
			 * because we became the singleton successfully before. */

			unsigned int soft_errs = 0;
			const unsigned int soft_errs_max = 2;
			
			while (true) {
				if (!run)
					break;
				if (soft_errs > soft_errs_max) {
					syslog(LOG_CRIT, "Too many errors (%d)!", soft_errs);
					bailout();
				}
				/* check for newly added devices */
				struct msghdr msg = {0};
				struct iovec iovec;
				struct sync_addr sa;
				iovec.iov_base = &sa;
				iovec.iov_len = sizeof(sa);
				msg.msg_iov = &iovec;
				msg.msg_iovlen = 1;

				while (run) {
					ret = recvmsg(socket_fd, &msg, MSG_DONTWAIT);
					if (ret != sizeof(sa)) {
						if (errno != EAGAIN && errno != EWOULDBLOCK) {
							syslog(LOG_ERR, "Error while accessing socket: %s", strerror(errno));
							bailout();
						}
						syslog(LOG_INFO, "No further device addresses in socket.");
						break;
					} else {
						sync_dev *new_dev = init_sync_dev(sa.busnum, sa.devaddr);
						if (new_dev != NULL) {
							new_dev->next = root_dev;
							root_dev = new_dev;
						}
					}
				}

				/* get a timestamp */
				uint8_t buf[10]; // 64 + 16 bits = 10 B
				ret = get_syncbuf(buf, sizeof(buf));
				if (ret != 0) {
					syslog(LOG_WARNING, "get_syncbuf returned %d.", ret);
					soft_errs += ret;
					usleep(100 * 1000);
					continue;
				}

				if (!run)
					break;
				ret = sync_devices(buf, sizeof(buf));
				if (ret != 0) {
					syslog(LOG_WARNING, "sync_devices returned %d.", ret);
					soft_errs += ret;
					usleep(100 * 1000);
					continue;
				}

				if (!run)
					break;
				syslog(LOG_DEBUG, "Sleeping for %d seconds.", interval);
				sleep(interval);
			}
			syslog(LOG_WARNING, "Dropped out of daemon loop. Shutting down.");
			bailout();
		}
		case 1: { /* Client */
			struct msghdr msg = {0};
			struct iovec iovec;
			struct sync_addr sa = {bus_num, dev_addr};
			iovec.iov_base = &sa;
			iovec.iov_len = sizeof(sa);
			msg.msg_iov = &iovec;
			msg.msg_iovlen = 1;
			ret = sendmsg(socket_fd, &msg, 0);
			if (ret != sizeof(sa)) {
				if (ret < 0)
					syslog(LOG_CRIT, "Could not send device address to daemon: '%s'!", strerror(errno));
				else
					syslog(LOG_CRIT, "Could not send device address to daemon completely!");
				bailout();
			}
			syslog(LOG_NOTICE, "Sent device information (bus %d, address %d) to daemon.", bus_num, dev_addr);
			break;
		}
		default:
			bailout();
			break;
	}

	cleanup();
	return EXIT_SUCCESS;
}

