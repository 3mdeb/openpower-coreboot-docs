in istep 21.1

* loadPMComplex
- called for all processors, not only for master
- useSRAM is false

* p9_pm_init
- called with PM_INIT instead PM_RESET

* loadOCCImageToHomer is called instead loadOCCImageDuringIpl

```cpp
void* call_host_runtime_setup (void *io_pArgs)
{
    // Need to wait here until Fsp tells us go
    INITSERVICE::waitForSyncPoint();

    // Enable PM Complex Reset FFDC to HOMER
    TARGETING::Target * sys = nullptr;
    TARGETING::targetService().getTopLevelTarget (sys);
    sys->trySetAttr<ATTR_PM_RESET_FFDC_ENABLE> (0x01);

    // Send the master node frequency attribute info
    // to slave nodes
    sendFreqAttrData();

    // Close PAYLOAD TCEs
    if (TCE::utilUseTcesForDmas())
    {
        TCE::utilClosePayloadTces();
        // Close TCEs on non-master nodes
        closeNonMasterTces();
    }

    // Need to load up the runtime module if it isn't already loaded
    if (!VFS::module_is_loaded( "libruntime.so" ) )
    {
        VFS::module_load( "libruntime.so" );
    }

    //Need to send System Configuration down to SBE for all HB
    //instances
    RUNTIME::sendSBESystemConfig();

#ifdef CONFIG_PLDM
    // On eBMC systems, the PHYP lids were loaded and verified earlier, so
    // need to re-verify/move here.
    if(INITSERVICE::spBaseServicesEnabled())
    {
#endif
        // Verify PAYLOAD and Move PAYLOAD+HDAT from Temporary TCE-related
        // memory region to the proper location
        RUNTIME::verifyAndMovePayload();
#ifdef CONFIG_PLDM
    }
#endif
    // Map the Host Data into the VMM if applicable
    RUNTIME::load_host_data();
#ifdef CONFIG_UCD_FLASH_UPDATES
    POWER_SEQUENCER::TI::UCD::call_update_ucd_flash();
#endif

    // Fill in Hostboot runtime data if there is a PAYLOAD
    if( !(TARGETING::is_no_load()) )
    {
        // API call to fix up the secureboot fields
        RUNTIME::populate_hbSecurebootData();
        // API call to populate the TPM Info fields
        RUNTIME::populate_hbTpmInfo();
    }

    RUNTIME::persistent_rwAttrRuntimeCheck();
#ifdef CONFIG_NVDIMM
    // Update the NVDIMM controller code, if necessary
    // Need to do this after LIDs are accessible
    NVDIMM_UPDATE::call_nvdimm_update();
#endif

#ifdef CONFIG_START_OCC_DURING_BOOT
    bool l_activatePM = TARGETING::is_sapphire_load();
#else
    bool l_activatePM = false;
#endif
    if(l_activatePM)
    {
        TARGETING::Target* l_failTarget = NULL;
        bool pmStartSuccess = true;

        loadAndStartPMAll(HBPM::PM_LOAD, l_failTarget);
#ifdef CONFIG_NVDIMM
        // Arm the nvdimms
        // Only get here if is_sapphire_load
        // and PM started and have NVDIMMs
        TARGETING::TargetHandleList l_nvdimmTargetList;
        TARGETING::TargetHandleList l_procList;
        TARGETING::getAllChips(l_procList, TARGETING::TYPE_PROC, true);

        // Arm nvdimms by proc
        for (auto l_proc : l_procList)
        {
            l_nvdimmTargetList = TARGETING::getProcNVDIMMs(l_proc);
            if (l_nvdimmTargetList.size() != 0)
            {
                NVDIMM::nvdimmArm(l_nvdimmTargetList);
            }
        }
#endif

#ifdef CONFIG_HTMGT
        // Report PM status to HTMGT
        HTMGT::processOccStartStatus(pmStartSuccess,l_failTarget);
#else
        // Verify all OCCs have reached the checkpoint
        if (pmStartSuccess)
        {
            HBPM::verifyOccChkptAll();
        }
#endif
    }
    // No support for OCC
    else if( !Util::isSimicsRunning() )
    {
        //Shouldnt clear this ATTR_PM_FIRINIT_DONE_ONCE_FLAG
        //when we reset pm complex  from here.
        //Reason is this executes during istpe 21.3 then in runtime we do pm
        //reset again so to avoid saving cme fir mask value we shouldn't
        //reset the above attribute
        uint8_t l_skip_fir_attr_reset = 1;
        // Since we are not leaving the PM complex alive, we will
        //  explicitly put it into reset and clean up any memory
        HBPM::resetPMAll(
            HBPM::RESET_AND_CLEAR_ATTRIBUTES,
            l_skip_fir_attr_reset);
    }

#ifdef CONFIG_IPLTIME_CHECKSTOP_ANALYSIS
    if(TARGETING::is_phyp_load() )
    {
        //Explicity clearing the SRAM flag before starting Payload.
        //This tells the OCC bootloader where to pull the OCC image from
        //0: mainstore, 1: SRAM. We want to use mainstore after this point

        //Get master proc
        TARGETING::TargetService & tS = TARGETING::targetService();
        TARGETING::Target* masterproc = NULL;
        tS.masterProcChipTargetHandle( masterproc );

        //Clear (up to and including the IPL flag)
        size_t sz_data = HBOCC::OCC_OFFSET_IPL_FLAG + 6;
        size_t sz_dw   = sizeof(uint64_t);
        uint64_t l_occAppData[(sz_data+(sz_dw-1))/sz_dw];
        memset(l_occAppData, 0x00, sizeof(l_occAppData) );
        const uint32_t l_SramAddrApp = HBOCC::OCC_405_SRAM_ADDRESS;
        HBOCC::writeSRAM(
            masterproc,
            l_SramAddrApp,
            l_occAppData,
            sz_data);
    }
#endif

    if( TARGETING::is_sapphire_load()
    && (!INITSERVICE::spBaseServicesEnabled()) )
    {
        //@fixme-RTC:172836-broken for HDAT mode?
        // Update the VPD switches for golden side boot
        // Must do this before building the devtree
        VPD::goldenSwitchUpdate();
    }

    // Update the MDRT Count and PDA Table Entries from Attribute
    TargetService& l_targetService = targetService();
    Target* l_sys = nullptr;
    l_targetService.getTopLevelTarget(l_sys);

    // Default captured data to 0s -- MPIPL if check fills in if
    // valid
    uint32_t threadRegSize = sizeof(DUMP::hostArchRegDataHdr)+
                            (95 * sizeof(DUMP::hostArchRegDataEntry));
    uint8_t threadRegFormat = REG_DUMP_SBE_HB_STRUCT_VER;
    uint64_t capThreadArrayAddr = 0;
    uint64_t capThreadArraySize = 0;

    if(l_sys->getAttr<ATTR_IS_MPIPL_HB>())
    {
        uint32_t l_mdrtCount = l_sys->getAttr<TARGETING::ATTR_MPIPL_HB_MDRT_COUNT>();
        //Update actual count in RUNTIME
        if(l_mdrtCount)
        {
            RUNTIME::saveActualCount( RUNTIME::MS_DUMP_RESULTS_TBL, l_mdrtCount);
        }


        threadRegSize = l_sys->getAttr<TARGETING::ATTR_PDA_THREAD_REG_ENTRY_SIZE>();
        threadRegFormat = l_sys->getAttr<TARGETING::ATTR_PDA_THREAD_REG_STATE_ENTRY_FORMAT>();
        capThreadArrayAddr = l_sys->getAttr<TARGETING::ATTR_PDA_CAPTURED_THREAD_REG_ARRAY_ADDR>();
        capThreadArraySize = l_sys->getAttr<TARGETING::ATTR_PDA_CAPTURED_THREAD_REG_ARRAY_SIZE>();
    }

    // Ignore return value
    RUNTIME::updateHostProcDumpActual( RUNTIME::PROC_DUMP_AREA_TBL,
                                        threadRegSize, threadRegFormat,
                                        capThreadArrayAddr, capThreadArraySize);


    //Update the MDRT value (for MS Dump)
    RUNTIME::writeActualCount(RUNTIME::MS_DUMP_RESULTS_TBL);
    // Fill in Hostboot runtime data for all nodes
    // (adjunct partition)
    // Write the HB runtime data into mainstore
    RUNTIME::populate_hbRuntimeData();

    if( !INITSERVICE::spBaseServicesEnabled() )
    {
        // Invalidate the VPD cache for golden side boot
        // Also invalidate in manufacturing mode
        // Must do this after saving away the VPD cache into mainstore,
        //  i.e. after RUNTIME::populate_hbRuntimeData()
        VPD::goldenCacheInvalidate();
    }

    if (TCE::utilUseTcesForDmas())
    {
        // Disable all TCEs
        TCE::utilDisableTces();
    }
}

errlHndl_t loadAndStartPMAll(loadPmMode i_mode,
                                TARGETING::Target* & o_failTarget)
{
    errlHndl_t l_errl = nullptr;

    TARGETING::Target * l_sys = nullptr;
    TARGETING::targetService().getTopLevelTarget( l_sys );
    assert(l_sys != nullptr);

    TargetHandleList l_procChips;
    getAllChips(l_procChips, TYPE_PROC, true);

    uint64_t l_homerPhysAddr = 0x0;
    uint64_t l_commonPhysAddr = 0x0;

    // Switching core checkstops from unit to system
    TARGETING::TargetHandleList l_coreTargetList;
    getAllChips(l_coreTargetList, TYPE_CORE);

    if(is_sapphire_load())
    {
        for( auto l_core_target : l_coreTargetList )
        {
            core_checkstop_helper_hwp(l_core_target, true);
        }
    }

    for (const auto & l_procChip: l_procChips)
    {
        // This attr was set during istep15 HCODE build
        l_homerPhysAddr = l_procChip->getAttr<TARGETING::ATTR_HOMER_PHYS_ADDR>();
        l_commonPhysAddr = l_sys->getAttr<TARGETING::ATTR_OCC_COMMON_AREA_PHYS_ADDR>();

        loadPMComplex( // common
            l_procChip,
            l_homerPhysAddr,
            l_commonPhysAddr,
            i_mode);
        startPMComplex(l_procChip);
    }

    if(is_sapphire_load())
    {
        core_checkstop_helper_homer();
    }
}

errlHndl_t startPMComplex(Target* i_target)
{
    TARGETING::Target * l_sys = nullptr;
    TARGETING::targetService().getTopLevelTarget(l_sys);
    assert(l_sys != nullptr);

    //Get homer image buffer
    uint64_t l_homerPhysAddr = 0x0;
    l_homerPhysAddr = i_target->getAttr<TARGETING::ATTR_HOMER_PHYS_ADDR>();
    void* l_homerVAddr = convertHomerPhysToVirt(i_target,l_homerPhysAddr);

    // cast OUR type of target to a FAPI type of target.
    // figure out homer offsets
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>
        l_fapiTarg(i_target);

    // Init path
    // p9_pm_init.C enum: PM_INIT
    if (TARGETING::is_phyp_load())
    {
        l_sys->setAttr <TARGETING::ATTR_PM_MALF_ALERT_ENABLE> (0x1);
    }

    pm_init(
        l_fapiTarg,
        l_homerVAddr);

    if (TARGETING::is_phyp_load() && nullptr != l_homerVAddr)
    {
            int lRc = HBPM_UNMAP(l_homerVAddr);
            uint64_t lZeroAddr = 0;
            i_target->setAttr<ATTR_HOMER_VIRT_ADDR>(reinterpret_cast<uint64_t>(lZeroAddr));
    }
}

fapi2::ReturnCode pm_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    void* i_pHomerImage)
{
    fapi2::ReturnCode l_rc;
    fapi2::ATTR_PM_MALF_CYCLE_Type l_malfCycle =
        fapi2::ENUM_ATTR_PM_MALF_CYCLE_INACTIVE;
    fapi2::ATTR_PM_MALF_ALERT_ENABLE_Type malfAlertEnable =
        fapi2::ENUM_ATTR_PM_MALF_ALERT_ENABLE_FALSE;
    fapi2::buffer<uint64_t> l_data64       = 0;
    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM;

    p9_pm_corequad_init(
        i_target,
        p9pm::PM_INIT,
        0,//CME FIR MASK for reset
        0,//Core Error Mask for reset
        0); //Quad Error Mask for reset
    p9_pm_ocb_init(
        i_target,
        p9pm::PM_INIT,// Channel setup type
        p9ocb::OCB_CHAN1,// Channel
        p9ocb:: OCB_TYPE_NULL,// Channel type
        0,// Channel base address
        0,// Push/Pull queue length
        p9ocb::OCB_Q_OUFLOW_NULL,// Channel flow control
        p9ocb::OCB_Q_ITPTYPE_NULL);// Channel interrupt control
    p9_pm_pss_init(i_target, p9pm::PM_INIT);
    p9_pm_occ_firinit(i_target, p9pm::PM_INIT);
    p9_pm_firinit(i_target, p9pm::PM_INIT);
    p9_pm_stop_gpe_init(i_target, p9pm::PM_INIT);
    p9_pm_pstate_gpe_init(i_target, p9pm::PM_INIT);
    if (i_pHomerImage != NULL)
    {
        p9_check_proc_config(i_target, i_pHomerImage);
    }
    clear_occ_special_wakeups(i_target);
    FAPI_ATTR_GET (fapi2::ATTR_PM_MALF_CYCLE, i_target, l_malfCycle));
    if (l_malfCycle == fapi2::ENUM_ATTR_PM_MALF_CYCLE_INACTIVE)
    {
        special_wakeup_all(i_target, false);
    }
    else
    {
        FAPI_ATTR_SET(
            fapi2::ATTR_PM_MALF_CYCLE,
            i_target,
            fapi2::ENUM_ATTR_PM_MALF_CYCLE_INACTIVE));
    }
    p9_pm_occ_control(
        i_target,
        p9occ_ctrl::PPC405_START,// Operation on PPC405
        p9occ_ctrl::PPC405_BOOT_MEM, // PPC405 boot location
        0); //Jump to 405 main instruction - not used here

    FAPI_ATTR_GET(
        fapi2::ATTR_PM_MALF_ALERT_ENABLE,
        FAPI_SYSTEM,
        malfAlertEnable);
    l_data64.flush<0>().setBit<p9hcd::STOP_RECOVERY_TRIGGER_ENABLE>();

    if (malfAlertEnable == fapi2::ENUM_ATTR_PM_MALF_ALERT_ENABLE_TRUE)
    {
        fapi2::putScom(i_target, PU_OCB_OCI_OCCFLG2_SET, l_data64);
    }
    else
    {
        fapi2::putScom(i_target, PU_OCB_OCI_OCCFLG2_CLEAR, l_data64);
    }
}

fapi2::ReturnCode p9_check_proc_config ( CONST_FAPI2_PROC& i_procTgt, void* i_pHomerImage )
{
    FAPI_INF( ">> p9_check_proc_config" );

    uint64_t l_configVectVal = INIT_CONFIG_VALUE;
    uint8_t* pHomer = (uint8_t*)i_pHomerImage + QPMR_HOMER_OFFSET +
                      QPMR_PROC_CONFIG_POS;
    uint8_t l_chipName = 0;
#ifndef __HOSTBOOT_MODULE
    g_targetTypeMap[fapi2::TARGET_TYPE_MCS]         =   "MCS";
    g_targetTypeMap[fapi2::TARGET_TYPE_MCA]         =   "MCA";
    g_targetTypeMap[fapi2::TARGET_TYPE_XBUS]        =   "XBUS";
    g_targetTypeMap[fapi2::TARGET_TYPE_OBUS]        =   "OBUS";
    g_targetTypeMap[fapi2::TARGET_TYPE_CAPP]        =   "CAPP";
    g_targetTypeMap[fapi2::TARGET_TYPE_PHB]         =   "PHB";
    g_targetTypeMap[fapi2::TARGET_TYPE_MEMBUF_CHIP] =   "MEM BUF";
    g_targetTypeMap[fapi2::TARGET_TYPE_MBA]         =   "MBA";
    g_targetTypeMap[fapi2::TARGET_TYPE_OBUS_BRICK]  =   "OBUS_BRICK";
#endif

    FAPI_TRY( validateInputArgs( i_procTgt, i_pHomerImage ),
              "Input Arguments Found Invalid" );

    FAPI_TRY( checkChiplet< fapi2::TARGET_TYPE_MCS >
              ( i_procTgt, fapi2::TARGET_TYPE_MCS, l_configVectVal, MCS_POS ),
              "Failed to get MCS configuration" );

    FAPI_TRY( checkChiplet< fapi2::TARGET_TYPE_XBUS>
              ( i_procTgt, fapi2::TARGET_TYPE_XBUS, l_configVectVal, XBUS_POS ),
              "Failed to get XBUS configuration" );

    FAPI_TRY( checkChiplet<fapi2::TARGET_TYPE_PHB>
              ( i_procTgt, fapi2::TARGET_TYPE_PHB, l_configVectVal, PHB_POS ),
              "Failed to get PHB configuration" );

    FAPI_TRY( checkChiplet<fapi2::TARGET_TYPE_CAPP>
              ( i_procTgt, fapi2::TARGET_TYPE_CAPP, l_configVectVal, CAPP_POS ),
              "Failed to get CAPP configuration" );

    FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_NAME, i_procTgt, l_chipName );

    if( fapi2::ENUM_ATTR_NAME_NIMBUS == l_chipName )
    {
        FAPI_TRY(  checkObusChipletHierarchy ( i_procTgt, l_configVectVal, OBUS_POS, NVLINK_POS ),
                   "Failed to get OBUS Hierarchy configuration" );
    }
    else
    {
        FAPI_TRY( checkAbusConfig( i_procTgt, l_configVectVal ) ,
                  "Failed to get Abus configuration" );
    }

    if( fapi2::ENUM_ATTR_NAME_CUMULUS == l_chipName )
    {
        FAPI_TRY( checkMemConfig( i_procTgt, l_configVectVal ),
                  "Failed to get Memory  configuration" );
    }
    else
    {
        //FIXME: RTC 180154 Needs Review for Axone

        FAPI_TRY( checkChiplet< fapi2::TARGET_TYPE_MCA >
                  ( i_procTgt, fapi2::TARGET_TYPE_MCA, l_configVectVal, MBA_POS ),
                  "Failed to get MCA Position" );
    }

    FAPI_INF( "Updating Vector in HOMER" );
    *(uint64_t*)pHomer  = htobe64( l_configVectVal );

    FAPI_IMP( "Config Vector is 0x%016lx  ", l_configVectVal );
    FAPI_IMP( "Reading back 0x%016lx  ", htobe64( (*(uint64_t*)pHomer)) );

fapi_try_exit:

    FAPI_INF( "<< p9_check_proc_config" );
    return fapi2::current_err;
}
```
