#include <stdlib.h>
#include <string.h>

#include "vmconvert.h"
#include "gsm.h"

static const struct riff_header {
	char szriff[4];
	long filesize;
	char szwave[4];
	char szfmt[4];
	long fmthsize;
	short format;
	short channels;
	long rate;
	long bsec;
	short align;
	short bits;
	char szdata[4];
	long datasize;
} rh_init = {
	"RIFF",
	0,
	"WAVE",
	"fmt ",
	16,		/* length of header */
	1,		/* PCM */
	1,		/* mono */
	8000,		/* 8 kHz */
	16000,
	2,		/* bytes/sample */
	16,		/* bits/sample */
	"data",
	0
};
	
static gsm r;

int write_riff_header(void *buffer, int vmo_file_size) {

	int wav_data_size;
	struct riff_header *rh;

	wav_data_size = (vmo_file_size / 34) * 320;

	rh = (struct riff_header *)buffer;
	*rh = rh_init;
	rh->filesize = sizeof(struct riff_header) + wav_data_size - 8;
	rh->datasize = wav_data_size;

	return sizeof(struct riff_header);
}


int vmo_start() {

	if (!(r = gsm_create())) {
		perror("gsm_create");
		return -1;
	}
	(void)gsm_option(r, GSM_OPT_FAST, 0);
	(void)gsm_option(r, GSM_OPT_VERBOSE, 0);

	return 0;
}

void vmo_decode(void *vmo_frame, void *buffer) {

	gsm_frame s;
	gsm_byte *p;
	int i;

	p = (gsm_byte *)vmo_frame;
	for (i=0; i<32; i+=2) {
		s[i] = *p;
		s[i+1] = *(p+3);
		p += 2;
	}
	s[i] = *p;
	s[0] &= 0x0f;
	s[0] |= 0xd0;

	if (gsm_decode(r, s, (gsm_signal *)buffer)) {
		/* invalid frame */
		bzero(buffer, 160 * sizeof(gsm_signal));
	}
	
	return;
}

void vmo_end() {

	gsm_destroy(r);

}

