COPT      := -g -Wall -Wextra -Wno-unused-parameter -O3 -std=gnu99

syslinux_version := 4.06
syslinux_dir     := syslinux-$(syslinux_version)

COM32DEPS := $(syslinux_dir)/com32/libutil/libutil_com.a $(syslinux_dir)/com32/lib/libcom32.a

.PHONY: all
all: nc2-bootloader.c32

.PRECIOUS: %.bz2

.PHONY: clean
clean:
	rm -f *~ *.o *.c32 *.elf .*.o.d *.orig nc2-version.h

.PHONY: realclean
realclean: clean
	rm -rf $(syslinux_dir) syslinux-$(syslinux_version).tar.bz2

syslinux-%.tar.bz2:
	wget -O $@ http://www.kernel.org/pub/linux/utils/boot/syslinux/4.xx/$@ || rm -f $@

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

%.o: %.c $(syslinux_dir)/com32/samples/Makefile
	(rm -f $@ && cd $(syslinux_dir)/com32/samples && make $(CURDIR)/$@ NOGPL=1)

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

nc2-version.h: nc2-defs.h nc2-access.h nc2-acpi.h nc2-bootloader.h nc2-access.c nc2-bootloader.c
	@echo \#define VER \"`git describe --always`\" >nc2-version.h

nc2-bootloader.elf: nc2-bootloader.o nc2-htscan.o nc2-acpi.o nc2-smbios.o nc2-options.o nc2-access.o nc2-i2cmaster.o nc2-spd.o nc2-spimaster.o $(COM32DEPS)

nc2-bootloader.o: nc2-bootloader.c nc2-defs.h nc2-bootloader.h nc2-access.h nc2-acpi.h nc2-version.h nc2-spd.h

nc2-htscan.o: nc2-htscan.c nc2-defs.h nc2-bootloader.h nc2-access.h

nc2-options.o: nc2-options.c nc2-defs.h nc2-bootloader.h nc2-access.h

nc2-access.o: nc2-access.c nc2-defs.h nc2-access.h

nc2-acpi.o: nc2-acpi.c nc2-acpi.h

nc2-smbios.o: nc2-smbios.c nc2-bootloader.h

nc2-spd.o: nc2-spd.c nc2-spd.h nc2-bootloader.h

nc2-i2cmaster.o: nc2-i2cmaster.c nc2-bootloader.h nc2-access.h

nc2-spimaster.o: nc2-spimaster.c nc2-bootloader.h nc2-access.h
