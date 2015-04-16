/*
    siefs: a virtual filesystem for accessing Siemens mobiles
    Copyright (C) 2003  Dmitry Zakharov (dmitry-z@mail.ru)

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
*/

/* communication layer */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include "comm.h"

#define DEFSPEED 19200
#define DEFTIMEOUT 30
#define COMMFLAGS (PARODD | HUPCL | CS8 | CLOCAL | CREAD)

int commflags(int speed) {

	int i;
	const struct {
		int speed;
		int value;
	}
	r[] = { { 2400, B2400 }, { 9600, B9600 }, { 19200, B19200 }, 
	      { 38400, B38400 }, { 57600, B57600 }, { 115200, B115200 },
	      { 230400, B230400 }, { 0, 0 } };

	for (i=0; r[i].speed != speed; i++)
		if (r[i].speed == 0) { errno = EINVAL; return -1; }

	return r[i].value | COMMFLAGS;

}

hcomm *comm_open(char *device) {

	hcomm *h;
	int fd;
	struct termios tio;
	
	fd = open(device, O_RDWR | O_NOCTTY | O_EXCL);
	if (fd < 0) return NULL;

	h = (hcomm *)malloc(sizeof(hcomm));
	h->device = strdup(device);
	h->fd = fd;
	h->speed = DEFSPEED;
	h->timeout = DEFTIMEOUT;
	return h;

}

int comm_getspeed(hcomm *h) {

	return h->speed;

}

int comm_setspeed(hcomm *h, int speed) {

	struct termios tio;
	int i, f;
	int fd = h->fd;

	f = commflags(speed);
	if (f == -1) return -1;
	tcgetattr(fd, &tio);
	tio.c_cflag = f;
	if (tcsetattr(fd, TCSANOW, &tio) != 0)
		return -1;

	h->speed = speed;
	return 0;
}

int comm_gettimeout(hcomm *h) {

	return h->timeout;

}

int comm_settimeout(hcomm *h, int timeout) {

	struct termios tio;
	int fd = h->fd;

	tcgetattr(fd, &tio);
	tio.c_cc[VTIME]    = timeout;
	if (tcsetattr(fd, TCSANOW, &tio) != 0)
		return -1;

	h->timeout = timeout;
	return 0;
}

int comm_restore(hcomm *h) {

	struct termios tio;
	int fd = h->fd;
	int f;

	if (fd >= 0) close(fd);
	h->fd = -1;
	fd = open(h->device, O_RDWR | O_NOCTTY | O_EXCL);
	if (fd < 0) return -1;
	h->fd = fd;

	bzero(&tio, sizeof(tio));
	f = commflags(h->speed);
	if (f == -1) return -1;
	tio.c_cflag = f;
	tio.c_iflag = IGNPAR | IGNBRK;
	tio.c_oflag = 0;
	tio.c_lflag = 0;
	tio.c_cc[VTIME]  = h->timeout;
	tio.c_cc[VMIN] = 0;

	tcflush(fd, TCIOFLUSH);
	if (tcsetattr(fd, TCSANOW, &tio) != 0)
		return -1;

	return 0;

}
	
int comm_rx(hcomm *h, void *buf, int len) {

	int c, n=0;
	int fd = h->fd;

	while (n < len){
		c = read(fd, buf+n, len-n);
		if (c < 0) return -1;
		if (c == 0) break;
		n += c;
	}

	return n;
}

int comm_tx(hcomm *h, void *buf, int len) {

	int c, n=0;
	int fd = h->fd;
	
	while (n < len){
		c = write(fd, buf+n, len-n);
		if (c < 0) return -1;
		if (c == 0) break;
		n += c;
	}

	return n;
}

int comm_printf(hcomm *h, const char *fmt, ...) {

	char *buf;
	va_list ap;
	int n;

	buf = malloc(4096);
	va_start(ap, fmt);
	n = vsnprintf(buf, 4096, fmt, ap);
	va_end(ap);

	n = comm_tx(h, buf, n);
	free(buf);
	return n;
}	

int comm_getline(hcomm *h, char *buf, int size) {

	int c, n=0;

	do {
		c = comm_rx(h, buf+n, 1);
		if (c < 0) return -1;
		if (c == 0) break;
		n ++;
		if (buf[n-1] == '\n') break;
	} while (n < size);

	return n;
}
	
int comm_close(hcomm *h) {

	tcsendbreak(h->fd, 0);
	close(h->fd);
	free(h->device);
	free(h);
	return 0;

}

