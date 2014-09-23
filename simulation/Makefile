CFLAGS := -DSIM -I../json-1.6/src -Wall -O3 -g -fpermissive -fno-rtti

routing: routing.c
	$(CXX) $(CFLAGS) -o routing library/access.c library/utils.c library/host.c library/trampoline.c \
	library/spi.c library/i2c.c routing.c \
	  ../numachip2/ringrouter.c \
	  ../platform/config.c ../platform/options.c ../platform/e820.c ../json-1.6/src/json.c ../node.c \
	  ../opteron/opteron.c ../opteron/maps.c ../opteron/sr56x0.c ../opteron/ht-scan.c ../numachip2/numachip.c \
	  ../numachip2/atts.c ../numachip2/maps.c ../numachip2/dram.c ../numachip2/fabric.c \
	  ../numachip2/lc5.c ../numachip2/spd.c
