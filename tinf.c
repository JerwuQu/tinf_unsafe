#include "tinf.h"

#include <limits.h>

#if defined(UINT_MAX) && (UINT_MAX) < 0xFFFFFFFFUL
#  error "tinf requires unsigned int to be at least 32-bit"
#endif

/* -- Internal data structures -- */

struct tinf_tree {
	unsigned short counts[16]; /* Number of codes with a given length */
	unsigned short symbols[288]; /* Symbols sorted by code */
	int max_sym;
};

struct tinf_data {
	const unsigned char *source;
	unsigned int tag;
	int bitcount;

	unsigned char *dest;

	struct tinf_tree ltree; /* Literal/length tree */
	struct tinf_tree dtree; /* Distance tree */
};

/* -- Utility functions -- */

static unsigned int read_le16(const unsigned char *p)
{
	return ((unsigned int) p[0])
	     | ((unsigned int) p[1] << 8);
}

/* Build fixed Huffman trees */
static void tinf_build_fixed_trees(struct tinf_tree *lt, struct tinf_tree *dt)
{
	int i;

	/* Build fixed literal/length tree */
	for (i = 0; i < 16; ++i) {
		lt->counts[i] = 0;
	}

	lt->counts[7] = 24;
	lt->counts[8] = 152;
	lt->counts[9] = 112;

	for (i = 0; i < 24; ++i) {
		lt->symbols[i] = 256 + i;
	}
	for (i = 0; i < 144; ++i) {
		lt->symbols[24 + i] = i;
	}
	for (i = 0; i < 8; ++i) {
		lt->symbols[24 + 144 + i] = 280 + i;
	}
	for (i = 0; i < 112; ++i) {
		lt->symbols[24 + 144 + 8 + i] = 144 + i;
	}

	lt->max_sym = 285;

	/* Build fixed distance tree */
	for (i = 0; i < 16; ++i) {
		dt->counts[i] = 0;
	}

	dt->counts[5] = 32;

	for (i = 0; i < 32; ++i) {
		dt->symbols[i] = i;
	}

	dt->max_sym = 29;
}

/* Given an array of code lengths, build a tree */
static void tinf_build_tree(struct tinf_tree *t, const unsigned char *lengths,
                            unsigned int num)
{
	unsigned short offs[16];
	unsigned int i, num_codes, available;

	for (i = 0; i < 16; ++i) {
		t->counts[i] = 0;
	}

	t->max_sym = -1;

	/* Count number of codes for each non-zero length */
	for (i = 0; i < num; ++i) {
		if (lengths[i]) {
			t->max_sym = i;
			t->counts[lengths[i]]++;
		}
	}

	/* Compute offset table for distribution sort */
	for (available = 1, num_codes = 0, i = 0; i < 16; ++i) {
		unsigned int used = t->counts[i];

		available = 2 * (available - used);

		offs[i] = num_codes;
		num_codes += used;
	}

	/* Fill in symbols sorted by code */
	for (i = 0; i < num; ++i) {
		if (lengths[i]) {
			t->symbols[offs[lengths[i]]++] = i;
		}
	}

	/*
	 * For the special case of only one code (which will be 0) add a
	 * code 1 which results in a symbol that is too large
	 */
	if (num_codes == 1) {
		t->counts[1] = 2;
		t->symbols[1] = t->max_sym + 1;
	}
}

/* -- Decode functions -- */

static void tinf_refill(struct tinf_data *d, int num)
{
	/* Read bytes until at least num bits available */
	while (d->bitcount < num) {
		d->tag |= (unsigned int) *d->source++ << d->bitcount;
		d->bitcount += 8;
	}
}

static unsigned int tinf_getbits_no_refill(struct tinf_data *d, int num)
{
	unsigned int bits;

	/* Get bits from tag */
	bits = d->tag & ((1UL << num) - 1);

	/* Remove bits from tag */
	d->tag >>= num;
	d->bitcount -= num;

	return bits;
}

