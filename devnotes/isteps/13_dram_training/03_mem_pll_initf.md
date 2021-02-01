## mem_pll_initf: PLL Initfile for MBAs (13.3)

> a) p9_mem_pll_initf.C (proc chip)
>     - This step is a no-op on cumulus
>     - This step is a no-op if memory is running in synchronous mode since the MCAs are using the nest PLL, HWP detect
>       and exits
>     - MCA PLL setup
>       - Note that Hostboot doesn't support twiddling bits, Looks up which "bucket" (ring) to use from attributes set
>         during mss_freq
>       - Then request the SBE to scan ringId with setPulse
>         - SBE needs to support 5 RS4 images
>         - Data is stored as a ring image in the SBE that is frequency specific
>         - 5 different frequencies (1866, 2133, 2400, 2667, EXP)

```
For each functional Proc:
> if ( !ATTR_MC_SYNC_MODE )
  For each functional MCBIST
    - fapi2::putRing(mbist, ring_id(depends on RAM freq), RING_MODE_SET_PULSE_NSL)
      // FIXME: depending on whether putRing() is used anywhere else, we may implement this as a function or directly
      - /src/user/scan/scandd.C:169   sbeScanPerformOp()
```
