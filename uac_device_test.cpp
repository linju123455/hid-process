/*
 * Copyright (C) 2018 Metanate Ltd
 *
 * John Keeping <john@metanate.com>
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

#include <errno.h>
#include <stdio.h>
#include "iostream"
#include "signal.h"
#include <linux/usb/ch9.h>
#include <usbg/usbg.h>
#include <usbg/function/uac2.h>

#define VENDOR          0x1d6b
#define PRODUCT         0x0105
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

int main() {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGKILL, ProgExit);
    signal(SIGINT, ProgExit);
    signal(SIGTERM, ProgExit);
    signal(SIGSEGV, errorCb);
    signal(SIGABRT, errorCb);

	usbg_config *c;
	usbg_function *f_uac2;
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

	struct usbg_config_strs c_strs = {
		.configuration = "1xUAC2"
	};

	struct usbg_f_uac2_attrs f_attrs = {
		.c_chmask = 3,
		.c_srate = 44100,
		.c_ssize = 4,
		.p_chmask = 3,
		.p_srate = 44100,
		.p_ssize = 4,
	};

	usbg_ret = (usbg_error)usbg_init("/sys/kernel/config", &s);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error on usbg init\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		return false;
	}

    // 检查并删除现有的 gadget
    usbg_gadget *existing_gadget = usbg_get_gadget(s, "g1");
    if (existing_gadget) {
		printf("rm exist gadget\n");
        usbg_rm_gadget(existing_gadget, USBG_RM_RECURSE);
    }

	usbg_ret = (usbg_error)usbg_create_gadget(s, "g1", &g_attrs, &g_strs, &g);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error creating gadget\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_cleanup(s);
        return false;
	}
	usbg_ret = (usbg_error)usbg_create_function(g, USBG_F_UAC2, "usb0", &f_attrs, &f_uac2);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error creating function\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_rm_gadget(g, USBG_RM_RECURSE);
	    usbg_cleanup(s);
        return false;
	}

	usbg_ret = (usbg_error)usbg_create_config(g, 1, "The only one", NULL, &c_strs, &c);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error creating config\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_rm_gadget(g, USBG_RM_RECURSE);
	    usbg_cleanup(s);
        return false;
	}

	usbg_ret = (usbg_error)usbg_add_config_function(c, "some_name", f_uac2);
	if (usbg_ret != USBG_SUCCESS) {
		fprintf(stderr, "Error adding function\n");
		fprintf(stderr, "Error: %s : %s\n", usbg_error_name(usbg_ret),
				usbg_strerror(usbg_ret));
		usbg_rm_gadget(g, USBG_RM_RECURSE);
	    usbg_cleanup(s);
        return false;
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
        return false;
	}

    printf("init usb success\n");

    while (1) {
        usleep(10000);
    }
    return 0;
}
