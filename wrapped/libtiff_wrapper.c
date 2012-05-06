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

static uint32 width;
static uint16 bitspersample;

int TIFFSetField(TIFF *tiff, uint32 tag, ...) {
  TIFFWrapper *wrapper = (TIFFWrapper *)tiff;
  va_list ap;

  // FIXME: implement TIFFGetField
  if (tag == TIFFTAG_IMAGEWIDTH) {
    va_list ap2;
    va_start(ap2, tag);
    width = va_arg(ap2, uint32);
  } else if (tag == TIFFTAG_BITSPERSAMPLE) {
    va_list ap2;
    va_start(ap2, tag);
    bitspersample = va_arg(ap2, int);
  }

  va_start(ap, tag);
  return WrappedTIFFVSetField(wrapper->fd, tag, ap);
}

int TIFFWriteScanline(TIFF *tiff, void *buf, uint32 row, uint16 sample) {
  TIFFWrapper *wrapper = (TIFFWrapper *)tiff;
  /* uint32 width; */
  /* uint16 bitspersample; */

  /* TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width); */
  /* TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &bitspersample); */
  return WrappedTIFFWriteScanline(wrapper->fd,
				  width * ((bitspersample + 7) / 8), buf, row,
				  sample);
}

void TIFFError(const char *module, const char *fmt, ...) {
  va_list ap;

  va_start(ap, fmt);
  // FIXME: pass this down?
  fprintf(stderr, "%s: ", module);
  vfprintf(stderr, fmt, ap);
}
