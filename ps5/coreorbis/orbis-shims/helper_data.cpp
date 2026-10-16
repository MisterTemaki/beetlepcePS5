// Beetle PCE PS5: BeetlePCEPS5-helper.elf built into the app (ProsperoJailbreak.h). Only the native eboot links
// this file; the Makefile passes HELPER_ELF, the helper payload's path.
//
// SPDX-License-Identifier: MIT

#include "ProsperoJailbreak.h"

__asm__(".section .rodata\n"
		".balign 16\n"
		".global pceps5_helper_elf_begin\n"
		"pceps5_helper_elf_begin:\n"
		".incbin \"" HELPER_ELF "\"\n"
		".global pceps5_helper_elf_end\n"
		"pceps5_helper_elf_end:\n"
		".previous\n");
extern "C" const unsigned char pceps5_helper_elf_begin[];
extern "C" const unsigned char pceps5_helper_elf_end[];

namespace jailbreak
{
Blob EmbeddedHelper()
{
	return {pceps5_helper_elf_begin, size_t(pceps5_helper_elf_end - pceps5_helper_elf_begin)};
}
} // namespace jailbreak
