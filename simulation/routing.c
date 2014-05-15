#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef uint16_t sci_t;
typedef uint16_t reg_t;

static sci_t sci;
static uint32_t registers[0x4000];
static const reg_t SIU_XBAR_LOW      = 0x2200;
static const reg_t SIU_XBAR_CHUNK    = 0x22c0;

static void write32(const uint16_t reg, const uint32_t val)
{
	registers[reg] = val;
}

static uint32_t read32(const uint16_t reg)
{
	return registers[reg];
}

static void fatal(const char *msg)
{
	printf("Error: %s\n", msg);
	exit(1);
}

// on LC5 'in', route packets to SCI 'dest' via LC 'out'
static void route(const uint8_t in, const sci_t dest, const uint8_t out)
{
	// sci[3:0] corresponds to bit offset
	// sci[7:4] corresponds to register offset
	// sci[11:8] corresponds to chunk offset

	const int regbase = 0x2200 + (in ? 0x500 : 0) + in * 0x100;
	const int regoffset = ((dest >> 4) & 0xf) << 2;
	const int chunk = dest >> 8;
	const int bitoffset = dest & 0xf;

	write32(regbase + 0xc0, chunk);

	for (int bit = 0; bit < 3; bit++) {
		uint32_t val = read32(regbase + regoffset + bit * 0x40);
		val &= ~(1 << bitoffset);
		val |= ((out >> bit) & 1) << bitoffset;
		write32(regbase + regoffset + bit * 0x40, val);
	}
}

// 2200 SIU
// 2800 XA
// 2900 XB

static void fabric_routing(void)
{
	printf("Initialising XBar routing for SCI%03x\n", sci);

#ifdef TEST
	switch (sci) {
	case 0x000:
		write32(SIU_XBAR_CHUNK, 0);
		write32(SIU_XBAR_LOW, 2);
		write32(0x2240, 0);
		write32(0x2280, 0);
		write32(0x28c0, 0);
		write32(0x2800, 2);
		write32(0x2840, 0);
		write32(0x2880, 0);
		write32(0x29c0, 0);
		write32(0x2900, 2);
		write32(0x2940, 0);
		write32(0x2980, 0);
		break;
	case 0x001:
		write32(SIU_XBAR_CHUNK, 0);
		write32(SIU_XBAR_LOW, 1);
		write32(0x2240, 0);
		write32(0x2280, 0);
		write32(0x28c0, 0);
		write32(0x2800, 1);
		write32(0x2840, 0);
		write32(0x2880, 0);
		write32(0x29c0, 0);
		write32(0x2900, 1);
		write32(0x2940, 0);
		write32(0x2980, 0);
		break;
	default:
		fatal("unexpected");
	}
#else
	switch (sci) {
	case 0x000:
		route(0, 0x001, 1);
		route(1, 0x001, 1);
		route(2, 0x001, 1);
		break;
	case 0x001:
		route(0, 0x000, 1);
		route(1, 0x000, 1);
		route(2, 0x000, 1);
		break;
	default:
		fatal("unexpected");
	}
#endif
}

static void fabric_print(void)
{
	for (uint16_t reg = 0; reg < sizeof(registers) / sizeof(registers[0]); reg++)
		if (read32(reg))
			printf("%04x: %08x\n", reg, read32(reg));
}

int main(void)
{
	for (sci = 0; sci < 2; sci++) {
		memset(registers, 0, sizeof(registers));
		fabric_routing();
		fabric_print();
	}

	return 0;
}
