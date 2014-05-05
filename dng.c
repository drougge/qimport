#include "qimport.h"

#include <openssl/md5.h>

#define OUTDATA_CHUNKSIZE (4 * 1024 * 1024)
void dng_putc(DNG *dng, u8 c)
{
	err1(dng->err);
	if (dng->outdata_pos >= dng->outdata_size) {
		dng->outdata_size += OUTDATA_CHUNKSIZE;
		dng->outdata = realloc(dng->outdata, dng->outdata_size);
		err1(!dng->outdata);
	}
	dng->outdata[dng->outdata_pos++] = c;
	return;
err:
	dng->err = 1;
}

static void dng_putstr(DNG *dng, const char *s, int count)
{
	while (count--) dng_putc(dng, *(s++));
}

static u8 get8(DNG *dng)
{
	return dng->data[dng->off++];
}

static u16 be_get16(DNG *dng)
{
	dng->off += 2;
	return dng->data[dng->off - 2] << 8 | dng->data[dng->off - 1];
}
static u16 le_get16(DNG *dng)
{
	dng->off += 2;
	return dng->data[dng->off - 1] << 8 | dng->data[dng->off - 2];
}

static u32 be_get32(DNG *dng)
{
	u16 a = be_get16(dng);
	u16 b = be_get16(dng);
	return a << 16 | b;
}
static u32 le_get32(DNG *dng)
{
	u16 a = le_get16(dng);
	u16 b = le_get16(dng);
	return b << 16 | a;
}

void dng_put8_data(DNG *dng, u8 v)
{
	dng->data[dng->off++] = v;
}
static void dng_put8_extradata(DNG *dng, u8 v)
{
	assert(dng->extradata_pos < sizeof(dng->extradata));
	dng->extradata[dng->extradata_pos++] = v;
}

static void be_put16(DNG *dng, u16 v)
{
	dng->put8(dng, v >> 8);
	dng->put8(dng, v);
}
static void le_put16(DNG *dng, u16 v)
{
	dng->put8(dng, v);
	dng->put8(dng, v >> 8);
}

static void be_put32(DNG *dng, u32 v)
{
	be_put16(dng, v >> 16);
	be_put16(dng, v);
}
static void le_put32(DNG *dng, u32 v)
{
	le_put16(dng, v);
	le_put16(dng, v >> 16);
}

static const int typesize[14] = {
	0, //  0: INVALID
	1, //  1: BYTE
	1, //  2: ASCII
	2, //  3: SHORT
	4, //  4: LONG
	8, //  5: RATIONAL
	1, //  6: SBYTE
	1, //  7: UNDEFINE
	2, //  8: SSHORT
	4, //  9: SLONG
	8, // 10: SRATIONAL
	4, // 11: FLOAT
	8, // 12: DOUBLE
	4, // 13: IFD
};

static u32 getter(DNG *dng, int z)
{
	switch (z) {
		case 1:
			return dng->data[dng->off++];
		case 2:
			return dng->get16(dng);
		case 4:
			return dng->get32(dng);
		default:
			dng->err = 1;
			return 0;
	}
}

u32 dng_readtag(DNG *dng, int ifd, u16 tag, int num, u16 *r_type)
{
	err1(dng->err);
	dng->off = dng->ifd[ifd];
	u16 count = dng->get16(dng);
	while (count--) {
		u16 d_tag = dng->get16(dng);
		err1(d_tag > tag);
		if (d_tag < tag) {
			dng->off += 10;
			continue;
		}
		u16 d_type = dng->get16(dng);
		u16 d_vc = dng->get32(dng);
		err1(d_vc <= num);
		if (num < 0) *r_type = d_type;
		if (d_type >= 1 && d_type <= 13) {
			int z = typesize[d_type];
			if (z * d_vc <= 4) {
				if (num < 0) return dng->off;
				while (num-- > 0) getter(dng, z);
				return getter(dng, z);
			} else {
				u32 off = dng->get32(dng);
				if (num < 0) return off;
				off += num * z;
				u32 saved_off = dng->off;
				dng->off = off;
				u32 res = getter(dng, z);
				dng->off = saved_off;
				return res;
			}
		} else {
			err1(num > 0);
			return dng->get32(dng);
		}
	}
err:
	dng->err = 1;
	return 0;
}

