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

#define XPAIR(s, d) \
	router->neigh[s][1] = {d, 2}; router->neigh[d][2] = {s, 1}

#define YPAIR(s, d) \
	router->neigh[s][3] = {d, 2}; router->neigh[d][4] = {s, 1}

#define X(n) \
	XPAIR(00+n, 02+n); \
	XPAIR(02+n, 01+n); \
	XPAIR(01+n, 00+n)

#define Y(n) \
	YPAIR(00+n, 02+n); \
	YPAIR(02+n, 04+n); \
	YPAIR(04+n, 06+n); \
	YPAIR(06+n, 05+n); \
	YPAIR(05+n, 03+n); \
	YPAIR(03+n, 01+n); \
	YPAIR(01+n, 00+n)

Router *router;

int main(int argc, char *argv[])
{
	router = new Router();

	X(0);
	X(3);
	X(6);
	X(9);
	X(12);
	X(15);
	X(18);

	Y(0);
	Y(7);
	Y(14);

	router->run(21);
	delete router;

	return 0;
}
