Power management common functions are used in both 6.11 and 21.1 isteps.

```cpp
static void loadPMComplex(
    TARGETING::Target * i_target,
    uint64_t i_homerPhysAddr,
    uint64_t i_commonPhysAddr)
{
    resetPMComplex(i_target);
    void* l_homerVAddr = convertHomerPhysToVirt(i_target, i_homerPhysAddr);
    if(nullptr == l_homerVAddr)
    {
        return;
    }
    uint64_t l_occImgPaddr = i_homerPhysAddr + HOMER_OFFSET_TO_OCC_IMG;
    uint64_t l_occImgVaddr = l_homerVAddr + HOMER_OFFSET_TO_OCC_IMG;
    loadOCCSetup(i_target, l_occImgPaddr, l_occImgVaddr, i_commonPhysAddr);
#ifdef CONFIG_IPLTIME_CHECKSTOP_ANALYSIS
    HBOCC::loadOCCImageDuringIpl(i_target, l_occImgVaddr); // analyzed
#endif
#if defined(CONFIG_IPLTIME_CHECKSTOP_ANALYSIS) && !defined(__HOSTBOOT_RUNTIME)
    HBOCC::loadHostDataToSRAM(i_target);
#else
    loadHostDataToHomer(i_target, l_occImgVaddr + HOMER_OFFSET_TO_OCC_HOST_DATA);
    loadHcode(i_target, l_homerVAddr, HBPM::PM_LOAD);
#endif
}
```
