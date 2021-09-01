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

StopReturnCode_t p9_stop_save_scom( void* const   i_pImage,
                                    const uint32_t i_scomAddress,
                                    const uint64_t i_scomData,
                                    const ScomOperation_t i_operation,
                                    const ScomSection_t i_section )
{
    return proc_stop_save_scom( i_pImage, i_scomAddress,
                                i_scomData, i_operation, i_section );
}

uint64_t p9_scominfo_createChipUnitScomAddr(const p9ChipUnits_t i_p9CU, const uint8_t i_chipUnitNum,
        const uint64_t i_scomAddr, const uint32_t i_mode)
{
    p9_scom_addr l_scom(i_scomAddr);
    uint8_t l_ring = l_scom.get_ring();
    uint8_t l_chiplet_id = l_scom.get_chiplet_id();
    uint8_t l_sat_id = l_scom.get_sat_id();
    uint8_t l_sat_offset = l_scom.get_sat_offset();

    if ((i_mode & PPE_MODE) == PPE_MODE)
    {
        switch (i_p9CU)
        {

            case PU_EX_CHIPUNIT:
                if (PPE_EP05_CHIPLET_ID >= l_scom.get_chiplet_id() &&
                    l_scom.get_chiplet_id() >= PPE_EP00_CHIPLET_ID)
                {
                    l_scom.set_chiplet_id(PPE_EP00_CHIPLET_ID + (i_chipUnitNum / 2));
                    l_scom.set_port( ( i_chipUnitNum % 2 ) + 1 );
                }
                break;

            default:
                l_scom.set_addr(FAILED_TRANSLATION);
                break;
        }
    }
    else
    {
        switch (i_p9CU)
        {
            case PU_PERV_CHIPUNIT:
                l_scom.set_chiplet_id(i_chipUnitNum);
                break;

            case PU_C_CHIPUNIT:
                l_scom.set_chiplet_id(EC00_CHIPLET_ID + i_chipUnitNum);
                break;

            case PU_EX_CHIPUNIT:
                if (EP05_CHIPLET_ID >= l_scom.get_chiplet_id() &&
                    l_scom.get_chiplet_id() >= EP00_CHIPLET_ID)
                {
                    l_scom.set_chiplet_id(EP00_CHIPLET_ID + (i_chipUnitNum / 2));
                    uint8_t l_ringId = (l_scom.get_ring() & 0xF); // Clear bits 16:17
                    l_ringId = ( l_ringId - ( l_ringId % 2 ) ) + ( i_chipUnitNum % 2 );
                    l_scom.set_ring( l_ringId & 0xF );
                }
                else if (EC23_CHIPLET_ID >= l_scom.get_chiplet_id() &&
                            l_scom.get_chiplet_id() >= EC00_CHIPLET_ID)
                {
                    l_scom.set_chiplet_id( EC00_CHIPLET_ID +
                                            (l_scom.get_chiplet_id() % 2) +
                                            (i_chipUnitNum * 2));
                }

                break;

            case PU_EQ_CHIPUNIT:
                l_scom.set_chiplet_id(EP00_CHIPLET_ID + i_chipUnitNum);
                break;

            case PU_CAPP_CHIPUNIT:
                l_scom.set_chiplet_id(N0_CHIPLET_ID + (i_chipUnitNum * 2));
                break;

            case PU_MCS_CHIPUNIT:
                l_scom.set_chiplet_id(N3_CHIPLET_ID - (2 * (i_chipUnitNum / 2)));
                l_scom.set_sat_id(2 * (i_chipUnitNum % 2));
                break;

            case PU_MCBIST_CHIPUNIT:
                l_scom.set_chiplet_id(MC01_CHIPLET_ID + i_chipUnitNum);
                break;

            case PU_MCA_CHIPUNIT:
                if (l_scom.get_chiplet_id() == MC01_CHIPLET_ID || l_scom.get_chiplet_id() ==  MC23_CHIPLET_ID)
                {
                    l_scom.set_chiplet_id(MC01_CHIPLET_ID + (i_chipUnitNum / 4));

                    if ( (l_scom.get_ring() & 0xF) == MC_MC01_0_RING_ID)
                    {
                        // mc
                        l_scom.set_sat_id( ( l_scom.get_sat_id() - ( l_scom.get_sat_id() % 4 ) ) +
                                            ( i_chipUnitNum % 4 ));
                    }
                    else
                    {
                        // iomc
                        l_scom.set_ring( (MC_IOM01_0_RING_ID + (i_chipUnitNum % 4)) & 0xF );
                    }
                }
                else
                {
                    //mcs->mca regisers
                    uint8_t i_mcs_unitnum = ( i_chipUnitNum / 2 );
                    l_scom.set_chiplet_id(N3_CHIPLET_ID - (2 * (i_mcs_unitnum / 2)));
                    l_scom.set_sat_id(2 * (i_mcs_unitnum % 2));
                    uint8_t i_mcs_sat_offset = (0x2F & l_scom.get_sat_offset());
                    i_mcs_sat_offset |= ((i_chipUnitNum % 2) << 4);
                    l_scom.set_sat_offset(i_mcs_sat_offset);
                }

                break;

            case PU_MC_CHIPUNIT:
                l_scom.set_chiplet_id(MC01_CHIPLET_ID + i_chipUnitNum);
                break;

            case PU_MI_CHIPUNIT:
                //-------------------------------------------
                // MI
                //-------------------------------------------
                //          Chiplet   Ring   Satid   Off
                //MCS0           05     02       0   !SCOM3
                //MCS1           05     02       2   !SCOM3
                //MCS2           03     02       0   !SCOM3
                //MCS3           03     02       2   !SCOM3
                l_scom.set_chiplet_id(N3_CHIPLET_ID - (2 * (i_chipUnitNum / 2)));
                l_scom.set_sat_id(2 * (i_chipUnitNum % 2));
                break;

            case PU_DMI_CHIPUNIT:
                if (((l_chiplet_id == N3_CHIPLET_ID) || (l_chiplet_id == N1_CHIPLET_ID)))
                {
                    //SCOM3   (See mc_clscom_rlm.fig <= 0xB vs mc_scomfir_rlm.fig > 0xB)
                    //DMI0           05     02       0   0x2X (X <= 0xB)
                    //DMI1           05     02       0   0x3X (X <= 0xB)
                    //DMI2           05     02       2   0x2X (X <= 0xB)
                    //DMI3           05     02       2   0x3X (X <= 0xB)
                    //DMI4           03     02       0   0x2X (X <= 0xB)
                    //DMI5           03     02       0   0x3X (X <= 0xB)
                    //DMI6           03     02       2   0x2X (X <= 0xB)
                    //DMI7           03     02       2   0x3X (X <= 0xB)
                    l_scom.set_chiplet_id(N3_CHIPLET_ID - (2 * (i_chipUnitNum / 4)));
                    l_scom.set_sat_id(2 * ((i_chipUnitNum / 2) % 2));
                    l_sat_offset = (l_sat_offset & 0xF) + ((2 + (i_chipUnitNum % 2)) << 4);
                    l_scom.set_sat_offset(l_sat_offset);
                }

                if (((l_chiplet_id == MC01_CHIPLET_ID) || (l_chiplet_id == MC23_CHIPLET_ID)))
                {
                    //-------------------------------------------
                    // DMI
                    //-------------------------------------------
                    //SCOM1,2
                    //DMI0           07     02       0
                    //DMI1           07     02       1
                    //DMI2           07     02       2
                    //DMI3           07     02       3
                    //DMI4           08     02       0
                    //DMI5           08     02       1
                    //DMI6           08     02       2
                    //DMI7           08     02       3
                    if (l_ring == P9C_MC_CHAN_RING_ID)
                    {
                        l_scom.set_chiplet_id(MC01_CHIPLET_ID + (i_chipUnitNum / 4));
                        uint8_t l_msat = l_scom.get_sat_id();
                        l_msat = l_msat & 0xC;
                        l_scom.set_sat_id(l_msat + i_chipUnitNum % 4);
                    }

                    //SCOM4
                    //DMI0           07     08     0xD   0x0X
                    //DMI1           07     08     0xD   0x1X
                    //DMI2           07     08     0xD   0x2X
                    //DMI3           07     08     0xD   0x3X
                    //DMI4           08     08     0xD   0x0X
                    //DMI5           08     08     0xD   0x1X
                    //DMI6           08     08     0xD   0x2X
                    //DMI7           08     08     0xD   0x3X
                    if (l_ring == P9C_MC_BIST_RING_ID)
                    {
                        l_scom.set_chiplet_id(MC01_CHIPLET_ID + (i_chipUnitNum / 4));
                        l_sat_offset = (l_sat_offset & 0xF) + ((i_chipUnitNum % 4) << 4);
                        l_scom.set_sat_offset(l_sat_offset);
                    }

                    //-------------------------------------------
                    // DMI IO
                    //-------------------------------------------
                    //          Chiplet   Ring   Satid    Off    RXTXGrp
                    //DMI0           07     04       0   0x3F       0x00
                    //DMI1           07     04       0   0x3F       0x01
                    //DMI2           07     04       0   0x3F       0x02
                    //DMI3           07     04       0   0x3F       0x03
                    //DMI4           08     04       0   0x3F       0x00
                    //DMI5           08     04       0   0x3F       0x01
                    //DMI6           08     04       0   0x3F       0x02
                    //DMI7           08     04       0   0x3F       0x03

                    //DMI0           07     04       0   0x3F       0x20
                    //DMI1           07     04       0   0x3F       0x21
                    //DMI2           07     04       0   0x3F       0x22
                    //DMI3           07     04       0   0x3F       0x23
                    //DMI4           08     04       0   0x3F       0x20
                    //DMI5           08     04       0   0x3F       0x21
                    //DMI6           08     04       0   0x3F       0x22
                    //DMI7           08     04       0   0x3F       0x23
                    //
                    //0 MC01.CHAN0  IOM01.TX_WRAP.TX3
                    //1 MC01.CHAN1  IOM01.TX_WRAP.TX2
                    //2 MC01.CHAN2  IOM01.TX_WRAP.TX0
                    //3 MC01.CHAN3  IOM01.TX_WRAP.TX1
                    //4 MC23.CHAN0  IOM23.TX_WRAP.TX3
                    //5 MC23.CHAN1  IOM23.TX_WRAP.TX2
                    //6 MC23.CHAN2  IOM23.TX_WRAP.TX0
                    //7 MC23.CHAN3  IOM23.TX_WRAP.TX1
                    // 3, 2, 0, 1
                    if (l_ring == P9C_MC_IO_RING_ID)
                    {
                        l_scom.set_chiplet_id(MC01_CHIPLET_ID + (i_chipUnitNum / 4));
                        uint8_t l_rxtx_grp = l_scom.get_rxtx_group_id();
                        l_rxtx_grp = l_rxtx_grp & 0xF0;

                        switch ((i_chipUnitNum % 4))
                        {
                            case  0:
                                l_rxtx_grp += 3;
                                break;

                            case  1:
                                l_rxtx_grp += 2;
                                break;

                            case  2:
                                l_rxtx_grp += 0;
                                break;

                            case  3:
                                l_rxtx_grp += 1;
                                break;

                            default:
                                //escape to bunker - math broke
                                break;
                        }

                        l_scom.set_rxtx_group_id(l_rxtx_grp); // 3,2,0,1
                    }

                }

                break;

            case PU_MCC_CHIPUNIT:
                if (((l_chiplet_id == N3_CHIPLET_ID) || (l_chiplet_id == N1_CHIPLET_ID)))
                {
                    //SCOM3   (See mc_clscom_rlm.fig <= 0xB vs mc_scomfir_rlm.fig > 0xB)
                    //DMI0           05     02       0   0x2X (X <= 0xB)
                    //DMI1           05     02       0   0x3X (X <= 0xB)
                    //DMI2           05     02       2   0x2X (X <= 0xB)
                    //DMI3           05     02       2   0x3X (X <= 0xB)
                    //DMI4           03     02       0   0x2X (X <= 0xB)
                    //DMI5           03     02       0   0x3X (X <= 0xB)
                    //DMI6           03     02       2   0x2X (X <= 0xB)
                    //DMI7           03     02       2   0x3X (X <= 0xB)
                    l_scom.set_chiplet_id(N3_CHIPLET_ID - (2 * (i_chipUnitNum / 4)));
                    l_scom.set_sat_id(2 * ((i_chipUnitNum / 2) % 2));
                    uint8_t l_satoff = (l_sat_offset & 0xF) + ((2 + (i_chipUnitNum % 2)) << 4);
                    l_scom.set_sat_offset(l_satoff);
                }

                if (((l_chiplet_id == MC01_CHIPLET_ID) || (l_chiplet_id == MC23_CHIPLET_ID)))
                {
                    //CHANX.USTL.  Sat_id: 10 + port_id (8,9,10,11)
                    //CHANX.DSTL.  Sat_id: 01 + port_id (4,5,6,7)
                    l_scom.set_chiplet_id(MC01_CHIPLET_ID + (i_chipUnitNum / 4));

                    if (l_ring == P9A_MC_CHAN_RING_ID)
                    {

                        if (P9A_MC_DSTL_CHAN0_SAT_ID <= l_sat_id && l_sat_id <= P9A_MC_DSTL_CHAN3_SAT_ID)
                        {
                            l_scom.set_sat_id(P9A_MC_DSTL_CHAN0_SAT_ID + (i_chipUnitNum % 4));
                        }

                        if (P9A_MC_USTL_CHAN0_SAT_ID <= l_sat_id && l_sat_id <= P9A_MC_USTL_CHAN3_SAT_ID)
                        {
                            l_scom.set_sat_id(P9A_MC_USTL_CHAN0_SAT_ID + (i_chipUnitNum % 4));
                        }
                    }

                    if (l_ring == P9A_MC_MC01_RING_ID && l_sat_id == P9A_MC_CHAN_SAT_ID)
                    {
                        l_scom.set_sat_offset((i_chipUnitNum * 16) + (l_sat_offset % 16));
                    }
                }

                break;

            case PU_OMIC_CHIPUNIT:
                l_scom.set_chiplet_id(MC01_CHIPLET_ID + (i_chipUnitNum / 3));

                if (P9A_MC_OMIC0_RING_ID <= l_ring && l_ring <= P9A_MC_OMIC2_RING_ID)
                {
                    l_scom.set_ring(P9A_MC_OMIC0_RING_ID + (i_chipUnitNum % 3));
                }

                if (P9A_MC_OMIC0_PPE_RING_ID <= l_ring && l_ring <= P9A_MC_OMIC2_PPE_RING_ID)
                {
                    l_scom.set_ring(P9A_MC_OMIC0_PPE_RING_ID + (i_chipUnitNum % 3));
                }

                if (P9A_MC_OMI_DL_RING_ID == l_ring)
                {
                    l_scom.set_sat_id(P9A_MC_DL_REG0_SAT_ID + (i_chipUnitNum % 3));
                }

                break;

            case PU_OMI_CHIPUNIT:
                {
                    //Unswizzle the OMI chip unit number
                    uint8_t l_chipUnitNum = P9_SCOMINFO_OMI_UNSWIZZLE[i_chipUnitNum];

                    l_scom.set_chiplet_id(MC01_CHIPLET_ID + (l_chipUnitNum / 8));

                    if (P9A_MC_OMIC0_RING_ID <= l_ring && l_ring <= P9A_MC_OMIC2_RING_ID)
                    {
                        // IO Ind regsiters  (Ring 4,5,6) reg == 63
                        // 0701103F MCP_OMI0. Lanes 0-7    omi0
                        // 0701103F MCP_OMI0. Lanes 8-15   omi1
                        // 0701103F MCP_OMI0. Lanes 16-23  omi2
                        // 0701143F MCP_OMI1. Lanes 0-7    omi3
                        // 0701143F MCP_OMI1. Lanes 8-15   omi4
                        // 0701143F MCP_OMI1. Lanes 16-23  omi5
                        // 0701183F MCP_OMI2. Lanes 0-7    omi6
                        // 0701183F MCP_OMI2. Lanes 8-15   omi7
                        l_scom.set_ring(P9A_MC_OMIC0_RING_ID + ((l_chipUnitNum % 8) / 3));
                        uint8_t l_lane = l_scom.get_lane_id();
                        l_lane = l_lane % 8;
                        uint8_t l_chipnum = l_chipUnitNum;

                        if (l_chipnum >= 8)
                        {
                            l_chipnum++;
                        }

                        l_scom.set_lane_id(((l_chipnum % 3) * 8) + l_lane);
                    }

                    if (l_ring == P9A_MC_OMI_DL_RING_ID)
                    {
                        // DL Registers
                        // reg0 dl0 -> omi0  ring: 12  sat_id: 13  regs 16..31
                        // reg0 dl1 -> omi1  ring: 12  sat_id: 13  regs 32..47
                        // reg0 dl2 -> omi2  ring: 12  sat_id: 13  regs 48..63
                        // reg1 dl0 -> omi3  ring: 12  sat_id: 14  regs 16..31
                        // reg1 dl1 -> omi4  ring: 12  sat_id: 14  regs 32..47
                        // reg1 dl2 -> omi5  ring: 12  sat_id: 14  regs 48..63
                        // reg2 dl0 -> omi6  ring: 12  sat_id: 15  regs 16..31
                        // reg2 dl1 -> omi7  ring: 12  sat_id: 15  regs 32..47
                        l_scom.set_sat_id(P9A_MC_DL_REG0_SAT_ID + ((l_chipUnitNum % 8) / 3));
                        uint8_t l_satoff = l_sat_offset % 16;
                        uint8_t l_chipnum = l_chipUnitNum;

                        if (l_chipnum >= 8)
                        {
                            l_chipnum++;
                        }

                        l_scom.set_sat_offset((((l_chipnum % 3) * 16) + P9A_MC_DL_OMI0_FRST_REG) +
                                                l_satoff);
                    }
                }
                break;

            case PU_NV_CHIPUNIT:
                if (i_mode == P9N_DD1_SI_MODE)
                {
                    l_scom.set_sat_id((l_scom.get_sat_id() % 4) + ((i_chipUnitNum / 2) * 4));
                    l_scom.set_sat_offset( (l_scom.get_sat_offset() % 32) +
                                            (32 * (i_chipUnitNum % 2)));
                }
                else if (i_mode != P9A_DD1_SI_MODE && i_mode != P9A_DD2_SI_MODE)
                {
                    uint64_t l_sa = i_scomAddr;

                    //                       rrrrrrSTIDxxx---
                    //                       000100       yyy
                    //       x"4900" & "00" when "00100010", -- stk0, ntl0, 00-07, hyp-only
                    //       x"0b00" & "11" when "00101001", -- stk0, ntl1, 24-31, user-acc
                    //                             STID
                    //       x"5900" & "00" when "01100010", -- stk1, ntl0, 00-07, hyp-only
                    //       x"1b00" & "11" when "01101001", -- stk1, ntl1, 24-31, user-acc
                    //                             STID
                    //       x"6900" & "00" when "10100010", -- stk2, ntl0, 00-07, hyp-only
                    //       x"2b00" & "11" when "10101001", -- stk2, ntl1, 24-31, user-acc

                    if ((i_chipUnitNum / 2) == 0)
                    {
                        l_sa = (l_sa & 0xFFFFFFFFFFFF007FULL) | 0x0000000000001100ULL ;
                    }

                    if ((i_chipUnitNum / 2) == 1)
                    {
                        l_sa = (l_sa & 0xFFFFFFFFFFFF007FULL) | 0x0000000000001300ULL ;
                    }

                    if ((i_chipUnitNum / 2) == 2)
                    {
                        l_sa = (l_sa & 0xFFFFFFFFFFFF007FULL) | 0x0000000000001500ULL ;
                    }

                    uint64_t l_eo = (l_sa & 0x71) >> 3;

                    if (l_eo > 5 && (i_chipUnitNum % 2 == 0))
                    {
                        l_sa -= 0x20ULL; // 0b100 000
                    }

                    if (l_eo <= 5 && (i_chipUnitNum % 2 == 1))
                    {
                        l_sa += 0x20ULL; // 0b100 000
                    }

                    l_scom.set_addr(l_sa);
                }
                else
                {
                    //NV not supported on Axone - unused
                    l_scom.set_addr(FAILED_TRANSLATION);
                }

                break;

            case PU_PEC_CHIPUNIT:
                if (l_scom.get_chiplet_id() == N2_CHIPLET_ID)
                {
                    // nest
                    l_scom.set_ring( (N2_PCIS0_0_RING_ID + i_chipUnitNum) & 0xF);
                }
                else
                {
                    // iopci / pci
                    l_scom.set_chiplet_id(PCI0_CHIPLET_ID + i_chipUnitNum);
                }

                break;

            case PU_PHB_CHIPUNIT:
                if (l_scom.get_chiplet_id() == N2_CHIPLET_ID)
                {
                    // nest
                    if (i_chipUnitNum == 0)
                    {
                        l_scom.set_ring(N2_PCIS0_0_RING_ID & 0xF);
                        l_scom.set_sat_id(((l_scom.get_sat_id() < 4) ? (1) : (4)));
                    }
                    else
                    {
                        l_scom.set_ring( (N2_PCIS0_0_RING_ID + (i_chipUnitNum / 3) + 1) & 0xF);
                        l_scom.set_sat_id( ((l_scom.get_sat_id() < 4) ? (1) : (4)) +
                                            ((i_chipUnitNum % 2) ? (0) : (1)) +
                                            (2 * (i_chipUnitNum / 5)));
                    }
                }
                else
                {
                    // pci
                    if (i_chipUnitNum == 0)
                    {
                        l_scom.set_chiplet_id(PCI0_CHIPLET_ID);
                        l_scom.set_sat_id(((l_scom.get_sat_id() < 4) ? (1) : (4)));
                    }
                    else
                    {
                        l_scom.set_chiplet_id(PCI0_CHIPLET_ID + (i_chipUnitNum / 3) + 1);
                        l_scom.set_sat_id(((l_scom.get_sat_id() < 4) ? (1) : (4)) +
                                            ((i_chipUnitNum % 2) ? (0) : (1)) +
                                            (2 * (i_chipUnitNum / 5)));
                    }
                }

                break;

            case PU_OBUS_CHIPUNIT:
                l_scom.set_chiplet_id(OB0_CHIPLET_ID + i_chipUnitNum);
                break;

            case PU_XBUS_CHIPUNIT:

                l_ring &= 0xF;

                if (XB_IOX_2_RING_ID >= l_ring &&
                    l_ring >= XB_IOX_0_RING_ID)
                {
                    l_scom.set_ring( (XB_IOX_0_RING_ID + i_chipUnitNum) & 0xF);
                }

                else if (XB_PBIOX_2_RING_ID >= l_ring &&
                            l_ring >= XB_PBIOX_0_RING_ID)
                {
                    l_scom.set_ring( (XB_PBIOX_0_RING_ID + i_chipUnitNum) & 0xF);
                }

                break;

            case PU_SBE_CHIPUNIT:
                l_scom.set_chiplet_id(i_chipUnitNum);
                break;

            case PU_PPE_CHIPUNIT:

                // PPE SBE
                if (i_chipUnitNum == PPE_SBE_CHIPUNIT_NUM)
                {
                    l_scom.set_chiplet_id(PIB_CHIPLET_ID);
                    l_scom.set_port(SBE_PORT_ID);
                    l_scom.set_ring(PPE_SBE_RING_ID);
                    l_scom.set_sat_id(PPE_SBE_SAT_ID);
                    l_scom.set_sat_offset(0x0F & l_scom.get_sat_offset());
                    break;
                }

                // Need to set SAT offset if address is that of PPE SBE
                if (l_scom.get_port() == SBE_PORT_ID)
                {
                    // Adjust offset if input address is of SBE
                    // (ex: 000E0005 --> GPE: xxxxxx1x)
                    l_scom.set_sat_offset(l_scom.get_sat_offset() | 0x10);
                }

                // PPE GPE
                if ( (i_chipUnitNum >= PPE_GPE0_CHIPUNIT_NUM) && (i_chipUnitNum <= PPE_GPE3_CHIPUNIT_NUM) )
                {
                    l_scom.set_chiplet_id(PIB_CHIPLET_ID);
                    l_scom.set_port(GPE_PORT_ID);
                    l_scom.set_ring( (i_chipUnitNum - PPE_GPE0_CHIPUNIT_NUM) * 8 );
                    l_scom.set_sat_id(PPE_GPE_SAT_ID);
                }

                // PPE CME
                else if ( (i_chipUnitNum >= PPE_EQ0_CME0_CHIPUNIT_NUM) && (i_chipUnitNum <= PPE_EQ5_CME1_CHIPUNIT_NUM) )
                {
                    if (i_chipUnitNum >= PPE_EQ0_CME1_CHIPUNIT_NUM)
                    {
                        l_scom.set_chiplet_id(EP00_CHIPLET_ID +
                                                (i_chipUnitNum % PPE_EQ0_CME1_CHIPUNIT_NUM));
                    }
                    else
                    {
                        l_scom.set_chiplet_id(EP00_CHIPLET_ID +
                                                (i_chipUnitNum % PPE_EQ0_CME0_CHIPUNIT_NUM));
                    }

                    l_scom.set_port(UNIT_PORT_ID);
                    l_scom.set_ring( ((i_chipUnitNum / PPE_EQ0_CME1_CHIPUNIT_NUM) + 8) & 0xF );
                    l_scom.set_sat_id(PPE_CME_SAT_ID);
                }

                // PPE IO (XBUS/OBUS)
                else if ( (i_chipUnitNum >= PPE_IO_XBUS_CHIPUNIT_NUM) && (i_chipUnitNum <= PPE_IO_OB3_CHIPUNIT_NUM) )
                {
                    l_scom.set_chiplet_id( XB_CHIPLET_ID +
                                            (i_chipUnitNum % PPE_IO_XBUS_CHIPUNIT_NUM) +
                                            ((i_chipUnitNum / PPE_IO_OB0_CHIPUNIT_NUM) * 2) );
                    l_scom.set_port(UNIT_PORT_ID);

                    if (i_chipUnitNum == PPE_IO_XBUS_CHIPUNIT_NUM)
                    {
                        l_scom.set_ring(XB_IOPPE_0_RING_ID & 0xF);
                    }
                    else
                    {
                        l_scom.set_ring(OB_PPE_RING_ID & 0xF);
                    }

                    l_scom.set_sat_id(OB_PPE_SAT_ID); // Same SAT_ID value for XBUS
                }

                // PPE IO (DMI)
                else if ( (i_chipUnitNum >= PPE_IO_DMI0_CHIPUNIT_NUM) && (i_chipUnitNum <= PPE_IO_DMI1_CHIPUNIT_NUM))
                {
                    l_scom.set_chiplet_id(MC01_CHIPLET_ID + (i_chipUnitNum - PPE_IO_DMI0_CHIPUNIT_NUM));
                    l_scom.set_ring(MC_IOM01_1_RING_ID);
                    l_scom.set_port(UNIT_PORT_ID);
                    l_scom.set_sat_id(P9C_MC_PPE_SAT_ID);
                }

                // PPE PB
                else if ( (i_chipUnitNum >= PPE_PB0_CHIPUNIT_NUM) && (i_chipUnitNum <= PPE_PB2_CHIPUNIT_NUM) )
                {
                    l_scom.set_chiplet_id(N3_CHIPLET_ID); // TODO: Need to set ChipID for PB1 and PB2 in Cummulus
                    l_scom.set_port(UNIT_PORT_ID);
                    l_scom.set_ring(N3_PB_3_RING_ID & 0xF);
                    l_scom.set_sat_id(PPE_PB_SAT_ID);
                }

                // Invalid i_chipUnitNum
                else
                {
                    l_scom.set_addr(FAILED_TRANSLATION);
                }

                break;

            case PU_NPU_CHIPUNIT:

                // NPU0 and NPU1 exist on the N3 chiplet, NPU2 exists on the N1 chiplet instead
                l_chiplet_id = ( 2 == i_chipUnitNum ) ? N1_CHIPLET_ID : N3_CHIPLET_ID ;
                l_scom.set_chiplet_id( l_chiplet_id );

                // Covers the following addresses:
                // NPU0: 05011000 to 050113FF
                // NPU1: 05011400 to 050117FF
                // NPU2: 03011C00 to 03011FFF
                if ( N3_NPU_0_RING_ID  == l_ring ||
                        N3_NPU_1_RING_ID  == l_ring ||
                        P9A_NPU_2_RING_ID == l_ring )
                {
                    // NPU0/NPU1
                    if ( N3_CHIPLET_ID == l_chiplet_id )
                    {
                        l_scom.set_ring( N3_NPU_0_RING_ID + i_chipUnitNum );
                    }
                    // NPU2
                    else if ( N1_CHIPLET_ID == l_chiplet_id )
                    {
                        l_scom.set_ring( P9A_NPU_2_RING_ID );
                    }
                    else
                    {
                        l_scom.set_addr( FAILED_TRANSLATION );
                    }
                }
                // Covers the following addresses:
                // NPU0: 05013C00 to 05013C8F
                // NPU1: 05013CC0 to 05013D4F
                // NPU2: 03012000 to 0301208F
                else if ( P9A_NPU_0_FIR_RING_ID == l_ring ||
                            P9A_NPU_2_FIR_RING_ID == l_ring )
                {
                    // NPU0/NPU1
                    if ( N3_CHIPLET_ID == l_chiplet_id )
                    {
                        l_scom.set_ring( P9A_NPU_0_FIR_RING_ID );
                        l_scom.set_sat_id( (l_sat_id % 3) + (3 * i_chipUnitNum) );
                    }
                    // NPU2
                    else if ( N1_CHIPLET_ID == l_chiplet_id )
                    {
                        l_scom.set_ring( P9A_NPU_2_FIR_RING_ID );
                        l_scom.set_sat_id( l_sat_id % 3 );
                    }
                    else
                    {
                        l_scom.set_addr( FAILED_TRANSLATION );
                    }
                }
                else
                {
                    l_scom.set_addr( FAILED_TRANSLATION );
                }

                break;

            default:
                l_scom.set_addr(FAILED_TRANSLATION);
                break;
        }
    }

    return l_scom.get_addr();
}

