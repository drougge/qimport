#include "qimport.h"

int main(void)
{
	DNG dng;
	err1(dng_open_orig(&dng, "/r/cam/20131125/IMGP2573.DNG"));
	dng_extra(&dng);
	err1(dng.err);
	dng.put8 = dng_putc;
	dng.s = dng.data + dng.raw_pos;
	dng.emit = emit_count;
	compress_loop(&dng);
	build_huff(&dng);
	ljpeg_header(&dng);
	dng.s = dng.data + dng.raw_pos;
	dng.emit = emit_data;
	compress_loop(&dng);
	while (dng.out_bits) emit_bit(&dng, 0);
	ljpeg_tail(&dng);
	u8 *image_data;
	u32 image_size;
	dng.put8 = dng_put8_data;
	if (dng.outdata_pos > dng_readtag(&dng, 1, 279, 0, 0)) { // compression made it bigger
		image_data = dng.data + dng.raw_pos;
		image_size = dng.raw_size;
	} else {
		image_data = dng.outdata;
		image_size = dng.outdata_pos;
		dng_writetag(&dng, 1, 259, 3, 7); // ljpeg compression
		dng_writetag(&dng, 1, 279, 4, image_size); // raw size
	}
	dng_writetag(&dng, 2, 273, 4, dng.raw_pos + image_size + dng.extradata_pos); // jpeg pos
	err1(dng.err);
	u32 dest_size = dng.raw_pos + image_size + dng.extradata_pos + dng.jpeg_size;
	u8 *dest = malloc(dest_size);
	err1(!dest);
	memcpy(dest, dng.data, dng.raw_pos);
	memcpy(dest + dng.raw_pos, image_data, image_size);
	memcpy(dest + dng.raw_pos + image_size, dng.extradata, dng.extradata_pos);
	memcpy(dest + dng.raw_pos + image_size + dng.extradata_pos, dng.data + dng.jpeg_pos, dng.jpeg_size);
	dng_close(&dng);
	err1(dng_open_imported(&dng, dest, dest_size));
	dng_close(&dng);
	FILE *fh = fopen("out.dng", "wb");
	err1(fwrite(dest, dest_size, 1, fh) != 1);
	fclose(fh);
	return 0;
err:
	return 1;
}

