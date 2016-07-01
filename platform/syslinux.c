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
	openconsole(&dev_rawcon_r, &dev_stdcon_w);
	get_hostname();
}

static int pxeapi_call(int func, const uint8_t *buf)
{
	static com32sys_t inargs, outargs;
	inargs.eax.w[0] = 0x0009; /* Call PXE Stack */
	inargs.ebx.w[0] = func; /* PXE function number */
	inargs.edi.w[0] = OFFS(buf);
	inargs.es = SEG(buf);
	__intcall(0x22, &inargs, &outargs);
	return outargs.eax.w[0] == PXENV_EXIT_SUCCESS;
}

void OS::udp_open(void)
{
	t_PXENV_TFTP_CLOSE *tftp_close_param = (t_PXENV_TFTP_CLOSE *)lzalloc(sizeof(t_PXENV_TFTP_CLOSE));
	xassert(tftp_close_param);
	pxeapi_call(PXENV_TFTP_CLOSE, (uint8_t *)tftp_close_param);
//	printf("TFTP close returns: %d\n", tftp_close_param->Status);
	lfree(tftp_close_param);

	t_PXENV_UDP_OPEN *pxe_open_param = (t_PXENV_UDP_OPEN *)lzalloc(sizeof(t_PXENV_UDP_OPEN));
	xassert(pxe_open_param);
	pxe_open_param->src_ip = os->ip.s_addr;
	pxeapi_call(PXENV_UDP_OPEN, (uint8_t *)pxe_open_param);
//	printf("PXE UDP open returns: %d\n", pxe_open_param->status);
	lfree(pxe_open_param);
}

void OS::udp_write(const void *buf, const size_t len, uint32_t to_ip)
{
	t_PXENV_UDP_WRITE *pxe_write_param = (t_PXENV_UDP_WRITE *)lzalloc(sizeof(t_PXENV_UDP_WRITE) + len);
	xassert(pxe_write_param);
	char *buf_reloc = (char *)pxe_write_param + sizeof(*pxe_write_param);

	pxe_write_param->ip = to_ip;
	pxe_write_param->src_port = htons(UDP_PORT_NO);
	pxe_write_param->dst_port = htons(UDP_PORT_NO);
	pxe_write_param->buffer.seg = SEG(buf_reloc);
	pxe_write_param->buffer.offs = OFFS(buf_reloc);
	pxe_write_param->buffer_size = len;

	memcpy(buf_reloc, buf, len);
	pxeapi_call(PXENV_UDP_WRITE, (uint8_t *)pxe_write_param);
	lfree(pxe_write_param);
}

int OS::udp_read(void *buf, const size_t len, uint32_t *from_ip)
{
	int ret = 0;

	t_PXENV_UDP_READ *pxe_read_param = (t_PXENV_UDP_READ *)lzalloc(sizeof(t_PXENV_UDP_READ) + len);
	xassert(pxe_read_param);
	char *buf_reloc = (char *)pxe_read_param + sizeof(*pxe_read_param);

	pxe_read_param->s_port = htons(UDP_PORT_NO);
	pxe_read_param->d_port = htons(UDP_PORT_NO);
	pxe_read_param->buffer.seg = SEG(buf_reloc);
	pxe_read_param->buffer.offs = OFFS(buf_reloc);
	pxe_read_param->buffer_size = len;
	pxeapi_call(PXENV_UDP_READ, (uint8_t *)pxe_read_param);

	if ((pxe_read_param->status == PXENV_STATUS_SUCCESS) &&
	    (pxe_read_param->s_port == htons(UDP_PORT_NO))) {
		memcpy(buf, buf_reloc, pxe_read_param->buffer_size);
		*from_ip = pxe_read_param->src_ip;
		ret = pxe_read_param->buffer_size;
	}

	lfree(pxe_read_param);
	return ret;
}

char *OS::read_file(const char *filename, size_t *const len)
{
	char *buf;
	if (loadfile(filename, (void **)&buf, len) < 0)
		return NULL;
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

	// detect if bootloader is called again
	if (!ent->length)
		fatal("Bootloader already executed; check boot configuration!");

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
	printf("Unloading bootloader stack");
	__intcall(0x22, &rm, NULL);
	printf("\n");
}
