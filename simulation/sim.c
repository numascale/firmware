#include <string.h>

#include "../numachip2/numachip.h"
//#include "../library/base.h"
//#include "../library/access.h"
//#include "../platform/acpi.h"
#include "../platform/options.h"
#include "../platform/syslinux.h"
#include "../platform/e820.h"
#include "../platform/config.h"
//#include "../platform/acpi.h"
//#include "../opteron/msrs.h"
#include "../nodes.h"

Syslinux *syslinux;
Options *options;
Config *config;
E820 *e820;
Node *local_node;
//Node **nodes;
//ACPI *acpi;

void *zalloc(size_t size)
{
	void *addr = malloc(size);
	assert(addr);
	memset(addr, 0, size);
	return addr;
}

void lfree(void *addr)
{
	free(addr);
}

namespace lib
{
	uint8_t pmio_read8(const uint16_t offset)
	{
		return 0x0;
	}

	void pmio_write8(const uint16_t offset, const uint8_t val)
	{
	}

	uint8_t mem_read8(const uint64_t addr)
	{
		return 0x0;
	}

	uint16_t mem_read16(const uint64_t addr)
	{
		return 0x0;
	}

	uint32_t mem_read32(const uint64_t addr)
	{
		return 0x0;
	}

	uint64_t mem_read64(const uint64_t addr)
	{
		return 0x0;
	}

	void mem_write8(const uint64_t addr, const uint8_t val)
	{
	}

	void mem_write16(const uint64_t addr, const uint16_t val)
	{
	}

	void mem_write32(const uint64_t addr, const uint32_t val)
	{
	}

	void mem_write64(const uint64_t addr, const uint64_t val)
	{
	}

	uint8_t mcfg_read8(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		return 0x0;
	}

	uint16_t mcfg_read16(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		return 0x0;
	}

	uint32_t mcfg_read32(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		return 0x0;
	}

	uint64_t mcfg_read64(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg)
	{
		return 0x0;
	}

	void mcfg_write8(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint8_t val)
	{
	}

	void mcfg_write16(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint16_t val)
	{
	}

	void mcfg_write32(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint32_t val)
	{
	}

	void mcfg_write64(const sci_t sci, const uint8_t bus, const uint8_t dev, const uint8_t func, const uint16_t reg, const uint64_t val)
	{
	}
}

int main(const int argc, const char *argv[])
{
	options = new Options(argc, argv); // needed before first PCI access

	Numachip2 *numachips[2];
	numachips[0] = new Numachip2(0x000);
	numachips[1] = new Numachip2(0x001);

	delete numachips[0];
	delete numachips[1];

	return 0;
}
