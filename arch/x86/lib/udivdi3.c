// SPDX-License-Identifier: GPL-2.0
/*
 * 64-bit division helpers for 32-bit x86.  Clang lowers 64-bit unsigned
 * division and modulo on i386 to __udivdi3()/__umoddi3() calls, whereas
 * GCC emits 32-bit operations inline.
 */
#include <linux/export.h>
#include <linux/math64.h>

u64 __udivdi3(u64 a, u64 b)
{
	return div64_u64(a, b);
}
EXPORT_SYMBOL(__udivdi3);

u64 __umoddi3(u64 a, u64 b)
{
	u64 r;

	div64_u64_rem(a, b, &r);
	return r;
}
EXPORT_SYMBOL(__umoddi3);