uint32_t p9_scominfo_xlate_mi(bool& o_chipUnitRelated, std::vector<p9_chipUnitPairing_t>& o_chipUnitPairing,
                                p9_scom_addr& i_scom, const p9ChipUnits_t mcc_dmi, const int i_low0, const int i_low1, const uint32_t i_mode)
{
    uint8_t l_chiplet_id = i_scom.get_chiplet_id();
    uint8_t l_port = i_scom.get_port();
    uint8_t l_ring = i_scom.get_ring();
    uint8_t l_sat_id = i_scom.get_sat_id();
    uint8_t l_sat_offset = i_scom.get_sat_offset();

    //==== AXONE MC/MI/OMIC/OMI  ============================================================================
    //==== MI target ===============================================
    if (((l_chiplet_id == N3_CHIPLET_ID) || (l_chiplet_id == N1_CHIPLET_ID)) &&
        (l_port == UNIT_PORT_ID) &&
        (l_ring == N3_MC01_0_RING_ID) &&
        (l_sat_id == P9_N3_MCS01_SAT_ID || l_sat_id == P9_N3_MCS23_SAT_ID))
    {
        //-------------------------------------------
        // DMI/MCC
        //-------------------------------------------
        //SCOM3   (See mc_clscom_rlm.fig <= 0xB vs mc_scomfir_rlm.fig > 0xB)
        //DMI0           05     02       0   0x2X (X <= 0xB)
        //DMI1           05     02       0   0x3X (X <= 0xB)
        //DMI2           05     02       2   0x2X (X <= 0xB)
        //DMI3           05     02       2   0x3X (X <= 0xB)
        //DMI4           03     02       0   0x2X (X <= 0xB)
        //DMI5           03     02       0   0x3X (X <= 0xB)
        //DMI6           03     02       2   0x2X (X <= 0xB)
        //DMI7           03     02       2   0x3X (X <= 0xB)
        if ((i_low0 <= l_sat_offset && l_sat_offset <= 0x2B) ||
            (i_low1 <= l_sat_offset && l_sat_offset <= 0x3B))
        {
            uint8_t l_off_nib0 = (l_sat_offset >> 4);
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(mcc_dmi,
                                        ((l_chiplet_id == N3_CHIPLET_ID) ? (0) : (4)) +
                                        (l_off_nib0 - 2) + l_sat_id));
        }
        //-------------------------------------------
        // MI
        //-------------------------------------------
        //          Chiplet   Ring   Satid   Off
        //MCS0           05     02       0   !SCOM3
        //MCS1           05     02       2   !SCOM3
        //MCS2           03     02       0   !SCOM3
        //MCS3           03     02       2   !SCOM3
        else
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_MI_CHIPUNIT,
                                        (l_sat_id / 2) +
                                        ((l_chiplet_id == N3_CHIPLET_ID) ? (0) : (2))));
        }
    }

    return 0;
}

