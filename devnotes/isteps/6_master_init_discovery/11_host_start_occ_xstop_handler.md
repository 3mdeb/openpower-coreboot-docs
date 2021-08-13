### OCC starting

*
    OCC is started from `SRAM` using
    ```cpp
    HBOCC::startOCCFromSRAM(masterproc);
    ```
*
    Before starting `OCC`, the image is loaded to SRAM by
    ```cpp
    HBPM::loadPMComplex(
        masterproc,
        l_homerPhysAddrBase,
        l_commonPhysAddr,
        HBPM::PM_LOAD,
        true);
    ```
*
    OCC starting is executed conditionally
    if `CONFIG_IPLTIME_CHECKSTOP_ANALYSIS` config is defined.

    This may indicate, that Hostboot can boot blatform correctly
    without starting `OCC`.

*
    Before starting `OCC`, checkstop's are configured,
    but only if `CONFIG_HANG_ON_MFG_SRC_TERM` config is not defined.

    This may mean, that `OCC` can be started after merely loading
    an image to `SRAM`.

*
    According to comment, `loadPMComplex` function repeats in
    `istep 21.1`
    ```
    //If i_useSRAM is true, then we're in istep 6.11. This address needs
    //to be reset here, so that it's recalculated again in istep 21.1
    //where this function is called.
    ```

*
    `OCC` data is loaded into `HOMER` structure
    ```
    l_errl = loadOCCImageToHomer(i_target,
                                l_occImgPaddr,
                                l_occImgVaddr,
                                i_mode);
    ```

*
    Logic in this part of `Hostboot` doesn't look to be complicated and twisted
    as much as usually.

    Not every function is short and simple but the code tries
    to keep some logic.

*
    One function with cryptic name was found `makeStart405Instruction()`

    It might be the one responsible for calculating instruction
    for starting OCC judging by comment
    ```cpp
    //  ************************************************************
    //  Start OCC PPC405
    //  ************************************************************
    l_errl = makeStart405Instruction(i_proc, &l_start405MainInstr);
    ```

*
    Some `SCOM` access is done using `DeviceWrite()`
    what can be confusing a bit during analysis.
