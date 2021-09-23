## Naming convensions

- HDAT

    `HDAT` or `Hostboot DATa` is the name used by `Hostboot`.

    Specification is mentioned by the source code,
    but accordign to the current knowledge it is not public.
    > "6.1.1 System Parameters" of the Hypervisor Interface Data
    > Specifications document (aka the "HDAT spec)

- SPIRA

    `SPIRA` means `SP Interface Root Array` and is the name used by Skiboot

- SPIRA-S

    `SPIRA-S` most likely refers to `Service processor SPIRA`

    ```cpp
    /* The service processor SPIRA-S structure */
    struct spiras *spiras;
    ```

- SPIRA-H

    `SPIRA-H` most likely refers to `Hypervisor SPIRA`
    ```cpp
    /* The Hypervisor SPIRA-H Structure */
    __section(".spirah.data") struct spirah spirah = {
    ```

    Hostboot uses this name sometimes to reference the HDAT
    > * @brief  Write actual architected register detail to HDAT/SPIRAH


## Dump from memory

- SPIRA-H
    - address: 0x30010400
    - size: 0x200
    ```
    ssh root@talos pdbg -p0 -c1 -t0 getmem 0x30010400 0x200 2>/dev/null > spira-h.bin
    ```
- SPIRA-S
    - address: 0x30010000
    - size: 0x400
    ```
    ssh root@talos pdbg -p0 -c1 -t0 getmem 0x30010000 0x400 2>/dev/null > spira-s.bin
    ```
- SPIRA_HEAP image

    - address: 0x31200000
    - size: 0x00800000
    ```
    ssh root@talos pdbg -p0 -c1 -t0 getmem 0x31200000 0x800000 2>/dev/null > spira-heap.bin
    ```

## Convert to device-tree

Skiboot includes a tool able to convert HDAT into device-tree.
Source is located in the [hdata/test/hdata_to_dt.c](https://github.com/open-power/skiboot/blob/master/hdata/test/hdata_to_dt.c) file.

### Build the tool

To build the `hdata_to_dt`, execute following command in the root
of the Skiboot repository.

```
make check
```

This command should execute `Skiboot` tests and create `hdata_to_dt` binary.
Binary will be located in the `hdata/test/hdata_to_dt` file.

### Using the tool

The tool requires `SPIRA` and `Heap images`

> hdata_to_dt: Converts HDAT dumps to DTB.
>
> Usage:
>         hdata <opts> <spira-dump> <heap-dump>
>         hdata <opts> -s <spirah-dump> <spiras-dump>
> Options:
>         -v Verbose
>         -q Quiet mode
>         -b Keep blobs in the output
>
>   -8 Force PVR to POWER8
>   -8E Force PVR to POWER8E
>   -9 Force PVR to POWER9 (nimbus)
>   -9P Force PVR to POWER9P (Axone)
>   -10 Force PVR to POWER10
>
> When no PVR is specified -8 is assumed
> Pipe to 'dtc -I dtb -O dts' for human readable output

```
hdata/test/hdata_to_dt -9 -s hdata/test/p8-840-spira.spirah hdata/test/op920.wsp.heap
```