uint32_t p9_scominfo_isChipUnitScom(const uint64_t i_scomAddr, bool& o_chipUnitRelated,
                                    std::vector<p9_chipUnitPairing_t>& o_chipUnitPairing, const uint32_t i_mode)
{
    p9_scom_addr l_scom(i_scomAddr);
    o_chipUnitRelated = false;

    uint32_t rc = 0;

    uint8_t l_chiplet_id = l_scom.get_chiplet_id();
    uint8_t l_port = l_scom.get_port();
    uint8_t l_ring = l_scom.get_ring();
    uint8_t l_sat_id = l_scom.get_sat_id();
    uint8_t l_sat_offset = l_scom.get_sat_offset();

    if((i_mode & PPE_MODE) == PPE_MODE)
    {
        if(PPE_EP00_CHIPLET_ID <= l_chiplet_id
        && l_chiplet_id <= PPE_EP05_CHIPLET_ID)
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(
                p9_chipUnitPairing_t(
                    PU_EX_CHIPUNIT,
                    ((l_chiplet_id - PPE_EP00_CHIPLET_ID) * 2)
                    + (l_port - 1)));

        }

    }
    else if (l_scom.is_unicast())
    {
        if (((l_port == GPREG_PORT_ID)
        || ((l_port >= CME_PORT_ID)
          && (l_port <= CPM_PORT_ID))
        || (l_port == PCBSLV_PORT_ID)
        || (l_port == UNIT_PORT_ID && l_ring == EC_PSCM_RING_ID)
        || (l_port == UNIT_PORT_ID && l_ring == EC_PERV_RING_ID
          && l_sat_id == PERV_DBG_SAT_ID)))
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_PERV_CHIPUNIT, l_chiplet_id));

            if(l_chiplet_id >= EC00_CHIPLET_ID && l_chiplet_id <= EC23_CHIPLET_ID)
            {
                o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_C_CHIPUNIT, l_chiplet_id - EC00_CHIPLET_ID));
                o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_EX_CHIPUNIT, (l_chiplet_id - EC00_CHIPLET_ID) / 2));
            }

            if(l_chiplet_id >= EP00_CHIPLET_ID && l_chiplet_id <= EP05_CHIPLET_ID)
            {
                o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_EQ_CHIPUNIT, l_chiplet_id - EP00_CHIPLET_ID));
            }
        }

        if(l_chiplet_id >= EC00_CHIPLET_ID
        && l_chiplet_id <= EC23_CHIPLET_ID
        && l_port == UNIT_PORT_ID
        && l_ring >= EC_PERV_RING_ID
        && l_ring <= EC_PC_3_RING_ID)
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_C_CHIPUNIT, l_chiplet_id - EC00_CHIPLET_ID));
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_EX_CHIPUNIT, (l_chiplet_id - EC00_CHIPLET_ID) / 2));
        }
        if(l_chiplet_id >= EP00_CHIPLET_ID
        && l_chiplet_id <= EP05_CHIPLET_ID
        && l_port == UNIT_PORT_ID
        && (l_ring >= EQ_PERV_RING_ID && l_ring <= EQ_L3_1_RING_ID)
        || (l_ring >= EQ_CME_0_RING_ID && l_ring <= EQ_L2_1_TRA_RING_ID))
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_EQ_CHIPUNIT, l_chiplet_id - EP00_CHIPLET_ID));

            if(l_ring == EQ_L2_0_RING_ID
            || l_ring == EQ_NC_0_RING_ID
            || l_ring == EQ_L3_0_RING_ID
            || l_ring == EQ_CME_0_RING_ID
            || l_ring == EQ_L2_0_TRA_RING_ID)
            {
                o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_EX_CHIPUNIT, (l_chiplet_id - EP00_CHIPLET_ID) * 2));
            }
            else if(l_ring == EQ_L2_1_RING_ID
              || l_ring == EQ_NC_1_RING_ID
              || l_ring == EQ_L3_1_RING_ID
              || l_ring == EQ_CME_1_RING_ID
              || l_ring == EQ_L2_1_TRA_RING_ID)
            {
                o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_EX_CHIPUNIT, ((l_chiplet_id - EP00_CHIPLET_ID) * 2) + 1));
            }
        }
        if (((l_chiplet_id == N0_CHIPLET_ID && l_ring == N0_CXA0_0_RING_ID)
            || (l_chiplet_id == N2_CHIPLET_ID && l_ring == N2_CXA1_0_RING_ID))
            && l_port == UNIT_PORT_ID)
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_CAPP_CHIPUNIT, (l_chiplet_id / 2) - 1));
        }

        if (i_mode == P9N_DD1_SI_MODE || i_mode == P9N_DD2_SI_MODE)
        {
            if((l_chiplet_id == N3_CHIPLET_ID || l_chiplet_id == N1_CHIPLET_ID)
            && l_port == UNIT_PORT_ID
            && l_ring == N3_MC01_0_RING_ID
            && (l_sat_id == MC_DIR_SAT_ID_PBI_01 || l_sat_id == MC_DIR_SAT_ID_PBI_23)
            && ((0x2F & l_sat_offset) < MC_MCS_MCA_OFFSET_MCP0XLT0 || MC_MCS_MCA_OFFSET_MCPERF3 < (0x2F & l_sat_offset)))
            {
                o_chipUnitRelated = true;
                o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_MCS_CHIPUNIT, ((l_chiplet_id == N3_CHIPLET_ID) ? (0) : (2)) + (l_sat_id / 2)));
            }

            if(((l_chiplet_id == MC01_CHIPLET_ID) || (l_chiplet_id == MC23_CHIPLET_ID))
            && (((l_port == UNIT_PORT_ID)
            && (((l_ring == MC_MC01_1_RING_ID)
            && ((l_sat_id & 0xC) == MC_DIR_SAT_ID_MCBIST))
              || ((l_ring == MC_PERV_RING_ID)
              || (l_ring == XB_PSCM_RING_ID)
              || (l_ring == MC_MCTRA_0_RING_ID)) ))
              || (l_port != UNIT_PORT_ID)) )
            {
                o_chipUnitRelated = true;
                o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_MCBIST_CHIPUNIT, l_chiplet_id - MC01_CHIPLET_ID));
            }

            if((l_chiplet_id == N3_CHIPLET_ID || l_chiplet_id == N1_CHIPLET_ID)
            && l_port == UNIT_PORT_ID
            && l_ring == N3_MC01_0_RING_ID
            && (l_sat_id == MC_DIR_SAT_ID_PBI_01 || l_sat_id == MC_DIR_SAT_ID_PBI_23)
            && (((0x2F & l_sat_offset) >= MC_MCS_MCA_OFFSET_MCP0XLT0 && MC_MCS_MCA_OFFSET_MCPERF3 >= (0x2F & l_sat_offset))))
            {
                o_chipUnitRelated = true;
                o_chipUnitPairing.push_back(
                    p9_chipUnitPairing_t(PU_MCA_CHIPUNIT,
                    ((((l_chiplet_id == N3_CHIPLET_ID) ? (0) : (2)) + (l_sat_id / 2)) * 2) + ((l_sat_offset & 0x10) >> 4)));
            }

            if((l_chiplet_id == MC01_CHIPLET_ID || l_chiplet_id == MC23_CHIPLET_ID)
            && l_port == UNIT_PORT_ID
            && l_ring == MC_MC01_0_RING_ID
            && (l_sat_id >= MC_DIR_SAT_ID_SRQ_0 && l_sat_id <= MC_DIR_SAT_ID_ECC64_3))
            {
                o_chipUnitRelated = true;
                o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_MCA_CHIPUNIT, (4 * (l_chiplet_id - MC01_CHIPLET_ID)) + (l_sat_id % 4)));
            }

            if((l_chiplet_id == MC01_CHIPLET_ID || l_chiplet_id == MC23_CHIPLET_ID)
            && l_port == UNIT_PORT_ID
            && l_ring >= MC_IOM01_0_RING_ID
            && l_ring <= MC_IOM23_1_RING_ID
            && l_sat_id == MC_IND_SAT_ID)
            {
                o_chipUnitRelated = true;
                o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_MCA_CHIPUNIT, (4 * (l_chiplet_id - MC01_CHIPLET_ID)) + (l_ring - MC_IOM01_0_RING_ID)));
            }
        }
        else if (i_mode == P9C_DD1_SI_MODE || i_mode == P9C_DD2_SI_MODE)
        {
            if (p9_scominfo_xlate_mi(o_chipUnitRelated, o_chipUnitPairing, l_scom, PU_DMI_CHIPUNIT, 0x20, 0x30, i_mode))
            {
                return;
            }

            if ((l_chiplet_id == MC01_CHIPLET_ID || l_chiplet_id == MC23_CHIPLET_ID)
            && (l_port != UNIT_PORT_ID
              || l_port == UNIT_PORT_ID
              && ((l_ring == P9C_MC_BIST_RING_ID && l_sat_id != P9C_SAT_ID_CHAN_MCBIST)
                || l_ring == P9C_MC_PERV_RING_ID
                || l_ring == P9C_MC_PSCM_RING_ID
                || l_ring == P9C_MC_MCTRA_RING_ID
                || l_ring == P9C_MC_PPE_RING_ID
                || (l_ring == P9C_MC_IO_RING_ID
                  && l_sat_id == MC_IND_SAT_ID
                  && l_sat_offset != P9C_MC_OFFSET_IND))))
            {

                o_chipUnitRelated = true;
                o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_MC_CHIPUNIT, l_chiplet_id - MC01_CHIPLET_ID));

            }

            if((l_chiplet_id == MC01_CHIPLET_ID || l_chiplet_id == MC23_CHIPLET_ID)
            && l_port == UNIT_PORT_ID)
            {
                if(l_ring == P9C_MC_CHAN_RING_ID)
                {
                    o_chipUnitRelated = true;
                    o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_DMI_CHIPUNIT, ((l_chiplet_id == MC01_CHIPLET_ID ? (0) : (4))) + (0x3 & l_sat_id)));
                }

                if (l_ring == P9C_MC_BIST_RING_ID && l_sat_id == P9C_SAT_ID_CHAN_MCBIST)
                {
                    uint8_t l_off_nib0 = (l_sat_offset >> 4);
                    o_chipUnitRelated = true;
                    o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_DMI_CHIPUNIT, ((l_chiplet_id == MC01_CHIPLET_ID ? (0) : (4))) + l_off_nib0));
                }

                if(l_ring == P9C_MC_IO_RING_ID
                && l_sat_id == MC_IND_SAT_ID
                && l_sat_offset == P9C_MC_OFFSET_IND )
                {
                    uint32_t l_rxtx_grp = l_scom.get_rxtx_group_id();
                    uint32_t l_ind_addr = l_scom.get_ind_addr();

                    if ((l_ind_addr & 0x1E0) == 0x1E0 && l_ind_addr != 0x1FF)
                    {
                        o_chipUnitRelated = true;
                        o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_MC_CHIPUNIT, l_chiplet_id - MC01_CHIPLET_ID));
                    }
                    else
                    {
                        if (l_rxtx_grp >= 0x20)
                        {
                            l_rxtx_grp -= 0x20;
                        }
                        uint8_t l_adder = 0;
                        switch (l_rxtx_grp % 4)
                        {
                            case 3:
                                l_adder = 0;
                                break;
                            case 2:
                                l_adder = 1;
                                break;
                            case 0:
                                l_adder = 2;
                                break;
                            case 1:
                                l_adder = 3;
                                break;
                        }

                        o_chipUnitRelated = true;
                        o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_DMI_CHIPUNIT, ((l_chiplet_id == MC01_CHIPLET_ID ? (0) : (4))) + l_adder));
                    }
                }
                if(l_ring == P9C_MC_PPE_RING_ID)
                {
                    o_chipUnitRelated = true;
                    o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_PPE_CHIPUNIT, (l_chiplet_id - MC01_CHIPLET_ID) + PPE_IO_DMI0_CHIPUNIT_NUM));
                }
            }
        }
        else
        {
            if (p9_scominfo_xlate_mi(o_chipUnitRelated, o_chipUnitPairing, l_scom, PU_MCC_CHIPUNIT, 0x23, 0x33, i_mode))
            {
                return rc;
            }
            if(l_chiplet_id == MC01_CHIPLET_ID || l_chiplet_id == MC23_CHIPLET_ID)
            {
                o_chipUnitRelated = true;
                o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_MC_CHIPUNIT, l_chiplet_id - MC01_CHIPLET_ID));

                if (l_ring == P9A_MC_CHAN_RING_ID && l_port == UNIT_PORT_ID)
                {
                    if((P9A_MC_DSTL_CHAN0_SAT_ID <= l_sat_id && l_sat_id <= P9A_MC_DSTL_CHAN3_SAT_ID)
                    || (P9A_MC_USTL_CHAN0_SAT_ID <= l_sat_id && l_sat_id <= P9A_MC_USTL_CHAN3_SAT_ID))
                    {
                        o_chipUnitRelated = true;
                        o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_MCC_CHIPUNIT, ((l_chiplet_id - MC01_CHIPLET_ID) * 4) + (l_sat_id % 4)));
                    }
                }
                if(l_ring == P9A_MC_MC01_RING_ID
                && l_port == UNIT_PORT_ID
                && l_sat_id == P9A_MC_CHAN_SAT_ID)
                {
                    o_chipUnitRelated = true;
                    o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_MCC_CHIPUNIT, l_sat_offset / 16));
                }

                if(P9A_MC_OMIC0_RING_ID <= l_ring
                && l_ring <= P9A_MC_OMIC2_RING_ID
                && l_port == UNIT_PORT_ID)
                {
                    o_chipUnitRelated = true;
                    o_chipUnitPairing.push_back(
                        p9_chipUnitPairing_t(
                            PU_OMIC_CHIPUNIT,
                            ((l_chiplet_id - MC01_CHIPLET_ID) * 3)
                            + (l_ring - P9A_MC_OMIC0_RING_ID)));

                    if (l_sat_id == P9A_MC_IND_SAT_ID && l_sat_offset == P9A_MC_IND_REG)
                    {
                        uint32_t l_ind_reg = l_scom.get_ind_addr();
                        if ((l_ind_reg & 0x100) == 0x000)
                        {
                            uint32_t l_ind_lane = l_scom.get_lane_id();
                            o_chipUnitRelated = true;
                            o_chipUnitPairing.push_back(
                                p9_chipUnitPairing_t(
                                    PU_OMI_CHIPUNIT,
                                    P9_SCOMINFO_OMI_SWIZZLE[((l_chiplet_id - MC01_CHIPLET_ID) * 8)
                                    + ((l_ring - P9A_MC_OMIC0_RING_ID) * 3)
                                    + ((l_ind_lane / 8))]));
                        }
                    }
                }

                if (P9A_MC_OMIC0_PPE_RING_ID <= l_ring && l_ring <= P9A_MC_OMIC2_PPE_RING_ID  && l_port == UNIT_PORT_ID)
                {
                    o_chipUnitRelated = true;
                    o_chipUnitPairing.push_back(
                        p9_chipUnitPairing_t(
                            PU_OMIC_CHIPUNIT,
                            ((l_chiplet_id - MC01_CHIPLET_ID) * 3)
                            + (l_ring - P9A_MC_OMIC0_PPE_RING_ID)));
                }

                if (l_ring == P9A_MC_OMI_DL_RING_ID
                && l_port == UNIT_PORT_ID
                && P9A_MC_DL_REG0_SAT_ID <= l_sat_id
                && l_sat_id <= P9A_MC_DL_REG2_SAT_ID)
                {
                    if(P9A_MC_DL_OMI0_FRST_REG <= l_sat_offset)
                    {
                        o_chipUnitRelated = true;
                        o_chipUnitPairing.push_back(
                            p9_chipUnitPairing_t(
                                PU_OMI_CHIPUNIT,
                                P9_SCOMINFO_OMI_SWIZZLE[((l_chiplet_id - MC01_CHIPLET_ID) * 8)
                                + ((l_sat_id - P9A_MC_DL_REG0_SAT_ID) * 3)
                                + ((l_sat_offset / 16) - 1)]));
                    }
                    o_chipUnitRelated = true;
                    o_chipUnitPairing.push_back(
                        p9_chipUnitPairing_t(
                            PU_OMIC_CHIPUNIT,
                            ((l_chiplet_id - MC01_CHIPLET_ID) * 3)
                            + (l_sat_id - P9A_MC_DL_REG0_SAT_ID)));
                }
            }
        }

        if(i_mode == P9N_DD1_SI_MODE)
        {
            if(l_chiplet_id == N3_CHIPLET_ID
            && l_port == UNIT_PORT_ID
            && l_ring == N3_NPU_0_RING_ID
            && (l_sat_id % 4) == 3
            && l_sat_id <= 11)
            {
                o_chipUnitRelated = true;
                o_chipUnitPairing.push_back(
                    p9_chipUnitPairing_t(
                        PU_NV_CHIPUNIT,
                        (2 * (l_sat_id / 4))
                        + (l_sat_offset / 32)));
            }
        }
        else if(i_mode != P9A_DD1_SI_MODE
        && i_mode != P9A_DD2_SI_MODE
        && l_chiplet_id == N3_CHIPLET_ID
        && l_port == UNIT_PORT_ID)
        {
            uint64_t npuaddr = (i_scomAddr & 0xFFF8ULL) >> 3;
            if (0x0222ULL <= npuaddr && npuaddr <= 0x0225ULL)
            {
                o_chipUnitRelated = true;
                o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_NV_CHIPUNIT, 0));
            }
            if (0x0226ULL <= npuaddr && npuaddr <= 0x0229ULL)
            {
                o_chipUnitRelated = true;
                o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_NV_CHIPUNIT, 1));
            }
            if (0x0262ULL <= npuaddr && npuaddr <= 0x0265ULL)
            {
                o_chipUnitRelated = true;
                o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_NV_CHIPUNIT, 2));
            }
            if (0x0266ULL <= npuaddr && npuaddr <= 0x0269ULL)
            {
                o_chipUnitRelated = true;
                o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_NV_CHIPUNIT, 3));
            }
            if (0x02A2ULL <= npuaddr && npuaddr <= 0x02A5ULL)
            {
                o_chipUnitRelated = true;
                o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_NV_CHIPUNIT, 4));
            }
            if (0x02A6ULL <= npuaddr && npuaddr <= 0x02A9ULL)
            {
                o_chipUnitRelated = true;
                o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_NV_CHIPUNIT, 5));
            }
        }

        if(l_chiplet_id == N2_CHIPLET_ID
        && l_port == UNIT_PORT_ID
        && l_ring >= N2_PCIS0_0_RING_ID
        && l_ring <= N2_PCIS2_0_RING_ID
        && l_sat_id == PEC_SAT_ID)
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(
                p9_chipUnitPairing_t(
                    PU_PEC_CHIPUNIT,
                    l_ring - N2_PCIS0_0_RING_ID));
        }

        if(l_chiplet_id >= PCI0_CHIPLET_ID
        && l_chiplet_id <= PCI2_CHIPLET_ID
        && (l_port != UNIT_PORT_ID
          || (l_port == UNIT_PORT_ID
            && (l_ring == PCI_IOPCI_0_RING_ID || l_ring == PCI_PE_0_RING_ID || l_ring == PCI_PERV_RING_ID)
            && l_sat_id == PEC_SAT_ID)))
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_PEC_CHIPUNIT,(l_chiplet_id - PCI0_CHIPLET_ID)));
        }

        if(l_chiplet_id == N2_CHIPLET_ID
        && l_port == UNIT_PORT_ID
        && l_ring >= N2_PCIS0_0_RING_ID
        && l_ring <= N2_PCIS2_0_RING_ID
        && (l_ring - l_sat_id) >= 2
        && (l_ring - l_sat_id) < l_ring)
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(
                p9_chipUnitPairing_t(
                    PU_PHB_CHIPUNIT,
                    ((l_ring != N2_PCIS0_0_RING_ID) ?
                      ((l_ring - N2_PCIS0_0_RING_ID) * 2) - 1
                    : 0)
                    + l_sat_id - 1));
        }

