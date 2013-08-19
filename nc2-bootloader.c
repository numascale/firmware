/*
 * Copyright (C) 2008-2012 Numascale AS, support@numascale.com
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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <console.h>
#include <com32.h>
#include <inttypes.h>
#include <syslinux/pxe.h>
#include <sys/io.h>

#include "nc2-defs.h"
#include "nc2-bootloader.h"
#include "nc2-access.h"
#include "nc2-acpi.h"
#include "nc2-spd.h"
#include "nc2-version.h"

/* Global constants found in initialization */
int family = 0;
uint32_t southbridge_id = -1;
int nc2_ht_id = -1; /* HT id of NC-II */
uint32_t nc2_chip_rev = -1;
uint32_t tsc_mhz = 0;
struct in_addr myip = {0xffffffff};
char *hostname = NULL;
char nc2_card_type[16];

static ddr2_spd_eeprom_t spd_eeproms[2]; /* 0 - MCTag, 1 - CData */

static void constants(void)
{
	uint32_t val;
	int fam, model, stepping;

	val = cht_readl(0, FUNC3_MISC, 0xfc);
	fam = ((val >> 20) & 0xf) + ((val >> 8) & 0xf);
	model = ((val >> 12) & 0xf0) | ((val >> 4) & 0xf);
	stepping = val & 0xf;
	family = (fam << 16) | (model << 8) | stepping;

	if (family >= 0x15) {
		uint32_t val = cht_readl(0, FUNC5_EXTD, 0x160);
		tsc_mhz = 200 * (((val >> 1) & 0x1f) + 4) / (1 + ((val >> 7) & 1));
	} else {
		uint32_t val = cht_readl(0, FUNC3_MISC, 0xd4);
		uint64_t val6 = rdmsr(0xc0010071);
		tsc_mhz = 200 * ((val & 0x1f) + 4) / (1 + ((val6 >> 22) & 1));
	}

	printf("NB/TSC frequency is %dMHz\n", tsc_mhz);

	southbridge_id = extpci_readl(0, 0x14, 0, 0);

	if (southbridge_id != VENDEV_SP5100)
		warning("Unable to disable SMI due to unknown southbridge 0x%08x; this may cause hangs", southbridge_id);
}

static void set_cf8extcfg_enable(void)
{
	uint64_t val = rdmsr(MSR_NB_CFG);
	wrmsr(MSR_NB_CFG, val | (1ULL << 46));
}

static void set_cf8extcfg_disable(void)
{
	uint64_t val = rdmsr(MSR_NB_CFG);
	wrmsr(MSR_NB_CFG, val & ~(1ULL << 46));
}

static void set_wrap32_disable(void)
{
	uint64_t val = rdmsr(MSR_HWCR);
	wrmsr(MSR_HWCR, val | (1ULL << 17));
}

static void set_wrap32_enable(void)
{
	uint64_t val = rdmsr(MSR_HWCR);
	wrmsr(MSR_HWCR, val & ~(1ULL << 17));
}

static int check_api_version(void)
{
	static com32sys_t inargs, outargs;
	int major, minor;
	inargs.eax.w[0] = 0x0001;
	__intcall(0x22, &inargs, &outargs);
	major = outargs.ecx.b[1];
	minor = outargs.ecx.b[0];
	printf("Detected SYSLINUX API version %d.%02d\n", major, minor);

	if ((major * 100 + minor) < 372) {
		printf("Error: SYSLINUX API version >= 3.72 is required\n");
		return -1;
	}

	return 0;
}

static void start_user_os(void)
{
	static com32sys_t rm;
	/* Disable CF8 extended access */
	set_cf8extcfg_disable();
	/* Restore 32-bit only access */
	set_wrap32_enable();
	strcpy(__com32.cs_bounce, next_label);
	rm.eax.w[0] = 0x0003;
	rm.ebx.w[0] = OFFS(__com32.cs_bounce);
	rm.es = SEG(__com32.cs_bounce);
	printf("Unification succeeded; loading %s...\n", next_label);

	if (boot_wait)
		wait_key();

	__intcall(0x22, &rm, NULL);
}

static void get_hostname(void)
{
	int sts;
	char *dhcpdata;
	size_t dhcplen;

	if ((sts = pxe_get_cached_info(PXENV_PACKET_TYPE_DHCP_ACK, (void **)&dhcpdata, &dhcplen)) != 0) {
		printf("pxe_get_cached_info() returned status : %d\n", sts);
		return;
	}

	/* Save MyIP for later (in udp_open) */
	myip.s_addr = ((pxe_bootp_t *)dhcpdata)->yip;
	printf("My IP address is %s\n", inet_ntoa(myip));

	/* Skip standard fields, as hostname is an option */
	unsigned int offset = 4 + offsetof(pxe_bootp_t, vendor.d);

	while (offset < dhcplen) {
		int code = dhcpdata[offset];
		int len = dhcpdata[offset + 1];

		/* Sanity-check length */
		if (len == 0)
			return;

		/* Skip non-hostname options */
		if (code != 12) {
			offset += 2 + len;
			continue;
		}

		/* Sanity-check length */
		if ((offset + len) > dhcplen)
			break;

		/* Create a private copy */
		hostname = strndup(&dhcpdata[offset + 2], len);
		assert(hostname);
		printf("Hostname is %s\n", hostname);
		return;
	}

	hostname = NULL;
}

