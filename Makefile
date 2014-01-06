COPT      := -g -Wall -Wextra -Wno-unused-parameter -Wshadow -O3

syslinux_version := 4.07
syslinux_dir     := syslinux-$(syslinux_version)

mjson_version    := 1.5
mjson_dir        := json-$(mjson_version)

COM32DEPS := $(syslinux_dir)/com32/libutil/libutil_com.a $(syslinux_dir)/com32/lib/libcom32.a
DIRS := platform/ opteron/ numachip2/ library/

.PHONY: all
all: platform/bootloader.c32

.PRECIOUS: %.bz2

.PHONY: clean
clean:
	rm -f $(addsuffix *.o, $(DIRS)) $(addsuffix .*.o.d, $(DIRS)) version.h platform/bootloader.c32 platform/bootloader.elf

.PHONY: realclean
realclean: clean
	rm -rf $(syslinux_dir) syslinux-$(syslinux_version).tar.bz2

syslinux-%.tar.bz2:
	wget -O $@ http://www.kernel.org/pub/linux/utils/boot/syslinux/4.xx/$@ || rm -f $@

mjson-%.tar.gz:
	wget -O $@ http://sourceforge.net/projects/mjson/files/latest/download?source=files || rm -f $@

$(syslinux_dir)/com32/samples/Makefile: syslinux-$(syslinux_version).tar.bz2
	tar -jxf $<
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
	(rm -f $@ && cd $(syslinux_dir)/com32/samples && make CC="g++ -fpermissive -fno-threadsafe-statics" $(CURDIR)/$@ NOGPL=1)

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

version.h: opteron/defs.h library/access.h platform/acpi.h platform/bootloader.h library/access.c platform/bootloader.c
	@echo \#define VER \"`git describe --always`\" >version.h

platform/bootloader.elf: platform/bootloader.o platform/config.o platform/syslinux.o opteron/ht-scan.o platform/acpi.o platform/smbios.o platform/options.o library/access.o numachip2/i2c-master.o numachip2/spd.o numachip2/spi-master.o platform/syslinux.o $(mjson_dir)/src/json.o $(COM32DEPS)

platform/bootloader.o: platform/bootloader.c opteron/defs.h platform/bootloader.h library/access.h platform/acpi.h version.h numachip2/spd.h

opteron/ht-scan.o: opteron/ht-scan.c opteron/defs.h platform/bootloader.h library/access.h

platform/options.o: platform/options.c opteron/defs.h platform/bootloader.h library/access.h

library/access.o: library/access.c opteron/defs.h library/access.h

platform/acpi.o: platform/acpi.c platform/acpi.h

platform/smbios.o: platform/smbios.c platform/bootloader.h

platform/syslinux.o: platform/syslinux.c platform/syslinux.h

platform/config.o: platform/config.c platform/config.h $(mjson_dir)/src/json.h

numachip2/spd.o: numachip2/spd.c numachip2/spd.h platform/bootloader.h

numachip2/i2c-master.o: numachip2/i2c-master.c platform/bootloader.h library/access.h

numachip2/spi-master.o: numachip2/spi-master.c platform/bootloader.h library/access.h
