COPT_BASE := -fno-rtti -fno-threadsafe-statics -Wall -Wextra -Wunused -Wuninitialized -Wshadow -Wformat=2 -Wwrite-strings -Wlogical-op -Wredundant-decls -fpermissive -std=gnu++11
COPT      := -g -O3 $(COPT_BASE)

syslinux_version := 4.07
syslinux_dir     := syslinux-$(syslinux_version)

COM32DEPS := $(syslinux_dir)/com32/libutil/libutil_com.a $(syslinux_dir)/com32/lib/libcom32.a
DIRS := platform/ opteron/ numachip2/ library/ simulation/ ./

.PHONY: all
all: bootloader.c32

.PHONY: upload
upload: bootloader.c32
	rsync -z bootloader.c32 build2:/net/numastore/tftpboot/nc2-bootloader-$(USER).c32

.PHONY: publish
publish: bootloader.c32
	rsync -z bootloader.c32 build2:/net/numastore/tftpboot/nc2-bootloader-$(shell git describe).c32
	rsync -z bootloader.c32 resources@resources:resources/bootloader/validating/nc2-bootloader-$(shell git describe).c32

.PHONY: check
check:
	cppcheck -q --language=c++ --suppress=unusedStructMember --enable=all --inconclusive --force $(addsuffix *.h, $(DIRS)) $(addsuffix *.c, $(DIRS))

.PHONY: clean
clean:
	@rm -f $(addsuffix *~, $(DIRS)) $(addsuffix *.o, $(DIRS)) $(addsuffix .*.o.d, $(DIRS)) version.h bootloader.c32 bootloader.elf

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

%.o: %.c $(syslinux_dir)/com32/samples/Makefile
	(rm -f $@ && cd $(syslinux_dir)/com32/samples && make CC="g++ $(COPT_BASE)" $(CURDIR)/$@ NOGPL=1)

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

version.h: library/access.h platform/acpi.h bootloader.h library/access.c bootloader.c
	@echo \#define VER \"`git describe --always`\" >version.h

bootloader.elf: bootloader.o node.o platform/config.o platform/syslinux.o opteron/ht-scan.o opteron/maps.o opteron/opteron.o opteron/sr56x0.o opteron/tracing.o platform/acpi.o platform/aml.o platform/smbios.o platform/ipmi.o platform/options.o library/access.o library/utils.o numachip2/i2c.o numachip2/numachip.o numachip2/pe.o numachip2/spd.o numachip2/spi.o numachip2/lc5.o numachip2/dram.o numachip2/fabric.o numachip2/router.o numachip2/maps.o numachip2/atts.o numachip2/flash.o platform/syslinux.o platform/e820.o platform/trampoline.o platform/devices.o platform/pcialloc.o $(COM32DEPS)

bootloader.o: bootloader.c bootloader.h library/access.h library/utils.h platform/acpi.h version.h numachip2/spd.h numachip2/info.h platform/trampoline.h

node.o: node.h

opteron/ht-scan.o: opteron/ht-scan.c bootloader.h library/access.h
opteron/maps.o: opteron/maps.c
opteron/opteron.o: opteron/opteron.c opteron/opteron.h platform/trampoline.h
opteron/sr56x0.o: opteron/sr56x0.c opteron/sr56x0.h
opteron/tracing.o: opteron/tracing.c opteron/opteron.h

platform/options.o: platform/options.c bootloader.h library/access.h version.h
platform/acpi.o: platform/acpi.c platform/acpi.h library/utils.h
platform/aml.o: platform/aml.c platform/aml.h
platform/smbios.o: platform/smbios.c bootloader.h
platform/ipmi.o: platform/ipmi.c platform/ipmi.h
platform/syslinux.o: platform/syslinux.c platform/os.h
platform/config.o: platform/config.c platform/config.h
platform/e820.o: platform/e820.c platform/e820.h platform/trampoline.h
platform/devices.o: platform/devices.c platform/devices.h
platform/devices.o: platform/pcialloc.c platform/pcialloc.h
platform/trampoline.o: platform/trampoline.S platform/trampoline.h
platform/pcialloc.o: platform/pcialloc.c platform/pcialloc.h library/base.h

library/base.h: platform/pcialloc.h platform/pcialloc.c
library/access.o: library/access.c library/access.h
library/utils.o: library/utils.h

numachip2/spd.o: numachip2/spd.c numachip2/spd.h bootloader.h
numachip2/numachip.o: numachip2/numachip.c numachip2/numachip.h
numachip2/i2c.o: numachip2/i2c.c bootloader.h library/access.h
numachip2/spi.o: numachip2/spi.c bootloader.h library/access.h
numachip2/pe.o: numachip2/pe.c numachip2/numachip2_mseq.h
numachip2/lc4.o: numachip2/lc4.c numachip2/lc.h
numachip2/lc5.o: numachip2/lc5.c numachip2/lc.h
numachip2/fabric.o: numachip2/fabric.c
numachip2/router.o: numachip2/router.c numachip2/router.h
numachip2/dram.o: numachip2/dram.c
numachip2/maps.o: numachip2/maps.c
numachip2/atts.o: numachip2/atts.c
numachip2/flash.o: numachip2/flash.c