/////////////// CLEANME
        if (((l_chiplet_id >= PCI0_CHIPLET_ID) && (l_chiplet_id <= PCI2_CHIPLET_ID)) &&
            (l_port == UNIT_PORT_ID) &&
            (l_ring == PCI_PE_0_RING_ID) &&
            (((l_sat_id >= 1) && (l_sat_id <= (l_chiplet_id - PCI0_CHIPLET_ID + 1))) || // aib_stack
                ((l_sat_id >= 4) && (l_sat_id <= (l_chiplet_id - PCI0_CHIPLET_ID + 4)))))  // pbcq_etu
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_PHB_CHIPUNIT,
                                        ((l_chiplet_id - PCI0_CHIPLET_ID) ?
                                            (((l_chiplet_id - PCI0_CHIPLET_ID) * 2) - 1) :
                                            (0)) +
                                        l_sat_id -
                                        ((l_sat_id >= 4) ? (4) : (1))));
        }

        // PU_OBUS_CHIPUNIT
        // obus: 0..3
        if (((l_chiplet_id >= OB0_CHIPLET_ID) && (l_chiplet_id <= OB3_CHIPLET_ID)))
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_OBUS_CHIPUNIT,
                                        (l_chiplet_id - OB0_CHIPLET_ID)));
        }

        // PU_XBUS_CHIPUNIT
        // xbus: 0..2
        if ((l_chiplet_id == XB_CHIPLET_ID) &&
            (l_port == UNIT_PORT_ID) &&
            (((l_ring >= XB_IOX_0_RING_ID) && (l_ring <= XB_IOX_2_RING_ID) && (l_sat_id == XB_IOF_SAT_ID)) ||
                ((l_ring >= XB_PBIOX_0_RING_ID) && (l_ring <= XB_PBIOX_2_RING_ID) && (l_sat_id == XB_PB_SAT_ID))))
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_XBUS_CHIPUNIT,
                                        l_ring % 3));
        }

        // -----------------------------------------------------------------------------
        // Common 'ppe' registers associated with each pervasive chiplet type
        // Permit addressing by PPE target type (for all ppe chiplet instances)
        // -----------------------------------------------------------------------------

        // SBE PM registers
        //    Port ID = 14
        if ( (l_port == SBE_PORT_ID) &&
                (l_chiplet_id == PIB_CHIPLET_ID) &&
                (l_ring == PPE_SBE_RING_ID) &&
                (l_sat_id == PPE_SBE_SAT_ID) )
        {
            o_chipUnitRelated = true;
            // PU_SBE_CHIPUNIT
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_SBE_CHIPUNIT,
                                        l_chiplet_id));
            // PU_PPE_CHIPUNIT
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_PPE_CHIPUNIT,
                                        l_chiplet_id));
        }

        // GPE registers
        //    Port ID = 1
        if ( (l_port == GPE_PORT_ID) &&
                (l_chiplet_id == PIB_CHIPLET_ID) &&
                ( (l_ring == PPE_GPE0_RING_ID) ||
                (l_ring == PPE_GPE1_RING_ID) ||
                (l_ring == PPE_GPE2_RING_ID) ||
                (l_ring == PPE_GPE3_RING_ID) ) &&
                (l_sat_id == PPE_GPE_SAT_ID) )
        {
            o_chipUnitRelated = true;
            // PU_PPE_CHIPUNIT
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(
                                            PU_PPE_CHIPUNIT,
                                            PPE_GPE0_CHIPUNIT_NUM + (l_ring / 8)));
        }

        // CME registers which can be addressed by PPE target type
        //    Port ID = 1
        //    0x10 <= Chiplet ID <= 0x15
        //    Ring_ID = 0x8 or Ring_ID = 0x9
        //    SAT_ID = 0
        if ( (l_port == UNIT_PORT_ID) &&
                ((l_chiplet_id >= EP00_CHIPLET_ID) && (l_chiplet_id <= EP05_CHIPLET_ID)) &&
                ( (l_ring == EQ_CME_0_RING_ID) || (l_ring == EQ_CME_1_RING_ID) ) &&
                (l_sat_id == PPE_CME_SAT_ID) )
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_PPE_CHIPUNIT,
                                        (l_chiplet_id - EP00_CHIPLET_ID) +
                                        PPE_EQ0_CME0_CHIPUNIT_NUM +
                                        ((l_ring % 8) * 10)));
        }

        // PB registers which can be addressed by PPE target type
        //    Port ID = 1
        //    Chiplet ID = 0x05
        //    Ring_ID = 0x9
        //    SAT_ID = 0
        if ( (l_port == UNIT_PORT_ID) &&
                (l_chiplet_id == N3_CHIPLET_ID) &&
                (l_ring == N3_PB_3_RING_ID) &&
                (l_sat_id == PPE_PB_SAT_ID) )
        {
            o_chipUnitRelated = true;
            // TODO: Need to update for PB1/PB2 of Cummulus whenever address
            //       values are available.
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_PPE_CHIPUNIT,
                                        PPE_PB0_CHIPUNIT_NUM));
        }

        // XBUS registers which can be addressed by PPE target type (IOPPE)
        //    Port ID = 1
        //    Chiplet ID = 0x06
        //    Ring_ID = 0x2
        //    SAT_ID = 1
        if ( (l_port == UNIT_PORT_ID) &&
                (l_chiplet_id == XB_CHIPLET_ID) &&
                (l_ring == XB_IOPPE_0_RING_ID) &&
                (l_sat_id == XB_PPE_SAT_ID) )
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_PPE_CHIPUNIT,
                                        PPE_IO_XBUS_CHIPUNIT_NUM));
        }

        // OBUS registers which can be addressed by PPE target type (IOPPE)
        //    Port ID = 1
        //    Chiplet ID = 0x09, 0x0A, 0x0B, or 0x0C
        //    Ring_ID = 0x4
        //    SAT_ID = 1
        if ( (l_port == UNIT_PORT_ID) &&
                ((l_chiplet_id >= OB0_CHIPLET_ID) && (l_chiplet_id <= OB3_CHIPLET_ID)) &&
                (l_ring == OB_PPE_RING_ID) &&
                (l_sat_id == OB_PPE_SAT_ID) )
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_PPE_CHIPUNIT,
                                        (l_chiplet_id - OB0_CHIPLET_ID) + PPE_IO_OB0_CHIPUNIT_NUM));
        }

        // PU_NPU_CHIPUNIT
        // npu: 0..1
        if ( (l_port == UNIT_PORT_ID) &&
                (l_chiplet_id == N3_CHIPLET_ID) &&
                (N3_NPU_0_RING_ID <= l_ring && l_ring <= N3_NPU_1_RING_ID) )
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_NPU_CHIPUNIT,
                                        (l_ring - N3_NPU_0_RING_ID)));
        }

        if ( (l_port == UNIT_PORT_ID) &&
                (l_chiplet_id == N3_CHIPLET_ID) &&
                (l_ring == P9A_NPU_0_FIR_RING_ID) )
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_NPU_CHIPUNIT,
                                        (l_sat_id / 3)));
        }

        if ( (l_port == UNIT_PORT_ID) &&
                (l_chiplet_id == N1_CHIPLET_ID) &&
                (l_ring == P9A_NPU_2_RING_ID || l_ring == P9A_NPU_2_FIR_RING_ID) )
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_NPU_CHIPUNIT, 2));
        }

    }

    return (!l_scom.is_valid());
}

