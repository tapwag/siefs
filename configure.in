AC_INIT(siefs/siefs.c)
AM_INIT_AUTOMAKE(siefs, 0.5)
AM_CONFIG_HEADER(config.h)

AC_PROG_CC
AC_PROG_RANLIB

if test -z "$LD"; then
	LD=ld
fi
AC_SUBST(LD)

CFLAGS="-Wall -W -g -O2"

AC_ARG_WITH(fuse,
	[  --with-fuse=DIR	specify location of fuse],
	[fuseinst="$withval"])

CPPFLAGS="$CPPFLAGS -I${fuseinst}/include"
LDFLAGS="$LDFLAGS -L${fuseinst}/lib"

AC_MSG_CHECKING([fuse installation])
if test -z "$fuseinst" ; then
	for d in /usr /usr/local /opt ; do
		if test -f $d/include/fuse/fuse.h ; then
			fuseinst=$d
			break
		fi
	done
fi

if ! test -f $fuseinst/include/fuse/fuse.h ; then
	AC_MSG_RESULT([Not found])
	AC_MSG_ERROR([
*** Please specify the location of the fuse with
*** the '--with-fuse=DIR' option.
*** You can download latest version of fuse
*** at http://sourceforge.net/projects/fuse
])
fi

if ! grep -q '\(\*release\)' $fuseinst/include/fuse/fuse.h ; then
	AC_MSG_RESULT([old])
	AC_MSG_ERROR([
*** You need fuse version 2.0 or later.
*** Please go to http://sourceforge.net/projects/fuse
*** and download the latest version
])
fi
AC_MSG_RESULT(${fuseinst})
AC_SUBST(fuseinst)

subdirs="siefs converter"
AC_SUBST(subdirs)

AC_DEFINE_UNQUOTED(FUSEINST, "${fuseinst}")
if test "x${prefix}" = "xNONE"; then
	AC_DEFINE_UNQUOTED(PREFIX, "${ac_default_prefix}")
else
	AC_DEFINE_UNQUOTED(PREFIX, "${prefix}")
fi

AC_OUTPUT([Makefile siefs/Makefile converter/Makefile])

