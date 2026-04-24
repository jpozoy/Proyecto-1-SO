#include "huffman.h"
#include <stdlib.h>
#include <string.h>

/* ── Min-heap interno (máximo 512 elementos) ── */

typedef struct {
    HuffNode *nodes[NUM_SYMBOLS * 2];
    int size;
} MinHeap;

static int heap_cmp(const HuffNode *a, const HuffNode *b) {
    if (a->freq != b->freq)
        return (a->freq < b->freq) ? -1 : 1;
    if (a->min_byte != b->min_byte)
        return (a->min_byte < b->min_byte) ? -1 : 1;
    return 0;
}

static void heap_push(MinHeap *h, HuffNode *node) {
    int i = h->size++;
    h->nodes[i] = node;
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (heap_cmp(h->nodes[i], h->nodes[parent]) < 0) {
            HuffNode *tmp    = h->nodes[i];
            h->nodes[i]      = h->nodes[parent];
            h->nodes[parent] = tmp;
            i = parent;
        } else {
            break;
        }
    }
}

static HuffNode *heap_pop(MinHeap *h) {
    HuffNode *min = h->nodes[0];
    h->nodes[0]   = h->nodes[--h->size];
    int i = 0;
    while (1) {
        int left = 2 * i + 1, right = 2 * i + 2, smallest = i;
        if (left  < h->size && heap_cmp(h->nodes[left],  h->nodes[smallest]) < 0) smallest = left;
        if (right < h->size && heap_cmp(h->nodes[right], h->nodes[smallest]) < 0) smallest = right;
        if (smallest == i) break;
        HuffNode *tmp        = h->nodes[i];
        h->nodes[i]          = h->nodes[smallest];
        h->nodes[smallest]   = tmp;
        i = smallest;
    }
    return min;
}

/* ── Helpers de creación de nodos ── */

static HuffNode *new_leaf(uint8_t byte_val, uint64_t freq) {
    HuffNode *n = malloc(sizeof(HuffNode));
    n->freq       = freq;
    n->byte_value = byte_val;
    n->min_byte   = byte_val;
    n->is_leaf    = 1;
    n->left = n->right = NULL;
    return n;
}

static HuffNode *new_internal(HuffNode *left, HuffNode *right) {
    HuffNode *n = malloc(sizeof(HuffNode));
    n->freq       = left->freq + right->freq;
    n->byte_value = 0;
    n->min_byte   = left->min_byte < right->min_byte ? left->min_byte : right->min_byte;
    n->is_leaf    = 0;
    n->left       = left;
    n->right      = right;
    return n;
}

void count_frequencies(const uint8_t *data, size_t size, FreqTable freq) {
    memset(freq, 0, sizeof(FreqTable));
    for (size_t i = 0; i < size; i++)
        freq[data[i]]++;
}

HuffNode *build_tree(const FreqTable freq) {
    MinHeap heap;
    heap.size = 0;
    for (int i = 0; i < NUM_SYMBOLS; i++) {
        if (freq[i] > 0)
            heap_push(&heap, new_leaf((uint8_t)i, freq[i]));
    }
    if (heap.size == 0) return NULL;
    if (heap.size == 1) return heap.nodes[0];
    while (heap.size > 1) {
        HuffNode *left  = heap_pop(&heap);
        HuffNode *right = heap_pop(&heap);
        heap_push(&heap, new_internal(left, right));
    }
    return heap_pop(&heap);
}

static void gen_codes(HuffNode *node, uint32_t value, int length, CodeTable table) {
    if (node->is_leaf) {
        if (length == 0) {
            table[node->byte_value].value  = 0;
            table[node->byte_value].length = 1;
        } else {
            table[node->byte_value].value  = value;
            table[node->byte_value].length = (uint8_t)length;
        }
        return;
    }
    gen_codes(node->left,  (value << 1),       length + 1, table);
    gen_codes(node->right, (value << 1) | 1u,  length + 1, table);
}

void generate_codes(HuffNode *root, CodeTable table) {
    if (!root) return;
    memset(table, 0, sizeof(CodeTable));
    gen_codes(root, 0, 0, table);
}

void serialize_tree(HuffNode *node, BitWriter *bw) {
    if (node->is_leaf) {
        bitwriter_write_bit(bw, 1);
        bitwriter_write_bits(bw, node->byte_value, 8);
    } else {
        bitwriter_write_bit(bw, 0);
        serialize_tree(node->left,  bw);
        serialize_tree(node->right, bw);
    }
}

HuffNode *deserialize_tree(BitReader *br) {
    int bit = bitreader_read_bit(br);
    if (bit == 1) {
        uint8_t byte_val = (uint8_t)bitreader_read_bits(br, 8);
        return new_leaf(byte_val, 0);
    }
    HuffNode *left  = deserialize_tree(br);
    HuffNode *right = deserialize_tree(br);
    return new_internal(left, right);
}

void free_tree(HuffNode *node) {
    if (!node) return;
    free_tree(node->left);
    free_tree(node->right);
    free(node);
}
