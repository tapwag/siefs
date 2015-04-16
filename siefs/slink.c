/*
    siefs: a virtual filesystem for accessing Siemens mobiles
    Copyright (C) 2003  Dmitry Zakharov (dmitry-z@mail.ru)

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
*/

/* slink.c - a testing application */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>

#include "obex.h"

obexsession *os = NULL;

void cleanup() {
	if (os) obex_shutdown(os);
}


int main(int argc, char **argv) {

	int i, n, h, r;
	long m;
	char buf[4096];
	char *s, *device;
	char mode[12] = "----------";
	obexdirentry *e;

	if (argc == 2 && argv[1][0] == 'i') argc++;
	if (argc<3) {
		fprintf(stderr, "Usage: %s <command> [parameters]\n\n"
			"Commands:\n"
			"\tl <path>\t\t\tdirectory listing\n"
			"\tg <remotepath> <localpath>\tget file\n"
			"\tp <localpath> <remotepath>\tput file\n"
			"\tc <path>\t\t\tcreate directory\n"
			"\tm <src> <dest>\t\t\trename/move file or directory\n"
			"\td <path>\t\t\tdelete file\n"
			"\ti\t\t\t\tdisk information\n"
			"\n"
			"Environment:\n"
			"\tSLINK_DEVICE\tdevice file for communication (default is /dev/ttyS0)\n"
			"\tSLINK_SPEED\tbaudrate (default is 57600)\n"
			, argv[0]);
		exit(1);
	}

	atexit(cleanup);
	device = getenv("SLINK_DEVICE");
	if (device == NULL) device = "/dev/ttyS0";
	s = getenv("SLINK_SPEED");
	if (s == NULL) s = "0";
	os = obex_startup(device, atoi(s));
	if (! os) { perror("obex_startup"); exit(1); }

	switch(argv[1][0]) {

		case 'l':
			if (obex_readdir(os, argv[2]) < 0) {
				perror("obex_readdir");
				exit(1);
			}
			while ((e = obex_nextentry(os)) != NULL) {
				m = e->mode;
				mode[0] = e->isdir ? 'd' : '-';
				mode[1] = (m & 0400) ? 'r' : '-';
				mode[2] = (m & 0200) ? 'w' : '-';
				mode[4] = (m & 0040) ? 'r' : '-';
				mode[5] = (m & 0020) ? 'w' : '-';
				mode[7] = (m & 0004) ? 'r' : '-';
				mode[8] = (m & 0002) ? 'w' : '-';
				s = ctime(&(e->mtime));
				printf("%.10s %9i %.6s %.4s %.5s %s\n", 
					mode,
					e->size,
					s+4, s+20, s+11,
					e->name);
			}
			break;

		case 'g':
			if (argc < 4) {
				fprintf(stderr, "too few parameters\n");
				exit(1);
			}
			if (strcmp(argv[3], "-") == 0) {
				h = 1;
			} else {
				h = creat(argv[3], 0644);
				if (h < 0) {
					perror("creat");
					exit(1);
				}
			}
			if (obex_get(os, argv[2], 0) < 0) {
				perror("obex_get");
				exit(1);
			}
			while((n = obex_read(os, buf, 4096)) > 0) {
				write(h, buf, n);
			}
			if (n < 0) {
				perror("obex_read");
				exit(1);
			}
			close(h);
			obex_close(os);
			break;

		case 'p':
			if (argc < 4) {
				fprintf(stderr, "too few parameters\n");
				exit(1);
			}
			if (strcmp(argv[2], "-") == 0) {
				h = 0;
			} else {
				h = open(argv[2], O_RDONLY);
				if (h < 0) {
					perror("creat");
					exit(1);
				}
			}
			if (obex_put(os, argv[3]) < 0) {
				perror("obex_put");
				exit(1);
			}
			while((n = read(h, buf, 4096)) > 0) {
				r = obex_write(os, buf, n);
				if (r < 0) {
					perror("obex_write");
					exit(1);
				}
			}
			close(h);
			obex_close(os);
			break;

		case 'm':
			if (argc < 4) {
				fprintf(stderr, "too few parameters\n");
				exit(1);
			}
			if (obex_move(os, argv[2], argv[3]) < 0) {
				perror("obex_move");
				exit(1);
			}
			break;

		case 'd':
			if (obex_delete(os, argv[2]) < 0) {
				perror("obex_delete");
				exit(1);
			}
			break;

		case 'c':
			if (obex_mkdir(os, argv[2]) < 0) {
				perror("obex_mkdir");
				exit(1);
			}
			break;

		case 'i':
			n = obex_capacity(os);
			if (n != 0) {
				printf("Capacity:  %7i Kbytes\n", n/1024);
				n = obex_available(os);
				printf("Available: %7i Kbytes\n", n/1024);
			}
			break;
	}
	exit(0);
}


