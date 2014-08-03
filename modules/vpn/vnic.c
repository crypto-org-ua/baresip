/*
 * vnic.c
 *
 * Copyright (C) Ignat Korchagin
 *
 */
#include "vnic.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <linux/if.h>
#include <linux/if_tun.h>

#include <string.h>

static const char *clonedev = "/dev/net/tun";

/* int vnic_create(const char *name)
{
	vnic_handle handle = vnic_open(name);
	int res, fd;

	if (!handle)
		return -1;

	fd = (int)handle;

	res = ioctl(fd, TUNSETOWNER, 1000);
	if (res < 0)
	{
		vnic_close(handle);
		return -1;
	}

	res = ioctl(fd, TUNSETPERSIST, 1);
	if (res < 0)
	{
		vnic_close(handle);
		return -1;
	}

	vnic_close(handle);
	return 1;
} */

vnic_handle vnic_open(const char *name)
{
	struct ifreq ifr;
	int res;
	int fd = open(clonedev, O_RDWR);

	if (fd < 0)
		return -1;

	memset(&ifr, 0, sizeof(ifr));

	ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
	if (name)
		strncpy(ifr.ifr_name, name, IFNAMSIZ);

	res = ioctl(fd, TUNSETIFF, (void *)&ifr);
	if (res < 0)
	{
		close(fd);
		return -1;
	}

	return (vnic_handle)fd;
}

void vnic_close(vnic_handle handle)
{
	close((int)handle);
}

int vnic_wait_for_data(vnic_handle handle)
{
	/* One sec timeout */
	static struct timeval timeout = {1, 0};
	fd_set fds;

	FD_ZERO(&fds);
	FD_SET((int)handle, &fds);

	return select((int)handle + 1, &fds, NULL, NULL, &timeout);
}

ssize_t vnic_read(vnic_handle handle, void *buffer, size_t length)
{
	return read((int)handle, buffer, length);
}

ssize_t vnic_write(vnic_handle handle, const void *buffer, size_t length)
{
	return write((int)handle, buffer, length);
}
