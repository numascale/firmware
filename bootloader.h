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

#include "platform/config.h"
#include "platform/syslinux.h"
#include "platform/options.h"
#include "platform/e820.h"
#include "library/access.h"
#include "opteron/opteron.h"
#include "numachip2/numachip.h"

class Node {
	sci_t sci;
public:
	int nopterons;
	Opteron *opterons[7];
	Numachip2 *numachip;

	Node(const sci_t _sci);
	/* Returns 0 on success */
	bool probe(void) {return 0;};
};

/* Global constants found in initialization */
extern Syslinux *syslinux;
extern Options *options;
extern Config *config;
extern Opteron *opteron;
extern Numachip2 *numachip;
extern E820 *e820;
extern Node *local_node;

void udelay(const uint32_t usecs);
void wait_key(void);
int smbios_parse(const char **biosver, const char **biosdate,
		 const char **sysmanuf, const char **sysproduct,
		 const char **boardmanuf, const char **boardproduct);

#endif
