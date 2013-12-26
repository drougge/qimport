#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define err1(v) if (v) goto err

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef unsigned int uint;

#define BPS 12

struct dng;
typedef struct dng DNG;

typedef void (*emitter_f)(DNG *dng, uint bitcnt, u16 value);
typedef u16 (*get16_f)(DNG *dng);
typedef u32 (*get32_f)(DNG *dng);
typedef void (*put8_f)(DNG *dng, u8 v);
typedef void (*put16_f)(DNG *dng, u16 v);
typedef void (*put32_f)(DNG *dng, u32 v);

// The last two digits are not checked, they are for supposedly compatible
// changes (that can later be reliable detected in written files).
#define QIMPORT_ID "qimport 0 00 00"
#define QIMPORT_ID_CHECKLEN (strlen(QIMPORT_ID) - 2)

struct dng {
	u8  *s;
	u8  *data;
	u8  *outdata;
	u8  extradata[112];
	u32 outdata_size, outdata_pos;
	u32 extradata_pos;
	int err;
	u32 size;
	u32 off;
	u32 width, height;
	int counts[BPS + 1];
	int half;
	int out_bitcnt;
	u8  out_bits;
	emitter_f emit;
	get16_f get16;
	get32_f get32;
	put8_f  put8;
	put16_f put16;
	put32_f put32;
	u32 ifd[4];
	u32 raw_pos;
	u32 raw_size;
	u32 jpeg_pos;
	u32 jpeg_size;
	u32 comp;
};

void emit_count(DNG *dng, uint bitcnt, u16 value);
void emit_data(DNG *dng, uint bitcnt, u16 value);
void compress_loop(DNG *dng);
void emit_bit(DNG *dng, u8 bit);
void build_huff(DNG *dng);
void dng_putc(DNG *dng, u8 c);
void ljpeg_huff(DNG *dng);
int decompress(const u8 *ljpeg, u32 ljpeg_size, u8 *dest, u32 dest_size, u16 width, u16 height);
int dng_open_orig(DNG *dng, u8 *data, u32 size);
int dng_open_imported(DNG *dng, const u8 *data, u32 size);
void ljpeg_header(DNG *dng);
void ljpeg_tail(DNG *dng);
void dng_put8_data(DNG *dng, u8 v);
u32 dng_readtag(DNG *dng, int ifd, u16 tag, int num, u16 *r_type);
void dng_writetag(DNG *dng, int ifd, u16 tag, u16 type, u32 value);
void dng_close(DNG *dng);
