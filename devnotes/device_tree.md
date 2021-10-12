## Naming conventions

- DT

    Device Tree.

- FDT

    Flat Device Tree.

## Dump from skiboot

Hostboot prepares HDAT for `skiboot`, which turns it into a DT that we want to
dump here.  The idea is to make `skiboot` build FDT, print out its location
and size and then dump it from memory using `pdbg` while `skiboot` is halted.

### Patching skiboot

Find `skiboot` sources in your locally built [talos-op-build](https://scm.raptorcs.com/scm/git/talos-op-build).
It lies in `output/build/skiboot-*/`.  Apply the following patch to the sources:

```diff
 core/init.c | 7 +++++++
 1 file changed, 7 insertions(+)

diff --git a/core/init.c b/core/init.c
index 0fe6c168..9da8c3b6 100644
--- a/core/init.c
+++ b/core/init.c
@@ -1015,8 +1015,15 @@ void __noreturn __nomcount main_cpu_entry(const void *fdt)
 		if (parse_hdat(true) < 0)
 			abort();
 	} else if (fdt == NULL) {
+		void *tmp_fdt;
+
 		if (parse_hdat(false) < 0)
 			abort();
+
+		tmp_fdt = create_dtb(dt_root, false);
+		printf("FDT is located at 0x%8p, its size is 0x%08x\n",
+			   tmp_fdt, fdt_totalsize(tmp_fdt));
+		abort();
 	} else {
 		dt_expand(fdt);
 	}
```

### Updating skiboot

Rebuild flash image:

```bash
export KERNEL_BITS=64
. op-build-env
op-build -j7 skiboot-rebuild openpower-pnor-rebuild
```

Possibly extract just the `PAYLOAD` section and update only it (its size is 1
MiB):
```
fcp file:output/images/talos.pnor:PAYLOAD - -o 0 -R > PAYLOAD
```

### Obtaining the dump

Update flash image, start the machine and wait for it to hang in this state:

```
[   36.253046600,5] FDT is located at 0x30423a00, it's size is 0x0001728f
[   36.253141301,0] Aborting!
CPU 0010 Backtrace:
 S: 0000000031c83d10 R: 0000000030013840   .backtrace+0x48
 S: 0000000031c83db0 R: 000000003001b2c4   ._abort+0x4c
 S: 0000000031c83e30 R: 0000000030014c84   .main_cpu_entry+0x13c
 S: 0000000031c83f00 R: 0000000030002730   boot_entry+0x1c0
```

Finally, dump DT in binary format (update address and size in the command!):

```
ssh root@talos pdbg -p0 -c1 -t0 getmem 0x30423a00 0x0001728f > hb-skiboot.dtb
# convert to text
dtc -I dtb -O dts < hb-skiboot.dtb > hb-skiboot.dts
```
