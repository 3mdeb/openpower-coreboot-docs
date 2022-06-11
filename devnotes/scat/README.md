## cat for obmc-console

Server of [obmc-console] used by Talos 2 communicates with its client using
Unix socket with abstract address (see `man 7 unix`).  BMC doesn't have much
tools installed and it's not clear if there are any for sockets with abstract
addresses, so here's a tiny C program to get the output (socket name is
hard-coded at the moment).

It's purpose is to collect logs without a tty, so that termination of ssh
connection wouldn't interrupt anything.

### Prebuilt binary

For convenience, binary is provided right here to avoid building toolchain.

### Building from sources

Target system uses armv6 which is not supported by popular prebuilt toolchains
for years, so `crosstool-ng` configuration for building required toolchain is
provided, skip below if you have a suitable one already.

Prerequisites:
 * [crosstool-ng]
 * regular build environment with GCC, GNU make and binutils

Preparation:
```bash
# build toolchain for armv6
ct-ng armv6-obmc-linux-gnueabi
ct-ng build
```

Build `scat` binary:
```bash
make
```

With your own toolchain, proceed as usual:
```bash
CROSS=armv6-linux-gnueabi- make
```

### Usage

```
$ scp scat root@talos:/tmp
$ ssh root@talos
# /tmp/scat | gzip > /tmp/boot-log &
```

[crosstool-ng]: https://crosstool-ng.github.io/
[obmc-console]: https://github.com/openbmc/obmc-console
