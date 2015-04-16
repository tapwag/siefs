/* vi: set ts=4 sw=4: */
/*
    siefs: a virtual filesystem for accessing Siemens mobiles
    Copyright (C) 2003  Dmitry Zakharov (dmitry-z@mail.ru)

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.
*/

#include <fuse/fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <sys/statfs.h>
#include <pthread.h>
#include "obex.h"

#include "config.h"

//#define DBG(x...) fprintf(stderr, x); 
#define DBG(x...)

#define SIEFS_IDLE 0
#define SIEFS_GET 1
#define SIEFS_PUT 2

#define MOUNTPROG			FUSEINST "/bin/fusermount"

static obexsession *g_os;
static char *comm_device;
static char *g_iocharset = "utf8";
static int g_baudrate;
static int g_uid, g_gid, g_umask;
static int g_hidetc;
static char *g_currentdir = NULL;
static char *g_currentfile = NULL;
static int g_operation = SIEFS_IDLE;
static int g_currentpos = 0;
static int g_dirsize = 0;
static obexdirentry *g_dirlist = NULL;
static time_t g_lastscan = 0;
static pthread_mutex_t smx = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t gmx = PTHREAD_MUTEX_INITIALIZER;

static struct stat dir_st, file_st;

static int start_session() {

	int i;

	for (i=0; i<10; i++) {
		if (pthread_mutex_trylock(&smx) == 0)
			return 0;
		usleep(100000);
	}

	return -1;
}

static void end_session() {
	pthread_mutex_unlock(&smx);
}

static void start_freq() {
	pthread_mutex_lock(&gmx);
	if (g_operation != SIEFS_IDLE) obex_suspend(g_os);
}

static void end_freq() {
	if (g_operation != SIEFS_IDLE) obex_resume(g_os);
	pthread_mutex_unlock(&gmx);
}

static void start_xfer() {
	pthread_mutex_lock(&gmx);
}

static void end_xfer() {
	pthread_mutex_unlock(&gmx);
}

#define STARTSESSION start_session()
#define ENDSESSION   end_session()
#define STARTFREQ    start_freq()
#define ENDFREQ      end_freq()
#define STARTXFER    start_xfer()
#define ENDXFER      end_xfer()

static void invalidate() {
	g_lastscan = 0;
}

static char *new_ascii2utf(char *s) {

	int size = strlen(s) * 3;
	char *r = malloc(size + 1);
	return ascii2utf(s, r, size);

}

static void refill(fuse_dirh_t h, fuse_dirfil_t filler) {

	int i, topdir;
	obexdirentry *de;
	char buf[256];

	topdir = (strcmp(g_currentdir, "/") == 0);
	if (filler) {
		for (i=0; i<g_dirsize; i++) {
			de = &g_dirlist[i];
			if (topdir && g_hidetc && strcasecmp(de->name, "telecom") == 0)
				continue;
			utf2ascii(de->name, buf, 255);
        	if (filler(h, buf, de->isdir ? 04 : 010, 0) != 0)
            	break;
		}
    }
}

static int getdir(const char *path, fuse_dirh_t h, fuse_dirfil_t filler) {

	time_t t;
	int d, n, allocd;
	obexdirentry *de;
	int res = 0;

	path = new_ascii2utf(path);

	d = (g_operation == SIEFS_IDLE) ? 2 : 5;
	t = time(NULL);
	if (g_currentdir && strcasecmp(path, g_currentdir) == 0 && t-g_lastscan < d)
	{
		refill(h, filler);
		free(path);
		return 0;
	}

	STARTFREQ;
	res = obex_readdir(g_os, (char *)path);
	if (res < 0) {
		ENDFREQ;
		free(path);
		return -errno;
	}

	free(g_currentdir);
	g_currentdir = strdup(path);
	free(g_dirlist);
	g_dirlist = NULL;
	g_dirsize = allocd = 0;
    while((de = obex_nextentry(g_os)) != NULL) {
		if (g_dirsize >= allocd) {
			allocd += 16;
			g_dirlist = (obexdirentry *) realloc(g_dirlist, allocd * sizeof(obexdirentry));
		}
		memcpy(&g_dirlist[g_dirsize++], de, sizeof(obexdirentry));
	}

	refill(h, filler);

	g_lastscan = time(NULL);
	ENDFREQ;
	free(path);
	return 0;
}

static int siefs_getdir(const char *path, fuse_dirh_t h, fuse_dirfil_t filler)
{
    int res;

	DBG("[getdir %s ..", path);
	path = new_ascii2utf(path);
	res = getdir(path, h, filler);
	free(path);
	DBG(" = %i]\n", res);

	return res;
}

