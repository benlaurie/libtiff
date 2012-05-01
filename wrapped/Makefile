all: listtif

CFLAGS = -I ../libtiff -Wall -Werror

listtif: listtif.o
	 $(CC) -o listtif listtif.o ../libtiff/.libs/libtiff.a -lm -llzma -lz
