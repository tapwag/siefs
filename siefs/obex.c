/*
    siefs: a virtual filesystem for accessing Siemens mobiles
    Copyright (C) 2003, 2004  Dmitry Zakharov (dmitry-z@mail.ru)

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
*/

/* obex layer */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "transport.h"
#include "obex.h"

#define TIMEOUT 70

void set_errno(unsigned char obex_response) {

	int i;
	unsigned char r;

	static const unsigned char resp[] =
		{	0x40,	0x41,	0x43,	0x44, 
			0x45,	0x46,	0x48,	0x49,
			0x4d,	0x4f,	0x50,	0x51,
			0x53,	0x60,	0x61,	0
		};

	static const int err[] = 
		{	EINVAL,	EACCES,	EACCES,	ENOENT,
			EINVAL,	EPERM,	ETIMEDOUT, EINVAL,
			EFBIG,	EIO, EIO, ENOSYS,
			ENODEV,	ENOSPC,	EBUSY,	0
		};

	r = obex_response & 0x7F;
	if (r < 0x30) {
		/* this shouldn't happen */
		errno = EPROTO;
	} else {
		errno = EIO;
		for (i=0; resp[i]!=0; i++) {
			if (r == resp[i]) {
				errno = err[i];
				break;
			}
		}
	}

	return;
}

int str2uni(char *s, char *buffer) {

	char *p;
	unsigned char c;
	unsigned short h;

	p = buffer;
	while (*s) {
		c = (unsigned char) *(s++);
		if (c <= 0x7f) {
			h = c;
		} else if (c >= 0xc0 && c <= 0xdf && (*s & 0xc0) == 0x80) {
			h = ((c & 0x1f) << 6) | (*(s++) & 0x3f);
		} else if (c >= 0xe0 && c <= 0xef &&
		  (*s & 0xc0) == 0x80 && (*(s+1) & 0xc0) == 0x80) {
			h = ((c & 0x0f) << 12) | ((*(s++) & 0x3f) << 6);
			h |= (*(s++) & 0x3f);
		} else {
			h = '?';
		}

		*(p++) = (char)(h >> 8);
		*(p++) = (char)(h & 0xff);
	}

	return p-buffer;
}

void init_packet(obexpacket *p, unsigned char cmd) {

	p->data[0] = cmd;
	p->len = 3;

}

void *find_header(obexpacket *p, unsigned char h) {

	unsigned char *s;
	int l;

	l = (p->data[1] << 8) + p->data[2];
	s = p->data + 3;

	while (s - p->data < l) {
		if (*s == h)
			return s + 1;

		switch (*s & 0xc0) {
			case 0x00:
			case 0x40:
				s += (*(s+1) << 8) + *(s+2);
				break;
			case 0x80:
				s += 2;
				break;
			case 0xc0:
				s += 5;
				break;
		}
	}

	return NULL;
}

void append_byte(obexpacket *p, unsigned char v) {

	p->data[p->len++] = v;
}

void append_data(obexpacket *p, int header, unsigned char *data, int size) {

	unsigned char *s;

	s = p->data + p->len;
	*(s++) = header;
	*(s++) = (size+3) >> 8;
	*(s++) = (size+3) & 0xff;
	memcpy(s, data, size);
	p->len += (size+3);
}

void append_string(obexpacket *p, int header, char *str) {

	unsigned char *s;
	int l;

	l = strlen(str)+1;
	s = p->data + p->len;
	*(s++) = header;
	*(s++) = (l+3) >> 8;
	*(s++) = (l+3) & 0xff;
	memcpy(s, str, l);
	p->len += (l+3);
}

void append_unicode(obexpacket *p, int header, char *str) {

	unsigned char *s;
	int l;

	s = p->data + p->len;
	l = str2uni(str, s+3);
	*(s+3+l) = *(s+3+l+1) = '\0';
	*(s++) = header;
	*(s++) = (l+5) >> 8;
	*(s++) = (l+5) & 0xff;
	p->len += (l+5);
}