const Target * getParentChip( const Target * i_pChiplet )
{
    TARGETING::PredicateCTM l_predicate(TARGETING::CLASS_CHIP);
    TARGETING::TargetHandleList l_chipList;
    TARGETING::targetService().getAssociated(
        l_chipList,
        i_pChiplet,
        TARGETING::TargetService::PARENT,
        TARGETING::TargetService::ALL, &l_predicate);

    if (l_chipList.size() == 1)
    {
        return l_chipList[0];
    }
    return NULL;
}

uint8_t getChipLevel( TARGETING::TargetHandle_t i_trgt )
{
    return getParentChip(i_trgt)->getAttr<ATTR_EC>();
}

errlHndl_t p9_translation (TARGETING::Target * &i_target,
                           TARGETING::TYPE i_type,
                           uint64_t &io_addr,
                           bool & o_needsWakeup,
                           uint64_t i_opMode)
{
    uint64_t l_original_addr = io_addr;
    uint32_t l_chip_mode = STANDARD_MODE;
    bool l_scomAddrIsRelatedToUnit = false;
    bool l_scomAddrAndTargetTypeMatch = false;

    uint16_t l_instance = 0;
    p9ChipUnits_t l_chipUnit = NONE;
    std::vector<p9_chipUnitPairing_t> l_scomPairings;

    uint32_t l_chipLevel = getChipLevel(i_target);
    l_chip_mode |= l_chipLevel;

    p9_scominfo_isChipUnitScom(
        io_addr,
        l_scomAddrIsRelatedToUnit,
        l_scomPairings,
        l_chip_mode);


#if __HOSTBOOT_RUNTIME
    if(((i_type == TARGETING::TYPE_EQ) ||
        (i_type == TARGETING::TYPE_EX) ||
        (i_type == TARGETING::TYPE_CORE)) &&
        (!g_wakeupInProgress) &&
        !(i_opMode & fapi2::DO_NOT_DO_WAKEUP) )
    {
        o_needsWakeup = true;
        for(uint16_t i = 0; i < l_scomPairings.size(); i++)
        {
            if( l_scomPairings[i].chipUnitType == PU_PERV_CHIPUNIT)
            {
                o_needsWakeup = false;
                break;
            }
        }
    }
#endif

    for(uint32_t i = 0; i < l_scomPairings.size(); i++)
    {
        if( (l_scomPairings[i].chipUnitType == l_chipUnit) &&
            (l_scomPairings[i].chipUnitNum == 0))
        {
            l_scomAddrAndTargetTypeMatch = true;
            break;
        }

    }

    l_instance = i_target->getAttr<TARGETING::ATTR_CHIP_UNIT>();
    io_addr = p9_scominfo_createChipUnitScomAddr(
        l_chipUnit,
        l_instance,
        io_addr,
        l_chip_mode);
}

