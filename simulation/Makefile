CFLAGS := -DSIM -I../json-1.6/src -Wall -O1 -g -fpermissive -fno-rtti -std=gnu++11

routing: routing.c ../numachip2/router.c ../numachip2/router.h
	$(CXX) $(CFLAGS) -o routing routing.c ../numachip2/router.c

.PHONY: clean
clean:
	rm routing

.PHONY: check
check:
	cppcheck -q --enable=all --inconclusive ../numachip2/router.c ../numachip2/router.h routing.c
