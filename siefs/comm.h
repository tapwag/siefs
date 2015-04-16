/*
    siefs: a virtual filesystem for accessing Siemens mobiles
    Copyright (C) 2003  Dmitry Zakharov (dmitry-z@mail.ru)

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
*/

#ifndef COMM_H
#define COMM_H

typedef struct _hcomm {

	char *device;
	int fd;
	int speed;
	int timeout;

} hcomm;

hcomm *comm_open(char *device);
int comm_restore(hcomm *h);
int comm_setspeed(hcomm *h, int speed);
int comm_getspeed(hcomm *h);
int comm_settimeout(hcomm *h, int timeout);
int comm_gettimeout(hcomm *h);
int comm_rx(hcomm *h, void *buf, int len);
int comm_tx(hcomm *h, void *buf, int len);
int comm_printf(hcomm *h, const char *fmt, ...);
int comm_getline(hcomm *h, char *buf, int size);
int comm_close(hcomm *h);

#endif
