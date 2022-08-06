#ifndef TINF_H_INCLUDED
#define TINF_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decompress deflate data from `source` to `dest`.
 *
 * @param dest pointer to where to place decompressed data
 * @param source pointer to compressed data
 * @return size of decompressed data
 */
unsigned int tinf_uncompress(void *dest, const void *source);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TINF_H_INCLUDED */
