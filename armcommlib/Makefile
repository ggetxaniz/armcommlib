# Makefile template for a shared library in C

CC = gcc  # C compiler
CFLAGS = -fPIC  # C flags
LDFLAGS = -shared   # linking flags
RM = rm -f   # rm command
TARGET_LIB = libarmcommlib.so  # target lib

SRCS = calculate.c aes_encrypt.c aes_decrypt.c  # source files
OBJS = $(SRCS:.c=.o)

subsystem:
	$(MAKE) -C ../AArch64cryptolib

.PHONY: all
all: ${TARGET_LIB}

$(SRCS:.c=.d):%.d:%.c
	$(CC) $(CFLAGS) -MM $< >$@

$(TARGET_LIB): $(OBJS)
	$(CC) ${LDFLAGS} -o $@ $^

include $(SRCS:.c=.d)

.PHONY: clean
clean:
	-${RM} ${TARGET_LIB} ${OBJS} $(SRCS:.c=.d)
