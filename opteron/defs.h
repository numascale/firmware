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

#ifndef __DEFS_H
#define __DEFS_H

#define VENDEV_NC2      0x07001b47
#define VENDEV_SR5690	0x5a101002
#define VENDEV_SR5670	0x5a121002
#define VENDEV_SR5650	0x5a131002
#define VENDEV_MCP55	0x036910de

#define MSR_APIC_BAR    0x0000001b
#define MSR_FS_BASE     0xc0000100
#define MSR_SYSCFG      0xc0010010
#define MSR_HWCR        0xc0010015
#define MSR_TOPMEM      0xc001001a
#define MSR_TOPMEM2     0xc001001d
#define MSR_NB_CFG      0xc001001f
#define MSR_INT_HALT    0xc0010055
#define MSR_MCFG_BASE   0xc0010058
#define MSR_COFVID_STAT 0xc0010071
#define MSR_SMM_BASE    0xc0010111
#define MSR_OSVW_ID_LEN 0xc0010140
#define MSR_OSVW_STATUS 0xc0010141
#define MSR_NODE_ID     0xc001100c
#define MSR_LSCFG       0xc0011020
#define MSR_CU_CFG2     0xc001102a
#define MSR_CU_CFG3     0xc001102b

#define MSR_MTRR_PHYS_BASE0 0x00000200
#define MSR_MTRR_PHYS_BASE1 0x00000202
#define MSR_MTRR_PHYS_BASE2 0x00000204
#define MSR_MTRR_PHYS_BASE3 0x00000206
#define MSR_MTRR_PHYS_BASE4 0x00000208
#define MSR_MTRR_PHYS_BASE5 0x0000020a
#define MSR_MTRR_PHYS_BASE6 0x0000020c
#define MSR_MTRR_PHYS_BASE7 0x0000020e

#define MSR_MTRR_PHYS_MASK0 0x00000201
#define MSR_MTRR_PHYS_MASK1 0x00000203
#define MSR_MTRR_PHYS_MASK2 0x00000205
#define MSR_MTRR_PHYS_MASK3 0x00000207
#define MSR_MTRR_PHYS_MASK4 0x00000209
#define MSR_MTRR_PHYS_MASK5 0x0000020b
#define MSR_MTRR_PHYS_MASK6 0x0000020d
#define MSR_MTRR_PHYS_MASK7 0x0000020f

#define MSR_PERF_CTL0       0xc0010001
#define MSR_PERF_CTR0       0xc0010005

#define MSR_IORR_PHYS_BASE0 0xc0010016
#define MSR_IORR_PHYS_BASE1 0xc0010018
#define MSR_IORR_PHYS_MASK0 0xc0010017
#define MSR_IORR_PHYS_MASK1 0xc0010019

#define MSR_MTRR_FIX64K_00000 0x00000250
#define MSR_MTRR_FIX16K_80000 0x00000258
#define MSR_MTRR_FIX16K_A0000 0x00000259
#define MSR_MTRR_FIX4K_C0000 0x00000268
#define MSR_MTRR_FIX4K_C8000 0x00000269
#define MSR_MTRR_FIX4K_D0000 0x0000026a
#define MSR_MTRR_FIX4K_D8000 0x0000026b
#define MSR_MTRR_FIX4K_E0000 0x0000026c
#define MSR_MTRR_FIX4K_E8000 0x0000026d
#define MSR_MTRR_FIX4K_F0000 0x0000026e
#define MSR_MTRR_FIX4K_F8000 0x0000026f

#define MSR_MTRR_DEFAULT 0x000002ff

#define ERR_API_VERSION            -1
#define ERR_MASTER_HT_ID           -2
#define ERR_GENERAL_NC_START_ERROR -9

#define NC2_F0_DEVICE_VENDOR_ID_REGISTER        (0x00)
#define NC2_F0_STATUS_COMMAND_REGISTER          (0x04)
#define NC2_F0_CLASS_CODE_REVISION_ID_REGISTER  (0x08)
#define NC2_F0_HEADER_TYPE_REGISTER             (0x0C)
#define NC2_F0_BASE_ADDRESS_REGISTER_0          (0x10)
#define NC2_F0_CAPABILITIES_POINTER_REGISTER    (0x34)
#define NC2_F0_LINK_CONTROL_REGISTER            (0x84)
#define NC2_F0_LINK_FREQUENCY_REVISION_REGISTER (0x88)
#define NC2_F0_NODE_ID_REGISTER                 (0xC8)
#define NC2_F0_UNIT_ID_REGISTER                 (0xD0)

#endif