/* Get num bits from source stream */
static unsigned int tinf_getbits(struct tinf_data *d, int num)
{
	tinf_refill(d, num);
	return tinf_getbits_no_refill(d, num);
}

/* Read a num bit value from stream and add base */
static unsigned int tinf_getbits_base(struct tinf_data *d, int num, int base)
{
	return base + (num ? tinf_getbits(d, num) : 0);
}

/* Given a data stream and a tree, decode a symbol */
static int tinf_decode_symbol(struct tinf_data *d, const struct tinf_tree *t)
{
	int base = 0, offs = 0;
	int len;

	/*
	 * Get more bits while code index is above number of codes
	 *
	 * Rather than the actual code, we are computing the position of the
	 * code in the sorted order of codes, which is the index of the
	 * corresponding symbol.
	 *
	 * Conceptually, for each code length (level in the tree), there are
	 * counts[len] leaves on the left and internal nodes on the right.
	 * The index we have decoded so far is base + offs, and if that
	 * falls within the leaves we are done. Otherwise we adjust the range
	 * of offs and add one more bit to it.
	 */
	for (len = 1; ; ++len) {
		offs = 2 * offs + tinf_getbits(d, 1);

		if (offs < t->counts[len]) {
			break;
		}

		base += t->counts[len];
		offs -= t->counts[len];
	}

	return t->symbols[base + offs];
}

/* Given a data stream, decode dynamic trees from it */
static void tinf_decode_trees(struct tinf_data *d, struct tinf_tree *lt,
                              struct tinf_tree *dt)
{
	unsigned char lengths[288 + 32];

	/* Special ordering of code length codes */
	static const unsigned char clcidx[19] = {
		16, 17, 18, 0,  8, 7,  9, 6, 10, 5,
		11,  4, 12, 3, 13, 2, 14, 1, 15
	};

	unsigned int hlit, hdist, hclen;
	unsigned int i, num, length;

	/* Get 5 bits HLIT (257-286) */
	hlit = tinf_getbits_base(d, 5, 257);

	/* Get 5 bits HDIST (1-32) */
	hdist = tinf_getbits_base(d, 5, 1);

	/* Get 4 bits HCLEN (4-19) */
	hclen = tinf_getbits_base(d, 4, 4);

	for (i = 0; i < 19; ++i) {
		lengths[i] = 0;
	}

	/* Read code lengths for code length alphabet */
	for (i = 0; i < hclen; ++i) {
		/* Get 3 bits code length (0-7) */
		unsigned int clen = tinf_getbits(d, 3);

		lengths[clcidx[i]] = clen;
	}

	/* Build code length tree (in literal/length tree to save space) */
	tinf_build_tree(lt, lengths, 19);

	/* Decode code lengths for the dynamic trees */
	for (num = 0; num < hlit + hdist; ) {
		int sym = tinf_decode_symbol(d, lt);

		switch (sym) {
		case 16:
			/* Copy previous code length 3-6 times (read 2 bits) */
			sym = lengths[num - 1];
			length = tinf_getbits_base(d, 2, 3);
			break;
		case 17:
			/* Repeat code length 0 for 3-10 times (read 3 bits) */
			sym = 0;
			length = tinf_getbits_base(d, 3, 3);
			break;
		case 18:
			/* Repeat code length 0 for 11-138 times (read 7 bits) */
			sym = 0;
			length = tinf_getbits_base(d, 7, 11);
			break;
		default:
			/* Values 0-15 represent the actual code lengths */
			length = 1;
			break;
		}

		while (length--) {
			lengths[num++] = sym;
		}
	}

	/* Build dynamic trees */
	tinf_build_tree(lt, lengths, hlit);
	tinf_build_tree(dt, lengths + hlit, hdist);
}

/* -- Block inflate functions -- */

/* Given a stream and two trees, inflate a block of data */
static void tinf_inflate_block_data(struct tinf_data *d, struct tinf_tree *lt,
                                    struct tinf_tree *dt)
{
	/* Extra bits and base tables for length codes */
	static const unsigned char length_bits[30] = {
		0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
		1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
		4, 4, 4, 4, 5, 5, 5, 5, 0, 127
	};