int send_packet(obexsession *os, obexpacket *p) {

	unsigned char *s;

	s = p->data;
	*(s+1) = p->len >> 8;
	*(s+2) = p->len & 0xff;

	if (tra_send(os->b, s, p->len) >= 0) {
		return 0;
	} else {
		abort_exchange(os);
		return -1;
	}
}

int recv_packet(obexsession *os, obexpacket *p) {

	int l;

	l = tra_recv(os->b, p->data, os->maxsize+16);
	if (l <= 0) {
		abort_exchange(os);
		return -1;
	}

	p->pos = p->data;
	p->len = l;
	set_errno(p->data[0]);
	return p->data[0];
}

char *getparm(char *str, char *parm) {

	char *s, *p1;
	int l;

	l = strlen(parm);
	p1 = strchr(str, '>');
	while (1) {
		s = strstr(str, parm);
		if (s == NULL) return NULL;
		if (! isalnum(*(s+l)))
			break;
	}

	if (p1 && s >= p1) return NULL;

	s += l;
	do {
		if (! *s) return NULL;

	} while (*(s++) != '\"');

	return s;
}

char *lastitem(char *name) {

	char *s;

	s = strrchr(name, '/');
	if (! s) s = strrchr(name, '\\');
	if (! s) return name;

	return ++s;
}

int cdtop(obexsession *os) {

	obexpacket *p = os->pc;

	init_packet(p, 0x85);
	append_byte(p, 0x02);
	append_byte(p, 0x00);
	append_byte(p, 0x01);
	append_byte(p, 0x00);
	append_byte(p, 0x03);
	if (send_packet(os, p) < 0)
		return -1;

	if (recv_packet(os, p) != 0xa0)
		return -1;

	return 0;
}

int cdup(obexsession *os) {

	obexpacket *p = os->pc;

	init_packet(p, 0x85);
	append_byte(p, 0x03);
	append_byte(p, 0x00);
	if (send_packet(os, p) < 0)
		return -1;

	if (recv_packet(os, p) != 0xa0)
		return -1;

	return 0;
}

int cddown(obexsession *os, char *name, int create_if_missing) {

	obexpacket *p = os->pc;

	init_packet(p, 0x85);
	append_byte(p, create_if_missing ? 0x00 : 0x02);
	append_byte(p, 0x00);
	append_unicode(p, 0x01, name);
	if (send_packet(os, p) < 0)
		return -1;

	if (recv_packet(os, p) != 0xa0)
		return -1;

	return 0;
}

int cdto(obexsession *os, char *name, int strip_last, int create_if_missing) {

	char *buf, *s, *s1, *s2, *ss, *se;
	int depth, cdepth, eqd, l, er;

	s = buf = malloc(strlen(name)+2);
	ss = name;
	depth = os->depth;
	cdepth = 0;

	while (*ss == '/' || *ss == '\\') ss++;

	while ( (se = strchr(ss, '/')) != NULL ||
		(se = strchr(ss, '\\')) != NULL) {

		l = se - ss;
		strncpy(s, ss, l);
		s += l;
		*(s++) = '\0';
		ss = se + 1;
		cdepth++;
	}

	if (*ss && ! strip_last) {
		strcpy(s, ss);
		cdepth++;
	}

	eqd=0;
	s1 = os->currentdir;
	s2 = buf;
	while (eqd < cdepth && eqd < depth) {

		if (strcasecmp(s1, s2) != 0)
			break;

		l = strlen(s1);
		s1 += (l+1);
		s2 += (l+1);
		eqd++;
	}

	s = s2;
	if (eqd < depth) {
		if (eqd <= depth/2) {
			if (cdtop(os) != 0) goto err_cd;
			depth = 0;
			s = buf;
		} else {
			while (depth > eqd) {
				if (cdup(os) != 0) goto err_cd;
				depth--;
			}
		}
	}

	while (depth < cdepth) {
		if (cddown(os, s, create_if_missing) != 0) goto err_cd;
		s += strlen(s)+1;
		depth++;
	}

	if (os->currentdir) free(os->currentdir);
	os->currentdir = buf;
	os->depth = cdepth;
	return 0;

err_cd:
	er = errno;
	if (os->currentdir) free(os->currentdir);
	cdtop(os);
	errno = er;
	os->currentdir = NULL;
	os->depth = 0;
	return -1;
}


