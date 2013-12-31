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

#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#include <inttypes.h>
#include <stdbool.h>

#include "../library/access.h"

/* ASCII-Art */
#define COL_DEFAULT   "\033[0m"
#define COL_RED       "\033[31m"
#define COL_YELLOW    "\033[33m"
#define CLEAR         "\033\143"
#define BANNER        "\033[1m"

#define assert(cond) do { if (!(cond)) {				\
	printf(COL_RED "Error: assertion '%s' failed in %s at %s:%d\n",	\
	    #cond, __FUNCTION__, __FILE__, __LINE__);			\
	printf(COL_DEFAULT);						\
	while (1) cpu_relax();						\
    } } while (0)

#define assertf(cond, format, args...) do { if (!(cond)) {		\
	printf(COL_RED "Error: ");					\
	printf(format, ## args);					\
	printf(COL_DEFAULT);						\
	while (1) cpu_relax();						\
    } } while(0)

#define fatal(format, args...) do {					\
	printf(COL_RED "Error: ");					\
	printf(format, ## args);					\
	printf(COL_DEFAULT);						\
	while (1) cpu_relax();						\
   } while (0)

#define warning(format, args...) do {					\
	printf(COL_YELLOW "Warning: ");					\
	printf(format, ## args);					\
	printf(COL_DEFAULT "\n");					\
   } while (0)

#define error(format, args...) do {					\
	printf(COL_RED "Error: ");					\
	printf(format, ## args);					\
	printf(COL_DEFAULT "\n");					\
   } while (0)

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

/* Global constants found in initialization */
extern int family;
extern uint32_t southbridge_id;
extern int nc2_ht_id;
extern uint32_t nc2_chip_rev;
extern uint32_t tsc_mhz;
extern struct in_addr myip;
extern char *hostname;
extern char nc2_card_type[16];

/* Options */
extern const char *next_label;
extern int ht_force_ganged;
extern int ht_200mhz_only;
extern int ht_8bit_only;
extern bool boot_wait;
extern bool handover_acpi;
extern int verbose;

void udelay(const uint32_t usecs);
void wait_key(void);
void parse_cmdline(const int argc, const char *argv[]);
int ht_fabric_fixup(uint32_t *p_chip_rev);
int i2c_master_seq_read(const uint8_t device_adr, const uint8_t byte_addr, const int len, uint8_t *data);
int spi_master_read(const uint16_t addr, const int len, uint8_t *data);
int smbios_parse(const char **biosver, const char **biosdate,
		 const char **sysmanuf, const char **sysproduct,
		 const char **boardmanuf, const char **boardproduct);

#endif
