export PATH := $(PWD)/.build/armv6-obmc-linux-gnueabi/buildtools/bin/:$(PATH)
CROSS := armv6-obmc-linux-gnueabi-

CC := $(CROSS)gcc
STRIP := $(CROSS)strip
CFLAGS := -march=armv6zk -mcpu=arm1176jz-s

scat: scat.c
	$(CC) -o $@ $^ $(CFLAGS)
	$(STRIP) -S $@

clean:
	$(RM) scat
