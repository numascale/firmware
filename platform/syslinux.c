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
	#include <syslinux/pxe.h>
	#include <syslinux/loadfile.h>
	#include <syslinux/boot.h>
	#include <consoles.h>
}

#include "os.h"
#include "../bootloader.h"
#include "../library/base.h"
#include "../library/utils.h"

void OS::get_hostname(void)
{
	char *dhcpdata;
	size_t dhcplen;
	xassert(pxe_get_cached_info(PXENV_PACKET_TYPE_DHCP_ACK, (void **)&dhcpdata, &dhcplen) == 0);

	/* Save MyIP for later (in udp_open) */
	ip.s_addr = ((pxe_bootp_t *)dhcpdata)->yip;
	memcpy(mac, ((pxe_bootp_t *)dhcpdata)->CAddr, sizeof(mac));

	/* Skip standard fields, as hostname is an option */
	unsigned offset = 4 + offsetof(pxe_bootp_t, vendor.d);

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
		xassert(hostname);
		break;
	}
}

OS::OS(void): ent(0), hostname(NULL)
{
	/* Ensure console drains before opening */
	lib::udelay(100000);
	console_ansi_std();
	get_hostname();
}

char *OS::read_file(const char *filename, size_t *const len)
{
	char *buf;
	printf("Config %s", filename);
	int rv = loadfile(filename, (void **)&buf, len);
	assertf(!rv && *len > 0, "Failed to open file");

	return buf;
}

void OS::exec(const char *label)
{
	int rv = syslinux_run_command(label);
	xassert(!rv);
}

void OS::memmap_start(void)
{
	memset(&state, 0, sizeof(state));
}

bool OS::memmap_entry(uint64_t *base, uint64_t *length, uint64_t *type)
{
	state.eax.l = 0xe820;
	state.edx.l = STR_DW_N("SMAP");
	state.ecx.l = sizeof(*ent);
	state.edi.w[0] = OFFS(ent);
	state.es = SEG(ent);

	__intcall(0x15, &state, &state);
	xassert(state.eax.l == STR_DW_N("SMAP"));
	xassert(ent->length);

	*base = ent->base;
	*length = ent->length;
	*type = ent->type;

	return state.ebx.l > 0;
}

void OS::cleanup(void)
{
	static com32sys_t rm;
	rm.eax.w[0] = 0x000C;
	rm.edx.w[0] = 0x0000;
	printf("Unloading bootloader stack...");
	__intcall(0x22, &rm, NULL);
	printf("done\n");
}
