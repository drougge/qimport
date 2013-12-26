#include "qimport.h"

static int read_file(const char *fn, u8 **r_buf, u32 *r_size)
{
	u8 *res = NULL;
	int fd = open(fn, O_RDONLY);
	const char *msg = "Failed to open \"%s\"";
	err1(fd < 0);
	struct stat sb;
	msg = "Failed to stat \"%s\"";
	err1(fstat(fd, &sb));
	errno = 0;
	msg = "\"%s\" is not a file";
	err1(!S_ISREG(sb.st_mode));
	msg = "\"%s\" is too big";
	err1((u64)sb.st_size != (u32)sb.st_size);
	*r_size = (u32)sb.st_size;
	res = malloc(*r_size);
	msg = "malloc";
	err1(!res);
	msg = "Failed to read \"%s\"";
	err1(read(fd, res, *r_size) != sb.st_size);
	close(fd);
	*r_buf = res;
	return 0;
err:
	{ // because C
	char buf[strlen(fn) + strlen(msg)];
	snprintf(buf, sizeof(buf), msg, fn);
	perror(buf);
	if (res) free(res);
	if (fd >= 0) close(fd);
	return 1;
	}
}

int main(int argc, char **argv)
{
	DNG dng;
	u8 *filedata = NULL;
	u32 filedata_size;
	err1(argc != 3);
	err1(read_file(argv[1], &filedata, &filedata_size));
	err1(dng_open_orig(&dng, filedata, filedata_size));
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
	const u8 *image_data;
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
	FILE *fh = fopen(argv[2], "wbx");
	err1(fwrite(dest, dest_size, 1, fh) != 1);
	fclose(fh);
	return 0;
err:
	return 1;
}

