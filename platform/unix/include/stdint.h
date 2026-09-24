/* stdint.h - the C99 fixed-width types, which Atari System V (1991)
 * predates. toolchain/sysv4-cc puts this directory on the include path. */
#ifndef _ASV_STDINT_H
#define _ASV_STDINT_H
typedef signed char		int8_t;
typedef unsigned char		uint8_t;
typedef short			int16_t;
typedef unsigned short		uint16_t;
typedef int			int32_t;
typedef unsigned int		uint32_t;
typedef long long		int64_t;
typedef unsigned long long	uint64_t;
typedef long			intptr_t;
typedef unsigned long		uintptr_t;
typedef long long		intmax_t;
typedef unsigned long long	uintmax_t;
#define SIZE_MAX	4294967295U
#endif
