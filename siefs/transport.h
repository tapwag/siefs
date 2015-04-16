/*
    siefs: a virtual filesystem for accessing Siemens mobiles
    Copyright (C) 2003  Dmitry Zakharov (dmitry-z@mail.ru)

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
*/

#ifndef TRANSPORT_H
#define TRANSPORT_H

#include "comm.h"

#define LINK_UNKNOWN 0
#define LINK_BFB 1
#define LINK_QWE3 2

typedef struct tra_connection_s {

	hcomm *h;		/* file descriptor */
	int linktype;		/* bfb or qwe3 */
	int startup;		/* 1 = don't test connection */
	int timeout;
	int speed0;		/* requested baudrate */
	int speed;		/* current baudrate */
	unsigned char seq;	/* output sequence counter */
	unsigned char iseq;	/* input sequence counter */
	int buflen;
	unsigned char *buffer;	/* workspace */

} tra_connection;

unsigned short crc16(unsigned char *buf, int len);
tra_connection *tra_open(char *device, int speed, int timeout);
int tra_test(tra_connection *b, int cnt);
int tra_initiate(tra_connection *b);
int tra_send(tra_connection *b, void *buf, int len);
int tra_recv(tra_connection *b, void *buf, int size);
void tra_close(tra_connection *b);

#endif

