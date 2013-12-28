#include "qimport.h"

#include <libgen.h>
#include <dirent.h>

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

static const char *progname;
static void usage(FILE *fh)
{
	fprintf(fh, "Usage: %s [--reverse] source[s] destination\n", progname);
	fprintf(fh, "Source can be one or several directories or files.\n");
	fprintf(fh, "Destination can be a filename if source is a single file,\n");
	fprintf(fh, "otherwise it must be a directory.\n\n");
	fprintf(fh, "Unless --reverse is specified, files should be unmodified DNGs\n");
	fprintf(fh, "from a Pentax Q or similar camera.\n\n");
	fprintf(fh, "With --reverse, the files should be written by %s,\n", progname);
	fprintf(fh, "and the original files will be produced.\n");
	exit(fh == stderr);
}

static int is_dir(const char *pathname)
{
	struct stat sb;
	if (stat(pathname, &sb)) {
		if (errno == ENOENT) return 0;
		perror("stat");
		exit(1);
	}
	if (S_ISDIR(sb.st_mode)) return 1;
	if (S_ISREG(sb.st_mode)) return 0;
	fprintf(stderr, "%s is neither a file nor a directory\n", pathname);
	exit(1);
	return 0;
}

static const char *destname;
static int dest_is_dir;

static int save(char *filename, const u8 *data, const u32 data_size)
{
	FILE *fh;
	if (dest_is_dir) {
		const char *fn_part = basename(filename);
		char fn_buf[strlen(destname) + strlen(fn_part) + 2];
		snprintf(fn_buf, sizeof(fn_buf), "%s/%s", destname, fn_part);
		fh = fopen(fn_buf, "wbx");
	} else {
		fh = fopen(destname, "wbx");
	}
	err1(fwrite(data, data_size, 1, fh) != 1);
	fclose(fh);
	return 0;
err:
	if (fh) fclose(fh);
	return 1;
}

static int import(char *filename)
{
	DNG dng;
	u8 *filedata = NULL;
	u32 filedata_size;
	u8 *dest = NULL;
	err1(read_file(filename, &filedata, &filedata_size));
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
	dest = malloc(dest_size);
	err1(!dest);
	memcpy(dest, dng.data, dng.raw_pos);
	memcpy(dest + dng.raw_pos, image_data, image_size);
	memcpy(dest + dng.raw_pos + image_size, dng.extradata, dng.extradata_pos);
	memcpy(dest + dng.raw_pos + image_size + dng.extradata_pos, dng.data + dng.jpeg_pos, dng.jpeg_size);
	dng_close(&dng);
	err1(dng_open_imported(&dng, dest, dest_size));
	dng_close(&dng);
	err1(save(filename, dest, dest_size));
	free(dest);
	return 0;
err:
	if (dest) free(dest);
	return 1;
}

static int export(char *filename)
{
	DNG dng;
	u8 *filedata = NULL;
	u32 filedata_size;
	int ret = 1;
	err1(read_file(filename, &filedata, &filedata_size));
	err1(dng_open_imported(&dng, filedata, filedata_size));
	ret = save(filename, dng.data, dng.size);
	dng_close(&dng);
err:
	return ret;
}

static int(*process_one)(char *filename) = import;

static int process(char *sourcename)
{
	if (is_dir(sourcename)) {
		DIR *dp = opendir(sourcename);
		err1(!dp);
		int ret = 0;
		struct dirent *ent;
		errno = 0;
		while ((ent = readdir(dp))) {
			char *fn = ent->d_name;
			if (*fn == '.') continue;
			char fn_buf[strlen(sourcename) + strlen(fn) + 2];
			snprintf(fn_buf, sizeof(fn_buf), "%s/%s", sourcename, fn);
			errno = 0;
			ret |= process_one(fn_buf);
		}
		err1(errno);
		return ret;
	} else {
		return process_one(sourcename);
	}
err:
	return 1;
}

int main(int argc, char **argv)
{
	progname = basename(argv[0]);
	if (argc < 2) usage(stderr);
	if (!strcmp(argv[1], "-h")) usage(stdout);
	if (!strcmp(argv[1], "-help")) usage(stdout);
	if (!strcmp(argv[1], "--help")) usage(stdout);
	int source;
	if (!strcmp(argv[1], "--reverse")) {
		if (argc < 4) usage(stderr);
		source = 2;
		process_one = export;
	} else {
		if (argc < 3) usage(stderr);
		source = 1;
	}
	destname = argv[argc - 1];
	dest_is_dir = is_dir(destname);
	if (source + 1 > argc) { // several sources
		if (!dest_is_dir) usage(stderr);
	}
	if (!dest_is_dir && is_dir(argv[source])) usage(stderr);
	int ret = 0;
	while (source + 1 < argc) {
		ret |= process(argv[source]);
		source++;
	}
	return ret;
}
