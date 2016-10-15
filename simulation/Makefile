CFLAGS := -DSIM -Wall -Wextra -O3 -g -fno-rtti -std=gnu++11

.PHONY: all
all: routing aml

routing: routing.c ../numachip2/router.c ../numachip2/router.h
	$(CXX) $(CFLAGS) -o routing routing.c ../numachip2/router.c

aml: aml.c ../platform/aml.c
	$(CXX) $(CFLAGS) -o aml aml.c ../platform/aml.c
.PHONY: clean
clean:
	rm routing

.PHONY: check
check:
	cppcheck -q --enable=all --inconclusive ../numachip2/router.c ../numachip2/router.h routing.c
