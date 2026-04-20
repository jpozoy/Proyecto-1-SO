#include "bitio.h"

void bitwriter_init(BitWriter *bw, uint8_t *buffer, size_t capacity) {
    bw->buffer   = buffer;
    bw->capacity = capacity;
    bw->byte_pos = 0;
    bw->current  = 0;
    bw->bit_pos  = 7;
}

void bitwriter_write_bit(BitWriter *bw, int bit) {
    if (bit) bw->current |= (uint8_t)(1 << bw->bit_pos);
    bw->bit_pos--;
    if (bw->bit_pos < 0) {
        if (bw->byte_pos < bw->capacity)
            bw->buffer[bw->byte_pos] = bw->current;
        bw->byte_pos++;
        bw->current = 0;
        bw->bit_pos = 7;
    }
}

void bitwriter_write_bits(BitWriter *bw, uint32_t value, int num_bits) {
    for (int i = num_bits - 1; i >= 0; i--)
        bitwriter_write_bit(bw, (int)((value >> i) & 1));
}

void bitwriter_flush(BitWriter *bw) {
    if (bw->bit_pos < 7) {
        if (bw->byte_pos < bw->capacity)
            bw->buffer[bw->byte_pos] = bw->current;
        bw->byte_pos++;
        bw->bit_pos = 7;
        bw->current = 0;
    }
}

size_t bitwriter_bits_written(BitWriter *bw) {
    return bw->byte_pos * 8 + (size_t)(7 - bw->bit_pos);
}

void bitreader_init(BitReader *br, const uint8_t *buffer, size_t total_bits) {
    br->buffer     = buffer;
    br->total_bits = total_bits;
    br->pos        = 0;
}

int bitreader_read_bit(BitReader *br) {
    if (br->pos >= br->total_bits) return -1;
    size_t byte_idx = br->pos / 8;
    int    bit_idx  = 7 - (int)(br->pos % 8);
    br->pos++;
    return (br->buffer[byte_idx] >> bit_idx) & 1;
}

uint32_t bitreader_read_bits(BitReader *br, int num_bits) {
    uint32_t value = 0;
    for (int i = 0; i < num_bits; i++) {
        int bit = bitreader_read_bit(br);
        if (bit < 0) break;
        value = (value << 1) | (uint32_t)bit;
    }
    return value;
}

int bitreader_eof(BitReader *br) {
    return br->pos >= br->total_bits;
}
