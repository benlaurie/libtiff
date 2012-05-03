#include <stdlib.h>
#include <tiffio.h>

#include "capsicum.h"

#include "wrapped_libtiff_send.c"

typedef struct {
  int fd;
} TIFFWrapper;

TIFF *TIFFOpen(const char *file, const char *mode) {
  TIFFWrapper *wrapper;
  int fd = lc_fork("wrapped_libtiff");
  if (fd < 0)
    return NULL;

  WrappedTIFFOpen(fd, file, mode);

  wrapper = malloc(sizeof *wrapper);
  wrapper->fd = fd;
  return (TIFF *)wrapper;
}

void TIFFPrintDirectory(TIFF *tiff, FILE *out, long flags) {
  TIFFWrapper *wrapper = (TIFFWrapper *)tiff;
  
  fflush(out);
  WrappedTIFFPrintDirectory(wrapper->fd, fileno(out), flags);
}

void TIFFClose(TIFF *tiff) {
  TIFFWrapper *wrapper = (TIFFWrapper *)tiff;

  WrappedTIFFClose(wrapper->fd);
  lc_stop_child(wrapper->fd);
  free(wrapper);
}

