</$objtype/mkfile

TARG=\
	uwnread\
	uwnwrite\

LIB=libpanel/libpanel.$O.a 
CFILES= \
	reader.c

#OFILES=${CFILES:%.c=%.$O} version.$O
HFILES=libpanel/panel.h libpanel/rtext.h
BIN=/$objtype/bin
PROGS=${TARG:%=$O.%}
</sys/src/cmd/mkmany

CFLAGS=-Dplan9 -Ilibpanel
version.c:	$CFILES
	date|sed 's/^... //;s/ +/-/g;s/.*/char version[]="&";/' >version.c

$LIB:V:
	cd libpanel
	mk

clean nuke:V:
	@{ cd libpanel; mk $target }
	rm -f *.[$OS] [$OS].out $TARG

all:V:	$PROGS

local:V: all
	cp 8.uwnread $home/bin/386/uwnread
	cp 8.uwnwrite $home/bin/386/uwnwrite
