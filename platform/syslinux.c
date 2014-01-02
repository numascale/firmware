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

#include <stdio.h>
#include <string.h>

extern "C" {
	#include <com32.h>
}

#include "syslinux.h"
#include "../library/base.h"

char *Syslinux::read_file(const char *filename, int *const len)
{
	static com32sys_t inargs, outargs;
	int fd, bsize;

	char *buf = (char *)lmalloc(strlen(filename) + 1);
	strcpy(buf, filename);

	printf("Opening %s...", filename);
	memset(&inargs, 0, sizeof inargs);
	inargs.eax.w[0] = 0x0006; /* Open file */
	inargs.esi.w[0] = OFFS(buf);
	inargs.es = SEG(buf);
	__intcall(0x22, &inargs, &outargs);
	lfree(buf);

	fd = outargs.esi.w[0];
	*len = outargs.eax.l;
	bsize = outargs.ecx.w[0];

	if (!fd || *len < 0) {
		*len = 0;
		printf("not found\n");
		return NULL;
	}

	buf = (char *)lzalloc(roundup(*len, bsize));
	lassert(buf);

	memset(&inargs, 0, sizeof inargs);
	inargs.eax.w[0] = 0x0007; /* Read file */
	inargs.esi.w[0] = fd;
	inargs.ecx.w[0] = (*len / bsize) + 1;
	inargs.ebx.w[0] = OFFS(buf);
	inargs.es = SEG(buf);
	__intcall(0x22, &inargs, &outargs);
	*len = outargs.ecx.l;

	memset(&inargs, 0, sizeof inargs);
	inargs.eax.w[0] = 0x0008; /* Close file */
	inargs.esi.w[0] = fd;
	__intcall(0x22, &inargs, NULL);

	printf("done\n");
	return buf;
}

