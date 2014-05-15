#include <stdlib.h>
#include <string.h>

#include "../platform/syslinux.h"
#include "../platform/e820.h"

Syslinux::Syslinux(void)
{
}

char *Syslinux::read_file(const char *filename, int *const len)
{
	return 0;
}

void Syslinux::exec(const char *label)
{
	exit(0);
}

void Syslinux::e820_first(uint64_t *base, uint64_t *length, uint64_t *type)
{
	*base = 0x0;
	*length = 128ULL << 30;
	*type = E820::RAM;
}

bool Syslinux::e820_next(uint64_t *base, uint64_t *length, uint64_t *type)
{
	return 0;
}