obexsession *obex_startup(char *device, int speed) {

	tra_connection *b;
	obexsession *os;

	b = tra_open(device, speed, TIMEOUT);
	if (b == NULL) return NULL;

	os = (obexsession *) malloc(sizeof(obexsession));
	os->b = b;
	os->connected = 0;
	os->maxsize = MAXPACKETSIZE;
	os->pc = (obexpacket *) malloc(sizeof(obexpacket) + os->maxsize + 32);
	os->pd = (obexpacket *) malloc(sizeof(obexpacket) + os->maxsize + 32);
	os->dirlist = NULL;
	os->dirpos = NULL;
	os->depth = 0;
	os->currentdir = NULL;
	os->mode = OBEX_IDLE;
	os->filename = NULL;

	return os;
}

int handshake(obexsession *os) {

	obexpacket *p = os->pc;
	int n;

	os->connected = 0;

	if (tra_test(os->b, 3) == 0) {
		os->connected = 1;
		return 0;
	}

	if (tra_initiate(os->b) != 0) {
		if (tra_test(os->b, 20) == 0) {
			os->connected = 1;
			return 0;
		}
		return -1;
	}

	init_packet(p, 0x80);
	append_byte(p, 0x10);
	append_byte(p, 0x00);
	append_byte(p, os->maxsize >> 8);
	append_byte(p, os->maxsize & 0xFF);
	append_data(p, 0x46, (unsigned char *)sig_flex, sizeof(sig_flex));
	if (send_packet(os, p) < 0)
		return -1;

	if (recv_packet(os, p) != 0xa0) {
		return -1;
	}

	n = (p->data[5] << 8) + p->data[6];
	if (os->maxsize > n) os->maxsize = n;
	if (os->dirlist) {
		free(os->dirlist);
		os->dirlist = NULL;
	}
	os->depth = 0;
	if (os->currentdir) {
		free(os->currentdir);
		os->currentdir = NULL;
	}

	os->connected = 1;
	return 0;
}

void obex_shutdown(obexsession *os) {

	obexpacket *p = os->pc;

	if (os->connected) {
		init_packet(p, 0x81);
		append_byte(p, 0xcb);
		append_byte(p, 0x00);
		append_byte(p, 0x00);
		append_byte(p, 0x00);
		append_byte(p, 0x01);
		if (send_packet(os, p) >= 0)
			recv_packet(os, p);
	}

	tra_close(os->b);
	free(os->pc);
	free(os->pd);
	if (os->dirlist) free(os->dirlist);
	free(os);
}

int abort_exchange(obexsession *os) {

	unsigned char abuf[256];
	int l;

	abuf[0] = 0xff;
	abuf[1] = 0;
	abuf[2] = 0x03;

	if (tra_send(os->b, abuf, 3) < 0) return -1;
	l = tra_recv(os->b, abuf, sizeof(abuf));
	if (l <= 0) return -1;
	set_errno(abuf[0]);
	return (abuf[0] == 0xa0) ? 0 : -1;
}

int obex_readdir(obexsession *os, char *dir) {

	obexpacket *p = os->pc;
	int n, r, lsize;
	unsigned char *s;

	free(os->dirlist);
	os->dirlist = NULL;

	if (handshake(os) != 0)
		return -1;

	if (cdto(os, dir, 0, 0) < 0)
		return -1;

	init_packet(p, 0x83);
	append_string(p, 0x42, "x-obex/folder-listing");

	lsize = 0;

	do {
		if (send_packet(os, p) < 0)
			return -1;

		r = recv_packet(os, p);
		if (r == 0xa4) break;
		if (r != 0x90 && r != 0xa0) {
			return -1;
		}

		s = find_header(p, 0x48);
		if (s == NULL) s = find_header(p, 0x49);
		if (s != NULL) {
			n = (*s << 8) + *(s+1) - 3;
			os->dirlist = realloc(os->dirlist, lsize+n+1);
			memcpy(os->dirlist+lsize, s+2, n);
			lsize += n;
			*(os->dirlist+lsize) = '\0';
		}

		init_packet(p, 0x83);

	} while (r != 0xa0);

	os->dirpos = os->dirlist;
	return 0;
}