void dng_writetag(DNG *dng, int ifd, u16 tag, u16 type, u32 value)
{
	err1(dng->err);
	dng->off = dng->ifd[ifd];
	u16 count = dng->get16(dng);
	while (count--) {
		u16 d_tag = dng->get16(dng);
		err1(d_tag > tag);
		if (d_tag < tag) {
			dng->off += 10;
			continue;
		}
		u16 d_type = dng->get16(dng);
		err1(type != d_type);
		u16 d_vc = dng->get32(dng);
		err1(d_vc != 1);
		int z = typesize[d_type];
		switch (z) {
			case 1:
				dng->put8(dng, value);
				return;
			case 2:
				dng->put16(dng, value);
				return;
			case 4:
				dng->put32(dng, value);
				return;
			default:
				goto err;
		}
	}
err:
	dng->err = 1;
}

void dng_close(DNG *dng)
{
	if (dng->data) free(dng->data);
	if (dng->outdata) free(dng->outdata);
	dng->data = dng->outdata = NULL;
}

static int dng_open_common(DNG *dng)
{
	if (!memcmp(dng->data, "II*\0", 4)) {
		dng->get16 = le_get16;
		dng->get32 = le_get32;
		dng->put16 = le_put16;
		dng->put32 = le_put32;
	} else if (!memcmp(dng->data, "MM\0*", 4)) {
		dng->get16 = be_get16;
		dng->get32 = be_get32;
		dng->put16 = be_put16;
		dng->put32 = be_put32;
	} else {
		goto err;
	}
	dng->off = 4;
	dng->ifd[0] = dng->get32(dng);
	dng->ifd[1] = dng_readtag(dng, 0, 0x14a, 0, 0);
	dng->ifd[2] = dng_readtag(dng, 0, 0x14a, 1, 0);
	dng->ifd[3] = dng_readtag(dng, 0, 0x8769, 0, 0);
	dng->width = dng_readtag(dng, 1, 256, 0, 0);
	dng->height = dng_readtag(dng, 1, 257, 0, 0);
	dng->raw_pos = dng_readtag(dng, 1, 273, 0, 0);
	dng->raw_size = dng_readtag(dng, 1, 279, 0, 0);
	dng->jpeg_pos = dng_readtag(dng, 2, 273, 0, 0);
	dng->jpeg_size = dng_readtag(dng, 2, 279, 0, 0);
	err1(dng->jpeg_pos + dng->jpeg_size != dng->size);
	err1(dng_readtag(dng, 1, 262, 0, 0) != 32803); // PhotometricInterpretation CFA
	err1(dng_readtag(dng, 1, 258, 0, 0) != 12); // only handles 12 bits per sample
	err1(dng->err);
	return 0;
err:
	dng->err = 1;
	return 1;
}

typedef struct savefield {
	u8  ifd;
	u16 tag;
	u16 type;
	u8  bytes;
} SAVEFIELD;

SAVEFIELD savefields[] = {
	{1,    259, 3, 2}, // compression
	{1,    279, 4, 4}, // raw size
	{2,    273, 4, 4}, // jpeg pos
	// and some more that seem likely to be changed later
	{3, 0x829d, 5, 8}, // FNum
	{3, 0x920a, 5, 8}, // FL
	{3, 0xa405, 3, 2}, // FL135
	{0, 0x0112, 3, 2}, // Orientation
	{1, 0xc68d, 4, 16}, // ActiveArea
	{1, 0xc61f, 4, 8}, // DefaultCropOrigin
	{1, 0xc620, 4, 8}, // DefaultCropSize
};

static int dng_extra(DNG *dng)
{
	int fieldcount = sizeof(savefields) / sizeof(*savefields);
	dng->extradata_pos = strlen(QIMPORT_ID) + 1;
	assert(dng->extradata_pos < sizeof(dng->extradata));
	memcpy(dng->extradata, QIMPORT_ID, dng->extradata_pos);
	MD5(dng->data, dng->size, dng->extradata + dng->extradata_pos);
	dng->extradata_pos += 16;
	assert(dng->extradata_pos < sizeof(dng->extradata));
	dng->put8 = dng_put8_extradata;
	dng->put16(dng, fieldcount);
	SAVEFIELD *sf;
	for (int i = 0; i < fieldcount; i++) {
		sf = &savefields[i];
		u16 type = 0;
		u32 off = dng_readtag(dng, sf->ifd, sf->tag, -1, &type);
		err1(type != sf->type);
		dng->put8(dng, sf->ifd);
		dng->put16(dng, sf->tag);
		dng->put16(dng, sf->type);
		dng->put8(dng, sf->bytes);
		assert(dng->extradata_pos + sf->bytes <= sizeof(dng->extradata));
		memcpy(dng->extradata + dng->extradata_pos, dng->data + off, sf->bytes);
		dng->extradata_pos += sf->bytes;
	}
	return 0;
err:
	fprintf(stderr, "Failed to read tag %d:%d\n", sf->ifd, sf->tag);
	dng->err = 1;
	return 1;
}

