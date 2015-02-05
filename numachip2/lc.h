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
#include "numachip.h"

class LC
{
protected:
	const Numachip2& numachip;
	LC(Numachip2 &_numachip, const uint8_t _index, const uint16_t _chunkaddr, const uint16_t _tableaddr):
	  numachip(_numachip), index(_index), chunkaddr(_chunkaddr), tableaddr(_tableaddr) {};
public:
	const uint8_t index;
	const uint16_t chunkaddr, tableaddr;

	// can't use pure virtual (= 0) due to link-time dependency with libstdc++
	virtual bool is_up(void) {return 0;};
	virtual uint64_t status(void) {return 0;};
	virtual void check(void) {};
	virtual void clear(void) {};
};

class LC4: public LC
{
	static const reg_t SIZE          = 0x200;
	static const reg_t STATE_CLEAR   = 0x2400;
	static const reg_t STATE_SET     = 0x2404;
	static const reg_t NODE_IDS      = 0x2408;
	static const reg_t RESET_START   = 0x240c;
	static const reg_t ERROR_COUNT   = 0x2410;
	static const reg_t SYNC_INTERVAL = 0x2414;
	static const reg_t SAVE_ID       = 0x2418;
	static const reg_t ROUT_CTRL     = 0x2420;
	static const reg_t ROUT_MASK     = 0x2424;
	static const reg_t PHY_STAT      = 0x242c;
	static const reg_t CONFIG1       = 0x2430;
	static const reg_t CONFIG2       = 0x2434;
	static const reg_t CONFIG3       = 0x2438;
	static const reg_t CONFIG4       = 0x243c;
	static const reg_t UID1          = 0x2440;
	static const reg_t UID2          = 0x2444;
	static const reg_t DIAG          = 0x2448;
	static const reg_t INIT_STATE    = 0x244c;
	static const reg_t PC_CMD        = 0x2450;
	static const reg_t PC_EXT        = 0x2454;
	static const reg_t SEL           = 0x2458;
	static const reg_t PC_CNT        = 0x245c;
	static const reg_t VID           = 0x2460;
	static const reg_t SWST_CLEAR    = 0x2464;
	static const reg_t SWST_SET      = 0x2468;
	static const reg_t BLOCK         = 0x246c;
	static const reg_t ELOG0         = 0x2470;
	static const reg_t ELOG1         = 0x2474;
	static const reg_t ROUTE_RAM     = 0x2500;
	static const reg_t SCIROUTE      = 0x25c0;

public:
	bool is_up(void);
	uint64_t status(void);
	void check(void);
	void clear(void);
	LC4(Numachip2 &_numachip, const uint8_t _index);
};

class LC5: public LC
{
	static const reg_t SIZE          = 0x100;
	static const reg_t ROUTE_RAM     = 0x2800;
	static const reg_t ROUTE_CHUNK   = 0x28c0;
	static const reg_t LINKSTAT      = 0x28c4;
	static const reg_t EVENTSTAT     = 0x28c8;
	static const reg_t ERRORCNT      = 0x28cc;

public:
	bool is_up(void);
	uint64_t status(void);
	void check(void);
	void clear(void);
	LC5(Numachip2 &_numachip, const uint8_t _index);
};
