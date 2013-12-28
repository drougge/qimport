#include "qimport.h"

static u16 sample(DNG *dng)
{
	if (dng->half) {
		u16 top = *(dng->s++) & 0xf;
		u16 bottom = *(dng->s++);
		dng->half = 0;
		return (top << 8) | bottom;
	} else {
		u16 top = *(dng->s++);
		u16 bottom = *dng->s;
		dng->half = 1;
		return (top << 4) | (bottom >> 4);
	}
}

static uint count_bits(u16 v)
{
	uint count = 0;
	while (v) {
		v >>= 1;
		count++;
	}
	return count;
}

void emit_count(DNG *dng, uint bitcnt, u16 value)
{
	dng->counts[bitcnt]++;
	(void) value;
}

static int *counts;
static int huff_cmp(const void *avp, const void *bvp)
{
	const int *ap = avp;
	const int *bp = bvp;
	const int a = counts[*ap];
	const int b = counts[*bp];
	if (a > b) return -1;
	return b > a;
}

// Yes, of course I should use symbol freqs to decide this. Later.
static int huff[BPS + 1];
static const u16 huff_bits_[BPS + 1] = {
                                        0b00,
                                        0b01,
                                        0b100,
                                        0b101,
                                        0b110,
                                        0b1110,
                                        0b11110,
                                        0b111110,
                                        0b1111110,
                                        0b11111110,
                                        0b111111110,
                                        0b1111111110,
                                        0b1111111111,
                                       };
static u16 huff_bits[BPS + 1];
static const int huff_lens_[BPS + 1] = {
	2, 2, 3, 3, 3, 4, 5, 6, 7, 8, 9, 10, 10,
};
static int huff_lens[BPS + 1];

void build_huff(DNG *dng)
{
	counts = dng->counts;
	for (int i = 0; i <= BPS; i++) {
		huff[i] = i;
	}
	qsort(huff, BPS + 1, sizeof(*huff), huff_cmp);
	for (int i = 0; i <= BPS; i++) {
		huff_bits[huff[i]] = huff_bits_[i];
		huff_lens[huff[i]] = huff_lens_[i];
	}
}

void ljpeg_huff(DNG *dng)
{
	dng_putc(dng, 0xff);
	dng_putc(dng, 0xc4); // define huff
	dng_putc(dng, 0x00);
	dng_putc(dng, 0x20); // 32 bytes (2(len) + 1(table 0) + 16(counts) + 13(values))
	dng_putc(dng, 0x00); // table 0
	for (int i = 0; i < 16; i++) {
		int c = 0;
		for (int l = 0; l <= BPS; l++) {
			c += (huff_lens[l] == i + 1);
		}
		dng_putc(dng, c);
	}
	for (int i = 0; i <= BPS; i++) {
		dng_putc(dng, huff[i]);
	}
}

void emit_bit(DNG *dng, u8 bit)
{
	dng->out_bits = (dng->out_bits << 1) | bit;
	if (++dng->out_bitcnt == 8) {
		dng_putc(dng, dng->out_bits);
		if (dng->out_bits == 255) dng_putc(dng, 0);
		dng->out_bitcnt = 0;
		dng->out_bits = 0;
	}
}

static void emit_bits(DNG *dng, uint bitcnt, u16 value)
{
	while (bitcnt--) {
		emit_bit(dng, (value >> bitcnt) & 1);
	}
}

void emit_data(DNG *dng, uint bitcnt, u16 value)
{
	emit_bits(dng, huff_lens[bitcnt], huff_bits[bitcnt]);
	emit_bits(dng, bitcnt, value);
}

static void emit_value(DNG *dng, int value)
{
	uint bitcnt;
	if (value < 0) {
		bitcnt = count_bits(-value);
		value--;
	} else {
		bitcnt = count_bits(value);
	}
	dng->emit(dng, bitcnt, value);
}

void compress_loop(DNG *dng)
{
	u16 predictor[2] = {1 << (BPS - 1), 1 << (BPS - 1)};
	u16 predictor_up[2] = {0, 0};
	for (u32 y = 0; y < dng->height; y++) {
		for (u32 x = 0; x < dng->width; x++) {
			int pixel = sample(dng);
			int value = pixel - predictor[0];
			emit_value(dng, value);
			predictor[0] = predictor[1];
			predictor[1] = pixel;
			if (x < 2) predictor_up[x] = pixel;
		}
		predictor[0] = predictor_up[0];
		predictor[1] = predictor_up[1];
	}
}
