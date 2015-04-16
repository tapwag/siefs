#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "vmconvert.h"

void out_w(int fd, void *buf, int len) {

	int n;

	while (len > 0) {
		n = write(fd, buf, len);
		if (n <= 0) {
			perror("write");
			exit(1);
		}
		buf += n;
		len -= n;
	}
	return;
}

void usage() {

	fprintf(stderr, "usage: vmo2wav [ -o wav_file ] vmo_file\n");
	exit(1);
}

int main(int argc, char **argv) {

	unsigned char frame[34];
	unsigned char buffer[320];
	char *infile, *outfile;
	int ifd, ofd;
	int l, n;

	if (argc < 2) usage();
	if (argv[1][0] == '-') {
		if (argv[1][1] != 'o' || argc < 4) usage();
		infile = argv[3];
		outfile = argv[2];
	} else {
		infile = argv[1];
		l = strlen(infile);
		outfile = malloc(l+5);
		strcpy(outfile, infile);
		if (strcasecmp(infile+l-4, ".vmo") == 0)
			*(outfile+l-4) = '\0';
		strcat(outfile, ".wav");
	}

	ifd = open(infile, O_RDONLY);
	if (ifd < 0) {
		perror("open");
		exit(1);
	}

	ofd = creat(outfile, 0666);
	if (ofd < 0) {
		perror("creat");
		exit(1);
	}

	l = lseek(ifd, 0, SEEK_END);
	lseek(ifd, 0, SEEK_SET);
	n = write_riff_header(buffer, l);
	out_w(ofd, buffer, n);

	if (vmo_start() != 0) {
		perror("vmo_start");
		exit(1);
	}

	while ((n = read(ifd, frame, 34)) == 34) {
		vmo_decode(frame, buffer);
		out_w(ofd, buffer, 320);
	}
	if (n < 0) {
		perror("read");
		exit(1);
	}

	vmo_end();
	return 0;
}