static void stop_acpi(void)
{
	printf("ACPI handoff: ");
	acpi_sdt_p fadt = find_sdt("FACP");

	if (!fadt) {
		printf("ACPI FACP table not found\n");
		return;
	}

	uint32_t smi_cmd = *(uint32_t *)&fadt->data[48 - 36];
	uint8_t acpi_enable = fadt->data[52 - 36];

	if (!smi_cmd || !acpi_enable) {
		printf("legacy support not enabled\n");
		return;
	}

	uint32_t acpipm1cntblk = *(uint32_t *)&fadt->data[64 - 36];
	uint16_t sci_en = inb(acpipm1cntblk);
	outb(acpi_enable, smi_cmd);
	int limit = 100;

	do {
		udelay(100);
		sci_en = inb(acpipm1cntblk);

		if ((sci_en & 1) == 1) {
			printf("legacy handoff succeeded\n");
			return;
		}
	} while (--limit);

	printf("ACPI handoff timed out\n");
}

static int read_spd_info(int spd_no, ddr2_spd_eeprom_t *spd)
{
	const uint8_t spd_device_adr = 0x50 + spd_no;

	if (i2c_master_seq_read(spd_device_adr, 0x00, sizeof(ddr2_spd_eeprom_t), (uint8_t *)spd) < 0)
		return -1;

	/* Check SPD validity */
	if (nc2_ddr2_spd_check(spd) < 0) {
		error("Couldn't find a valid DDR2 SDRAM memory module on DIMM%d", spd_no);
		return -1;
	}

	uint8_t addr_bits = (spd->nrow_addr & 0xf) + (spd->ncol_addr & 0xf) + (spd->mod_ranks & 1) + ((spd->nbanks == 8) ? 3 : 2);

	printf("DIMM%d is a x%d %dMB %s-rank module (%s)\n", spd_no,
	       spd->primw, 1 << (addr_bits - 17),
	       (spd->mod_ranks & 1) ? "dual" : "single",
	       spd->mpart[0] ? (char *)spd->mpart : "unknown");
	
	return 0;
}

static void platform_quirks(void)
{
	const char *biosver = NULL, *biosdate = NULL;
	const char *sysmanuf = NULL, *sysproduct = NULL;
	const char *boardmanuf = NULL, *boardproduct = NULL;

	smbios_parse(&biosver, &biosdate, &sysmanuf, &sysproduct, &boardmanuf, &boardproduct);
	assert(biosver && biosdate && sysmanuf && sysproduct && boardmanuf && boardproduct);

	printf("Platform is %s %s (%s %s) with BIOS %s %s", sysmanuf, sysproduct, boardmanuf, boardproduct, biosver, biosdate);

	/* Skip if already set */
	if (!handover_acpi) {
		/* Systems where ACPI must be handed off early */
		const char *acpi_blacklist[] = {"H8QGL", NULL};

		for (unsigned int i = 0; i < (sizeof acpi_blacklist / sizeof acpi_blacklist[0]); i++) {
			if (!strcmp(boardproduct, acpi_blacklist[i])) {
				printf(" (blacklisted)");
				handover_acpi = 1;
				break;
			}
		}
	}

	printf("\n");
}

static uint32_t identify_eeprom(char p_type[16])
{
	uint8_t p_uuid[4];

	/* Read print type */
	(void)spi_master_read(0xffc0, 16, (uint8_t *)p_type);
	p_type[15] = '\0';

	/* Read UUID */
	(void)spi_master_read(0xfffc, 4, p_uuid);
	return *((uint32_t *)p_uuid);
}

static int nc2_start(const char *cmdline)
{	
	if (parse_cmdline(cmdline) < 0)
		return ERR_GENERAL_NC_START_ERROR;

	constants();
	get_hostname();

	platform_quirks();
	
	/* SMI often assumes HT nodes are Northbridges, so handover early */
	if (handover_acpi)
		stop_acpi();

	nc2_ht_id = ht_fabric_fixup(&nc2_chip_rev);
	if (nc2_ht_id < 0) {
		printf("NumaChip-II not found\n");
		return ERR_MASTER_HT_ID;
	}
	printf("NumaChip-II incorporated as HT node %d\n", nc2_ht_id);

	uint32_t uuid = identify_eeprom(nc2_card_type);
	printf("UUID: %08X, TYPE: %s\n", uuid, nc2_card_type);
	
	/* Read the SPD info from our DIMMs to see if they are supported */
	for (int i = 0; i < 2; i++) {
		if (read_spd_info(i, &spd_eeproms[i]) < 0)
			return ERR_GENERAL_NC_START_ERROR;
	}

	start_user_os();

	// XXX: Never reached
	return 0;
}

void udelay(const uint32_t usecs)
{
	uint64_t limit = rdtscll() + (uint64_t)usecs * tsc_mhz;

	while (rdtscll() < limit)
		cpu_relax();
}

void wait_key(void)
{
	char ch;
	printf("... ( press any key to continue ) ... ");

	while (fread(&ch, 1, 1, stdin) == 0)
		;

	printf("\n");
}

int main(void)
{
	int ret;
	openconsole(&dev_rawcon_r, &dev_stdcon_w);
	printf(CLEAR BANNER "NumaConnect system unification module " VER COL_DEFAULT "\n");

	if (check_api_version() < 0)
		return ERR_API_VERSION;

	/* Enable CF8 extended access, we use it extensively */
	set_cf8extcfg_enable();

	/* Disable 32-bit address wrapping to allow 64-bit access in 32-bit code */
	set_wrap32_disable();

	ret = nc2_start(__com32.cs_cmdline);

	if (ret < 0) {
		printf("Error: nc2_start() failed with error code %d\n", ret);
//		wait_key();
		reset_cf9(0xa, 0);
	}

	/* Disable CF8 extended access */
	set_cf8extcfg_disable();

	/* Restore 32-bit only access */
	set_wrap32_enable();

	return ret;
}