bool getChipUnitP9 (TARGETING::TYPE i_type,
                    p9ChipUnits_t &o_chipUnit)
{
    switch(i_type)
    {
        case(TARGETING::TYPE_EX):
            o_chipUnit = PU_EX_CHIPUNIT;
            break;
        case(TARGETING::TYPE_MCS):
            o_chipUnit = PU_MCS_CHIPUNIT;
            break;
        case(TARGETING::TYPE_XBUS):
            o_chipUnit = PU_XBUS_CHIPUNIT;
            break;
        case(TARGETING::TYPE_CORE) :
            o_chipUnit = PU_C_CHIPUNIT;
            break;
        case(TARGETING::TYPE_PERV) :
            o_chipUnit = PU_PERV_CHIPUNIT;
            break;
        case(TARGETING::TYPE_EQ) :
            o_chipUnit = PU_EQ_CHIPUNIT;
            break;
        case(TARGETING::TYPE_MCBIST) :
            o_chipUnit = PU_MCBIST_CHIPUNIT;
            break;
        case(TARGETING::TYPE_MCA) :
            o_chipUnit = PU_MCA_CHIPUNIT;
            break;
        case(TARGETING::TYPE_MI) :
            o_chipUnit = PU_MI_CHIPUNIT;
            break;
        case(TARGETING::TYPE_DMI) :
            o_chipUnit = PU_DMI_CHIPUNIT;
            break;
        case(TARGETING::TYPE_OBUS) :
            o_chipUnit = PU_OBUS_CHIPUNIT;
            break;
        case(TARGETING::TYPE_OBUS_BRICK) :
            o_chipUnit = PU_NV_CHIPUNIT;
            break;
        case(TARGETING::TYPE_SBE) :
            o_chipUnit = PU_SBE_CHIPUNIT;
            break;
        case(TARGETING::TYPE_PPE) :
            o_chipUnit = PU_PPE_CHIPUNIT;
            break;
        case(TARGETING::TYPE_PEC) :
            o_chipUnit = PU_PEC_CHIPUNIT;
            break;
        case(TARGETING::TYPE_PHB) :
            o_chipUnit = PU_PHB_CHIPUNIT;
            break;
        case(TARGETING::TYPE_CAPP) :
            o_chipUnit = PU_CAPP_CHIPUNIT;
            break;
        case(TARGETING::TYPE_MC) :
            o_chipUnit = PU_MC_CHIPUNIT;
            break;
        case(TARGETING::TYPE_MCC) :
            o_chipUnit = PU_MCC_CHIPUNIT;
            break;
        case(TARGETING::TYPE_OMI) :
            o_chipUnit = PU_OMI_CHIPUNIT;
            break;
        case(TARGETING::TYPE_OMIC) :
            o_chipUnit = PU_OMIC_CHIPUNIT;
            break;
        case(TARGETING::TYPE_NPU) :
            o_chipUnit = PU_NPU_CHIPUNIT;
            break;
    }
}

