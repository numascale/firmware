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

#pragma once

#include "../library/base.h"

class Numachip2;

class RingRouter {
//	const Numachip2 &numachip;
	uint8_t sizes[3];
	unsigned usage[4096];
	uint8_t route[16 + 16 + 16], bestroute[16 + 16 + 16];
//	uint32_t shadow[6][Numachip2::LC_SIZE];
	unsigned bestcost;

//	uint16_t lcbase(const uint8_t lc) const;
	void find(const sci_t src, const sci_t dst, const unsigned cost, const unsigned offset);
	sci_t neigh(const sci_t src, const uint8_t lc) const;
	void update(const sci_t src, const sci_t dst);
public:
	RingRouter(void); //const Numachip2 &_numachip);
//	void init(void);
};
