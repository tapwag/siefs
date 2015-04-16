/*
    siefs: a virtual filesystem for accessing Siemens mobiles
    Copyright (C) 2003  Dmitry Zakharov (dmitry-z@mail.ru)

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
*/

#ifndef OBEX_H
#define OBEX_H

#include "transport.h"

#define OBEX_IDLE 0
#define OBEX_GET 1
#define OBEX_PUT 2

#define BLOCKSIZE 2048
//#define BLOCKSIZE 16384
#define MAXPACKETSIZE (BLOCKSIZE+6)

static const unsigned char sig_flex[] = {
	0x6b, 0x01, 0xcb, 0x31, 0x41, 0x06, 0x11, 0xd4,
	0x9a, 0x77, 0x00, 0x50, 0xda, 0x3f, 0x47, 0x1f
};

static const unsigned char sig_sync[] = {
	0x49, 0x52, 0x4d, 0x43, 0x2d, 0x53, 0x59, 0x4e, 0x43
};

typedef struct _obexpacket {

	int len;
	unsigned char *pos;
	unsigned char data[0];

} obexpacket;

typedef struct _obexdirentry {

	char name[256];
	int isdir;
	int size;
	long mtime;
	int mode;

} obexdirentry;

typedef struct _obexsession {

	tra_connection *b;
	int connected;
	int maxsize;
	int mode;
	obexpacket *pc, *pd;
	int len;
	unsigned char *pos;
	int eof;
	char *currentdir;
	int depth;
	unsigned char *dirlist;
	unsigned char *dirpos;
	obexdirentry direntry;
	char *filename;
	long offset;

} obexsession;


/*
 * Start a new OBEX session. Device is a communication port
 * (eg. "/dev/ttyS0"), speed - requested baudrate (0 means
 * default baudrate). This call opens a communication port
 * and returns pointer to obexsession structure on success.
 * On failure, NULL is returned. No communication is done
 * during this call, it succeeds even if device is not
 * connected.
 */
obexsession *obex_startup(char *device, int speed);


/*
 * Terminate an OBEX session, exit BFB mode and close 
 * communication port.
 */
void obex_shutdown(obexsession *os);


/*
 * Read a directory.
 * - call obex_readdir(), supplied with obex session handle
 *   and absolute path. This call returns 0 on successful
 *   operation, and -1 on error (for all functions, errno
 *   will be set appropriately if error occured)
 * - call obex_nextentry() multiple times. It returns pointer
 *   to obexdirentry structure, containing information about
 *   next file/directory. When no records is left, NULL will
 *   be returned.
 */
int obex_readdir(obexsession *os, char *dir);
obexdirentry *obex_nextentry(obexsession *os);


/*
 * GET and PUT operations.
 * - call obex_get()/obex_put() to start reading/writing
 *   a file. obex_get() returns file size on success (0 if
 *   size is not known), -1 on error. obex_put() returns
 *   0 on success, -1 on error. Reading can be started
 *   from any position, writing is sequential only.
 * - call obex_read()/obex_write() one or more times.
 *   They return number of successfully read/written bytes,
 *   or -1 if error occured.
 * - call obex_close() to complete operation. (This call
 *   is obigatory, don't forget it!)
 */
int obex_get(obexsession *os, char *name, long offset);
int obex_read(obexsession *os, void *buf, int size);

int obex_put(obexsession *os, char *name);
int obex_write(obexsession *os, void *buf, int size);

int obex_close(obexsession *os);


/*
 * Suspend/resume current GET or PUT session to perform quick
 * operation (readdir, stat etc.)
 */
int obex_suspend(obexsession *os);
int obex_resume(obexsession *os);


/*
 * Create a new directory. Returns 0 on success, -1 on error
 */
int obex_mkdir(obexsession *os, char *name);


/*
 * Get total capacity of device memory (obex_capacity()) and
 * free space (obex_available()) in bytes. These calls always
 * succeed - if no device connected, 0 is returned.
 */
int obex_capacity(obexsession *os);
int obex_available(obexsession *os);


/*
 * Rename/move a file or directory. src and dest are
 * absolute paths.
 */
int obex_move(obexsession *os, char *src, char *dest);


/*
 * Delete a file or directory. Returns 0 on successful deletion,
 * -1 on error. Non-empty directories cannot be deleted. 
 */
int obex_delete(obexsession *os, char *name);


/* 
 * Change a file/directory attributes. mode is standart UNIX
 * value. Only 4 bits are meaningful: -rw-rw----
 */ 
int obex_chmod(obexsession *os, char *name, unsigned int mode);

#endif
