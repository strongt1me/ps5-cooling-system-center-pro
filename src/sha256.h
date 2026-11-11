/* SHA-256 (FIPS 180-4) for the checksum files next to a backup — see sha256.c. */
#ifndef PS5TM_SHA256_H
#define PS5TM_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t h[8];
  uint64_t bytes;             /* message length so far */
  uint8_t  buf[64];           /* a block not yet full   */
  unsigned fill;
} ps5tm_sha256_t;

void ps5tm_sha256_init(ps5tm_sha256_t *s);
void ps5tm_sha256_update(ps5tm_sha256_t *s, const void *data, size_t len);
void ps5tm_sha256_final(ps5tm_sha256_t *s, uint8_t out[32]);
/* 64 lower-case hex digits and a terminating zero. */
void ps5tm_sha256_hex(const uint8_t digest[32], char out[65]);

/* 1 when the SHA extensions of the CPU do the work (x86-64, CPUID, and a known-answer test passed), else 0: the plain C. */
int  ps5tm_sha256_hw(void);
/* For tests: 0 plain C, 1 the hardware where the CPU has it (else plain C), -1 decide again by the CPU. */
void ps5tm_sha256_force(int mode);

#endif
