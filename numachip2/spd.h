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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __SPD_H
#define __SPD_H

#include <inttypes.h>

/* Byte 2 Fundamental Memory Types */
#define SPD_MEMTYPE_FPM         0x01
#define SPD_MEMTYPE_EDO         0x02
#define SPD_MEMTYPE_PIPE_NIBBLE 0x03
#define SPD_MEMTYPE_SDRAM       0x04
#define SPD_MEMTYPE_ROM         0x05
#define SPD_MEMTYPE_SGRAM       0x06
#define SPD_MEMTYPE_DDR         0x07
#define SPD_MEMTYPE_DDR2        0x08
#define SPD_MEMTYPE_DDR2_FBDIMM 0x09
#define SPD_MEMTYPE_DDR2_FBDIMM_PROBE   0x0a
#define SPD_MEMTYPE_DDR3        0x0B

/* DIMM Type for DDR2 SPD (according to v1.3) */
#define DDR2_SPD_DIMMTYPE_UNDEFINED     0x00
#define DDR2_SPD_DIMMTYPE_RDIMM         0x01
#define DDR2_SPD_DIMMTYPE_UDIMM         0x02
#define DDR2_SPD_DIMMTYPE_SO_DIMM       0x04
#define DDR2_SPD_DIMMTYPE_72B_SO_CDIMM  0x06
#define DDR2_SPD_DIMMTYPE_72B_SO_RDIMM  0x07
#define DDR2_SPD_DIMMTYPE_MICRO_DIMM    0x08
#define DDR2_SPD_DIMMTYPE_MINI_RDIMM    0x10
#define DDR2_SPD_DIMMTYPE_MINI_UDIMM    0x20

/* Byte 3 Key Byte / Module Type for DDR3 SPD */
#define DDR3_SPD_MODULETYPE_MASK          0x0f
#define DDR3_SPD_MODULETYPE_RDIMM         0x01
#define DDR3_SPD_MODULETYPE_UDIMM         0x02
#define DDR3_SPD_MODULETYPE_SO_DIMM       0x03
#define DDR3_SPD_MODULETYPE_MICRO_DIMM    0x04
#define DDR3_SPD_MODULETYPE_MINI_RDIMM    0x05
#define DDR3_SPD_MODULETYPE_MINI_UDIMM    0x06
#define DDR3_SPD_MODULETYPE_MINI_CDIMM    0x07
#define DDR3_SPD_MODULETYPE_72B_SO_UDIMM  0x08
#define DDR3_SPD_MODULETYPE_72B_SO_RDIMM  0x09
#define DDR3_SPD_MODULETYPE_72B_SO_CDIMM  0x0a
#define DDR3_SPD_MODULETYPE_LRDIMM        0x0b
#define DDR3_SPD_MODULETYPE_16B_SO_DIMM   0x0c
#define DDR3_SPD_MODULETYPE_32B_SO_DIMM   0x0d

struct ddr3_spd_eeprom {
        /* General Section: Bytes 0-59 */
        uint8_t info_size_crc;   /*  0 # bytes written into serial memory,
                                             CRC coverage */
        uint8_t spd_rev;         /*  1 Total # bytes of SPD mem device */
        uint8_t mem_type;        /*  2 Key Byte / Fundamental mem type */
        uint8_t module_type;     /*  3 Key Byte / Module Type */
        uint8_t density_banks;   /*  4 SDRAM Density and Banks */
        uint8_t addressing;      /*  5 SDRAM Addressing */
        uint8_t module_vdd;      /*  6 Module nominal voltage, VDD */
        uint8_t organization;    /*  7 Module Organization */
        uint8_t bus_width;       /*  8 Module Memory Bus Width */
        uint8_t ftb_div;         /*  9 Fine Timebase (FTB)
                                             Dividend / Divisor */
        uint8_t mtb_dividend;    /* 10 Medium Timebase (MTB) Dividend */
        uint8_t mtb_divisor;     /* 11 Medium Timebase (MTB) Divisor */
        uint8_t tCK_min;         /* 12 SDRAM Minimum Cycle Time */
        uint8_t res_13;          /* 13 Reserved */
        uint8_t caslat_lsb;      /* 14 CAS Latencies Supported,
                                             Least Significant Byte */
        uint8_t caslat_msb;      /* 15 CAS Latencies Supported,
                                             Most Significant Byte */
        uint8_t tAA_min;         /* 16 Min CAS Latency Time */
        uint8_t tWR_min;         /* 17 Min Write REcovery Time */
        uint8_t tRCD_min;        /* 18 Min RAS# to CAS# Delay Time */
        uint8_t tRRD_min;        /* 19 Min Row Active to
                                             Row Active Delay Time */
        uint8_t tRP_min;         /* 20 Min Row Precharge Delay Time */
        uint8_t tRAS_tRC_ext;    /* 21 Upper Nibbles for tRAS and tRC */
        uint8_t tRAS_min_lsb;    /* 22 Min Active to Precharge
                                             Delay Time */
        uint8_t tRC_min_lsb;     /* 23 Min Active to Active/Refresh
                                             Delay Time, LSB */
        uint8_t tRFC_min_lsb;    /* 24 Min Refresh Recovery Delay Time */
        uint8_t tRFC_min_msb;    /* 25 Min Refresh Recovery Delay Time */
        uint8_t tWTR_min;        /* 26 Min Internal Write to
                                             Read Command Delay Time */
        uint8_t tRTP_min;        /* 27 Min Internal Read to Precharge
                                             Command Delay Time */
        uint8_t tFAW_msb;        /* 28 Upper Nibble for tFAW */
        uint8_t tFAW_min;        /* 29 Min Four Activate Window
                                             Delay Time*/
        uint8_t opt_features;    /* 30 SDRAM Optional Features */
        uint8_t therm_ref_opt;   /* 31 SDRAM Thermal and Refresh Opts */
        uint8_t therm_sensor;    /* 32 Module Thermal Sensor */
        uint8_t device_type;     /* 33 SDRAM device type */
        uint8_t res_34_59[26];   /* 34-59 Reserved, General Section */

