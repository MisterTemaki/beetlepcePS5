// Beetle PCE PS5: the dashboard app built into BeetlePCEPS5.elf (ProsperoInstall.h).
//
// The Makefile passes the five files' paths: eboot.bin (the launcher, which itself carries the emulator),
// sce_sys/param.json, sce_sys/icon0.png (the Beetle PCE PS5 icon, ps5/app/sce_sys/icon0.png),
// sce_sys/pic0.dds + pic1.dds (the home screen backgrounds) and sce_module/libc.prx (the C runtime module a native title carries, as PS5SX2's app folder). Only the final BeetlePCEPS5.elf links this file.
//
// SPDX-License-Identifier: MIT

#include "ProsperoInstall.h"

#define PCEPS5_INCBIN(sym, path)                                                                                  \
	__asm__(".section .rodata\n"                                                                               \
			".balign 16\n"                                                                                     \
			".global " #sym "_begin\n" #sym "_begin:\n"                                                        \
			".incbin \"" path "\"\n"                                                                           \
			".global " #sym "_end\n" #sym "_end:\n"                                                            \
			".previous\n");                                                                                    \
	extern "C" const unsigned char sym##_begin[];                                                              \
	extern "C" const unsigned char sym##_end[];

PCEPS5_INCBIN(pceps5_app_eboot, APP_EBOOT)
PCEPS5_INCBIN(pceps5_app_param, APP_PARAM)
PCEPS5_INCBIN(pceps5_app_icon0, APP_ICON0)
PCEPS5_INCBIN(pceps5_app_pic0, APP_PIC0)
#ifndef APP_PIC1_IS_PIC0
PCEPS5_INCBIN(pceps5_app_pic1, APP_PIC1)
#else
// the two backgrounds are the same image (the Makefile compared them): built in once, which keeps the payload
// under the 30 MiB the PS5 Payload Manager / the ELF loader take
#define pceps5_app_pic1_begin pceps5_app_pic0_begin
#define pceps5_app_pic1_end pceps5_app_pic0_end
#endif
PCEPS5_INCBIN(pceps5_app_libc, APP_LIBC)

const EmbeddedAppFile* EmbeddedAppFiles()
{
	static const EmbeddedAppFile files[] = {
		{"sce_sys/param.json", pceps5_app_param_begin, pceps5_app_param_end},
		{"sce_sys/icon0.png", pceps5_app_icon0_begin, pceps5_app_icon0_end},
		{"sce_sys/pic0.dds", pceps5_app_pic0_begin, pceps5_app_pic0_end},
		{"sce_sys/pic1.dds", pceps5_app_pic1_begin, pceps5_app_pic1_end},
		{"sce_module/libc.prx", pceps5_app_libc_begin, pceps5_app_libc_end},
		{"eboot.bin", pceps5_app_eboot_begin, pceps5_app_eboot_end},
		{nullptr, nullptr, nullptr},
	};
	return files;
}