static int siefs_getattr(const char *path, struct stat *stbuf)
{
	int i, l;
    int res = 0;
	char *s, *newdir, *item;

	path = new_ascii2utf(path);
	l = strlen(path);
	if (*path == '/' && *(path+1) == '\0') {

		/* root node is always a directory, isn't it? */
		*stbuf = dir_st;

	} else if (g_currentdir && strncasecmp(g_currentdir, path, l) == 0 &&
		(g_currentdir[l] == '\0' || g_currentdir[l] == '/'))
	{
		/* current and higher nodes are also a directories */
		*stbuf = dir_st;

	} else {

		newdir = strdup(path);
		s = strrchr(newdir, '/');
		*s = '\0';
		item = s+1;
		s = (s == newdir) ? "/" : newdir;

		res = getdir(s, 0, NULL);

		if (res == 0) {
			res = -ENOENT;
			for (i=0; i<g_dirsize; i++) {
				if (strcasecmp(item, g_dirlist[i].name) == 0) {
					res = 0;
					*stbuf = g_dirlist[i].isdir ? dir_st : file_st;
					stbuf->st_size = g_dirlist[i].size;
					stbuf->st_blocks = stbuf->st_size / 512;
					stbuf->st_atime = stbuf->st_mtime = stbuf->st_ctime =
						g_dirlist[i].mtime;
					break;
				}
			}
		}
		free(newdir);
	}
	free(path);

	return res;
}

static int siefs_mkdir(const char *path, mode_t mode)
{
	int res = 0;

	DBG("[mkdir %s ..", path);
	path = new_ascii2utf(path);
	STARTFREQ;
	if (obex_mkdir(g_os, (char *)path) < 0)
		res = -errno;
	invalidate();
	ENDFREQ;
	free(path);
	DBG(" = %i]\n", res);

    return res;
}

static int siefs_unlink(const char *path)
{
	int res = 0;

	DBG("[unlink %s ..", path);
	path = new_ascii2utf(path);
	STARTFREQ;
	if (obex_delete(g_os, (char *)path) < 0)
		res = -errno;
	invalidate();
	ENDFREQ;
	free(path);
	DBG(" = %i]\n", res);

    return res;
}

static int siefs_rmdir(const char *path)
{
    return siefs_unlink(path);
}

static int siefs_truncate(const char *path, off_t size)
{
	int res = 0;

	DBG("[truncate %s=%i ..", path, (int)size);
	path = new_ascii2utf(path);
	STARTFREQ;
	if (obex_delete(g_os, (char *)path) != 0) {
		res = -errno;
	} else if (obex_put(g_os, (char *)path) < 0) {
		res = -errno;
	} else {
		obex_close(g_os);
	}
	invalidate();
	ENDFREQ;
	free(path);
	DBG(" = %i]\n", res);

    return res;
}

static int siefs_rename(const char *from, const char *to)
{
	int res = 0;

	DBG("[rename %s->%s ..", from, to);
	from = new_ascii2utf(from);
	to = new_ascii2utf(to);
	STARTFREQ;
	if (obex_move(g_os, (char *)from, (char *)to) < 0)
		res = -errno;
	invalidate();
	ENDFREQ;
	free(from);
	free(to);
	DBG(" = %i]\n", res);

    return res;
}

static int siefs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res = 0;
	long t;

	t = mode & S_IFMT;
	if (t != 0 && t != 0100000)
    	return -EPERM;

	if (STARTSESSION != 0)
		return -EBUSY;

	DBG("[mknod %s(%08o)..", path, mode);
	path = new_ascii2utf(path);
	STARTXFER;
	if (obex_put(g_os, (char *)path) < 0) {
		res = -errno;
	} else {
		obex_close(g_os);
	}
	invalidate();
	ENDXFER;
	free(path);
	ENDSESSION;
	DBG(" = %i]\n", res);

	return res;
}

static int siefs_open(const char *path, struct fuse_file_info *finfo)
{
	int res = 0;

	DBG("[open %s,%04x ..", path, finfo->flags);
	path = new_ascii2utf(path);
	switch (finfo->flags & O_ACCMODE) {
		case O_RDONLY:
			if (STARTSESSION != 0) {
				res = -EBUSY;
				break;
			}
			STARTXFER;
			if (obex_get(g_os, (char *)path, 0) < 0) {
				res = -errno;
				ENDXFER;
				ENDSESSION;
				break;
			}
			free(g_currentfile);
			g_currentfile = strdup(path);
			g_operation = SIEFS_GET;
			g_currentpos = 0;
			ENDXFER;
			break;

		case O_WRONLY:
			if (STARTSESSION != 0) {
				res = -EBUSY;
				break;
			}
			STARTXFER;
			if (obex_put(g_os, (char *)path) < 0) {
				res = -errno;
				ENDXFER;
				ENDSESSION;
				break;
			}
			free(g_currentfile);
			g_currentfile = strdup(path);
			g_operation = SIEFS_PUT;
			g_currentpos = 0;
			ENDXFER;
			break;

		default:
			res = -EPERM;
			break;
	}
	free(path);
	DBG("]\n");

	return res;
}

