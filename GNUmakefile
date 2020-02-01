-include ../GNUmakefile.inc

SRCS=		luatemplate.c reader.c
LIB=		template

LUAVER?=	$(shell lua -v 2>&1 | cut -c 5-7)
LUAINC?=	/usr/include/lua${LUAVER}

CFLAGS+=	-Wall -O3 -fPIC -I/usr/include -I${LUAINC}
LDADD+=		-L${XDIR}/lib -L${PKGDIR}/lib -lbsd

PKGDIR=		/usr

LIBDIR=		/usr/lib/lua/${LUAVER}

${LIB}.so:	${SRCS:.c=.o}
		cc -shared -o ${LIB}.so ${CFLAGS} ${SRCS:.c=.o} ${LDADD}

clean:
		rm -f *.o *.so
install:
	-mkdir -p ${DESTDIR}${LIBDIR}
	install -m 755 ${LIB}.so ${DESTDIR}${LIBDIR}
