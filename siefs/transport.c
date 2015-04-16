/*
    siefs: a virtual filesystem for accessing Siemens mobiles
    Copyright (C) 2003, 2004  Dmitry Zakharov (dmitry-z@mail.ru)

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
*/

/* transport layer (bfb or clean obex) */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "comm.h"
#include "crcmodel.h"
#include "transport.h"

#define ACKSEQ "\x16\x02\x14\x01\xfe"
#define ACKLEN 5

//#define DBG(x...) fprintf(stderr, x); 
#define DBG(x...)

static const struct {
	int speed;
	char *string;
	int len;
} rates[] = {
	{ 19200,  "\x01\x09\x08\xc0" "19200"  "\xce\x4d\xcf", 12 },
	{ 38400,  "\x01\x09\x08\xc0" "38400"  "\xcc\x4b\xcf", 12 },
	{ 57600,  "\x01\x09\x08\xc0" "57600"  "\xca\x89\xcf", 12 },
	{ 115200, "\x01\x0a\x0b\xc0" "115200" "\x0d\xd2\x2b", 13 },
	{ 230000, "\x01\x0a\x0b\xc0" "230000" "\x0f\x90\x2b", 13 },
	{ 460000, "\x01\x0a\x0b\xc0" "460000" "\x4a\x90\x2b", 13 },
	{ 0, NULL, 0 }
};

int at_exec(hcomm *h, char *atcmd) {

	int n;
	char buf[256];

	comm_printf(h, "%s\r\n", atcmd);

	do {
		n = comm_getline(h, buf, 254);
		if (n < 0) return -1;
		if (n == 0) { errno = EIO; return -1; }

	} while (n < 4 || strncmp(buf, "OK\r\n", 4) != 0);

	return 0;
}

void bflush(tra_connection *b) {

	unsigned char tbuf[2];
	int n=0, t;

	t = comm_gettimeout(b->h);
	comm_settimeout(b->h, 2);
	while(comm_rx(b->h, tbuf, 1) == 1) n++;
	comm_settimeout(b->h, t);
	DBG("bflush(%i)\n", n);
}

int tra_ping(tra_connection *b, int cnt) {

	int i, n=0, t, l;
	hcomm *h = b->h;
	int tspeed = -1;
	unsigned char buf[16];

	static int tspeeds[] = { 57600, 115200, 230400, 38400, 19200 };

	DBG("bping... ");
	t = comm_gettimeout(h);
	comm_settimeout(h, 3);
	for (i=0; i<cnt; i++) {

		if (b->linktype == LINK_UNKNOWN || b->linktype == LINK_BFB) {		
			comm_tx(h, "\x02\x01\x03\x14", 4);
			n = comm_rx(h, buf, 5);
			if (n == 5 && memcmp(buf, "\x02\x02\x00\x14\xaa", 5) == 0) {
				DBG("BFB\n");
				b->linktype = LINK_BFB;
				if (tspeed != -1) b->speed = tspeed;
				comm_settimeout(h, t);
				return 0;
			}
		}

		if (b->linktype == LINK_UNKNOWN || b->linktype == LINK_QWE3) {
			comm_tx(h, "\xff\x00\x03", 3);
			n = comm_rx(h, buf, 3);
			if (n == 3 && buf[0] == 0xa0) {
				DBG("QWE3\n");
				l = (buf[1] >> 8) + buf[2] - 3;
				if (l > 0) comm_rx(h, buf, l);
				b->linktype = LINK_QWE3;
				if (tspeed != -1) b->speed = tspeed;
				comm_settimeout(h, t);
				return 0;
			}
		}


		if (n > 0) {
			while (comm_rx(h, buf, 1) == 1) n++;
			if (buf[0] == 0x16 && (buf[3] | 1) == 0x03) {
				comm_tx(h, "\x16\x02\x14\x01\xfe", 5);
			}
			DBG("(%i garbage) ", n);
		}

		if (i == 0) {
			DBG("restore... ");
			comm_restore(h);
			comm_settimeout(h, 3);
		}

		if (i >= 3) {
			comm_setspeed(h, tspeed = tspeeds[(i-3) % 6]);
			DBG("trying %i... ", tspeed);
		}
	}

	comm_settimeout(h, t);
	DBG("no answer\n");
	errno = EIO;
	return -1;
}


tra_connection *tra_open(char *device, int speed, int timeout) {

	hcomm *h;
	tra_connection *b;

	DBG("tra_open: %s, speed=%i\n... ", device, speed);
	h = comm_open(device);
	if (h == NULL) {
		DBG("failed\n");
		return NULL;
	}

	b = (tra_connection *) malloc(sizeof(tra_connection));
	b->startup = 1;
	b->h = h;
	b->speed0 = speed;
	b->timeout = timeout;
	b->seq = 0;
	b->iseq = 0xff;

	DBG("OK\n");
	return b;
}

int tra_test(tra_connection *b, int cnt) {

	int r;

	if (b->startup) {
		b->startup = 0;
		return -1;
	}

	r = tra_ping(b, cnt);
	return r;
}

