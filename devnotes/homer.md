## Homer filled by Hostboot
Homer documentation is
[available here](https://github.com/open-power/docs/blob/master/occ/p9_pmcd_homer.pdf).

Homer dump after Hostboot fills the the structure with the data is
[available here](https://cloud.3mdeb.com/index.php/s/cNZJYE9ysgSaebJ).

## Dumping Homer image

To create a memdump of Homer:

1.
    Log in to BMC.

2.
    Check if you have at least 4MB of space available in `\tmp`.
    If not, remove unimportant files from `/tmp` to free up space.

    ```
    $ df
    ```
3.
    (Optional) Check if Homer is located it the correct address.
    ```
    pdbg -p0 -c1 -t0 getmem 0x11fd800000 0x100 2>/dev/null | hexdump -C
    ```
    > Note: If OCC Image is not filled yet, the memory will still be zeroed.

4.
    Dump Homer image. It should take approximately 40-50 minutes.

    ```
    pdbg -p0 -c1 -t0 getmem 0x11fd800000 0x400000 2>/dev/null >/tmp/homer.bin
    ```

## Homer dumped at istep 15.2

[Dumped image](https://cloud.3mdeb.com/index.php/s/HDNikYe7Jmfc7Pg)

Homer was dumped after executing
istep 15.2 `proc_set_pba_homer_bar` in Hostboot.
Code was stopped after 15.2 in `proc_set_pba_homer_bar` function.

```
            TRACFCOMP(ISTEPS_TRACE::g_trac_isteps_trace,
                      "proc_set_pba_homer_bar: unsecure HOMER addr = 0x%.16llX",
                      l_unsecureHomerAddr);
        }

    }
    for(;;);

    TRACFCOMP( ISTEPS_TRACE::g_trac_isteps_trace, "call_proc_set_pba_homer_bar exit" );
    // end task, returning any errorlogs to IStepDisp
    return l_StepError.getErrorHandle();
}
```

### Building modified Hostboot image

Modified Hostboot was build in the following way:

1.
    Build the official Hostboot image:
    ```
    $ git clone -b raptor-v2.00 --recursive https://scm.raptorcs.com/scm/git/talos-op-build
    $ cd talos-op-build
    $ git checkout raptor-v2.00
    $ git submodule update
    $ . op-build-env
    $ export KERNEL_BITS=64
    $ op-build talos_defconfig
    $ op-build
    ```

2.
    Modify `Hostboot` inside the `output/build/hostboot-<hash>` directory.

3.
    Rebuild `Hostboot`
    ```
    $ rm -rf output/build/machine-xml-*
    $ rm -rf output/build/hostboot-*
    $ op-build hostboot-rebuild openpower-pnor-rebuild
    ```
