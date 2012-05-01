/*
 * listtif.c -- lists a tiff file.
 */

#include <stdlib.h>
#include <tiffio.h>

int main(int argc,char *argv[])
{
	char *fname="newtif.tif";
	int flags;

	TIFF *tif=(TIFF*)0;  /* TIFF-level descriptor */

	if (argc>1) fname=argv[1];
	
	tif=TIFFOpen(fname,"r");
	if (!tif) goto failure;
	
	/* We want the double array listed */
	//flags = TIFFPRINT_MYMULTIDOUBLES;
	flags = 0;
	
	TIFFPrintDirectory(tif,stdout,flags);
	TIFFClose(tif);
	exit (0);
	
failure:
	printf("failure in listtif\n");
	if (tif) TIFFClose(tif);
	exit (-1);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 8
 * fill-column: 78
 * End:
 */
