/*
 * Copyright (C) 2014 Samsung Electronics
 *
 * Krzysztof Opasiak <k.opasiak@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/**
 * @file gadget-ms.c
 * @example gadget-ms.c
 * This is an example of how to create gadget with mass storage function
 * with two luns.
 */

#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include "iostream"
#include "cstring"
#include "signal.h"
#include <sys/sysmacros.h>
#include <linux/usb/ch9.h>
#include <usbg/usbg.h>
#include <usbg/function/ms.h>

#define VENDOR          0x1d6b
#define PRODUCT         0x0104
usbg_state *s;
usbg_gadget *g;

void ProgExit(int signo)
{
    printf("come in prog exit\n");
	usbg_rm_gadget(g, USBG_RM_RECURSE);
	usbg_cleanup(s);
    exit(0);
}

void errorCb(int signo)
{
	printf("come in errorCb\n");
	usbg_rm_gadget(g, USBG_RM_RECURSE);
	usbg_cleanup(s);
    exit(-1);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGKILL, ProgExit);
    signal(SIGINT, ProgExit);
    signal(SIGTERM, ProgExit);
    signal(SIGSEGV, errorCb);
    signal(SIGABRT, errorCb);

	usbg_config *c;
	usbg_function *f_ms;
	int ret = -EINVAL;
	usbg_error usbg_ret;

	struct usbg_gadget_attrs g_attrs = {
		.bcdUSB = 0x0200,
		.bDeviceClass =	USB_CLASS_PER_INTERFACE,
		.bDeviceSubClass = 0x00,
		.bDeviceProtocol = 0x00,
		.bMaxPacketSize0 = 64, /* Max allowed ep0 packet size */
		.idVendor = VENDOR,
		.idProduct = PRODUCT,
		.bcdDevice = 0x0001, /* Verson of device */
	};

	struct usbg_gadget_strs g_strs = {
        .manufacturer = "Foo Inc.", /* Manufacturer */
		.product = "Bar Gadget", /* Product string */
        .serial = "0123456789" /* Serial number */
	};

	struct usbg_f_ms_lun_attrs f_ms_luns_array[] = {
		{
			.id = -1, /* allows to place in any position */
			.cdrom = 0,
			.ro = 0,
			.nofua = 0,
			.removable = 1,
			.file = "/dev/sda1",
		}
	};

	struct usbg_f_ms_lun_attrs *f_ms_luns[] = {
		/*
		 * When id in lun structure is below 0 we can place it in any
		 * arbitrary position
		 */
		&f_ms_luns_array[0],
		NULL,
	};

	struct usbg_f_ms_attrs f_attrs = {
		.stall = 0,
		.nluns = 1,
		.luns = f_ms_luns,
	};

	struct usbg_config_strs c_strs = {
			"1xMass Storage"
	};

	usbg_ret = (usbg_error)usbg_init("/sys/kernel/config", &s);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error on USB gadget init\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_cleanup(s);
	}

	usbg_ret = (usbg_error)usbg_create_gadget(s, "g1", &g_attrs, &g_strs, &g);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error on create gadget\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
	    usbg_cleanup(s);
	}

	usbg_ret = (usbg_error)usbg_create_function(g, USBG_F_MASS_STORAGE, "my_reader",
					&f_attrs, &f_ms);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error creating mass storage function\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_rm_gadget(g, USBG_RM_RECURSE);
	    usbg_cleanup(s);
	}

	/* NULL can be passed to use kernel defaults */
	usbg_ret = (usbg_error)usbg_create_config(g, 1, "The only one", NULL, &c_strs, &c);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error creating config\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_rm_gadget(g, USBG_RM_RECURSE);
	    usbg_cleanup(s);
	}

	usbg_ret = (usbg_error)usbg_add_config_function(c, "some_name_here", f_ms);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error adding ms function\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_rm_gadget(g, USBG_RM_RECURSE);
	    usbg_cleanup(s);
	}

    usbg_udc* udc = usbg_get_udc(s, "fc400000.usb");
    if (udc == NULL) {
        fprintf(stderr, "No available UDC found\n");
        usbg_rm_gadget(g, USBG_RM_RECURSE);
        usbg_cleanup(s);
        return false;
    }

	printf("udc name is : %s\n", usbg_get_udc_name(udc));

	usbg_ret = (usbg_error)usbg_enable_gadget(g, udc);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error enabling gadget\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_rm_gadget(g, USBG_RM_RECURSE);
	    usbg_cleanup(s);
	}

    printf("init usb success\n");

    while (1) {
        usleep(10000);
    }

	return 0;
}
