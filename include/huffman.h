#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <stdint.h>
#include <stddef.h>
#include "bitio.h"

#define NUM_SYMBOLS 256

typedef uint64_t FreqTable[NUM_SYMBOLS];

typedef struct {
    uint32_t value;
    uint8_t  length;
} HuffCode;

typedef HuffCode CodeTable[NUM_SYMBOLS];

typedef struct HuffNode {
    uint64_t          freq;
    uint8_t           byte_value;
    uint8_t           min_byte; /* byte mínimo del subárbol (para desempate) */
    int               is_leaf;
    struct HuffNode  *left;
    struct HuffNode  *right;
} HuffNode;

void      count_frequencies(const uint8_t *data, size_t size, FreqTable freq);
HuffNode *build_tree(const FreqTable freq);
void      generate_codes(HuffNode *root, CodeTable table);
void      serialize_tree(HuffNode *node, BitWriter *bw);
HuffNode *deserialize_tree(BitReader *br);
void      free_tree(HuffNode *node);

#endif
