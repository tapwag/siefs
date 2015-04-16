## Process this file with automake to produce Makefile.in

CFLAGS = -I$(fuseinst)/include -DFUSEINST="\"$(fuseinst)\"" -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=22

bin_PROGRAMS = siefs slink

siefs_SOURCES = siefs.c obex.c obex.h transport.c transport.h comm.c comm.h \
	crcmodel.c crcmodel.h charset.c charset.h
slink_SOURCES = slink.c obex.c obex.h transport.c transport.h comm.c comm.h \
	crcmodel.c crcmodel.h

LDADD = -lfuse -lpthread

install-exec-hook:
	-rm -f /sbin/mount.siefs
	-ln -s $(DESTDIR)$(bindir)/siefs /sbin/mount.siefs
