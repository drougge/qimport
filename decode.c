#include "qimport.h"

#define NO_VALUE 0xff

typedef struct huffnode {
	struct huffnode *child[2];
	u8 value[2];
} HUFFNODE;

typedef struct huff {
	const u8 *data;
	HUFFNODE *nodes;
	u8 bits;
	int left;
	int err;
} HUFF;

static HUFFNODE *new_node(void)
{
	HUFFNODE *node = malloc(sizeof(HUFFNODE));
	node->child[0] = NULL;
	node->child[1] = NULL;
	node->value[1] = NO_VALUE;
	node->value[0] = NO_VALUE;
	return node;
}

static int put(HUFFNODE *node, int depth, u8 value)
{
	if (depth) {
		for (int i = 0; i < 2; i++) {
			if (node->value[i] == NO_VALUE) {
				if (!node->child[i]) node->child[i] = new_node();
				if (!put(node->child[i], depth - 1, value)) return 0;
			}
		}
		return 1;
	} else {
		for (int i = 0; i < 2; i++) {
			if (node->value[i] == NO_VALUE) {
				node->value[i] = value;
				return 0;
			}
		}
		return 1;
	}
}

static int huff_parse(HUFF *huff, const u8 *table)
{
	const u8 *values = table + 16;
	huff->nodes = new_node();
	for (int bits = 1; bits < 16; bits++) {
		for (int p = 0; p < table[bits]; p++) {
			err1(put(huff->nodes, bits, *values));
			values++;
		}
	}
	return 0;
err:
	return 1;
}

static int get_bit(HUFF *huff)
{
	if (huff->left == 0) {
		huff->left = 8;
		huff->bits = *huff->data;
		huff->data++;
		if (huff->bits == 0xff) {
			err1(*huff->data);
			huff->data++;
		}
	}
	int bit = (huff->bits >> --huff->left) & 1;
	return bit;
err:
	huff->err = 1;
	return 0;
}

static int get_len(HUFF *huff)
{
	HUFFNODE *node = huff->nodes;
	for (int len = 1; len < 16; len++) {
		int bit = get_bit(huff);
		if (node->value[bit] != NO_VALUE) return node->value[bit];
		node = node->child[bit];
		if (!node) break;
	}
	huff->err = 1;
	return 0;
}

static int get_diff(HUFF *huff)
{
	int len, left;
	int value = 0;
	len = left = get_len(huff);
	while (left--) {
		value = (value << 1) | get_bit(huff);
	}
	if (value >> (len - 1) == 0) {
		value = -(((1 << len) - 1) & ~value);
	}
	return value;
}

int decompress(const u8 *ljpeg, u32 ljpeg_size, u8 *dest, u32 dest_size, u16 width, u16 height)
{
	err1(ljpeg_size <= 64);
	err1(memcmp(ljpeg, "\xff\xd8\xff\xc3\x00\x0e", 6));
	err1(memcmp(ljpeg + 11, "\x02\x00\x11\x00\x01\x11\x00\xff\xc4\x00\x20\x00", 12));
	err1(memcmp(ljpeg + 52, "\xff\xda\x00\x0a\x02\x00\x00\x01\x00\x01\x00\x00", 12));
	err1(ljpeg[6] != 12); // bits per sample
	err1(height != (ljpeg[7] << 8 | ljpeg[8]));
	err1(width != ((ljpeg[9] << 8 | ljpeg[10]) << 1));
	err1(dest_size != (u32)width * height * 12 / 8);
	HUFF huff;
	err1(huff_parse(&huff, ljpeg + 23));
	huff.data = ljpeg + 64;
	huff.left = 0;
	huff.err = 0;
	u16 predictor[2] = {1 << (BPS - 1), 1 << (BPS - 1)};
	u16 predictor_up[2] = {0, 0};
	const u8 *end = ljpeg + ljpeg_size - 2;
	int half = 0;
	for (u32 y = 0; y < height; y++) {
		for (u32 x = 0; x < width; x++) {
			err1(huff.data > end);
			err1(huff.err);
			int pixdiff = get_diff(&huff);
			u16 pixel = predictor[0] + pixdiff;
			predictor[0] = predictor[1];
			predictor[1] = pixel;
			if (x < 2) predictor_up[x] = pixel;
			if (half) {
				dest[0] |= pixel >> 8;
				dest[1] = pixel;
				dest += 2;
				half = 0;
			} else {
				dest[0] = pixel >> 4;
				dest[1] = pixel << 4;
				dest += 1;
				half = 1;
			}
		}
		predictor[0] = predictor_up[0];
		predictor[1] = predictor_up[1];
	}
	err1(half);
	return 0;
err:
	return 1;
}
