CLANG ?= clang
LLVM_STRIP ?= llvm-strip
BPFTOOL ?= bpftool
ARCH ?= $(shell uname -m | sed 's/x86_64/x86/' | sed 's/aarch64/arm64/')

CFLAGS ?= -O2 -g -Wall -Wextra
BPF_CFLAGS ?= -O2 -g -target bpf -D__TARGET_ARCH_$(ARCH) \
	-I. -Wno-unused-value -Wno-pointer-sign \
	-Wno-compare-distinct-pointer-types -fno-stack-protector

LDLIBS ?= -lbpf -lelf -lz

APPS := rtc_io pipeline_io

.PHONY: all clean vmlinux.h

all: $(APPS)

vmlinux.h:
	$(BPFTOOL) btf dump file /sys/kernel/btf/vmlinux format c > $@

%.bpf.o: %.bpf.c counts.h vmlinux.h
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@
	$(LLVM_STRIP) -g $@

%.skel.h: %.bpf.o
	$(BPFTOOL) gen skeleton $< > $@

rtc_io: rtc_io.c observe_loop.c rtc_io.skel.h observe_loop.h counts.h
	$(CC) $(CFLAGS) -I. rtc_io.c observe_loop.c -o $@ $(LDLIBS)

pipeline_io: pipeline_io.c observe_loop.c pipeline_io.skel.h observe_loop.h counts.h
	$(CC) $(CFLAGS) -I. pipeline_io.c observe_loop.c -o $@ $(LDLIBS)

clean:
	rm -f $(APPS) *.bpf.o *.skel.h
