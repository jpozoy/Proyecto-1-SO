GCC = gcc
CFLAGS = -Wall -Werror -O3
SRC = src
INC = include

CORE = $(SRC)/huffman.c $(SRC)/bitio.c $(SRC)/compress_core.c $(SRC)/decompress_core.c $(SRC)/timing.c

.PHONY: all
all:
	$(GCC) $(CFLAGS) -I$(INC) $(SRC)/serial_compress.c   $(CORE) -o serial_compress
	$(GCC) $(CFLAGS) -I$(INC) $(SRC)/serial_decompress.c $(CORE) -o serial_decompress
	$(GCC) $(CFLAGS) -I$(INC) $(SRC)/fork_compress.c     $(CORE) -o fork_compress
	$(GCC) $(CFLAGS) -I$(INC) $(SRC)/fork_decompress.c   $(CORE) -o fork_decompress
	$(GCC) $(CFLAGS) -I$(INC) $(SRC)/pthread_compress.c  $(CORE) -o pthread_compress -lpthread
	$(GCC) $(CFLAGS) -I$(INC) $(SRC)/pthread_decompress.c $(CORE) -o pthread_decompress -lpthread

.PHONY: clean
clean:
	rm -rf serial_compress serial_decompress \
	       fork_compress fork_decompress \
	       pthread_compress pthread_decompress \
	       out*