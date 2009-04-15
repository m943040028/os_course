#
# This makefile system follows the structuring conventions
# recommended by Peter Miller in his excellent paper:
#
#	Recursive Make Considered Harmful
#	http://aegis.sourceforge.net/auug97.pdf
#
OBJDIR := obj

ifdef LAB
SETTINGLAB := true
else
-include conf/lab.mk
endif

-include conf/env.mk

ifndef SOL
SOL := 0
endif
ifndef LABADJUST
LABADJUST := 0
endif

ifndef LABSETUP
LABSETUP := ./
endif

<<<<<<< HEAD:GNUmakefile
=======
ifndef BXSHARE
BXSHARE := $(PWD)/bochs/bios
endif
>>>>>>> master:GNUmakefile

TOP = .

# Cross-compiler jos toolchain
#
# This Makefile will automatically use the cross-compiler toolchain
# installed as 'i386-jos-elf-*', if one exists.  If the host tools ('gcc',
# 'objdump', and so forth) compile for a 32-bit x86 ELF target, that will
# be detected as well.  If you have the right compiler toolchain installed
# using a different name, set GCCPREFIX explicitly in conf/env.mk

# try to infer the correct GCCPREFIX
ifndef GCCPREFIX
GCCPREFIX := $(shell if i386-jos-elf-objdump -i 2>&1 | grep '^elf32-i386$$' >/dev/null 2>&1; \
	then echo 'i386-jos-elf-'; \
	elif objdump -i 2>&1 | grep 'elf32-i386' >/dev/null 2>&1; \
	then echo ''; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find an i386-*-elf version of GCC/binutils." 1>&2; \
	echo "*** Is the directory with i386-jos-elf-gcc in your PATH?" 1>&2; \
	echo "*** If your i386-*-elf toolchain is installed with a command" 1>&2; \
	echo "*** prefix other than 'i386-jos-elf-', set your GCCPREFIX" 1>&2; \
	echo "*** environment variable to that prefix and run 'make' again." 1>&2; \
	echo "*** To turn off this error, run 'gmake GCCPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

CC	:= $(GCCPREFIX)gcc -pipe
<<<<<<< HEAD:GNUmakefile
=======
GCC_LIB := $(shell $(CC) -print-libgcc-file-name)
>>>>>>> master:GNUmakefile
AS	:= $(GCCPREFIX)as
AR	:= $(GCCPREFIX)ar
LD	:= $(GCCPREFIX)ld
OBJCOPY	:= $(GCCPREFIX)objcopy
OBJDUMP	:= $(GCCPREFIX)objdump
NM	:= $(GCCPREFIX)nm
<<<<<<< HEAD:GNUmakefile
=======
BOCHS	:= BXSHARE=$(BXSHARE) bochs
>>>>>>> master:GNUmakefile

# Native commands
NCC	:= gcc $(CC_VER) -pipe
TAR	:= gtar
PERL	:= perl

# Compiler flags
# -fno-builtin is required to avoid refs to undefined functions in the kernel.
# Only optimize to -O1 to discourage inlining, which complicates backtraces.
<<<<<<< HEAD:GNUmakefile
CFLAGS := $(CFLAGS) $(DEFS) $(LABDEFS) -O1 -fno-builtin -I$(TOP) -MD 
CFLAGS += -Wall -Wno-format -Wno-unused -Werror -gstabs -m32

# Add -fno-stack-protector if the option exists.
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)

# Common linker flags
LDFLAGS := -m elf_i386
=======
CFLAGS	:= $(CFLAGS) $(DEFS) $(LABDEFS) -O -fno-builtin -I$(TOP) -MD -Wall -Wno-format -Wno-unused -Werror -gstabs
>>>>>>> master:GNUmakefile

# Linker flags for JOS user programs
ULDFLAGS := -T user/user.ld

<<<<<<< HEAD:GNUmakefile
GCC_LIB := $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

=======
>>>>>>> master:GNUmakefile
# Lists that the */Makefrag makefile fragments will add to
OBJDIRS :=

# Make sure that 'all' is the first target
all:

# Eliminate default suffix rules
.SUFFIXES:

# Delete target files if there is an error (or make is interrupted)
.DELETE_ON_ERROR:

# make it so that no intermediate .o files are ever deleted
.PRECIOUS: %.o $(OBJDIR)/boot/%.o $(OBJDIR)/kern/%.o \
	$(OBJDIR)/lib/%.o $(OBJDIR)/fs/%.o $(OBJDIR)/user/%.o

