/*
 * vnic.h
 *
 * Copyright (C) Ignat Korchagin
 *
 */

#ifndef VNIC_H_
#define VNIC_H_

#include <stdio.h>
#include <unistd.h>

typedef int vnic_handle;

vnic_handle vnic_open(const char *name);
void vnic_close(vnic_handle handle);
int vnic_wait_for_data(vnic_handle handle);
ssize_t vnic_read(vnic_handle handle, void *buffer, size_t length);
ssize_t vnic_write(vnic_handle handle, const void *buffer, size_t length);

#endif /* VNIC_H_ */