errlHndl_t scomTranslate(TARGETING::Target * &i_target,
                         uint64_t & io_addr,
                         bool & o_needsWakeup,
                         uint64_t i_opMode)
{
    errlHndl_t l_err = NULL;

    // Get the type attribute.
    TARGETING::TYPE l_type = i_target->getAttr<TARGETING::ATTR_TYPE>();

    centaurChipUnits_t l_cenChipUnit = CENTAUR_CHIP;
    p9ChipUnits_t l_p9ChipUnit = NONE;
    if(false == getChipUnitP9(l_type,l_p9ChipUnit))
    {
        p9_translation(
            i_target,
            l_type,
            io_addr,
            o_needsWakeup,
            i_opMode);
    }
}

errlHndl_t core_checkstop_helper_homer()
{
    errlHndl_t l_errl = NULL;
    TARGETING::Target* l_sys = NULL;
    TARGETING::targetService().getTopLevelTarget(l_sys);

    uint64_t l_action0 = l_sys->getAttr<TARGETING::ATTR_ORIG_FIR_SETTINGS_ACTION0>();
    uint64_t l_action1 = l_sys->getAttr<TARGETING::ATTR_ORIG_FIR_SETTINGS_ACTION1>();

    uint64_t l_local_xstop = l_action0 & l_action1;
    l_action0 &= ~l_local_xstop;
    l_action1 &= ~l_local_xstop;

    TARGETING::TargetHandleList l_coreIds;
    getAllChiplets( l_coreIds, TYPE_CORE, true );

    for(TARGETING::Target* l_core : l_coreIds)
    {
        const TARGETING::Target* l_procChip = TARGETING::getParentChip(l_core);
        const uint64_t l_homerAddr = l_procChip->getAttr<TARGETING::ATTR_HOMER_PHYS_ADDR>();
        void* l_homerVAddr = HBPM::convertHomerPhysToVirt((TARGETING::Target*) l_procChip, l_homerAddr);
        uint64_t l_scomAddr = C_CORE_ACTION0;
        bool l_needsWakeup = false;

        l_errl = SCOM::scomTranslate( l_core, l_scomAddr,
                                        l_needsWakeup );
        if( l_errl )
        {
            break;
        }

        stopImageSection::StopReturnCode_t l_srErrl =
            p9_stop_save_scom( l_homerVAddr,
            l_scomAddr, l_action0,
            stopImageSection::P9_STOP_SCOM_REPLACE,
            stopImageSection::P9_STOP_SECTION_CORE_SCOM );

        if( l_srErrl != stopImageSection::StopReturnCode_t::
                            STOP_SAVE_SUCCESS )
        {
            break;
        }
        l_scomAddr = C_CORE_ACTION1;

        l_errl = SCOM::scomTranslate(l_core, l_scomAddr,
                            l_needsWakeup);

        if( l_errl )
        {
            break;
        }

        l_srErrl = p9_stop_save_scom( l_homerVAddr,
                            l_scomAddr, l_action1,
                            stopImageSection::P9_STOP_SCOM_REPLACE,
                            stopImageSection::P9_STOP_SECTION_CORE_SCOM );

        if( l_srErrl != stopImageSection::StopReturnCode_t::
                            STOP_SAVE_SUCCESS )
        {
            return;
        }

    }

}

