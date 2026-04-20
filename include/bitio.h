#ifndef BITIO_H
#define BITIO_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t byte_pos;
    uint8_t current;
    int bit_pos; /* siguiente bit a escribir en current (7=MSB, 0=LSB) */
} BitWriter;

typedef struct {
    const uint8_t *buffer;
    size_t total_bits;
    size_t pos;
} BitReader;

void bitwriter_init(BitWriter *bw, uint8_t *buffer, size_t capacity);
void bitwriter_write_bit(BitWriter *bw, int bit);
void bitwriter_write_bits(BitWriter *bw, uint32_t value, int num_bits);
void bitwriter_flush(BitWriter *bw);
size_t bitwriter_bits_written(BitWriter *bw);

void bitreader_init(BitReader *br, const uint8_t *buffer, size_t total_bits);
int bitreader_read_bit(BitReader *br);
uint32_t bitreader_read_bits(BitReader *br, int num_bits);
int bitreader_eof(BitReader *br);

#endif
