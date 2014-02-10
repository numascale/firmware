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
#include <console.h>

extern "C" {
//	#include <com32.h>
	#include <syslinux/pxe.h>
	#include <consoles.h>
}

#include "syslinux.h"
#include "../bootloader.h"
#include "../library/base.h"

void Syslinux::get_hostname(void)
{
	char *dhcpdata;
	size_t dhcplen;
	assert(pxe_get_cached_info(PXENV_PACKET_TYPE_DHCP_ACK, (void **)&dhcpdata, &dhcplen) == 0);

	/* Save MyIP for later (in udp_open) */
	ip.s_addr = ((pxe_bootp_t *)dhcpdata)->yip;
	memcpy(mac, ((pxe_bootp_t *)dhcpdata)->CAddr, sizeof(mac));

	/* Skip standard fields, as hostname is an option */
	unsigned int offset = 4 + offsetof(pxe_bootp_t, vendor.d);

	while (offset < dhcplen) {
		int code = dhcpdata[offset];
		int len = dhcpdata[offset + 1];

		/* Sanity-check length */
		if (len == 0)
			break;

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
		break;
	}
}

Syslinux::Syslinux(void)
{
	/* Ensure console drains before opening */
	udelay(100000);
	console_ansi_std();
	get_hostname();
}

char *Syslinux::read_file(const char *filename, int *const len)
{
	com32sys_t inargs, outargs;
	char *buf = (char *)lmalloc(strlen(filename) + 1);
	strcpy(buf, filename);

	printf("Opening %s...", filename);
	memset(&inargs, 0, sizeof inargs);
	inargs.eax.w[0] = 0x0006; /* Open file */
	inargs.esi.w[0] = OFFS(buf);
	inargs.es = SEG(buf);
	__intcall(0x22, &inargs, &outargs);
	lfree(buf);

	int fd = outargs.esi.w[0];
	*len = outargs.eax.l;
	int bsize = outargs.ecx.w[0];

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

void Syslinux::exec(const char *label)
{
	com32sys_t rm;

	strcpy((char *)__com32.cs_bounce, label);
	rm.eax.w[0] = 0x0003;
	rm.ebx.w[0] = OFFS(__com32.cs_bounce);
	rm.es = SEG(__com32.cs_bounce);

	__intcall(0x22, &rm, NULL);
}