static int siefs_close(const char *path, struct fuse_file_info *finfo) 
{
	DBG("[close %s ..", path);
	path = new_ascii2utf(path);
	if (g_operation != SIEFS_IDLE && strcasecmp(path, g_currentfile) == 0) {
		STARTXFER;
		obex_close(g_os);
		free(g_currentfile);
		g_currentfile = NULL;
		g_operation = SIEFS_IDLE;
		invalidate();
		ENDXFER;
		ENDSESSION;
	}
	free(path);
	DBG("]\n");

    return 0;
}

static int siefs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *finfo)
{
	int n, position;
	int r=0;

	DBG("[read %s,%i ..", path, size);
	path = new_ascii2utf(path);

	if (g_operation != SIEFS_GET || strcasecmp(path, g_currentfile) != 0) {
		free(path);
    	return -EBADF;
	}

	STARTXFER;
	if (offset != g_currentpos) {
		obex_close(g_os);
		if (obex_get(g_os, (char *)path, offset) < 0) {
			ENDXFER;
			free(path);
			return -errno;
		}
		g_currentpos = offset;
	}

	n = obex_read(g_os, buf, size);
	if (n < 0) {
		r = -errno;
	} else {
		g_currentpos += n;
	}
	ENDXFER;
	free(path);
	DBG(" = %i]\n", n);

	return n;
}

static int siefs_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *finfo)
{
	int n;

	DBG("[write %s,%i ..", path, size);
	path = new_ascii2utf(path);

	if (g_operation != SIEFS_PUT || strcasecmp(path, g_currentfile) != 0) {
		free(path);
    	return -EBADF;
	}

	if (offset != g_currentpos) {
		free(path);
		return -ESPIPE;
	}

	STARTXFER;
	n = obex_write(g_os, (char *)buf, size);
	if (n < 0) {
		n = -errno;
	} else {
		g_currentpos += n;
	}
	ENDXFER;
	free(path);
	DBG(" = %i]\n", n);

	return n;
	
}

static int siefs_statfs(const char *buf, struct statfs *fst)
{
	int n;

	DBG("[statfs ..");

	STARTFREQ;
	bzero(fst, sizeof(struct statfs));
	n = obex_capacity(g_os);
	if (n > 0) {
		fst->f_bsize = 512;
		fst->f_blocks = n / 512;
		fst->f_bfree = obex_available(g_os) / 512;
		fst->f_namelen = 255;
	}
	ENDFREQ;
	DBG("]\n");

    return 0;
}

static int siefs_chmod(const char *path, mode_t mode)
{
    return 0;
}

static int siefs_chown(const char *path, uid_t uid, gid_t gid)
{
    return 0;
}

static int siefs_utime(const char *path, struct utimbuf *buf)
{
    return 0;
}

static int siefs_readlink(const char *path, char *buf, size_t size)
{
    return -EPERM;
}

static int siefs_link(const char *from, const char *to)
{
    return -EPERM;
}

static int siefs_symlink(const char *from, const char *to)
{
    return -EPERM;
}

static struct fuse_operations siefs_oper = {
    getattr:	siefs_getattr,
    readlink:	siefs_readlink,
    getdir:     siefs_getdir,
    mknod:		siefs_mknod,
    mkdir:		siefs_mkdir,
    symlink:	siefs_symlink,
    unlink:		siefs_unlink,
    rmdir:		siefs_rmdir,
    rename:     siefs_rename,
    link:		siefs_link,
    chmod:		siefs_chmod,
    chown:		siefs_chown,
    truncate:	siefs_truncate,
    utime:		siefs_utime,
    open:		siefs_open,
    read:		siefs_read,
    write:		siefs_write,
    statfs:		siefs_statfs,
    release:	siefs_close,
};

