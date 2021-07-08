# MVPD data modification

## Testing environement

All tests were done using pnor image aquired using following command:
```
wget https://cloud.3mdeb.com/index.php/s/canxPx5d4X8c2wk/download -O /tmp/flash.pnor
```

`MVPD` partition was read using `pflash` tool:
```
pflash -P MVPD -r <output file>
```

If `MVPD` partition was zeroed-out for a test, it was done using:
```
pflash -e -P MVPD -p /dev/zero
```

## Test results

* After flashing original pnor image, `MVPD` partition contains some data.
* After running hostboot on default `PNOR` image, `MVPD` partition is modified.
* After running coreboot on default `PNOR` image, `MVPD` is modified
  and is identical to `MVPD` after running hostboot on default `PNOR` image.
* After running coreboot on zeroed-out `MVPD`, `MVPD` is still zeroed
* after running hostboot on zeroed `MVPD`, `MVPD` is modified
  and is different to `MVPD` after running hostboot on default image
  or coreboot on default or zeroed image.
