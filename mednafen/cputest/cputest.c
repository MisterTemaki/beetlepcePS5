/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "cputest.h"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

static int flags, checked = 0;

#if ARCH_X86 && !defined(HAVE_X86_CPUID_TEST)
/* libretro builds carry no CPUID test: with ARCH_X86 the flags are those of the build's own target (-march), which
 * any CPU running it has anyway -- so OwlResampler takes its SSE / AVX paths instead of the plain C one. */
int ff_get_cpu_flags_x86(void)
{
   int f = CPUTEST_FLAG_CMOV;
#ifdef __MMX__
   f |= CPUTEST_FLAG_MMX;
#endif
#ifdef __SSE__
   f |= CPUTEST_FLAG_MMX2 | CPUTEST_FLAG_SSE;
#endif
#ifdef __SSE2__
   f |= CPUTEST_FLAG_SSE2;
#endif
#ifdef __SSE3__
   f |= CPUTEST_FLAG_SSE3;
#endif
#ifdef __SSSE3__
   f |= CPUTEST_FLAG_SSSE3;
#endif
#ifdef __SSE4_1__
   f |= CPUTEST_FLAG_SSE4;
#endif
#ifdef __SSE4_2__
   f |= CPUTEST_FLAG_SSE42;
#endif
#ifdef __AVX__
   f |= CPUTEST_FLAG_AVX;
#endif
   return f;
}
#endif


void cputest_force_flags(int arg)
{
    flags   = arg;
    checked = 1;
}

int cputest_get_flags(void)
{
    if (checked)
        return flags;

//    if (ARCH_ARM) flags = ff_get_cpu_flags_arm();
#if ARCH_POWERPC
    flags = ff_get_cpu_flags_ppc();
#endif

#if ARCH_X86
    flags = ff_get_cpu_flags_x86();
#endif

    checked = 1;
    return flags;
}
