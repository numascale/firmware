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

enum ports {A=1, B, C, D, E, F};

#define PAIR(sn, sp, dn, dp) \
	assert(router->neigh[sn][sp].nodeid = 255); \
	assert(router->neigh[sn][sp].xbarid = 255); \
	assert(router->neigh[dn][dp].nodeid = 255); \
	assert(router->neigh[dn][dp].xbarid = 255); \
	router->neigh[sn][sp] = {dn, dp}; router->neigh[dn][dp] = {sn, sp}

#define XPAIR(s, d) \
	PAIR(s, 1, d, 2)

#define YPAIR(s, d) \
	PAIR(s, 3, d, 4)

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

int main(int argc, char *argv[])
{
	printf("unconstrained 21-server topology:\n");
	Router *router = new Router();

	PAIR( 0, A,  1, A); PAIR( 0, B,  5, A); PAIR( 0, C,  9, A); PAIR( 0, D, 13, A); PAIR( 0, E, 17, A); PAIR( 0, F, 20, A);
	PAIR( 1, B,  2, A); PAIR( 1, C,  6, A); PAIR( 1, D, 10, A); PAIR( 1, E, 14, A); PAIR( 1, F, 18, A);
	PAIR( 2, B,  3, A); PAIR( 2, C,  7, A); PAIR( 2, D, 11, A); PAIR( 2, E, 15, A); PAIR( 2, F, 19, A);
	PAIR( 3, B,  4, A); PAIR( 3, C,  8, A); PAIR( 3, D, 12, A); PAIR( 3, E, 16, A); PAIR( 2, F, 20, B);
	PAIR( 4, B,  5, B); PAIR( 5, C,  9, B); PAIR( 4, D, 13, B); PAIR( 4, E, 17, B);
	PAIR( 5, C,  6, B); PAIR( 5, D, 10, B); PAIR( 5, E, 14, B); PAIR( 5, F, 18, B);
	PAIR( 6, C,  7, B); PAIR( 6, D, 11, B); PAIR( 6, E, 15, B); PAIR( 6, F, 19, B);
	PAIR( 7, C,  8, B); PAIR( 7, D, 12, B); PAIR( 7, E, 16, B); PAIR( 7, F, 20, B);
	PAIR( 8, C,  9, C); PAIR( 8, D, 13, C); PAIR( 9, E, 17, C); PAIR( 8, F,  4, F);
	PAIR( 9, D, 10, C); PAIR( 9, E, 14, C); PAIR( 9, F, 18, C);
	PAIR(10, D, 11, C); PAIR(10, E, 15, C); PAIR(10, F, 19, C);
	PAIR(11, D, 12, C); PAIR(11, E, 16, C); PAIR(11, F, 20, D);
	PAIR(12, D, 13, D); PAIR(12, E, 17, D);
	PAIR(13, E, 14, D); PAIR(13, F, 18, D);
	PAIR(14, E, 15, D); PAIR(14, F, 19, D);
	PAIR(15, E, 16, D); PAIR(15, F, 20, D);
	PAIR(16, E, 17, E); PAIR(16, F, 12, F);
	PAIR(17, F, 18, E);
	PAIR(18, F, 19, E);
	PAIR(19, F, 20, F);

	router->run(21);
	delete router;

	printf("\n21-server 3x7 torus topology:\n");
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
