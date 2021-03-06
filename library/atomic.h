/*
 * Copyright (C) 2008-2014 Numascale AS, support@numascale.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#define __STDC_FORMAT_MACROS
#include <stdint.h>

#define mb()    asm volatile("mfence":::"memory")

#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

#ifndef LOCK_PREFIX
#define LOCK_PREFIX "lock;"
#endif

#define atomic_compare_and_exchange(mem, oldval, newval)		\
	({ __typeof (*mem) ret;						\
		if (sizeof (*mem) == 1)					\
			asm volatile (LOCK_PREFIX "cmpxchgb %b2, %1"	\
				      : "=a" (ret), "+m" (*mem)		\
				      : "q" (newval), "0" (oldval)	\
				      : "memory");			\
		else if (sizeof (*mem) == 2)				\
			asm volatile (LOCK_PREFIX "cmpxchgw %w2, %1"	\
				      : "=a" (ret), "+m" (*mem)		\
				      : "r" (newval), "0" (oldval)	\
				      : "memory");			\
		else if (sizeof (*mem) == 4)				\
			asm volatile (LOCK_PREFIX "cmpxchgl %k2, %1"	\
				      : "=a" (ret), "+m" (*mem)		\
				      : "r" (newval), "0" (oldval)	\
				      : "memory");			\
		else							\
			asm volatile (LOCK_PREFIX "cmpxchgq %q2, %1"	\
				      : "=a" (ret), "+m" (*mem)		\
				      : "r" (newval), "0" (oldval)	\
				      : "memory");			\
		ret; })

#define atomic_exchange(mem, newvalue)					\
	({__typeof(*(mem)) ret;						\
		if (sizeof(*mem) == 1)					\
			asm volatile("xchgb %b0,%1"			\
				     : "=q" (ret), "+m" (*mem)		\
				     : "0" (newvalue)			\
				     : "memory");			\
		else if (sizeof(*mem) == 2)				\
			asm volatile("xchgw %w0,%1"			\
				     : "=r" (ret), "+m" (*mem)		\
				     : "0" (newvalue)			\
				     : "memory");			\
		else if (sizeof(*mem) == 4)				\
			asm volatile("xchgl %k0,%1"			\
				     : "=r" (ret), "+m" (*mem)		\
				     : "0" (newvalue)			\
				     : "memory");			\
		else							\
			asm volatile("xchgq %0,%1"			\
				     : "=r" (ret), "+m" (*mem)		\
				     : "0" (newvalue)			\
				     : "memory");			\
		ret; })

#define atomic_exchange_and_add(mem, value)				\
	({ __typeof(*(mem)) result;					\
		if (sizeof (*mem) == 1)					\
			asm volatile (LOCK_PREFIX "xaddb %b0, %1"	\
				      : "=q" (result), "+m" (*mem)	\
				      : "0" (value)			\
				      : "memory");			\
		else if (sizeof (*mem) == 2)				\
			asm volatile (LOCK_PREFIX "xaddw %w0, %1"	\
				      : "=r" (result), "+m" (*mem)	\
				      : "0" (value)			\
				      : "memory");			\
		else if (sizeof (*mem) == 4)				\
			asm volatile (LOCK_PREFIX "xaddl %k0, %1"	\
				      : "=r" (result), "+m" (*mem)	\
				      : "0" (value)			\
				      : "memory");			\
		else							\
			asm volatile (LOCK_PREFIX "xaddq %q0, %1"	\
				      : "=r" (result), "+m" (*mem)	\
				      : "0" (value)			\
				      : "memory");			\
		result; })

#define atomic_add(mem, value)						\
	do {								\
		if (__builtin_constant_p (value) && (value) == 1)	\
			atomic_increment (mem);				\
		else if (__builtin_constant_p (value) && (value) == -1)	\
			atomic_decrement (mem);				\
		else if (sizeof (*mem) == 1)				\
			asm volatile (LOCK_PREFIX "addb %b1, %0"	\
				      : "=m" (*mem)			\
				      : "iq" (value), "m" (*mem));	\
		else if (sizeof (*mem) == 2)				\
			asm volatile (LOCK_PREFIX "addw %w1, %0"	\
				      : "=m" (*mem)			\
				      : "ir" (value), "m" (*mem));	\
		else if (sizeof (*mem) == 4)				\
			asm volatile (LOCK_PREFIX "addl %1, %0"		\
				      : "=m" (*mem)			\
				      : "ir" (value), "m" (*mem));	\
		else							\
			asm volatile (LOCK_PREFIX "addq %q1, %0"	\
				      : "=m" (*mem)			\
				      : "ir" (value), "m" (*mem));	\
	} while (0)

#define atomic_increment(mem)						\
	do {								\
		if (sizeof (*mem) == 1)					\
			asm volatile (LOCK_PREFIX "incb %b0"		\
				      : "=m" (*mem)			\
				      : "m" (*mem));			\
		else if (sizeof (*mem) == 2)				\
			asm volatile (LOCK_PREFIX "incw %w0"		\
				      : "=m" (*mem)			\
				      : "m" (*mem));			\
		else if (sizeof (*mem) == 4)				\
			asm volatile (LOCK_PREFIX "incl %0"		\
				      : "=m" (*mem)			\
				      : "m" (*mem));			\
		else							\
			asm volatile (LOCK_PREFIX "incq %q0"		\
				      : "=m" (*mem)			\
				      : "m" (*mem));			\
	} while (0)

#define atomic_increment_and_test(mem)					\
	({ unsigned char __result;					\
		if (sizeof (*mem) == 1)					\
			asm volatile (LOCK_PREFIX "incb %b0; sete %1"	\
				      : "=m" (*mem), "=qm" (__result)	\
				      : "m" (*mem));			\
		else if (sizeof (*mem) == 2)				\
			asm volatile (LOCK_PREFIX "incw %w0; sete %1"	\
				      : "=m" (*mem), "=qm" (__result)	\
				      : "m" (*mem));			\
		else if (sizeof (*mem) == 4)				\
			asm volatile (LOCK_PREFIX "incl %0; sete %1"	\
				      : "=m" (*mem), "=qm" (__result)	\
				      : "m" (*mem));			\
		else							\
			asm volatile (LOCK_PREFIX "incq %q0; sete %1"	\
				      : "=m" (*mem), "=qm" (__result)	\
				      : "m" (*mem));			\
		__result; })

#define atomic_decrement(mem)						\
	do {								\
		if (sizeof (*mem) == 1)					\
			asm volatile (LOCK_PREFIX "decb %b0"		\
				      : "=m" (*mem)			\
				      : "m" (*mem));			\
		else if (sizeof (*mem) == 2)				\
			asm volatile (LOCK_PREFIX "decw %w0"		\
				      : "=m" (*mem)			\
				      : "m" (*mem));			\
		else if (sizeof (*mem) == 4)				\
			asm volatile (LOCK_PREFIX "decl %0"		\
				      : "=m" (*mem)			\
				      : "m" (*mem));			\
		else							\
			asm volatile (LOCK_PREFIX "decq %q0"		\
				      : "=m" (*mem)			\
				      : "m" (*mem));			\
	} while (0)

#define atomic_decrement_and_test(mem)					\
	({ unsigned char __result;					\
		if (sizeof (*mem) == 1)					\
			asm volatile (LOCK_PREFIX "decb %b0; sete %1"	\
				      : "=m" (*mem), "=qm" (__result)	\
				      : "m" (*mem));			\
		else if (sizeof (*mem) == 2)				\
			asm volatile (LOCK_PREFIX "decw %w0; sete %1"	\
				      : "=m" (*mem), "=qm" (__result)	\
				      : "m" (*mem));			\
		else if (sizeof (*mem) == 4)				\
			asm volatile (LOCK_PREFIX "decl %0; sete %1"	\
				      : "=m" (*mem), "=qm" (__result)	\
				      : "m" (*mem));			\
		else							\
			asm volatile (LOCK_PREFIX "decq %q0; sete %1"	\
				      : "=m" (*mem), "=qm" (__result)	\
				      : "m" (*mem));			\
		__result; })

#define atomic_bit_set(mem, bit)					\
	do {								\
		if (sizeof (*mem) == 1)					\
			asm volatile (LOCK_PREFIX "orb %b2, %0"		\
				      : "=m" (*mem)			\
				      : "m" (*mem), "iq" (1L << (bit))); \
		else if (sizeof (*mem) == 2)				\
			asm volatile (LOCK_PREFIX "orw %w2, %0"		\
				      : "=m" (*mem)			\
				      : "m" (*mem), "ir" (1L << (bit))); \
		else if (sizeof (*mem) == 4)				\
			asm volatile (LOCK_PREFIX "orl %2, %0"		\
				      : "=m" (*mem)			\
				      : "m" (*mem), "ir" (1L << (bit))); \
		else if (__builtin_constant_p (bit) && (bit) < 32)	\
			asm volatile (LOCK_PREFIX "orq %2, %0"		\
				      : "=m" (*mem)			\
				      : "m" (*mem), "i" (1L << (bit)));	\
		else							\
			asm volatile (LOCK_PREFIX "orq %q2, %0"		\
				      : "=m" (*mem)			\
				      : "m" (*mem), "r" (1UL << (bit))); \
	} while (0)

#define atomic_bit_test_set(mem, bit)					\
	({ unsigned char __result;					\
		if (sizeof (*mem) == 1)					\
			asm volatile (LOCK_PREFIX "btsb %3, %1; setc %0" \
				      : "=q" (__result), "=m" (*mem)	\
				      : "m" (*mem), "iq" (bit));	\
		else if (sizeof (*mem) == 2)				\
			asm volatile (LOCK_PREFIX "btsw %3, %1; setc %0" \
				      : "=q" (__result), "=m" (*mem)	\
				      : "m" (*mem), "ir" (bit));	\
		else if (sizeof (*mem) == 4)				\
			asm volatile (LOCK_PREFIX "btsl %3, %1; setc %0" \
				      : "=q" (__result), "=m" (*mem)	\
				      : "m" (*mem), "ir" (bit));	\
		else							\
			asm volatile (LOCK_PREFIX " btsq %3, %1; setc %0" \
				      : "=q" (__result), "=m" (*mem)	\
				      : "m" (*mem), "ir" (bit));	\
		__result; })
