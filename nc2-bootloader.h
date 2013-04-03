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

#ifndef __NC2_BOOTLOADER_H
#define __NC2_BOOTLOADER_H 1

#include <inttypes.h>
#include <stdbool.h>

/* cpu_relax() defined here */
#include "nc2-access.h"

#define assert(cond) do { if (!(cond)) {				\
	printf("Error: assertion '%s' failed in %s at %s:%d\n",		\
	    #cond, __FUNCTION__, __FILE__, __LINE__);			\
	while (1) cpu_relax();						\
    } } while (0)

#define assertf(cond, format, ...) do { if (!(cond)) {			\
	printf("Error: ");						\
	printf(format, __VA_ARGS__);					\
	while (1) cpu_relax();						\
    } } while(0)

#define fatal(format, ...) do {						\
	printf("Error: ");						\
	printf(format, __VA_ARGS__);					\
	while (1) cpu_relax();						\
   } while (0)

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

/* Constants found in initialization */
extern int family;
extern int nc2_ht_id;
extern uint32_t nc2_chip_rev;
extern uint32_t tsc_mhz;
extern struct in_addr myip;
extern char *hostname;

extern char *next_label;
extern int ht_force_ganged;
extern int ht_200mhz_only;
extern int ht_8bit_only;
extern bool boot_wait;
extern int verbose;

void set_cf8extcfg_enable(const int ht);
void udelay(const uint32_t usecs);
void wait_key(void);
int parse_cmdline(const char *cmdline);
int ht_fabric_fixup(uint32_t *p_chip_rev);

#endif