void usage() {

	fprintf(stderr, "Usage: mount -t siefs [-o options] comm_device mountpoint\n\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "\tuid=<value>\t\towner id\n");
	fprintf(stderr, "\tgid=<value>\t\tgroup id\n");
	fprintf(stderr, "\tumask=<value>\t\tumask value (octal)\n");
	fprintf(stderr, "\tbaudrate=<value>\t\tcommunication speed\n");
	fprintf(stderr, "\tdevice=<device>\t\tcommunication device (for use in fstab)\n");
	fprintf(stderr, "\tnohide\t\t\tdon't hide `telecom' directory\n");
	exit(1);
}

void cleanup() {
	obex_shutdown(g_os);
}

void parse_options(char *p)
{
	while (p && *p) {
		if (strncmp(p, "baudrate=", 9) == 0) {
			g_baudrate = atoi(p+9);
		} else if (strncmp(p, "uid=", 4) == 0) {
			g_uid = atoi(p+4);
		} else if (strncmp(p, "gid=", 4) == 0) {
			g_gid = atoi(p+4);
		} else if (strncmp(p, "umask=", 6) == 0) {
			g_umask = strtol(p+6, NULL, 8);
		} else if (strncmp(p, "iocharset=", 10) == 0) {
			g_iocharset = strdup(p+10);
			*(g_iocharset + strcspn(g_iocharset, ",")) = '\0';
		} else if (strncmp(p, "nohide", 6) == 0) {
			g_hidetc = 0;
		} else if (strncmp(p, "device=", 7) == 0) {
			comm_device = strdup(p+7);
			*(comm_device + strcspn(comm_device, ",")) = '\0';
		}
		p = strchr(p, ',');
		if (p) p++;
	}

}

int main(int argc, char *argv[])
{
	char *mountprog = MOUNTPROG;
	char **fuse_argv = (char **) malloc(3 + argc + 1);
	char *p, *pp, *env_path;
	int i, j, path_size;
	pid_t pid;
	char default_comm[] = "/dev/mobile";
	char *mntpoint;

	p = strrchr(argv[0], '/');
	p = (p == NULL) ? argv[0] : p+1;
	if (strncmp(p, "mount", 5) == 0) {

		/* original call, "mount.siefs /dev/ttyS0 /mnt/mobile -o ..." */
		if (argc < 3)
			usage();

		comm_device = argv[1];
		mntpoint = argv[2];
		if ((argc>3) && (strncmp(argv[3],"-o", 2) == 0))
			parse_options(argv[4]);
	}
	else {

		/* recall from fusermount, "mount.siefs /dev/ttyS0 ..." */
		if ((argc != 2) && (argc < 4))
			usage();

		comm_device = argv[1];
		g_baudrate = -1;
		g_uid = getuid();
		g_gid = getgid();
		g_umask = umask(0);
		g_hidetc = 1;
		umask(g_umask);

		if (argc == 2) {
			comm_device = default_comm;
			mntpoint = argv[1];
		}
		else if (argc >= 4) {
			if ( strncmp(argv[1], "-o", 2) == 0) {
				parse_options(argv[2]);
				mntpoint = argv[3];
			}
		}
	}

	bzero(&dir_st, sizeof(dir_st));
	bzero(&file_st, sizeof(file_st));
	dir_st.st_nlink = file_st.st_nlink = 1;
	dir_st.st_blksize = file_st.st_blksize = 512;
	dir_st.st_mode = 0040777 & ~g_umask;
	file_st.st_mode = 0100666 & ~g_umask;
	dir_st.st_uid = file_st.st_uid = g_uid;
	dir_st.st_gid = file_st.st_gid = g_gid;

	if (! init_charset(g_iocharset)) {
		fprintf(stderr, "siefs: unknown charset %s\n", g_iocharset);
		exit(1);
	}

	if (g_baudrate == -1) g_baudrate = 115200;
	g_os = obex_startup(comm_device, g_baudrate);
	if (g_os == NULL) {
		perror("siefs: cannot open communication port");
		exit(1);
	}

	pid = fork();
	if (pid < 0) {

		perror("fork");
		exit(1);

	} else if (pid != 0) {

		/* parent process */
		usleep(200000);
		return 0;

	}

	/* child process */
	setsid();

	atexit(cleanup);

	env_path = getenv("PATH");
	path_size = env_path ? strlen(env_path) : 0;
	p = malloc(path_size + strlen(FUSEINST) + 7);
	sprintf(p, "%s:%s/bin", env_path ? env_path : "", FUSEINST);
	setenv("PATH", p, 1);
	free(p);

	fuse_argv[0] = argv[0];
	fuse_argv[1] = mntpoint;
	fuse_argv[2] = NULL;
	fuse_main(2, fuse_argv, &siefs_oper);

	return 0;

}