	static const unsigned short length_base[30] = {
		 3,  4,  5,   6,   7,   8,   9,  10,  11,  13,
		15, 17, 19,  23,  27,  31,  35,  43,  51,  59,
		67, 83, 99, 115, 131, 163, 195, 227, 258,   0
	};

	/* Extra bits and base tables for distance codes */
	static const unsigned char dist_bits[30] = {
		0, 0,  0,  0,  1,  1,  2,  2,  3,  3,
		4, 4,  5,  5,  6,  6,  7,  7,  8,  8,
		9, 9, 10, 10, 11, 11, 12, 12, 13, 13
	};

	static const unsigned short dist_base[30] = {
		   1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
		  33,   49,   65,   97,  129,  193,  257,   385,   513,   769,
		1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
	};

	for (;;) {
		int sym = tinf_decode_symbol(d, lt);

		if (sym < 256) {
			*d->dest++ = sym;
		}
		else {
			int length, dist, offs;
			int i;

			/* Check for end of block */
			if (sym == 256) {
				return;
			}

			sym -= 257;

			/* Possibly get more bits from length code */
			length = tinf_getbits_base(d, length_bits[sym],
			                           length_base[sym]);

			dist = tinf_decode_symbol(d, dt);

			/* Possibly get more bits from distance code */
			offs = tinf_getbits_base(d, dist_bits[dist],
			                         dist_base[dist]);

			/* Copy match */
			for (i = 0; i < length; ++i) {
				d->dest[i] = d->dest[i - offs];
			}

			d->dest += length;
		}
	}
}

/* Inflate an uncompressed block of data */
static void tinf_inflate_uncompressed_block(struct tinf_data *d)
{
	unsigned int length;

	/* Get length */
	length = read_le16(d->source);

	d->source += 4;

	/* Copy block */
	while (length--) {
		*d->dest++ = *d->source++;
	}

	/* Make sure we start next block on a byte boundary */
	d->tag = 0;
	d->bitcount = 0;
}

/* Inflate a block of data compressed with fixed Huffman trees */
static void tinf_inflate_fixed_block(struct tinf_data *d)
{
	/* Build fixed Huffman trees */
	tinf_build_fixed_trees(&d->ltree, &d->dtree);

	/* Decode block using fixed trees */
	tinf_inflate_block_data(d, &d->ltree, &d->dtree);
}

/* Inflate a block of data compressed with dynamic Huffman trees */
static void tinf_inflate_dynamic_block(struct tinf_data *d)
{
	/* Decode trees from stream */
	tinf_decode_trees(d, &d->ltree, &d->dtree);

	/* Decode block using decoded trees */
	tinf_inflate_block_data(d, &d->ltree, &d->dtree);
}

/* -- Public functions -- */

/* Inflate stream from source to dest */
unsigned int tinf_uncompress(void *dest, const void *source)
{
	struct tinf_data d;
	int bfinal;

	/* Initialise data */
	d.source = (const unsigned char *) source;
	d.tag = 0;
	d.bitcount = 0;
	d.dest = (unsigned char *) dest;

	do {
		unsigned int btype;

		/* Read final block flag */
		bfinal = tinf_getbits(&d, 1);

		/* Read block type (2 bits) */
		btype = tinf_getbits(&d, 2);

		/* Decompress block */
		switch (btype) {
		case 0:
			/* Decompress uncompressed block */
			tinf_inflate_uncompressed_block(&d);
			break;
		case 1:
			/* Decompress block with fixed Huffman trees */
			tinf_inflate_fixed_block(&d);
			break;
		case 2:
			/* Decompress block with dynamic Huffman trees */
			tinf_inflate_dynamic_block(&d);
			break;
		default:
			break;
		}
	} while (!bfinal);

	return d.dest - (unsigned char *) dest;
}