        /* Module-Specific Section: Bytes 60-116 */
        union {
                struct {
                        /* 60 (Unbuffered) Module Nominal Height */
                        uint8_t mod_height;
                        /* 61 (Unbuffered) Module Maximum Thickness */
                        uint8_t mod_thickness;
                        /* 62 (Unbuffered) Reference Raw Card Used */
                        uint8_t ref_raw_card;
                        /* 63 (Unbuffered) Address Mapping from
                              Edge Connector to DRAM */
                        uint8_t addr_mapping;
                        /* 64-116 (Unbuffered) Reserved */
                        uint8_t res_64_116[53];
                } unbuffered;
                struct {
                        /* 60 (Registered) Module Nominal Height */
                        uint8_t mod_height;
                        /* 61 (Registered) Module Maximum Thickness */
                        uint8_t mod_thickness;
                        /* 62 (Registered) Reference Raw Card Used */
                        uint8_t ref_raw_card;
                        /* 63 DIMM Module Attributes */
                        uint8_t modu_attr;
                        /* 64 RDIMM Thermal Heat Spreader Solution */
                        uint8_t thermal;
                        /* 65 Register Manufacturer ID Code, Least Significant Byte */
                        uint8_t reg_id_lo;
                        /* 66 Register Manufacturer ID Code, Most Significant Byte */
                        uint8_t reg_id_hi;
                        /* 67 Register Revision Number */
                        uint8_t reg_rev;
                        /* 68 Register Type */
                        uint8_t reg_type;
                        /* 69-76 RC1,3,5...15 (MS Nibble) / RC0,2,4...14 (LS Nibble) */
                        uint8_t rcw[8];
                } registered;
                uint8_t uc[57]; /* 60-116 Module-Specific Section */
        } mod_section;

        /* Unique Module ID: Bytes 117-125 */
        uint8_t mmid_lsb;        /* 117 Module MfgID Code LSB - JEP-106 */
        uint8_t mmid_msb;        /* 118 Module MfgID Code MSB - JEP-106 */
        uint8_t mloc;            /* 119 Mfg Location */
        uint8_t mdate[2];        /* 120-121 Mfg Date */
        uint8_t sernum[4];       /* 122-125 Module Serial Number */

        /* CRC: Bytes 126-127 */
        uint8_t crc[2];          /* 126-127 SPD CRC */

        /* Other Manufacturer Fields and User Space: Bytes 128-255 */
        uint8_t mpart[18];       /* 128-145 Mfg's Module Part Number */
        uint8_t mrev[2];         /* 146-147 Module Revision Code */

        uint8_t dmid_lsb;        /* 148 DRAM MfgID Code LSB - JEP-106 */
        uint8_t dmid_msb;        /* 149 DRAM MfgID Code MSB - JEP-106 */

        uint8_t msd[26];         /* 150-175 Mfg's Specific Data */
        uint8_t cust[80];        /* 176-255 Open for Customer Use */
};

static inline const char *nc2_ddr3_module_type(const uint8_t module_type)
{
	switch (module_type & DDR3_SPD_MODULETYPE_MASK) {
		case DDR3_SPD_MODULETYPE_RDIMM:         return "RDIMM";
		case DDR3_SPD_MODULETYPE_UDIMM:         return "UDIMM";
		case DDR3_SPD_MODULETYPE_SO_DIMM:       return "SO-DIMM";
		case DDR3_SPD_MODULETYPE_MICRO_DIMM:    return "Micro-DIMM";
		case DDR3_SPD_MODULETYPE_MINI_RDIMM:    return "Mini-RDIMM";
		case DDR3_SPD_MODULETYPE_MINI_UDIMM:    return "Mini-UDIMM";
		case DDR3_SPD_MODULETYPE_MINI_CDIMM:    return "Mini-CDIMM";
		case DDR3_SPD_MODULETYPE_72B_SO_UDIMM:  return "72b-SO-UDIMM";
		case DDR3_SPD_MODULETYPE_72B_SO_RDIMM:  return "72b-SO-RDIMM";
		case DDR3_SPD_MODULETYPE_72B_SO_CDIMM:  return "72b-SO-CDIMM";
		case DDR3_SPD_MODULETYPE_LRDIMM:        return "LRDIMM";
		case DDR3_SPD_MODULETYPE_16B_SO_DIMM:   return "16b-SO-DIMM";
		case DDR3_SPD_MODULETYPE_32B_SO_DIMM:   return "32b-SO-DIMM";
		default: return "unknown";
	}
}

extern void ddr3_spd_check(const struct ddr3_spd_eeprom *spd);

#endif

