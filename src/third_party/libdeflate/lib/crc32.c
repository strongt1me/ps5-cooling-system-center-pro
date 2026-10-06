/*
 * crc32.c - CRC-32 checksum, for this app's copy of libdeflate 1.26
 *
 * The vendored libdeflate (README.PS5TM) left CRC-32 out; the x86 PCLMULQDQ
 * implementation and the generated tables were already here, only this entry
 * point was missing. Written for this app (03.10.2026) on the pattern of
 * adler32.c next to it: the portable slice-by-8 routine as the fallback, the
 * best x86 routine chosen at run time. The result is the usual CRC-32 (gzip,
 * zlib's crc32(), PNG): libdeflate_crc32(0, "123456789", 9) == 0xCBF43926.
 * Same licence as the rest of libdeflate (COPYING, MIT).
 */

#include "lib_common.h"
#include "crc32_multipliers.h"
#include "crc32_tables.h"

/* One byte at a time: the tail behind the wide routines and short inputs. */
static u32 MAYBE_UNUSED
crc32_slice1(u32 crc, const u8 *p, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		crc = (crc >> 8) ^ crc32_slice1_table[(u8)crc ^ p[i]];
	return crc;
}

/* Eight bytes at a time with eight tables of 256 entries each, table k
   advancing a byte by k further positions. */
static u32 MAYBE_UNUSED
crc32_slice8(u32 crc, const u8 *p, size_t len)
{
	const u8 * const end = p + len;
	const u8 *end64;

	for (; ((uintptr_t)p & 7) && p != end; p++)
		crc = (crc >> 8) ^ crc32_slice8_table[(u8)crc ^ *p];

	end64 = p + ((end - p) & ~7);
	for (; p != end64; p += 8) {
		u32 v1 = le32_bswap(*(const u32 *)(p + 0));
		u32 v2 = le32_bswap(*(const u32 *)(p + 4));

		crc = crc32_slice8_table[0x700 + (u8)((crc ^ v1) >> 0)] ^
		      crc32_slice8_table[0x600 + (u8)((crc ^ v1) >> 8)] ^
		      crc32_slice8_table[0x500 + (u8)((crc ^ v1) >> 16)] ^
		      crc32_slice8_table[0x400 + (u8)((crc ^ v1) >> 24)] ^
		      crc32_slice8_table[0x300 + (u8)(v2 >> 0)] ^
		      crc32_slice8_table[0x200 + (u8)(v2 >> 8)] ^
		      crc32_slice8_table[0x100 + (u8)(v2 >> 16)] ^
		      crc32_slice8_table[0x000 + (u8)(v2 >> 24)];
	}

	for (; p != end; p++)
		crc = (crc >> 8) ^ crc32_slice8_table[(u8)crc ^ *p];

	return crc;
}

/* Include architecture-specific implementation(s) if available. */
#undef DEFAULT_IMPL
#undef arch_select_crc32_func
typedef u32 (*crc32_func_t)(u32 crc, const u8 *p, size_t len);
#if defined(ARCH_X86_32) || defined(ARCH_X86_64)
#  include "x86/crc32_impl.h"
#endif

#ifndef DEFAULT_IMPL
#  define DEFAULT_IMPL crc32_slice8
#endif

#ifdef arch_select_crc32_func
static u32 dispatch_crc32(u32 crc, const u8 *p, size_t len);

/* Read and written with relaxed atomics: the copy and conversion threads may
   all make their first call at once, each one stores the same pointer, and
   upstream's volatile variable (adler32.c) would be flagged by a thread
   sanitizer for exactly that. */
static crc32_func_t crc32_impl = dispatch_crc32;

/* Choose the best implementation at runtime. */
static u32 dispatch_crc32(u32 crc, const u8 *p, size_t len)
{
	crc32_func_t f = arch_select_crc32_func();

	if (f == NULL)
		f = DEFAULT_IMPL;

	__atomic_store_n(&crc32_impl, f, __ATOMIC_RELAXED);
	return f(crc, p, len);
}
#define CRC32_IMPL() __atomic_load_n(&crc32_impl, __ATOMIC_RELAXED)
#else
/* The best implementation is statically known, so call it directly. */
#define CRC32_IMPL() DEFAULT_IMPL
#endif

LIBDEFLATEAPI u32
libdeflate_crc32(u32 crc, const void *p, size_t len)
{
	if (p == NULL) /* Return initial value. */
		return 0;
	return ~CRC32_IMPL()(~crc, p, len);
}
