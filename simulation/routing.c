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

#include "../numachip2/router.h"
#include "../platform/os.h"
#include "../platform/config.h"
#include "../platform/options.h"
#include "../platform/e820.h"
#include "../node.h"
#include <string.h>

int main(int argc, char *argv[])
{
	Router r(4);

//          snode  xbarid dnode  xbarid
	r.neigh[0x000][1] = {0x001, 1};
	r.neigh[0x000][2] = {0x002, 1};
	r.neigh[0x000][3] = {0x003, 1};

	r.neigh[0x001][1] = {0x000, 1};
	r.neigh[0x001][2] = {0x002, 2};
	r.neigh[0x001][3] = {0x003, 2};

	r.neigh[0x002][1] = {0x000, 2};
	r.neigh[0x002][2] = {0x001, 2};
	r.neigh[0x002][3] = {0x003, 3};

	r.neigh[0x003][1] = {0x000, 3};
	r.neigh[0x003][2] = {0x001, 3};
	r.neigh[0x003][3] = {0x002, 3};

	r.run();

	return 0;
}
