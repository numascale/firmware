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

unsigned nnodes = 4;
Router *router;

int main(int argc, char *argv[])
{
	router = new Router();

	router->neigh[0x000][1] = {0x001, 1};
	router->neigh[0x000][2] = {0x002, 1};
	router->neigh[0x000][3] = {0x003, 1};

	router->neigh[0x001][1] = {0x000, 1};
	router->neigh[0x001][2] = {0x002, 2};
	router->neigh[0x001][3] = {0x003, 2};

	router->neigh[0x002][1] = {0x000, 2};
	router->neigh[0x002][2] = {0x001, 2};
	router->neigh[0x002][3] = {0x003, 3};

	router->neigh[0x003][1] = {0x000, 3};
	router->neigh[0x003][2] = {0x001, 3};
	router->neigh[0x003][3] = {0x002, 3};

	router->run();
	delete router;

	return 0;
}
