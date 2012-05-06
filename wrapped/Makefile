all: .depend listtif wrapped_listtif wrapped_libtiff maketif wrapped_maketif \
     test

CFLAGS = -I ../libtiff -Wall -Werror

.depend: *.c *.h
	$(CC) $(CFLAGS) -MM *.c > .depend

listtif: listtif.o
	$(CC) -o listtif listtif.o ../libtiff/.libs/libtiff.a -lm -llzma -lz

wrapped_listtif: listtif.o libtiff_wrapper.o capsicum.o
	$(CC) -o wrapped_listtif listtif.o libtiff_wrapper.o capsicum.o

maketif: maketif.o
	$(CC) -o maketif maketif.o ../libtiff/.libs/libtiff.a -lm -llzma -lz

wrapped_maketif: maketif.o libtiff_wrapper.o capsicum.o
	$(CC) -o wrapped_maketif maketif.o libtiff_wrapper.o capsicum.o

wrapped_libtiff_send.c wrapped_libtiff_recv.c: wrapped_libtiff.cap wrap.py
	python wrap.py wrapped_libtiff

wrapped_libtiff: wrapped_libtiff.o capsicum.o
	$(CC) -o wrapped_libtiff wrapped_libtiff.o capsicum.o \
		../libtiff/.libs/libtiff.a -lm -llzma -lz

test:
	./maketif
	./listtif newtif.tif
	rm newtif.tif
	./wrapped_maketif
	./wrapped_listtif newtif.tif