int tra_initiate(tra_connection *b) {

	static int aspeeds[] = { 115200, 115200, 19200, 57600, 230400, 0 };
	static int bspeeds[] = { 57600, 57600, 115200, 230400, 0 };
	unsigned char buf[64];
	int i, cr;
	int v_speed, v_timeout;
	hcomm *h;

	h = b->h;

	DBG("tra_initiate... ");
	errno = EIO;
	comm_restore(h);
	v_speed = comm_getspeed(h);
	v_timeout = comm_gettimeout(h);

	comm_settimeout(h, 4);
	if (b->speed0 != 0) aspeeds[0] = b->speed0;
	for (i=0; aspeeds[i]>0; i++) {
		comm_setspeed(h, aspeeds[i]);
		if (at_exec(h, "at") == 0) break;
		if (at_exec(h, "at") == 0) break;
	}

	if (aspeeds[i] == 0) {
		DBG("no answer\n");
		comm_setspeed(h, v_speed);
		comm_settimeout(h, v_timeout);
		return -1;
	}
	DBG(" AT:%i ", aspeeds[i]);

	aspeeds[0] = aspeeds[i];
	at_exec(h, "at^sqwe=0");
	usleep(200000);
	if (at_exec(h, "at^sqwe=3") == 0) {
		b->linktype = LINK_QWE3;
		DBG("QWE3 ");
	} else {
		if (at_exec(h, "at^sbfb=1") != 0) {
			DBG("can't enter bfb mode\n");
			return -1;
		}
		b->linktype = LINK_BFB;
		DBG("BFB ");
	}

	if (b->linktype == LINK_BFB) {
		usleep(200000);
		for (i=0; bspeeds[i]>0; i++) {
			comm_setspeed(h, cr=bspeeds[i]);
			if (tra_ping(b, 2) == 0) break;
		}
		if (bspeeds[i] == 0) {
			DBG("no answer\n");
			return -1;
		}
	} else {
		// assuming that speed is equal to AT speed
		usleep(200000);
		cr = aspeeds[0];
	}
	DBG(" speed:%i ", cr);

	b->speed = (b->speed0 == 0) ? cr : b->speed0;

	if (b->linktype == LINK_BFB && b->speed != cr) {
		for (i=0; rates[i].speed != 0; i++) {
			if (rates[i].speed == b->speed) {
				comm_tx(h, rates[i].string, rates[i].len);
				if (comm_rx(h, buf, sizeof(buf)) == rates[i].len && buf[3] == 0xcc) {
					usleep(100000);
					comm_setspeed(h, b->speed);
					DBG(" bfb:%i ", b->speed);
				} else {
					b->speed = cr;
				}
				break;
			}
		}
		if (rates[i].speed == 0) b->speed = cr;
	}

	DBG("OK\n");
	b->startup = 0;
	b->seq = 0;
	b->iseq = 0xff;
	b->buffer = NULL;
	b->buflen = 0;
	comm_settimeout(h, b->timeout);
	return 0;

l_err:
	return -1;
}

void tra_close(tra_connection *b) {

	static const char BRESETCMD[] =
	{ 0x06, 0x0a, 0x0c, 'a', 't', '^', 's', 'b', 'f', 'b', '=', '0', 0x0d };
	static const char QRESETCMD[] =
	{ 0x81, 0x00, 0x03 };

	DBG("tra_close... ");
	bflush(b);

	switch (b->linktype) {

		case LINK_BFB:
			comm_tx(b->h, (char *)BRESETCMD, sizeof(BRESETCMD));
			bflush(b);
			break;

		case LINK_QWE3:
			comm_tx(b->h, (char *)QRESETCMD, sizeof(QRESETCMD));
			bflush(b);
			usleep(1000000);
			comm_tx(b->h, "+++", 3);
			bflush(b);
			break;

	}

	comm_close(b->h);
	free(b);
	DBG("OK\n");
}

void sendack(hcomm *h) {

	DBG(">ack\n");
	comm_tx(h, ACKSEQ, ACKLEN);
}

int waitack(hcomm *h) {

	unsigned char tbuf[6];

	if (comm_rx(h, tbuf, ACKLEN) < ACKLEN) {
		DBG("waitack: got no ack\n");
		return -1;
	}
	if (memcmp(tbuf, ACKSEQ, ACKLEN) != 0) {
		DBG("waitack: garbage\n");
		return -1;
	}
	DBG("<ack\n");
	return 0;

}

