COPT      := -g -Wall -Wextra -Wno-unused-parameter -Wshadow -O3

syslinux_version := 4.07
syslinux_dir     := syslinux-$(syslinux_version)

mjson_version    := 1.5
mjson_dir        := json-$(mjson_version)

COM32DEPS := $(syslinux_dir)/com32/libutil/libutil_com.a $(syslinux_dir)/com32/lib/libcom32.a
DIRS := platform/ opteron/ numachip2/ library/ ./
MAKEFLAGS += -j $(shell getconf _NPROCESSORS_ONLN)

.PHONY: all
all: bootloader.c32

.PHONY: upload
upload: bootloader.c32
	rsync bootloader.c32 numascale:/net/numastore/tftpboot/nc2-bootloader-$(USER).c32

.PHONY: reset
reset:
	ssh numascale /net/numastore/storage/software/local-linux-x86/numascale/bin/ipmi loop3336-ipmi chassis power reset

.PHONY: check
check:
	cppcheck --enable=all $(addsuffix *.h, $(DIRS)) $(addsuffix *.c, $(DIRS))

.PHONY: clean
clean:
	rm -f $(addsuffix *.o, $(DIRS)) $(addsuffix .*.o.d, $(DIRS)) version.h bootloader.c32 bootloader.elf

.PHONY: realclean
realclean: clean
	rm -rf $(syslinux_dir) syslinux-$(syslinux_version).tar.xz $(mjson_dir) mjson-$(mjson_version).tar.gz

syslinux-%.tar.xz:
	wget -O $@ http://www.kernel.org/pub/linux/utils/boot/syslinux/4.xx/$@ || rm -f $@

mjson-%.tar.gz:
	wget -O $@ http://sourceforge.net/projects/mjson/files/latest/download?source=files || rm -f $@

$(syslinux_dir)/com32/samples/Makefile: syslinux-$(syslinux_version).tar.xz
	tar -Jxf $<
	touch -c $(syslinux_dir)/com32/samples/Makefile
	(cd $(syslinux_dir) && make all-local)

# Needed for syslinux 4
$(syslinux_dir)/com32/tools/relocs: $(syslinux_dir)/com32/samples/Makefile
	(cd $(syslinux_dir)/com32/tools && make all)

$(syslinux_dir)/com32/libutil/libutil_com.a: $(syslinux_dir)/com32/samples/Makefile $(syslinux_dir)/com32/tools/relocs
	(cd $(syslinux_dir)/com32/libutil && make all)

$(syslinux_dir)/com32/lib/libcom32.a: $(syslinux_dir)/com32/samples/Makefile $(syslinux_dir)/com32/tools/relocs
	(cd $(syslinux_dir)/com32/lib && make all)

$(mjson_dir)/src/json.h \
$(mjson_dir)/src/json.c: mjson-$(mjson_version).tar.gz
	echo $@
	tar -zxf $<
	touch -c $(mjson_dir)/src/json.h
	perl -npi -e 's/#include <memory.h>/#include <string.h>/' $(mjson_dir)/src/json.c
	perl -npi -e 's/SIZE_MAX/10485760/' $(mjson_dir)/src/json.h

%.o: %.c $(syslinux_dir)/com32/samples/Makefile
	(rm -f $@ && cd $(syslinux_dir)/com32/samples && make CC="g++ -Wextra -Wshadow -fno-rtti -fpermissive -fno-threadsafe-statics" $(CURDIR)/$@ NOGPL=1)

%.o: %.S $(syslinux_dir)/com32/samples/Makefile
	(rm -f $@ && cd $(syslinux_dir)/com32/samples && make $(CURDIR)/$@ NOGPL=1)

%.elf: %.o
	(rm -f $@ && \
	cd $(syslinux_dir)/com32/samples && \
	cmd=$$(make -s -n $(CURDIR)/$@ NOGPL=1) && \
	cmd="$$cmd $(patsubst %, $(CURDIR)/%, $(wordlist 2, $(words $^), $^))" && \
	echo $$cmd && \
	$$cmd)

%.c32: %.elf $(syslinux_dir)/com32/samples/Makefile
	(cd $(syslinux_dir)/com32/samples && make $(CURDIR)/$@ NOGPL=1)

$(mjson_dir)/src/json.o: $(mjson_dir)/src/json.c

version.h: library/access.h platform/acpi.h bootloader.h library/access.c bootloader.c
	@echo \#define VER \"`git describe --always`\" >version.h

bootloader.elf: bootloader.o platform/config.o platform/syslinux.o opteron/ht-scan.o opteron/maps.o opteron/opteron.o platform/acpi.o platform/smbios.o platform/options.o library/access.o numachip2/i2c-master.o numachip2/numachip.o numachip2/spd.o numachip2/spi-master.o numachip2/lc5.o numachip2/selftest.o numachip2/dram.o numachip2/fabric.o numachip2/maps.o platform/syslinux.o platform/e820.o $(mjson_dir)/src/json.o $(COM32DEPS)

bootloader.o: $(mjson_dir)/src/json.h bootloader.c bootloader.h library/access.h platform/acpi.h version.h numachip2/spd.h

opteron/ht-scan.o: opteron/ht-scan.c bootloader.h library/access.h
opteron/maps.o: opteron/maps.c
opteron/opteron.o: opteron/opteron.c opteron/opteron.h

platform/options.o: platform/options.c bootloader.h library/access.h
platform/acpi.o: platform/acpi.c platform/acpi.h
platform/smbios.o: platform/smbios.c bootloader.h
platform/syslinux.o: platform/syslinux.c platform/syslinux.h
platform/config.o: platform/config.c platform/config.h
platform/e820.o: platform/e820.c platform/e820.h

library/access.o: library/access.c library/access.h

numachip2/spd.o: numachip2/spd.c numachip2/spd.h bootloader.h
numachip2/numachip.o: numachip2/numachip.c numachip2/numachip.h
numachip2/i2c-master.o: numachip2/i2c-master.c bootloader.h library/access.h
numachip2/spi-master.o: numachip2/spi-master.c bootloader.h library/access.h
numachip2/lc5.o: numachip2/lc5.c numachip2/lc5.h
numachip2/selftest.o: numachip2/selftest.c
numachip2/fabric.o: numachip2/fabric.c
numachip2/dram.o: numachip2/dram.c
numachip2/maps.o: numachip2/maps.c