int dng_open_orig(DNG *dng, u8 *data, u32 size)
{
	memset(dng, 0, sizeof(*dng));
	dng->data = data;
	dng->size = size;
	err1(dng_open_common(dng));
	err1(dng_readtag(dng, 1, 259, 0, 0) != 1); // uncompressed
	err1(dng->width * dng->height * 12 / 8 != dng->raw_size);
	err1(dng->raw_pos + dng->raw_size != dng->jpeg_pos);
	err1(dng_extra(dng));
	return 0;
err:
	dng->err = 1;
	dng_close(dng);
	return 1;
}

int dng_open_imported(DNG *dng, const u8 *data, u32 size)
{
	memset(dng, 0, sizeof(*dng));
	u8 *orig = NULL;
	dng->data = malloc(size);
	err1(!dng->data);
	memcpy(dng->data, data, size);
	dng->size = size;
	err1(dng_open_common(dng));
	u32 comp = dng_readtag(dng, 1, 259, 0, 0);
	dng->off = dng->raw_pos + dng->raw_size;
	err1(memcmp(data + dng->off, QIMPORT_ID, QIMPORT_ID_CHECKLEN));
	dng->off += strlen(QIMPORT_ID) + 1;
	const u8 *md5 = data + dng->off;
	dng->off += 16;
	int fieldcount = dng->get16(dng);
	for (int i = 0; i < fieldcount; i++) {
		u8  ifd   = get8(dng);
		err1(ifd > 3);
		u16 tag   = dng->get16(dng);
		u16 type  = dng->get16(dng);
		u8  bytes = get8(dng);
		u16 type2;
		u32 saved_off = dng->off;
		u32 off = dng_readtag(dng, ifd, tag, -1, &type2);
		err1(off == 0);
		err1(type != type2);
		err1(off > dng->raw_pos);
		memcpy(dng->data + off, dng->data + saved_off, bytes);
		dng->off = saved_off + bytes;
	}
	err1(dng->off != dng->jpeg_pos);
	u32 orig_size = dng_readtag(dng, 2, 273, 0, 0) + dng->jpeg_size;
	u32 uncompressed_size = dng->width * dng->height * 12 / 8;
	err1(uncompressed_size != dng_readtag(dng, 1, 279, 0, 0));
	err1(orig_size != dng->raw_pos + uncompressed_size + dng->jpeg_size);
	err1(dng->err);
	orig = malloc(orig_size);
	err1(!orig);
	memcpy(orig, dng->data, dng->raw_pos);
	memcpy(orig + orig_size - dng->jpeg_size, dng->data + dng->jpeg_pos, dng->jpeg_size);
	if (comp == 1) { // uncompressed
		err1(uncompressed_size != dng->raw_size);
		memcpy(orig + dng->raw_pos, dng->data + dng->raw_pos, dng->raw_size);
	} else if (comp == 7) { // ljpeg
		err1(decompress(dng->data + dng->raw_pos, dng->raw_size, orig + dng->raw_pos, uncompressed_size, dng->width, dng->height));
	} else {
		goto err;
	}
	free(dng->data);
	dng->data = orig;
	orig = NULL;
	dng->size = orig_size;
	u8 curr_md5[16];
	MD5(dng->data, dng->size, curr_md5);
	err1(memcmp(md5, curr_md5, 16));
	return 0;
err:
	dng->err = 1;
	dng_close(dng);
	if (orig) free(orig);
	return 1;
}

void ljpeg_header(DNG *dng)
{
	dng_putstr(dng, "\xff\xd8\xff\xc3\x00\x0e", 6);
	dng_putc(dng, 12);
	dng_putc(dng, dng->height >> 8);
	dng_putc(dng, dng->height);
	dng_putc(dng, dng->width >> 9);
	dng_putc(dng, dng->width >> 1);
	dng_putstr(dng, "\x02\x00\x11\x00\x01\x11\x00", 7);
	ljpeg_huff(dng);
	dng_putstr(dng, "\xff\xda\x00\x0a\x02\x00\x00\x01\x00\x01\x00\x00", 12);
}

void ljpeg_tail(DNG *dng)
{
	while (dng->out_bitcnt) emit_bit(dng, 0);
	dng_putc(dng, 0xff);
	dng_putc(dng, 0xd9);
}
