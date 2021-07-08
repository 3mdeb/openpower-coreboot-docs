# MVPD data modification

## Testing environement

Original `PNOR` image was aquired by building `op-build`.

`MVPD` partition was read using `pflash` tool:
```
pflash -P MVPD -r <output file>
```
## Test results

* After flashing original pnor image, `MVPD` partition
  is empty containg only ECC.
* After running `hostboot` on default `PNOR` image, `MVPD` partition
  is modified.
* After running `hostboot` for a second time, `MVPD` is not modified anymore and
  equal to image after first `hostboot` run.
* After running `coreboot` on default `PNOR` image, `MVPD` is not modified
  and still cleared.