<<<<<<< HEAD:GNUmakefile
=======
# used for native gcc-4.x
ifeq ($(GCCPREFIX),)
CFLAGS += -fno-stack-protector
endif

>>>>>>> master:GNUmakefile
KERN_CFLAGS := $(CFLAGS) -DJOS_KERNEL -gstabs
USER_CFLAGS := $(CFLAGS) -DJOS_USER -gstabs



<<<<<<< HEAD:GNUmakefile

=======
>>>>>>> master:GNUmakefile
# Include Makefrags for subdirectories
include boot/Makefrag
include kern/Makefrag
include lib/Makefrag
include user/Makefrag
<<<<<<< HEAD:GNUmakefile
include fs/Makefrag
=======
>>>>>>> master:GNUmakefile


<<<<<<< HEAD:GNUmakefile
IMAGES = $(OBJDIR)/kern/bochs.img $(OBJDIR)/fs/fs.img
=======
IMAGES = $(OBJDIR)/kern/bochs.img
>>>>>>> master:GNUmakefile

bochs: $(IMAGES)
<<<<<<< HEAD:GNUmakefile
	bochs 'display_library: nogui'
=======
	$(BOCHS) 'display_library: nogui'
>>>>>>> master:GNUmakefile

# For deleting the build
clean:
	rm -rf $(OBJDIR)

realclean: clean
	rm -rf lab$(LAB).tar.gz bochs.out bochs.log

distclean: realclean
	rm -rf conf/gcc.mk

grade: $(LABSETUP)grade.sh
	$(V)$(MAKE) clean >/dev/null 2>/dev/null
	$(MAKE) all
<<<<<<< HEAD:GNUmakefile
	sh $(LABSETUP)grade.sh
=======
	BXSHARE=$(BXSHARE) sh $(LABSETUP)grade.sh
>>>>>>> master:GNUmakefile

handin: tarball
	@echo Please visit http://pdos.csail.mit.edu/cgi-bin/828handin
	@echo and upload lab$(LAB)-handin.tar.gz.  Thanks!

tarball: realclean
<<<<<<< HEAD:GNUmakefile
	tar cf - `find . -type f | grep -v '^\.*$$' | grep -v '/CVS/' | grep -v '/\.svn/' | grep -v '/\.git/' | grep -v 'lab[0-9].*\.tar\.gz'` | gzip > lab$(LAB)-handin.tar.gz
=======
	tar cf - `find . -type f | grep -v '^\.*$$' | grep -v '/CVS/' | grep -v '/\.svn/' | grep -v 'lab[0-9].*\.tar\.gz'` | gzip > lab$(LAB)-handin.tar.gz
>>>>>>> master:GNUmakefile

# For test runs
run-%:
	$(V)rm -f $(OBJDIR)/kern/init.o $(IMAGES)
	$(V)$(MAKE) "DEFS=-DTEST=_binary_obj_user_$*_start -DTESTSIZE=_binary_obj_user_$*_size" $(IMAGES)
<<<<<<< HEAD:GNUmakefile
	bochs -q 'display_library: nogui'
=======
	$(BOCHS) -q 'display_library: nogui'
>>>>>>> master:GNUmakefile

xrun-%:
	$(V)rm -f $(OBJDIR)/kern/init.o $(IMAGES)
	$(V)$(MAKE) "DEFS=-DTEST=_binary_obj_user_$*_start -DTESTSIZE=_binary_obj_user_$*_size" $(IMAGES)
<<<<<<< HEAD:GNUmakefile
	bochs -q
=======
	$(BOCHS) -q
>>>>>>> master:GNUmakefile

# This magic automatically generates makefile dependencies
# for header files included from C source files we compile,
# and keeps those dependencies up-to-date every time we recompile.
# See 'mergedep.pl' for more information.
$(OBJDIR)/.deps: $(foreach dir, $(OBJDIRS), $(wildcard $(OBJDIR)/$(dir)/*.d))
	@mkdir -p $(@D)
	@$(PERL) mergedep.pl $@ $^

-include $(OBJDIR)/.deps

always:
	@:

.PHONY: all always \
<<<<<<< HEAD:GNUmakefile
	handin tarball clean realclean clean-labsetup distclean grade labsetup
=======
	handin tarball clean realclean clean-labsetup distclean grade labsetup bochs
>>>>>>> master:GNUmakefile
