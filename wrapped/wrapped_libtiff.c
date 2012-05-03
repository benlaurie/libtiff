#include <stdlib.h>
#include <string.h>
#include <tiffio.h>

#include "capsicum.h"

static TIFF *tiff;

static void WrappedTIFFOpen(const char *file, const char *mode) {
  fprintf(stderr, "TIFFOpen(%s, %s)\n", file, mode);
  tiff = TIFFOpen(file, mode);
  fprintf(stderr, "done\n");
}

static void WrappedTIFFPrintDirectory(int fd, long flags) {
  FILE *out = fdopen(fd, "w");
  TIFFPrintDirectory(tiff, out, flags);
  fclose(out);
}

static void WrappedTIFFClose() {
  TIFFClose(tiff);
}

#include "wrapped_libtiff_recv.c"

int main(int argc, char **argv) {
  int fd = atoi(argv[1]);

  dispatch(fd);

  return 0;
}