obexdirentry *obex_nextentry(obexsession *os) {

	struct tm ctm;
	char *s, *ss, *se;
	int isdir;
	int n;

	if (! os->dirlist)
		return NULL;

	s = os->dirpos;

	while (1) {
		if ((s = strchr(s, '<')) == NULL)
			return NULL;

		s++;
		if (strncasecmp(s, "file ", 5) == 0) {
			isdir = 0;
			break;
		}
		else if (strncasecmp(s, "folder ", 7) == 0) {
			isdir = 1;
			break;
		}
	}

	os->dirpos = s;
	os->direntry.isdir = isdir;
	os->direntry.mode = isdir ? 0040000 : 0100000;

	/* filename */
	ss = getparm(s, "name");
	if (ss == NULL) return NULL;
	se = strchr(ss, '\"');
	if (se == NULL) return NULL;
	*se = '\0';
	strncpy(os->direntry.name, ss, 255);
	*se = '\"';
	s = ++se;

	/* size */
	if (! isdir) {
		ss = getparm(s, "size");
		os->direntry.size = (ss == NULL) ? 0 : atoi(ss);
	} else {
		os->direntry.size = 0;
	}

	/* mtime */
	ss = getparm(s, "modified");
	os->direntry.mtime = 0;
	if (ss != NULL) {
		se = strchr(ss, '\"');
		if (se != NULL && se-ss == 15 && *(ss+8) == 'T') {
			n = atoi(ss);
			ctm.tm_mday = n % 100;
			ctm.tm_mon = (n % 10000) / 100 - 1;
			ctm.tm_year = n / 10000 - 1900;
			n = atoi(ss+9);
			ctm.tm_sec = n % 100;
			ctm.tm_min = (n % 10000) / 100;
			ctm.tm_hour = n / 10000;
			os->direntry.mtime = mktime(&ctm);
		}
	}

	/* mode */
	ss = getparm(s, "user-perm");
	if (ss != NULL) {
		while (*ss != '\"') {
			switch (*ss) {
				case 'R':
				case 'r':
					os->direntry.mode |= S_IRUSR;
					break;
				case 'W':
				case 'w':
					os->direntry.mode |= S_IWUSR;
			}
			ss++;
		}
	} else {
		os->direntry.mode |= (S_IRUSR | S_IWUSR);
	}

	ss = getparm(s, "group-perm");
	if (ss != NULL) {
		while (*ss != '\"') {
			switch (*ss) {
				case 'R':
				case 'r':
					os->direntry.mode |= (S_IRGRP|S_IROTH);
					break;
				case 'W':
				case 'w':
					os->direntry.mode |= (S_IWGRP|S_IWOTH);
			}
			ss++;
		}
	} else {
		os->direntry.mode |= (S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
	}
	
	return &(os->direntry);
}

void handle_data(obexsession *os, obexpacket *p) {

	unsigned char *s;
	int l;

	os->eof = (p->data[0] == 0x90) ? 0 : 1;

	os->pos = 0;
	os->len = 0;

	s = find_header(p, 0x48);
	if (s == NULL)
		s = find_header(p, 0x49);

	if (s != NULL) {
		l = (*s << 8) + *(s+1) - 3;
		os->len = l;
		if (l > 0) os->pos = s+2;
	}
}

int begin_get_request(obexsession *os) {

	obexpacket *p = os->pd;
	unsigned char *s;
	unsigned char tbuf[8];
	int r, i, offset, len;
	long pos, shift;

	if (handshake(os) != 0)
		return -1;

	if (cdto(os, os->filename, 1, 0) < 0)
		return -1;

	init_packet(p, 0x83);
	append_unicode(p, 0x01, lastitem(os->filename));
	offset = os->offset;
	shift = offset % BLOCKSIZE;
	pos = offset - shift;
	if (pos != 0) {
		tbuf[0] = 0x37;
		tbuf[1] = 0x04;
		for (i=5; i>1; i--) {
			tbuf[i] = (unsigned char) (pos & 0xff);
			pos >>= 8;
		}
		append_data(p, 0x4c, tbuf, 6);
	}
	if (send_packet(os, p) < 0)
		return -1;

	r = recv_packet(os, p);
	if (r != 0x90 && r != 0xa0)
		return -1;

	len = 0;
	s = find_header(p, 0xc3);
	if (s != NULL) {
		for (i=0; i<4; i++)
			len = (len << 8) + *(s++);
	}

	os->mode = OBEX_GET;
	handle_data(os, p);
	if (shift > os->len) shift = os->len;
	os->pos += shift;
	os->len -= shift;

	return len;
}

int obex_get(obexsession *os, char *name, long offset) {

	os->filename = strdup(name);
	os->offset = offset;
	return begin_get_request(os);
}

int obex_read(obexsession *os, void *buf, int size) {

	obexpacket *p = os->pd;
	unsigned char *ptr;
	int av, l, r;

	ptr = buf;
	av = size;
	while (av > 0) {
		l = (av < os->len) ? av : os->len;
		if (l > 0) memcpy(ptr, os->pos, l);
		ptr += l;
		av -= l;
		os->pos += l;
		os->len -= l;
		os->offset += l;

		if (os->len == 0) {
			if (os->eof) break;

			init_packet(p, 0x83);
			if (send_packet(os, p) < 0)
				return -1;

			r = recv_packet(os, p);
			if (r != 0x90 && r != 0xa0)
				return -1;

			handle_data(os, p);
		}
	}

	return size - av;
}

int begin_put_request(obexsession *os) {

	obexpacket *p = os->pd;
	unsigned char *s;
	int r, i, len;

	if (handshake(os) != 0)
		return -1;

	if (cdto(os, os->filename, 1, 0) < 0)
		return -1;

	init_packet(p, 0x02);
	append_unicode(p, 0x01, lastitem(os->filename));
	if (send_packet(os, p) < 0)
		return -1;

	r = recv_packet(os, p);
	if (r != 0x90)
		return -1;

	os->mode = OBEX_PUT;
	os->pos = p->data+6;
	os->len = 6;
	return 0;
}

int obex_put(obexsession *os, char *name) {

	os->filename = strdup(name);
	os->offset = 0;
	return begin_put_request(os);
}
	
int obex_write(obexsession *os, void *buf, int size) {

	obexpacket *p = os->pd;
	unsigned char *ptr;
	int l, n, r;

	ptr = buf;
	n = size;

	while (n > 0) {

		l = os->maxsize - os->len;
		if (l > n) l = n;
		memcpy(os->pos, ptr, l);
		os->pos += l;
		os->len += l;
		os->offset += l;
		ptr += l;
		n -= l;

		if (os->len == os->maxsize) {
			init_packet(p, 0x02);
			p->data[3] = 0x48;
			l = p->len = os->len;
			l -= 3;
			p->data[4] = (l >> 8);
			p->data[5] = (l & 0xff);

			if (send_packet(os, p) < 0)
				return -1;

			r = recv_packet(os, p);
			if (r != 0x90)
				return -1;

			os->pos = p->data+6;
			os->len = 6;
		}
	}

	return size - n;
}

int obex_suspend(obexsession *os) {

	return abort_exchange(os);
}

int obex_resume(obexsession *os) {

	switch (os->mode) {

		case OBEX_GET:
			return begin_get_request(os);

		case OBEX_PUT:
			return begin_put_request(os);

		default:
			return -1;
	}
}

int obex_close(obexsession *os) {

	obexpacket *p = os->pd;
	int l, r = 0;

	switch (os->mode) {

		case OBEX_GET:
			if (! os->eof) {
				abort_exchange(os);
			}
			break;

		case OBEX_PUT:
			init_packet(p, 0x82);
			p->data[3] = 0x49;
			l = p->len = os->len;
			l -= 3;
			p->data[4] = (l >> 8);
			p->data[5] = (l & 0xff);

			if (send_packet(os, p) < 0) {
				r = -1;
			} else if (recv_packet(os, p) != 0xa0) {
				r = -1;
			}
			break;
	}

	free(os->filename);
	os->filename = NULL;
	os->mode = OBEX_IDLE;
	return 0;
}

int obex_mkdir(obexsession *os, char *name) {

	if (handshake(os) != 0)
		return -1;

	return cdto(os, name, 0, 1);
}

int getinfo(obexsession *os, unsigned char req) {

	obexpacket *p = os->pc;
	unsigned char reqstr[3] = "\x32\x01";
	unsigned char *s;
	int n, l;

	if (handshake(os) != 0)
		return 0;

	init_packet(p, 0x83);
	reqstr[2] = req;
	append_data(p, 0x4c, reqstr, 3);
	if (send_packet(os, p) < 0)
		return 0;

	n = 0;
	if (recv_packet(os, p) == 0xa0) {
		s = find_header(p, 0x4c);
		if (s != NULL && *(s+2) == 0x32) {
			n = 0;
			l = *(s+3);
			for (s+=4; l>0; s++,l--) {
				n = (n << 8) + *s;
			}
		}
	}

	return n;
}

int obex_capacity(obexsession *os) {

	return getinfo(os, 0x01);
}

int obex_available(obexsession *os) {

	return getinfo(os, 0x02);
}

int obex_move(obexsession *os, char *src, char *dest) {

	unsigned char buf[540];
	obexpacket *p = os->pc;
	int l, n;

	if (handshake(os) != 0)
		return -1;

	init_packet(p, 0x82);
	n = 0;
	strcpy(buf+n, "\x34\x04move");
	n += 6;
	l = str2uni(src, buf+n+2);
	*(buf+n) = 0x35;
	*(buf+n+1) = l;
	n += (l + 2);
	l = str2uni(dest, buf+n+2);
	*(buf+n) = 0x36;
	*(buf+n+1) = l;
	n += (l + 2);
	append_data(p, 0x4c, buf, n);
	if (send_packet(os, p) < 0)
		return -1;

	if (recv_packet(os, p) != 0xa0)
		return -1;

	return 0;
}

int obex_delete(obexsession *os, char *name) {

	obexpacket *p = os->pc;

	if (handshake(os) != 0)
		return -1;

	if (cdto(os, name, 1, 0) < 0)
		return -1;

	init_packet(p, 0x82);
	append_unicode(p, 0x01, lastitem(name));
	if (send_packet(os, p) < 0)
		return -1;

	if (recv_packet(os, p) != 0xa0)
		return -1;

	return 0;
}

int obex_chmod(obexsession *os, char *name, unsigned int mode) {

	unsigned char *umode[4] = { "\"D\"", "\"WD\"", "\"RD\"", "\"RWD\"" };
	unsigned char *gmode[4] = { "\"\"", "\"W\"", "\"R\"", "\"RW\"" };
	unsigned char buf[16];
	obexpacket *p = os->pc;

	if (handshake(os) != 0)
		return -1;

	if (cdto(os, name, 1, 0) < 0)
		return -1;

	init_packet(p, 0x82);
	append_unicode(p, 0x01, lastitem(name));

	strcpy(buf+2, umode[(mode>>7) & 0x03]);
	strcat(buf+2, gmode[(mode>>4) & 0x03]);
	buf[0] = 0x38;
	buf[1] = strlen(buf+2);
	append_data(p, 0x4c, buf, buf[1]+2);
	if (send_packet(os, p) < 0)
		return -1;

	if (recv_packet(os, p) != 0xa0)
		return -1;

	return 0;
}

