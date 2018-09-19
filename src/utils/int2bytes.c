/**
 * Copyright 2018 Lipeng WANG (wanglipeng@sensetime.com)
 */

#include "int2bytes.h"

void uint16_to_bytes(uint16_t val, unsigned char *bytes) {
    unsigned char *b = (unsigned char *) bytes;
    unsigned char *p = (unsigned char *) &val;
#ifdef _BIG_ENDIAN
    b[0] = p[0];
    b[1] = p[1];
#else
    b[0] = p[1];
    b[1] = p[0];
#endif
}

uint16_t bytes_to_uint16(unsigned char *bytes) {
    unsigned char internal_buf[2];
#ifdef _BIG_ENDIAN
    return *((uint16_t *) bytes);
#else
    internal_buf[0] = bytes[1];
    internal_buf[1] = bytes[0];
    return *((uint16_t *) internal_buf);
#endif
}

void uint32_to_bytes(uint32_t val, unsigned char *bytes) {
    unsigned char *b = (unsigned char *) bytes;
    unsigned char *p = (unsigned char *) &val;
#ifdef _BIG_ENDIAN
    b[0] = p[0];
    b[1] = p[1];
    b[2] = p[2];
    b[3] = p[3];
#else
    b[0] = p[3];
    b[1] = p[2];
    b[2] = p[1];
    b[3] = p[0];
#endif
}

uint32_t bytes_to_uint32(unsigned char *bytes) {
    unsigned char internal_buf[4];
#ifdef _BIG_ENDIAN
    return *((uint32_t *) bytes);
#else
    internal_buf[0] = bytes[3];
    internal_buf[1] = bytes[2];
    internal_buf[2] = bytes[1];
    internal_buf[3] = bytes[0];
    return *((uint32_t *) internal_buf);
#endif

}

void uint64_to_bytes(uint64_t val, unsigned char *bytes) {
    unsigned char *b = (unsigned char *) bytes;
    unsigned char *p = (unsigned char *) &val;
#ifdef _BIG_ENDIAN
    b[0] = p[0];
    b[1] = p[1];
    b[2] = p[2];
    b[3] = p[3];
    b[4] = p[4];
    b[5] = p[5];
    b[6] = p[6];
    b[7] = p[7];
#else
    b[0] = p[7];
    b[1] = p[6];
    b[2] = p[5];
    b[3] = p[4];
    b[4] = p[3];
    b[5] = p[2];
    b[6] = p[1];
    b[7] = p[0];
#endif
}

uint64_t bytes_to_uint64(unsigned char *bytes) {
    unsigned char internal_buf[8];
#ifdef _BIG_ENDIAN
    return *((uint64_t *) bytes);
#else
    internal_buf[0] = bytes[7];
    internal_buf[1] = bytes[6];
    internal_buf[2] = bytes[5];
    internal_buf[3] = bytes[4];
    internal_buf[4] = bytes[3];
    internal_buf[5] = bytes[2];
    internal_buf[6] = bytes[1];
    internal_buf[7] = bytes[0];
    return *((uint64_t *) internal_buf);
#endif
}