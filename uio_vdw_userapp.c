/*
 * user.c
 *
 *  Created on: Jun 14, 2022
 *      Author: Gert Boddaert
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define APP_NAME "simple-uio-user"
#define APP_VERSION "1.0.0"
#define UIODEV "/dev/uio"
#define DRV_NAME "uio_vdw"
#define DRV_DEVICE_NAME "uio_vdw_device"

static const char* readsysparam(const char *strparampath, char *readstr,
		unsigned int paramsiz) {
	char *retstring = 0;
	int fd = -1;
	ssize_t r = 0;
	char errorstr[512];
	fd = open(strparampath, O_RDONLY);
	if (fd < 0) {
		snprintf(errorstr, sizeof(errorstr), "open %s failed: ", strparampath);
		perror(errorstr);
		goto exit_func;
	}
	memset(readstr, 0, paramsiz);
	r = read(fd, readstr, paramsiz - 1);
	if (r < 0) {
		snprintf(errorstr, sizeof(errorstr), "open %s failed: ", strparampath);
		perror(errorstr);
		goto exit_func;
	} else {
		retstring = readstr;
	}
	exit_func: if (fd >= 0) {
		close(fd);
	}
	return retstring;
}

void printhelp() {
	/* hi:o:w:c:d: */
	const char *helpstring =
			"uio_vdw_user test program\r\n"
					"options:\r\n"
					"\th: print this help\r\n"
					"\ti <x>: DEC x milliseconds to wait for interrupt\r\n"
					"\to <x>: HEX offset x from start mmap (please align on 32-bit)\r\n"
					"\tw <x>: HEX x = value to write, without -w option, only read\r\n"
					"\tc <x>: DEC x = number of incremental address iterations\r\n"
					"\td <x>: HEX select /dev/uio<x> instead of looping to find first 'vdw_uio_device' device\r\n";
	fprintf(stderr, "%s", helpstring);
}

int main(int argc, char *argv[]) {
	int error = -1;
	int uiofd = -1;
	int deviter = 0;
	uint32_t PAGE_SIZE = sysconf(_SC_PAGESIZE);
	uint32_t size = 0;
	uint32_t *iomem = (uint32_t*) -1;
	char fname[256];
	int waitinttime = 0;
	uint32_t offset = 0;
	uint32_t writeval = 0;
	bool writeop = false;
	uint32_t count = 1;
	int devsel = -1;
	int opt = 0;

	fprintf(stderr, "%s - %s (build %s / %s)\r\n", APP_NAME, APP_VERSION,
			__DATE__, __TIME__);

	while ((opt = getopt(argc, argv, "hi:o:w:c:d:")) != -1) {
		switch (opt) {
		case 'i':
			waitinttime = atoi(optarg);
			break;
		case 'o':
			offset = strtol(optarg, NULL, 16);
			break;
		case 'w':
			writeop = true;
			writeval = strtol(optarg, NULL, 16);
			break;
		case 'c':
			count = strtol(optarg, NULL, 10);
			break;
		case 'd':
			devsel = (int) strtol(optarg, NULL, 16);
			deviter = devsel;
			break;
		default: // intentional fall through
			fprintf(stderr, "\r\nInvalid option received\r\n");
		case 'h':
			printhelp();
			goto exit_func;
		}
	}

	// find the right /dev/uioX ...
	do {
		snprintf(fname, sizeof(fname), "/sys/class/uio/uio%d/name", deviter);
		if (readsysparam(fname, fname, sizeof(fname))) {
			fprintf(stderr,
					"check using %s%d name \"%s\" with uio provided name: %s",
					UIODEV, deviter, DRV_DEVICE_NAME, fname);
			if (0 == strncmp(DRV_DEVICE_NAME, fname, strlen(DRV_DEVICE_NAME))) {
				fprintf(stderr, "check mapsize %s%d\r\n", UIODEV, deviter);
				snprintf(fname, sizeof(fname),
						"/sys/class/uio/uio%d/maps/map0/size", deviter);
				if (readsysparam(fname, fname, sizeof(fname))) {
					size = strtol(fname, NULL, 16);
					fprintf(stderr, "to be mapped size = %u\r\n", size);
					devsel = deviter;
				}
				if (!size) {
					fprintf(stderr, "Failed to get mapped size\r\n");
				} else {
					break;
				}
			}
		}
		if (devsel > 0)
			break;
		++deviter;
	} while (deviter < 10);

	if (deviter >= 10 || !size) {
		goto exit_func;
	}

	fprintf(stderr, "%s operation on /dev/uio%d at offset 0x%08x count %u\r\n",
			writeop ? "write" : "read", devsel, offset, count);

	snprintf(fname, sizeof(fname), "%s%d", UIODEV, deviter);
	uiofd = open(fname, O_RDWR);
	if (uiofd < 0) {
		perror("uio open:");
		error = errno;
		goto exit_func;
	}

	fprintf(stderr, "%s opened\r\n", fname);

	iomem = (uint32_t*) mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, uiofd,
			0 * PAGE_SIZE);
	fprintf(stderr, "%s mapped at %p\r\n", fname, iomem);

	if ((uintptr_t) iomem == (uintptr_t) -1) {
		perror("uio mmap:");
		error = errno;
		goto exit_func;
	}

	fprintf(stderr, "waiting for interrupt %d ms\r\n", waitinttime);
	ssize_t nb = -1;
	struct pollfd fds = { .fd = uiofd, .events = POLLIN, };
	uint32_t info = 1; /* unmask */
#if 0 /* write not implemented in custom uio driver, but in uio_pdrv_genirq it is */
	nb = write(uiofd, &info, sizeof(info));
	if (nb != (ssize_t)sizeof(info)) {
		perror("write");
		goto exit_func;
	}
#endif

	int ret = poll(&fds, 1, waitinttime);
	if (ret >= 1) {
		/* Do something in response to the interrupt. */
		fprintf(stderr, "Interrupt! ");
		nb = read(uiofd, &info, sizeof(info));
		fprintf(stderr, "read %u bytes, ", (unsigned int) nb);
		if (nb == (ssize_t) sizeof(info)) {
			printf("#%u!\n", info);
		} else {
			perror("read()");
		}
	} else if (waitinttime) {
		perror("poll()");
	}

	uint32_t *iomem_iter = iomem + (offset / 4);
	for (uint32_t iter = 0; iter < count; iter++) {
		fprintf(stderr, "0x%08x (%p) ", offset + iter * 4, iomem_iter);
		if (writeop) {
			fprintf(stderr, "was 0x%08x, ", *iomem_iter);
			*iomem_iter = writeval;
		}
		fprintf(stderr, "is now 0x%08x\r\n", *iomem_iter);
		++iomem_iter;
	}

	exit_func: if ((uintptr_t) iomem != (uintptr_t) -1) {
		munmap(iomem, size);
		fprintf(stderr, "%s unmapped\r\n", fname);
	}

	if (uiofd >= 0) {
		close(uiofd);
		fprintf(stderr, "%s closed\r\n", fname);
	}

	return error;
}