fapi2::ReturnCode p9_core_checkstop_handler(
    const fapi2::Target<fapi2::TARGET_TYPE_CORE>& i_target_core,
    bool i_override_restore)
{
    fapi2::buffer<uint64_t> l_action0;
    fapi2::buffer<uint64_t> l_action1;
    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM;
    if(i_override_restore == CORE_XSTOP_HNDLR__SAVE_AND_ESCALATE)
    {
        fapi2::getScom(i_target_core, C_CORE_ACTION0, l_action0);
        fapi2::getScom(i_target_core, C_CORE_ACTION1, l_action1);
        FAPI_ATTR_SET(fapi2::ATTR_ORIG_FIR_SETTINGS_ACTION0, FAPI_SYSTEM, l_action0);
        FAPI_ATTR_SET(fapi2::ATTR_ORIG_FIR_SETTINGS_ACTION1, FAPI_SYSTEM, l_action1);
        uint64_t l_local_xstop = l_action0 & l_action1;
        l_action0 &= ~l_local_xstop;
        l_action1 &= ~l_local_xstop;
    }
    else
    {
        FAPI_ATTR_GET(fapi2::ATTR_ORIG_FIR_SETTINGS_ACTION0, FAPI_SYSTEM, l_action0);
        FAPI_ATTR_GET(fapi2::ATTR_ORIG_FIR_SETTINGS_ACTION1, FAPI_SYSTEM, l_action1);
    }
    fapi2::putScom(i_target_core, C_CORE_ACTION0, l_action0);
    fapi2::putScom(i_target_core, C_CORE_ACTION1, l_action1);
}

errlHndl_t core_checkstop_helper_hwp( const TARGETING::Target* i_core_target,
                            bool i_override_restore)
{
    errlHndl_t l_errl = NULL;
    TARGETING::Target* l_sys = NULL;
    TARGETING::targetService().getTopLevelTarget(l_sys);

    const fapi2::Target<fapi2::TARGET_TYPE_CORE> l_fapi2_coreTarget(
            const_cast<TARGETING::Target*> ( i_core_target ));

    p9_core_checkstop_handler(l_fapi2_coreTarget, i_override_restore);
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
    uint64_t l_homerPhysAddr = 0x0;
    l_homerPhysAddr = i_target->getAttr<TARGETING::ATTR_HOMER_PHYS_ADDR>();
    void* l_homerVAddr = convertHomerPhysToVirt(i_target,l_homerPhysAddr);


    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>
        l_fapiTarg(i_target);

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

template< fapi2::TargetType K >
fapi2::ReturnCode checkChiplet( CONST_FAPI2_PROC& i_procTgt, fapi2::TargetType  i_type,
                                uint64_t& io_configVector, uint8_t i_chipletStartPos )
{
    auto l_childVector =
        i_procTgt.getChildren < K > ( fapi2::TARGET_STATE_PRESENT );

    for( auto itv : l_childVector )
    {
        uint8_t l_chipletPos = 0;
        FAPI_ATTR_GET( fapi2::ATTR_CHIP_UNIT_POS, itv, l_chipletPos );
        if( itv.isFunctional() )
        {
            uint8_t l_configPos = i_chipletStartPos + l_chipletPos;
            io_configVector |= (0x8000000000000000ull >> l_configPos);
        }
    }
}

fapi2::ReturnCode checkMemConfig( CONST_FAPI2_PROC& i_procTgt, uint64_t& io_configVector )
{
    auto l_dmiChiplets = i_procTgt.getChildren<fapi2::TARGET_TYPE_DMI>( fapi2::TARGET_STATE_PRESENT );
    for( auto l_dmi : l_dmiChiplets )
    {
        auto l_cenChips = l_dmi.getChildren<fapi2::TARGET_TYPE_MEMBUF_CHIP>( fapi2::TARGET_STATE_PRESENT );
        for( auto l_cent : l_cenChips )
        {
            auto l_mbaChiplets = l_cent.getChildren<fapi2::TARGET_TYPE_MBA>( fapi2::TARGET_STATE_PRESENT );
            uint8_t l_memBufPos = 0;
            uint8_t l_memBufBitPos = 0;
            FAPI_ATTR_GET( fapi2::ATTR_CHIP_UNIT_POS, l_dmi, l_memBufPos );

            if( l_cent.isFunctional( ) )
            {
                l_memBufBitPos = l_memBufPos + MEM_BUF_POS;
                io_configVector |= (0x8000000000000000ull >> l_memBufBitPos);
            }

            for( auto l_mba : l_mbaChiplets )
            {
                if( l_mba.isFunctional( ) )
                {
                    uint8_t l_mbaPos = 0;
                    uint8_t l_mbaBitPos = 0;
                    FAPI_ATTR_GET( fapi2::ATTR_CHIP_UNIT_POS, l_mba, l_mbaPos );

                    l_mbaBitPos = ( l_mbaChiplets.size() * l_memBufPos ) + l_mbaPos + MBA_POS;
                    io_configVector |= (0x8000000000000000ull >> l_mbaBitPos);
                }
            }
        }
    }
}

fapi2::ReturnCode checkAbusConfig( CONST_FAPI2_PROC& i_procTgt, uint64_t& io_configVector )
{
    auto l_obusList         = i_procTgt.getChildren<fapi2::TARGET_TYPE_OBUS>(  );
    uint8_t l_obusPos       = 0;
    uint8_t l_configMode    = 0;
    uint64_t l_tempVector   = 0;

    for( auto l_obus : l_obusList )
    {
        FAPI_ATTR_GET( fapi2::ATTR_OPTICS_CONFIG_MODE, l_obus, l_configMode );
        FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, l_obus, l_obusPos );

        if( fapi2::ENUM_ATTR_OPTICS_CONFIG_MODE_SMP == l_configMode )
        {
            l_tempVector |= ((0x8000000000000000ull) >> ( ABUS_POS + l_obusPos));
        }
    }
    io_configVector |= l_tempVector;
}

fapi2::ReturnCode checkObusChipletHierarchy( CONST_FAPI2_PROC& i_procTgt,
        uint64_t& io_configVector, uint8_t i_oBusStartPos, uint8_t i_nvLinkPos )
{
    auto l_obusList =
        i_procTgt.getChildren < fapi2::TARGET_TYPE_OBUS > ( fapi2::TARGET_STATE_PRESENT );

    for( auto itv : l_obusList )
    {
        uint8_t l_oBusPos = 0;

        FAPI_ATTR_GET( fapi2::ATTR_CHIP_UNIT_POS, itv, l_oBusPos );

        if( itv.isFunctional() )
        {
            uint8_t l_configMode = 0;
            auto l_oBusBrickList = itv.getChildren<fapi2::TARGET_TYPE_OBUS_BRICK>( fapi2::TARGET_STATE_FUNCTIONAL );

            FAPI_ATTR_GET( fapi2::ATTR_OPTICS_CONFIG_MODE, itv, l_configMode );

            for(auto l_obusBrick : l_oBusBrickList)
            {
                uint8_t l_brickPos   = 0;
                uint8_t l_brickBitPos = 0;


                if( fapi2::ENUM_ATTR_OPTICS_CONFIG_MODE_NV == l_configMode )
                {
                    FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, l_obusBrick, l_brickPos);

                    switch(l_brickPos)
                    {
                        case OBUS_BRICK_0_POS:
                        case OBUS_BRICK_1_POS:
                        case OBUS_BRICK_2_POS:
                            l_brickBitPos = l_brickPos + i_nvLinkPos;
                            break;
                        case OBUS_BRICK_9_POS:
                            l_brickBitPos = i_nvLinkPos + 3;
                            break;
                        case OBUS_BRICK_10_POS:
                            l_brickBitPos = i_nvLinkPos + 4;
                            break;
                        case OBUS_BRICK_11_POS:
                            l_brickBitPos = i_nvLinkPos + 5;
                            break;
                    }
                    io_configVector |= 0x8000000000000000ull >> l_brickBitPos;
                }
            }
        }
    }
}

fapi2::ReturnCode p9_check_proc_config ( CONST_FAPI2_PROC& i_procTgt, void* i_pHomerImage )
{
    uint64_t l_configVectVal = INIT_CONFIG_VALUE;
    uint8_t* pHomer = (uint8_t*)i_pHomerImage + QPMR_HOMER_OFFSET + QPMR_PROC_CONFIG_POS;
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

    checkChiplet< fapi2::TARGET_TYPE_MCS >( i_procTgt, fapi2::TARGET_TYPE_MCS, l_configVectVal, MCS_POS );
    checkChiplet< fapi2::TARGET_TYPE_XBUS>( i_procTgt, fapi2::TARGET_TYPE_XBUS, l_configVectVal, XBUS_POS );
    checkChiplet<fapi2::TARGET_TYPE_PHB>( i_procTgt, fapi2::TARGET_TYPE_PHB, l_configVectVal, PHB_POS );
    checkChiplet<fapi2::TARGET_TYPE_CAPP>( i_procTgt, fapi2::TARGET_TYPE_CAPP, l_configVectVal, CAPP_POS );

    FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_NAME, i_procTgt, l_chipName );

    if( fapi2::ENUM_ATTR_NAME_NIMBUS == l_chipName )
    {
        checkObusChipletHierarchy ( i_procTgt, l_configVectVal, OBUS_POS, NVLINK_POS );
    }
    else
    {
        checkAbusConfig( i_procTgt, l_configVectVal );
    }

    if( fapi2::ENUM_ATTR_NAME_CUMULUS == l_chipName )
    {
        checkMemConfig( i_procTgt, l_configVectVal );
    }
    else
    {
        checkChiplet<fapi2::TARGET_TYPE_MCA>(i_procTgt, fapi2::TARGET_TYPE_MCA, l_configVectVal, MBA_POS);
    }
    *(uint64_t*)pHomer = htobe64( l_configVectVal );
}
```
