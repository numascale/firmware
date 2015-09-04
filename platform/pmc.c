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

#define PMIC_INDEX 0x2e
#define PMIC_DATA  0x2f
#define PMIC_ID_W83627DHGP 0x73b0
#define PMIC_ID 0x20

#define PMIC_REG_BANK 0x4f
#define PMIC_REG_ID 0x4f

static uint8_t pmic_read8(const uint8_t reg)
{
    outb(reg, PMIC_INDEX);
    return inb(PMIC_DATA);
}

static uint16_t pmic_read16(const uint8_t reg)
{
    return pmic_read8(reg) | (uint16_t)pmic_read8(reg + 1) << 8;
}

static void pmic_write8(const uint8_t reg, const uint8_t val)
{
    outb(reg, PMIC_INDEX);
    outb(val, PMIC_DATA);
}

// GP40 p85, GP42 p83 GP44 p81 GP46 p79
void pmic_reset(void)
{
    // enter extended mode
    outb(0x87, PMIC_INDEX);
    outb(0x87, PMIC_INDEX);

    xassert(pmic_read16(PMIC_ID) == PMIC_ID_W83627DHGP);
    pmic_write8(0x07, 8); // select WDT

    // pin 77 -> WDT0#
    uint8_t val8 = pmic_read8(0x2d);
    pmic_write8(0x2d, val8 | 1);

    val8 = pmic_read8(0xf5);
    val8 |= 2; // KBRST#
    val8 &= ~0xc; // second mode
    pmic_write8(0xf5, val8 | 2);

    pmic_write8(0xf7, 1 << 6);

    pmic_write8(0xf6, 2);

    // exit advanced mode
    outb(0xaa, PMIC_INDEX);

    printf("waiting for reset...");

    while (1)
        cpu_relax();
}