int tra_send(tra_connection *b, void *buf, int len) {

	unsigned char tbuf[4];
	unsigned char *ws, *p;
	unsigned short csum;
	int i, n, l;

	DBG("tra_send (%i bytes)...\n", len);

	if (b->linktype == LINK_QWE3) {
		if (comm_tx(b->h, buf, len) == len) {
			DBG("...OK\n");
			return len;
		} else {
			DBG("...failed\n");
			return -1;
		}
	}

	if (b->buflen < len+16) {
		free(b->buffer);
		b->buffer = malloc(b->buflen = len+32);
	}
	ws = b->buffer;
	ws[0] = (b->seq == 0) ? 0x02 : 0x03;
	ws[1] = ~ws[0];
	ws[2] = (b->seq)++;
	ws[3] = (unsigned char) (len >> 8);
	ws[4] = (unsigned char) (len & 0xff);
	memcpy(ws+5, buf, len);
	csum = crc16(ws+2, len+3);
	ws[5+len] = (unsigned char) (csum & 0xff);
	ws[5+len+1] = (unsigned char) (csum >> 8);

	for (i=0; i<3; i++) {

		if (i > 0) {
			DBG(" --- trying %i time...\n", i+1);
			bflush(b);
			sendack(b->h);
		}

		p = ws;
		n = len + 7;
		while (n > 0) {

			l = (n > 0x20) ? 0x20 : n;
			tbuf[0] = 0x16;
			tbuf[1] = l;
			tbuf[2] = 0x16 ^ l;
			DBG("tx%i ", l);
			if (comm_tx(b->h, tbuf, 3) < 3) break;
			if (comm_tx(b->h, p, l) < l) break;
			n -= l;
			p += l;

		}

		if (n == 0 && waitack(b->h) == 0) {
			DBG("...OK\n");
			return len;
		}
	}

	DBG("...failed\n");
	return -1;

}

int getblock(hcomm *h, void *buf) {

	unsigned char tbuf[4];
	int l;

	if (comm_rx(h, tbuf, 3) < 3) return -1;
	if (tbuf[0] != 0x16) return -1;
	l = tbuf[1];
	if (l < 1 || l > 0x20) return -1;
	if ((l ^ tbuf[0]) != tbuf[2]) return -1;

	l = comm_rx(h, buf, l);
	DBG("rx%i ", l);
	return l;

}

int tra_recv(tra_connection *b, void *buf, int size) {

	unsigned char tbuf[32];
	unsigned char *ws, *p;
	int len, len1, iseq, csum;
	int i, n, r;

	DBG("tra_recv...\n");
	if (b->linktype == LINK_QWE3) {
		p = buf;
		if (comm_rx(b->h, p, 3) != 3) return -1;
		len = len1 = (p[1] << 8) + p[2];
		DBG("len=%i\n", len);
		if (len > size) {
			DBG("too small buffer size (%i)\n", size);
			return -1;
		}

		p += 3;
		len -= 3;
		while (len > 0) {
			n = comm_rx(b->h, p, len);
			if (n <= 0) return -1;
			p += n;
			len -= n;
		}
		return len1;
	}


	r = -1;
	for (i=0; i<3; i++) {

		if (i > 0) {
			DBG(" --- trying %i time...\n", i+1);
			bflush(b);
		}

		if ((n = getblock(b->h, tbuf)) < 5) {
			if (tbuf[0] == 0x01) i--;
			continue;
		}
		if ((tbuf[0] | 1) != 0x03) continue;
		if ((tbuf[0] ^ tbuf[1]) != 0xff) continue;
		iseq = tbuf[2];
		if (iseq == b->iseq) {
			/* it's previous block, just reacknowledge it */
			DBG("reack prev\n");
			bflush(b);
			sendack(b->h);
			i = -1;
			continue;
		}

		len = len1 = (tbuf[3] << 8) + tbuf[4];
		DBG("len=%i\n", len);
		if (len > size) {
			DBG("too small buffer size\n");
			return -1;
		}
		len += 2;	/* add checksum bytes */

		if (b->buflen < len+16) {
			free(b->buffer);
			b->buffer = malloc(b->buflen = len+32);
		}
		p = ws = b->buffer;
		if (n >= 5) {
			memcpy(p, tbuf, n);
			p += n;
			len -= (n-5);
		}

		while (len > 0) {
			n = getblock(b->h, tbuf);
			if (n <= 0) break;
			memcpy(p, tbuf, n);
			p += n;
			len -= n;
		}

		if (len == 0) {
			csum = (p[-2] & 0xff) + (p[-1] << 8);
			if (csum == crc16(ws+2, len1+3)) {
				b->iseq = iseq;
				sendack(b->h);
				memcpy(buf, ws+5, len1);
				r = len1;
				break;
			} else {
				DBG("CRC error\n");
			}
		} else {
			DBG("block read error\n");
		}
	}

	return r;

}

unsigned short crc16(unsigned char *buf, int len) {

	int i;

	cm_t cm;
	p_cm_t p_cm = &cm;

	p_cm->cm_width = 16;
	p_cm->cm_poly = 0x1021L;
	p_cm->cm_init = 0xFFFFL;
	p_cm->cm_refin = TRUE;
	p_cm->cm_refot = TRUE;
	p_cm->cm_xorot = 0xFFFFL;

	cm_ini(p_cm);
	cm_blk(p_cm, buf, len);

	return (unsigned short) cm_crc(p_cm);
}


