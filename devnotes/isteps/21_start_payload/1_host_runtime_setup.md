In istep 21.1:

* `call_host_runtime_setup` is the entry point

* `loadPMComplex`
   - called for all processors, not only for master
   - `useSRAM` parameter is `false`

* `p9_pm_init`
   - called with `PM_INIT` instead `PM_RESET`

* `loadOCCSetup()`
   - called by `loadPMComplex()`

* `loadOCCImageToHomer` is called instead of `loadOCCImageDuringIpl`
   - called by `loadPMComplex()`
   - LID image for OCC is loaded from "OCC" partion of PNOR (whole partition)

* `pm_init`
   - called by `startPMComplex()`

* `p9_pm_occ_control()`
   - called by `pm_init()`

***

Analisys assumptions:

* `#define CONFIG_START_OCC_DURING_BOOT`
* `#define __HOSTBOOT_MODULE`
* `#define CONFIG_HTMGT`
* `#undef CONFIG_IPLTIME_CHECKSTOP_ANALYSIS`
* `#undef CONFIG_PNOR_TWO_SIDE_SUPPORT`
* `#undef CONFIG_ENABLE_CHECKSTOP_ANALYSIS`
* `#undef __HOSTBOOT_RUNTIME`
* `#undef CONFIG_BMC_IPMI`
* `#undef SIMICS_TESTING`
* `#undef CONFIG_CONSOLE_OUTPUT_OCC_COMM`
* Not in MPIPL mode (`ATTR_IS_MPIPL_HB == false`).
* Simics is not running (`ATTR_PAYLOAD_KIND != PAYLOAD_KIND_NONE`).
* Payload isn't PHYP (`ATTR_PAYLOAD_KIND != PAYLOAD_KIND_PHYP`).
* Payload is sapphire (`ATTR_PAYLOAD_KIND == PAYLOAD_KIND_SAPPHIRE`).
* Loading a payload (`TARGETING::is_no_load() == false`, `ATTR_PAYLOAD_KIND != PAYLOAD_KIND_NONE`)
* `ATTR_PM_MALF_ALERT_ENABLE == ENUM_ATTR_PM_MALF_ALERT_ENABLE_FALSE`.
* `ATTR_PM_MALF_CYCLE == ENUM_ATTR_PM_MALF_CYCLE_INACTIVE`.
* `INITSERVICE::spBaseServicesEnabled() == false` (from the log).
* `TCE::utilUseTcesForDmas() == false` because of `INITSERVICE::spBaseServicesEnabled() == false`.
* Homer address is never `NULL`.
* Virtual addresses equal physical addresses.
* Not in manufacturing mode or golden-side boot.
* SMF is not enabled.
* Have only one OCC and it's master, but not a FIR master.

***

HDAT -- Host data area. Collection of attributes, device information, and other
derived information used by the payload to manage the computer system.

Data (stored in HDAT):

* NACA
* SPIRA

"PROC_DUMP"/"CPU CTRL" area might be needed by payload for something.

***

Control flow of OCC start and initialization process:

1. `loadAndStartPMAll(HBPM::PM_LOAD)`
   1. `loadPMComplex()` (for each processor get OCC ready for work)
      1. `loadOCCSetup()` (write addresses of OCC image and OCC area through SCOM)
         - address is `homer + HOMER_OFFSET_TO_OCC_IMG`
      2. `loadOCCImageToHomer()` (actually copy OCC image to that address)
      3. `loadHostDataToHomer()` (just a couple of fields are set)
      4. `loadHcode()` (builds HOMER image, which we already have at this point)
   2. `startPMComplex()` -> `pm_init()`
      1. Various inits, probably not directly related.
      2. `p9_check_proc_config()` (generates a bit vector corresponding to P9 chip config and stores in HOMER)
      3. `clear_occ_special_wakeups()` (resets previous state? might be needed)
      4. `special_wakeup_all(false)` (might be needed for synchronization)
      5. `p9_pm_occ_control()` (setups boot code in SRAM and starts OCC)
         1. Setup boot vector registers in SRAM
         2. `bootMemory()`
            - composes boot code by calling `bootMemory()`
            - uses OCB to write 128 bytes to SRAM
         3. Start OCC
      6. `putScom(PU_OCB_OCI_OCCFLG2_CLEAR, STOP_RECOVERY_TRIGGER_ENABLE)` (needed?)
2. `processOccStartStatus(true)`
   1. `calcMemThrottles()` (seems to compute data to be sent to OCC, e.g. power) (1 level)
   2. `waitForOccCheckpoint()` (waits for all OCCs to finish their initialization)
   3. `sendOccPoll()` (also called by several functions below)
      1. `pollForErrors()` (checks whether OCCs are in error state) (2 levels)
   4. `sendOccConfigData()` (collects and sends various data to OCCs) (1 level and some more)
   5. `sendOccUserPowerCap()` (sets user power limit, 0 means "not set") (1 level)
   6. `waitForOccState()` (wait for OCCs to become "Active-ready" and set "Active" state)
   7. `setOccActiveSensors()` (signals to BMC that it can interract with OCC)

Non-essential (as it appears) paths weren't followed too deeply.

From https://raw.githubusercontent.com/open-power/docs/master/occ/OCC_OpenPwr_FW_Interfaces.pdf:

    4  OCC Boot Process
    After the OCC is loaded and taken out of reset it will default to “standby” state and wait for
    configuration data from HTMGT and for HTMGT to send Set State command to Active.  There
    is no thermal or power monitoring until the OCC is in active state.  When OCC is told to go
    active it will populate OCC-OPAL shared memory interface with ‘valid’ and all Pstate data.

On detecting errors in OCCs, Hostboot can reset them.

***

```cpp
errlHndl_t utilDisableTces(void)
{
    return Singleton<UtilTceMgr>::instance().disableTces();
};

/**
 * @brief Populate HB runtime data in mainstore
 *
 * @return errlHndl_t NULL on Success
 */
errlHndl_t populate_hbRuntimeData( void )
{
    errlHndl_t  l_elog = nullptr;

    do {
        TRACFCOMP(g_trac_runtime, "Running populate_hbRuntimeData");

        // Figure out which node we are running on
        TARGETING::Target* mproc = nullptr;
        TARGETING::targetService().masterProcChipTargetHandle(mproc);

        TARGETING::EntityPath epath =
            mproc->getAttr<TARGETING::ATTR_PHYS_PATH>();

        const TARGETING::EntityPath::PathElement pe =
            epath.pathElementOfType(TARGETING::TYPE_NODE);

        uint64_t l_masterNodeId = pe.instance;

        TRACFCOMP( g_trac_runtime, "Master node nodeid = %x",
                   l_masterNodeId);

        // ATTR_HB_EXISTING_IMAGE only gets set on a multi-drawer system.
        // Currently set up in host_sys_fab_iovalid_processing() which only
        // gets called if there are multiple physical nodes.   It eventually
        // needs to be setup by a hb routine that snoops for multiple nodes.
        TARGETING::Target * sys = nullptr;
        TARGETING::targetService().getTopLevelTarget( sys );
        assert(sys != nullptr);

        TARGETING::ATTR_HB_EXISTING_IMAGE_type hb_images =
            sys->getAttr<TARGETING::ATTR_HB_EXISTING_IMAGE>();

        TRACFCOMP( g_trac_runtime, "ATTR_HB_EXISTING_IMAGE (hb_images) = %x",
                hb_images);

        if (0 == hb_images)  //Single-node
        {
            l_elog = populate_HbRsvMem(l_masterNodeId,true);
            if(l_elog != nullptr)
            {
                TRACFCOMP( g_trac_runtime, "populate_HbRsvMem failed" );
            }
        }
        else
        {
            // multi-node system
            uint64_t payloadBase = sys->getAttr<TARGETING::ATTR_PAYLOAD_BASE>();

            // populate our own node specific data + the common stuff
            l_elog = populate_HbRsvMem(l_masterNodeId,true);

            if(l_elog != nullptr)
            {
                TRACFCOMP( g_trac_runtime, "populate_HbRsvMem failed" );
                break;
            }

            // This msgQ catches the node responses from the commands
            msg_q_t msgQ = msg_q_create();
            l_elog = MBOX::msgq_register(MBOX::HB_POP_ATTR_MSGQ,msgQ);

            if(l_elog)
            {
                TRACFCOMP( g_trac_runtime, "MBOX::msgq_register failed!" );
                break;
            }

            // keep track of the number of messages we send so we
            // know how many responses to expect
            uint64_t msg_count = 0;

            // loop thru rest all nodes -- sending msg to each
            TARGETING::ATTR_HB_EXISTING_IMAGE_type mask = 0x1 <<
                ((sizeof(TARGETING::ATTR_HB_EXISTING_IMAGE_type) * 8) -1);

            TRACFCOMP( g_trac_runtime, "HB_EXISTING_IMAGE (mask) = %x",
                    mask);

            for (uint64_t l_node=0; (l_node < MAX_NODES_PER_SYS); l_node++ )
            {
                // skip sending to ourselves, we did our construction above
                if(l_node == l_masterNodeId)
                    continue;

                if( 0 != ((mask >> l_node) & hb_images ) )
                {
                    TRACFCOMP( g_trac_runtime, "send IPC_POPULATE_ATTRIBUTES "
                            "message to node %d",
                            l_node );

                    msg_t * msg = msg_allocate();
                    msg->type = IPC::IPC_POPULATE_ATTRIBUTES;
                    msg->data[0] = l_node;      // destination node
                    msg->data[1] = l_masterNodeId; // respond to this node
                    msg->extra_data = reinterpret_cast<uint64_t*>(payloadBase);

                    // send the message to the slave hb instance
                    l_elog = MBOX::send(MBOX::HB_IPC_MSGQ, msg, l_node);

                    if( l_elog )
                    {
                        TRACFCOMP( g_trac_runtime, "MBOX::send to node %d"
                                " failed", l_node);
                        break;
                    }

                    ++msg_count;

                } // end if node to process
            } // end for loop on nodes

            // wait for a response to each message we sent
            if( l_elog == nullptr )
            {
                //$TODO RTC:189356 - need timeout here
                while(msg_count)
                {
                    msg_t * response = msg_wait(msgQ);
                    TRACFCOMP(g_trac_runtime,
                            "IPC_POPULATE_ATTRIBUTES : drawer %d completed",
                            response->data[0]);
                    msg_free(response);
                    --msg_count;
                }
            }

            MBOX::msgq_unregister(MBOX::HB_POP_ATTR_MSGQ);
            msg_q_destroy(msgQ);
        }

    } while(0);

    return(l_elog);
}

errlHndl_t writeActualCount( SectionId i_id )
{
    return Singleton<hdatService>::instance().writeActualCount(i_id);
}

errlHndl_t hdatService::getAndCheckTuple(const SectionId i_section,
                                         hdat5Tuple_t*& o_tuple)
{
    errlHndl_t errhdl = nullptr;
    o_tuple = nullptr;

    do
    {
        hdatSpiraSDataAreas l_spiraS = SPIRAS_INVALID;
        hdatSpiraLegacyDataAreas l_spiraL = SPIRAL_INVALID;
        hdatSpiraHDataAreas l_spiraH = SPIRAH_INVALID;

        switch(i_section)
        {
        case RUNTIME::RESERVED_MEM:
            l_spiraS = SPIRAS_MDT;
            l_spiraL = SPIRAL_MDT;
            break;
        case RUNTIME::HBRT:
        case RUNTIME::HBRT_DATA:
            l_spiraS = SPIRAS_HBRT_DATA;
            l_spiraL = SPIRAL_HBRT_DATA;
            break;
        case RUNTIME::IPLPARMS_SYSTEM:
            l_spiraS = SPIRAS_IPL_PARMS;
            l_spiraL = SPIRAL_IPL_PARMS;
            break;
        case RUNTIME::NODE_TPM_RELATED:
            l_spiraS = SPIRAS_TPM_DATA;
            l_spiraL = SPIRAL_TPM_DATA;
            break;
        case RUNTIME::PCRD:
            l_spiraS = SPIRAS_PCRD;
            l_spiraL = SPIRAL_PCRD;
            break;
        case RUNTIME::MS_DUMP_SRC_TBL:
            l_spiraH = SPIRAH_MS_DUMP_SRC_TBL;
            l_spiraL = SPIRAL_MS_DUMP_SRC_TBL;
            break;
        case RUNTIME::MS_DUMP_DST_TBL:
            l_spiraH = SPIRAH_MS_DUMP_DST_TBL;
            l_spiraL = SPIRAL_MS_DUMP_DST_TBL;
            break;
        case RUNTIME::MS_DUMP_RESULTS_TBL:
            l_spiraH = SPIRAH_MS_DUMP_RSLT_TBL;
            l_spiraL = SPIRAL_MS_DUMP_RSLT_TBL;
            break;
        case RUNTIME::PROC_DUMP_AREA_TBL:
            l_spiraH = SPIRAH_PROC_DUMP_TBL;
            l_spiraL = SPIRAL_INVALID;
            break;
        case RUNTIME::HSVC_SYSTEM_DATA:
        case RUNTIME::HSVC_NODE_DATA:
            l_spiraS = SPIRAS_HSVC_DATA;
            l_spiraL = SPIRAL_HSVC_DATA;
            break;
        case RUNTIME::HRMOR_STASH:
            l_spiraS = SPIRAS_MDT;
            l_spiraL = SPIRAL_MDT;
            break;
        case RUNTIME::CPU_CTRL:
            l_spiraH = SPIRAH_CPU_CTRL;
            l_spiraL = SPIRAL_CPU_CTRL;
            break;
        default:
            TRACFCOMP(g_trac_runtime, ERR_MRK"getAndCheckTuple> section %d not supported",
                      i_section );
            /*@
             * @errortype
             * @moduleid     RUNTIME::MOD_HDATSERVICE_GETANDCHECKTUPLE
             * @reasoncode   RUNTIME::RC_GETTUPLE_UNSUPPORTED
             * @userdata1    Section Id
             * @userdata2    <unused>
             * @devdesc      Unsupported section requested
             * @custdesc     Unexpected boot firmware error.
             */
            errhdl = new ERRORLOG::ErrlEntry(
                           ERRORLOG::ERRL_SEV_UNRECOVERABLE,
                           RUNTIME::MOD_HDATSERVICE_GETANDCHECKTUPLE,
                           RUNTIME::RC_GETTUPLE_UNSUPPORTED,
                           i_section,
                           0,
                           true /*Add HB Software Callout*/);
            errhdl->collectTrace(RUNTIME_COMP_NAME,KILOBYTE);;
            break;
        }

        if( iv_spiraS && l_spiraS != SPIRAS_INVALID )
        {
            o_tuple = &(iv_spiraS->hdatDataArea[l_spiraS]);
        }
        else if( iv_spiraH && l_spiraH != SPIRAH_INVALID )
        {
            o_tuple = &(iv_spiraH->hdatDataArea[l_spiraH]);
        }
        else if( unlikely(iv_spiraL != nullptr && l_spiraL != SPIRAL_INVALID) )
        {
            o_tuple = &(iv_spiraL->hdatDataArea[l_spiraL]);
        }
        errhdl = check_tuple( i_section, o_tuple );
        if( errhdl )
        {
            break;
        }

    } while (0);

    return errhdl;
}

void hdatService::verify_hdat_address( const void* i_addr, size_t i_size )
{
    bool found = false;
    uint64_t l_end =  reinterpret_cast<uint64_t>(i_addr) + i_size;

    // Make sure that the entire range is within the memory
    //  space that we allocated
    for(cmemRegionItr region = iv_mem_regions.begin();
        region != iv_mem_regions.end() && !found; ++region)
    {
        hdatMemRegion_t memR = *region;

        uint64_t l_range_end = reinterpret_cast<uint64_t>(memR.virt_addr)
                             +  memR.size;
        if (i_addr >= memR.virt_addr && l_end <= l_range_end)
        {
            found = true;
            break;
        }
    }

    if(!found)
        TRACFCOMP( g_trac_runtime, "Invalid HDAT Address : i_addr=%p, i_size=0x%X", i_addr, i_size );
}

errlHndl_t hdatService::getSpiraTupleVA(hdat5Tuple_t* i_tuple, uint64_t & o_vaddr)
{
    errlHndl_t errhdl = NULL;
    bool found = false;
    o_vaddr = 0x0;
    uint64_t l_phys_addr, l_size;

    //PHYP and Sapphire have different philsophies about how they
    //lay the HDAT memory out.  PHYP puts it all within a 128MB
    //area.  Sapphire puts the NACA in one area and then all of the
    //SPIRA data sections in another (way up in memory).  This
    //function checks to see if the requested region is already
    //mapped, and if not it will map it.
    //
    //It then returns the "base" virtual pointer for the requested
    //tuple

    //Note that if Sapphire/PHYP change how they do things this
    //code will break (and the various address checking is expected
    //to catch it)

    do
    {
        // Get the absolute address = tuple addr + HRMOR (payload base)
        l_phys_addr = i_tuple->hdatAbsAddr + iv_mem_regions[0].phys_addr;
        l_size = i_tuple->hdatActualCnt * i_tuple->hdatActualSize;

        TRACUCOMP( g_trac_runtime, "SPIRA Data ptr 0x%X, size 0x%X",
                   l_phys_addr, l_size);

        //Check to see if the requested data fully falls within
        //an existing mapping if so do nothing
        for(memRegionItr region = iv_mem_regions.begin();
            (region != iv_mem_regions.end()) && !found; ++region)
        {
            hdatMemRegion_t memR = *region;

            if ((l_phys_addr >= memR.phys_addr) &&
                ((l_phys_addr + l_size) < (memR.phys_addr + memR.size)))
            {
                found = true;
                o_vaddr = reinterpret_cast<uint64_t>(memR.virt_addr);
                o_vaddr = o_vaddr + (l_phys_addr-memR.phys_addr);
                break;
            }
        }

        //if not found, then map it in
        if(!found)
        {
            TRACFCOMP( g_trac_runtime, "SPIRA Data @ 0x%X not mapped, mapping",
                       l_phys_addr);
            errhdl = mapRegion(l_phys_addr, l_size, o_vaddr);
            if(errhdl)
            {
                break;
            }
        }
    }while(0);

    TRACUCOMP( g_trac_runtime, "SPIRA Data Base Data ptr 0x%X", o_vaddr);


    return errhdl;
}

/**
 * @brief Locates the proper SPIRA structure and sets instance vars
 *
 * Walks the NACA and interrogates structures to determine which
 * kind of SPIRA is available (if any).
 *
 * @return errlHndl_t  NULL on success
 */
errlHndl_t hdatService::findSpira( void )
{
    errlHndl_t errhdl = NULL;
    errlHndl_t errhdl_s = NULL; //SPIRA-S error
    errlHndl_t errhdl_l = NULL; //Legacy SPIRA error

    do {
        // Only do this once
        if( iv_spiraL || iv_spiraH || iv_spiraS )
        {
            break;
        }

        // Go fetch the relative zero address that PHYP uses
        // This is always the first entry in the vector
        uint64_t payload_base =
          reinterpret_cast<uint64_t>(iv_mem_regions[0].virt_addr);

        // Everything starts at the NACA
        //   The NACA is part of the platform dependent LID which
        //   is loaded at relative memory address 0x0
        hdatNaca_t* naca = reinterpret_cast<hdatNaca_t*>
          (HDAT_NACA_OFFSET + payload_base);
        TRACFCOMP( g_trac_runtime, "NACA=%.X->%p", HDAT_NACA_OFFSET, naca );

        // Do some sanity checks on the NACA
        if( naca->nacaPhypPciaSupport != 1 )
        {
            TRACFCOMP( g_trac_runtime, "findSpira> nacaPhypPciaSupport=%.8X", naca->nacaPhypPciaSupport );

            // Figure out what kind of payload we have
            TARGETING::Target * sys = NULL;
            TARGETING::targetService().getTopLevelTarget( sys );
            TARGETING::PAYLOAD_KIND payload_kind
              = sys->getAttr<TARGETING::ATTR_PAYLOAD_KIND>();

            // Go get the physical address we mapped in
            uint64_t phys_addr =
              mm_virt_to_phys(reinterpret_cast<void*>(naca));

            /*@
             * @errortype
             * @moduleid     RUNTIME::MOD_HDATSERVICE_FINDSPIRA
             * @reasoncode   RUNTIME::RC_BAD_NACA
             * @userdata1    Mainstore address of NACA
             * @userdata2[0:31]    Payload Base Address
             * @userdata2[32:63]   Payload Kind
             * @devdesc      NACA data doesn't seem right
             */
            errhdl = new ERRORLOG::ErrlEntry(
                            ERRORLOG::ERRL_SEV_UNRECOVERABLE,
                            RUNTIME::MOD_HDATSERVICE_FINDSPIRA,
                            RUNTIME::RC_BAD_NACA,
                            reinterpret_cast<uint64_t>(phys_addr),
                            TWO_UINT32_TO_UINT64(payload_base,
                                                 payload_kind));
            errhdl->addProcedureCallout( HWAS::EPUB_PRC_HB_CODE,
                                         HWAS::SRCI_PRIORITY_MED );
            errhdl->addProcedureCallout( HWAS::EPUB_PRC_SP_CODE,
                                         HWAS::SRCI_PRIORITY_MED );
            errhdl->collectTrace(RUNTIME_COMP_NAME,KILOBYTE);
            RUNTIME::UdNaca(naca).addToLog(errhdl);
            break;
        }


        // Are we using the SPIRA-H/S or Legacy format
        if( naca->spiraH != 0 )
        {
            // pointer is also relative to PHYP's zero
            iv_spiraH = reinterpret_cast<hdatSpira_t*>
              (naca->spiraH + payload_base);
            TRACFCOMP( g_trac_runtime, "SPIRA-H=%X->%p", naca->spiraH, iv_spiraH );

            // Check the headers and version info
            errhdl = check_header( &(iv_spiraH->hdatHDIF),
                                   SPIRAH_HEADER );
            if( errhdl )
            {
                RUNTIME::UdNaca(naca).addToLog(errhdl);
                break;
            }

            // SPIRA-S is at the beginning of the Host Data Area Tuple
            uint64_t tuple_addr = reinterpret_cast<uint64_t>
              (&(iv_spiraH->hdatDataArea[SPIRAH_HOST_DATA_AREAS]));
            TRACUCOMP( g_trac_runtime, "SPIRA-S tuple offset=%.8X", tuple_addr );
            // need to offset from virtual zero
            //tuple_addr += payload_base;
            hdat5Tuple_t* tuple = reinterpret_cast<hdat5Tuple_t*>(tuple_addr);
            TRACUCOMP( g_trac_runtime, "SPIRA-S tuple=%p", tuple );

            errlHndl_t errhdl_s = check_tuple( SPIRA_S,
                                               tuple );
            if( errhdl_s )
            {
                TRACFCOMP( g_trac_runtime, "SPIRA-S is invalid, will try legacy SPIRA" );
                RUNTIME::UdNaca(naca).addToLog(errhdl_s);
                iv_spiraS = NULL;
            }
            else
            {
                uint64_t tmp_addr = 0;
                errhdl_s = getSpiraTupleVA( tuple, tmp_addr );
                if( errhdl_s )
                {
                    TRACFCOMP( g_trac_runtime, "Couldn't map SPIRA-S, will try legacy SPIRA" );
                    iv_spiraS = NULL;
                }
                else
                {
                    iv_spiraS = reinterpret_cast<hdatSpira_t*>(tmp_addr);
                    TRACFCOMP( g_trac_runtime, "SPIRA-S=%p", iv_spiraS );

                    // Check the headers and version info
                    errhdl_s = check_header( &(iv_spiraS->hdatHDIF),
                                             SPIRAS_HEADER );
                    if( errhdl_s )
                    {
                        TRACFCOMP( g_trac_runtime, "SPIRA-S is invalid, will try legacy SPIRA" );
                        RUNTIME::UdNaca(naca).addToLog(errhdl_s);
                        RUNTIME::UdSpira(iv_spiraS).addToLog(errhdl_s);
                        iv_spiraS = NULL;
                    }
                }
            }
        }

        //Legacy SPIRA
        // pointer is also relative to PHYP's zero
        iv_spiraL = reinterpret_cast<hdatSpira_t*>
          (naca->spiraOld + payload_base);
        TRACFCOMP( g_trac_runtime, "Legacy SPIRA=%X->%p", naca->spiraOld, iv_spiraL );

        // Make sure the SPIRA is valid
        errhdl_l = verify_hdat_address( iv_spiraL,
                                        sizeof(hdatSpira_t) );
        if( errhdl_l )
        {
            TRACFCOMP( g_trac_runtime, "Legacy Spira is at a wacky offset!!! %.16X", naca->spiraOld );
            iv_spiraL = NULL;
            RUNTIME::UdNaca(naca).addToLog(errhdl_l);
        }
        else
        {
            // Look for a filled in HEAP section to see if FSP is using the
            //  new or old format
            // (Note: this is the logic PHYP is using)
            hdat5Tuple_t* heap_tuple = &(iv_spiraL->hdatDataArea[SPIRAL_HEAP]);
            TRACUCOMP( g_trac_runtime, "HEAP tuple=%p", heap_tuple );
            if( heap_tuple->hdatActualSize == 0 )
            {
                TRACFCOMP( g_trac_runtime, "Legacy SPIRA is not filled in, using SPIRA-H/S" );
                iv_spiraL = NULL;
            }
            else
            {
                TRACFCOMP( g_trac_runtime, "Legacy SPIRA is filled in so we'll use it" );
                iv_spiraS = NULL;
            }
        }

        // Make sure we have a good SPIRA somewhere
        if( (iv_spiraL == NULL) && (iv_spiraS == NULL) )
        {
            TRACFCOMP( g_trac_runtime, "Could not find a valid SPIRA of any type" );
            /*@
             * @errortype
             * @moduleid     RUNTIME::MOD_HDATSERVICE_FINDSPIRA
             * @reasoncode   RUNTIME::RC_NO_SPIRA
             * @userdata1[0:31]    RC for Legacy SPIRA fail
             * @userdata1[32:64]   EID for Legacy SPIRA fail
             * @userdata2[0:31]    RC for SPIRA-S fail
             * @userdata2[32:64]   EID for SPIRA-S fail
             * @devdesc      Could not find a valid SPIRA of any type
             */
            errhdl = new ERRORLOG::ErrlEntry(
                           ERRORLOG::ERRL_SEV_UNRECOVERABLE,
                           RUNTIME::MOD_HDATSERVICE_FINDSPIRA,
                           RUNTIME::RC_NO_SPIRA,
                           TWO_UINT32_TO_UINT64(ERRL_GETRC_SAFE(errhdl_l),
                                                ERRL_GETEID_SAFE(errhdl_l)),
                           TWO_UINT32_TO_UINT64(ERRL_GETRC_SAFE(errhdl_s),
                                                ERRL_GETEID_SAFE(errhdl_s)));
            errhdl->addProcedureCallout( HWAS::EPUB_PRC_HB_CODE,
                                         HWAS::SRCI_PRIORITY_MED );
            errhdl->addProcedureCallout( HWAS::EPUB_PRC_SP_CODE,
                                         HWAS::SRCI_PRIORITY_MED );
            errhdl->collectTrace(RUNTIME_COMP_NAME,KILOBYTE);

            // commit the errors related to each SPIRA
            if( errhdl_s )
            {
                errhdl_s->plid(errhdl->plid());
                errlCommit(errhdl_s,RUNTIME_COMP_ID);
            }
            if( errhdl_l )
            {
                errhdl_l->plid(errhdl->plid());
                errlCommit(errhdl_l,RUNTIME_COMP_ID);
            }

            // return the summary log
            break;
        }
    } while(0);

    if( errhdl_s ) { delete errhdl_s; errhdl_s = nullptr;}
    if( errhdl_l ) { delete errhdl_l; errhdl_l = nullptr; }

    return errhdl;
}

/**
 * Save the actual count value for sections
 * -Used to keep track of things across HDAT writes
 *  from FSP
 */
uint16_t iv_actuals[RUNTIME::LAST_SECTION+1];

/**
 * @brief  Get a pointer to the beginning of a particular section of
 *         the host data memory.
 *
 * @description  The returned pointer will not include any hdat header
 *     information.
 *
 * @param[in] i_section  Chunk of data to find
 * @param[in] i_instance  Instance of section when there are multiple
 *                        entries
 * @param[out] o_dataAddr  Virtual memory address of data
 * @param[out] o_dataSize  Size of data in bytes, 0 on error,
 *                         DATA_SIZE_UNKNOWN if unknown
 *
 * @return errlHndl_t  NULL on success
 */
errlHndl_t hdatService::getHostDataSection( SectionId i_section = PROC_DUMP_AREA_TBL,
                                            uint64_t i_instance = 0,
                                            uint64_t& o_dataAddr,
                                            size_t& o_dataSize)
{
    o_dataAddr = 0;

    loadHostData();

    size_t record_size = 0;

    findSpira();
    if( RUNTIME::PROC_DUMP_AREA_TBL == i_section )
    {
        hdat5Tuple_t* tuple = nullptr;
        getAndCheckTuple(i_section, tuple);
        o_dataSize = tuple->hdatAllocSize * tuple->hdatAllocCnt;
        record_size = tuple->hdatAllocSize;
        getSpiraTupleVA(tuple, o_dataAddr);
    }

    verify_hdat_address( (void*)o_dataAddr, o_dataSize );

    if( iv_actuals[i_section] != ACTUAL_NOT_SET )
    {
        o_dataSize = iv_actuals[i_section] * record_size;
    }

    return errhdl;
}

/**
 * @brief  Update Processor Dump area section.
 *
 * @param[in] i_section        Chunk of data to find
 * @param[in] threadRegSize    Size of each thread register data
 * @param[in] threadRegVersion Register data format version
 * @param[in] capArrayAddr     Destination memory address
 * @param[in] capArraySize     Destination memory size
 *
 * @return errlHndl_t          NULL on success
 */
errlHndl_t hdatService::updateHostProcDumpActual( SectionId i_section,
                                                  uint32_t threadRegSize,
                                                  uint8_t threadRegVersion,
                                                  uint64_t capArrayAddr,
                                                  uint32_t capArraySize)
{
    uint64_t l_hostDataAddr = 0;
    uint64_t l_hostDataSize = 0;
    DUMP::procDumpAreaEntry *procDumpTable = nullptr;

    getHostDataSection(i_section, 0, l_hostDataAddr, l_hostDataSize);

    procDumpTable = reinterpret_cast<DUMP::procDumpAreaEntry *>(l_hostDataAddr);
    procDumpTable->threadRegSize    = threadRegSize;
    procDumpTable->threadRegVersion = threadRegVersion;
    procDumpTable->capArrayAddr     = capArrayAddr;
    procDumpTable->capArraySize     = capArraySize;
}

/**
 * @brief  Store the actual count of a section.
 *
 * @param[in] i_section  Chunk of data to update
 * @param[in] i_count   Actual count for MDRT entries
 *
 */
void hdatService::saveActualCount( SectionId i_id, uint16_t i_count )
{
    iv_actuals[i_id] = i_count;
}

errlHndl_t sendSBESystemConfig( void )
{
    errlHndl_t  l_elog = nullptr;
    uint64_t l_systemFabricConfigurationMap = 0;

    TARGETING::Target * sys = nullptr;
    TARGETING::targetService().getTopLevelTarget( sys );
    TARGETING::Target* mproc = nullptr;
    TARGETING::targetService().masterProcChipTargetHandle(mproc);
    TARGETING::EntityPath epath = mproc->getAttr<TARGETING::ATTR_PHYS_PATH>();
    const TARGETING::EntityPath::PathElement pe = epath.pathElementOfType(TARGETING::TYPE_NODE);
    uint64_t nodeid = pe.instance;

    TARGETING::TargetHandleList l_procChips;
    getAllChips( l_procChips, TARGETING::TYPE_PROC , true);
    for(auto l_proc : l_procChips)
    {
        uint8_t l_fabricChipId = l_proc->getAttr<TARGETING::ATTR_FABRIC_CHIP_ID>();
        uint8_t l_fabricGroupId = l_proc->getAttr<TARGETING::ATTR_FABRIC_GROUP_ID>();
        uint8_t l_bitPos = l_fabricChipId + (MAX_PROCS_PER_NODE * l_fabricGroupId);
        l_systemFabricConfigurationMap |= (0x8000000000000000 >> l_bitPos);
    }

    // ATTR_HB_EXISTING_IMAGE only gets set on a multi-drawer system.
    // Currently set up in host_sys_fab_iovalid_processing() which only
    // gets called if there are multiple physical nodes.   It eventually
    // needs to be setup by a hb routine that snoops for multiple nodes.
    TARGETING::ATTR_HB_EXISTING_IMAGE_type hb_images =
        sys->getAttr<TARGETING::ATTR_HB_EXISTING_IMAGE>();
    if (0 != hb_images)
    {
        msg_q_t msgQ = msg_q_create();
        MBOX::msgq_register(MBOX::HB_SBE_SYSCONFIG_MSGQ,msgQ);
        uint64_t msg_count = 0;

        TARGETING::ATTR_HB_EXISTING_IMAGE_type mask = 0x1 << ((sizeof(TARGETING::ATTR_HB_EXISTING_IMAGE_type) * 8) -1);
        for (uint64_t l_node=0; (l_node < MAX_NODES_PER_SYS); l_node++ )
        {
            // skip sending to ourselves, we did our construction above
            if(l_node == nodeid)
                continue;

            if( 0 != ((mask >> l_node) & hb_images ) )
            {
                msg_t * msg = msg_allocate();
                msg->type = IPC::IPC_QUERY_CHIPINFO;
                msg->data[0] = l_node;
                msg->data[1] = nodeid;
                MBOX::send(MBOX::HB_IPC_MSGQ, msg, l_node);
                ++msg_count;
            }
        }

        collectRespFromAllDrawers( &msgQ, msg_count, IPC::IPC_QUERY_CHIPINFO, l_systemFabricConfigurationMap);
        msg_count = 0;
        for (uint64_t l_node=0; (l_node < MAX_NODES_PER_SYS); l_node++ )
        {
            // skip sending to ourselves, we will do our set below
            if(l_node == nodeid)
                continue;

            if( 0 != ((mask >> l_node) & hb_images ) )
            {
                msg_t * msg = msg_allocate();
                msg->type = IPC::IPC_SET_SBE_CHIPINFO;
                msg->data[0] = l_node;
                msg->data[1] = nodeid;
                msg->extra_data = (uint64_t*)(l_systemFabricConfigurationMap);
                MBOX::send(MBOX::HB_IPC_MSGQ, msg, l_node);
                ++msg_count;
            }
        }

        collectRespFromAllDrawers( &msgQ, msg_count, IPC::IPC_SET_SBE_CHIPINFO, l_systemFabricConfigurationMap);
        MBOX::msgq_unregister(MBOX::HB_SBE_SYSCONFIG_MSGQ);
        msg_q_destroy(msgQ);
    }
    for(auto l_proc : l_procChips)
    {
        SBEIO::sendSystemConfig(l_systemFabricConfigurationMap, l_proc);
    }

}

VfsSystemModule * vfs_find_module(VfsSystemModule * i_table, const char * i_name)
{
    VfsSystemModule* module = i_table;
    VfsSystemModule* ret = NULL;
    while ('\0' != module->module[0])
    {
        if (0 == strcmp(i_name,module->module))
        {
            ret = module;
            break;
        }
        module++;
    }
    return ret;
}

const VfsSystemModule * VfsRp::get_vfs_info(const char * i_name) const
{
    return vfs_find_module((VfsSystemModule *)(iv_pnor_vaddr + VFS_EXTENDED_MODULE_TABLE_OFFSET), i_name);
}


bool VfsRp::is_module_loaded(const char * i_name)
{
    bool result = false;
    const VfsSystemModule * module = get_vfs_info(i_name);
    if(module)
    {
        mutex_lock(&iv_mutex);
        ModuleList_t::const_iterator i = std::find(iv_loaded.begin(),iv_loaded.end(), module);
        if(i != iv_loaded.end())
        {
            result = true;
        }
        mutex_unlock(&iv_mutex);
    }
    if(!result)  // look in the base
    {
        module = vfs_find_module(VFS_MODULES,i_name);
        if(module)
        {
            // all base modules are always loaded
            result = true;
        }
    }

    return result;
}

bool VFS::module_is_loaded(const char * i_name)
{
    return Singleton<VfsRp>::instance().is_module_loaded(i_name);
}

errlHndl_t VFS::module_load_unload(const char * i_module, VfsMessages i_msgtype)
{
    errlHndl_t err = NULL;
    msg_q_t vfsQ = msg_q_resolve(VFS_ROOT_MSG_VFS);
    msg_t* msg = msg_allocate();
    msg->type = i_msgtype;
    msg->data[0] = (uint64_t) i_module;
    int rc = msg_sendrecv(vfsQ, msg);

    if (0 == rc)
    {
        err = (errlHndl_t) msg->data[0];
    }

    msg_free(msg);
    return err;
}

inline errlHndl_t module_load(const char * i_module)
{
    return VFS::module_load_unload(i_module,VFS_MSG_LOAD);
}

errlHndl_t closeNonMasterTces(void)
{
    errlHndl_t l_err = nullptr;
    uint64_t nodeid = TARGETING::UTIL::getCurrentNodePhysId();
    uint64_t msg_count = 0;
    TARGETING::Target * sys = nullptr;
    TARGETING::targetService().getTopLevelTarget( sys );

    TARGETING::ATTR_HB_EXISTING_IMAGE_type hb_images = sys->getAttr<TARGETING::ATTR_HB_EXISTING_IMAGE>();
    msg_q_t msgQ = msg_q_create();
    MBOX::msgq_register(MBOX::HB_CLOSE_TCES_MSGQ,msgQ);

    TARGETING::ATTR_HB_EXISTING_IMAGE_type mask = 0x1 <<
        ((sizeof(TARGETING::ATTR_HB_EXISTING_IMAGE_type) * 8) -1);

    for (uint64_t l_node = 0; (l_node < MAX_NODES_PER_SYS); l_node++ )
    {
        if (l_node == nodeid)
        {
            continue;
        }

        if(0 != ((mask >> l_node) & hb_images))
        {
            msg_t * msg = msg_allocate();
            msg->type = IPC::IPC_CLOSE_TCES;
            msg->data[0] = l_node;
            msg->data[1] = nodeid;

            MBOX::send(MBOX::HB_IPC_MSGQ, msg, l_node);
            ++msg_count;
        }
    }
    while(msg_count)
    {
        msg_t * response = msg_wait(msgQ);
        msg_free(response);
        --msg_count;
    }
    MBOX::msgq_unregister(MBOX::HB_CLOSE_TCES_MSGQ);
    msg_q_destroy(msgQ);
    return l_err;
}

errlHndl_t UtilTceMgr::deallocateTces(const uint32_t i_startingToken)
{

    errlHndl_t errl = nullptr;
    uint32_t startingIndex = 0;
    uint64_t startingAddress = 0;
    size_t size = 0;

    TceEntry_t *tablePtr = nullptr;

    std::map<uint32_t, TceEntryInfo_t>::iterator map_itr = iv_allocatedAddrs.find(i_startingToken);
    if( map_itr == iv_allocatedAddrs.end() )
    {
        return;
    }
    else
    {
        startingIndex = (map_itr->first) / PAGESIZE;
        startingAddress = map_itr->second.start_addr;
        size = map_itr->second.size;
    }

    uint32_t numTcesNeeded = ALIGN_PAGE(size)/PAGESIZE;
    uint64_t previousAddress = 0;

    tablePtr = reinterpret_cast<TceEntry_t*>(iv_tceTableVaAddr);

    for(uint32_t tceIndex = startingIndex;
        tceIndex < (startingIndex + numTcesNeeded);
        tceIndex++)
        previousAddress = tablePtr[tceIndex].realPageNumber;
        tablePtr[tceIndex].WholeTceEntry = 0;
    }
    iv_allocatedAddrs.erase(i_startingToken);
}

errlHndl_t utilDeallocateTces(const uint32_t i_startingToken)
{
    return Singleton<UtilTceMgr>::instance().deallocateTces(i_startingToken);
};

uint32_t UtilTceMgr::getToken(const tokenLabels i_tokenLabel)
{
    return (i_tokenLabel==UtilTceMgr::PAYLOAD_TOKEN)
            ? iv_payloadToken
            : iv_hdatToken;

}

errlHndl_t MemRegionMgr::closeUnsecureMemRegion(
    const uint64_t    i_start_addr,
    TARGETING::Target* i_target)
{

    bool region_found = false;
    regionData l_region;

    auto l_memRegions = getTargetRegionList(i_target);

    auto itr = l_memRegions->begin();
    while (itr != l_memRegions->end())
    {
        if (itr->start_addr == i_start_addr)
        {
            region_found = true;

            l_region.start_addr = itr->start_addr;
            l_region.size = itr->size;
            l_region.flags = SbePsu::SBE_MEM_REGION_CLOSE;
            l_region.tgt = itr->tgt;

            doUnsecureMemRegionOp(l_region);
            itr = l_memRegions->erase(itr);
            break;
        }
        ++itr;
    }
}

inline uint64_t getHRMOR()
{
    register uint64_t hrmor = 0;
    asm volatile("mfspr %0, 313" : "=r" (hrmor));
    return hrmor;
}

void CpuSprValue(void *t)
{
    uint64_t spr = t;
    uint64_t l_smf_bit = 0x0;

    switch (spr)
    {
        case CPU_SPR_MSR:
            l_smf_bit = getMSR() & MSR_SMF_MASK;
            TASK_SETRTN(t, WAKEUP_MSR_VALUE | l_smf_bit);
            break;

        case CPU_SPR_LPCR:
            TASK_SETRTN(t, WAKEUP_LPCR_VALUE);
            break;

        case CPU_SPR_HRMOR:
            TASK_SETRTN(t, getHRMOR());
            break;

        case CPU_SPR_HID:
            TASK_SETRTN(t, getHID());
            break;

        default:
            TASK_SETRTN(t, -1);
            break;
    }
};

uint64_t cpu_spr_value(CpuSprNames spr)
{
    return (uint64_t)(CpuSprValue((void*)(spr)));
}

void getClassResources( TARGETING::TargetHandleList & o_vector,
                     CLASS i_class, TYPE  i_type, ResourceState i_state )
{
    switch(i_state)
    {
        case UTIL_FILTER_ALL:
            TARGETING::PredicateCTM l_CtmFilter(i_class, i_type);
            TARGETING::TargetRangeFilter l_targetList(
                TARGETING::targetService().begin(),
                TARGETING::targetService().end(),
                &l_CtmFilter);
            o_vector.clear();
            for (; l_targetList; ++l_targetList)
            {
                o_vector.push_back(*l_targetList);
            }
            break;
        case UTIL_FILTER_PRESENT:
            PredicateHwas l_predPres;
            l_predPres.present(true);
            TARGETING::PredicateCTM l_CtmFilter(i_class, i_type);
            TARGETING::PredicatePostfixExpr l_present;
            l_present.push(&l_CtmFilter).push(&l_predPres).And();
            TARGETING::TargetRangeFilter l_presTargetList(
                TARGETING::targetService().begin(),
                TARGETING::targetService().end(),
                &l_present);
            o_vector.clear();
            for (; l_presTargetList; ++l_presTargetList)
            {
                o_vector.push_back(*l_presTargetList);
            }
            break;
        case UTIL_FILTER_FUNCTIONAL:
            TARGETING::PredicateIsFunctional l_isFunctional;
            TARGETING::PredicateCTM l_CtmFilter(i_class, i_type);
            TARGETING::PredicatePostfixExpr l_functional;
            l_functional.push(&l_CtmFilter).push(&l_isFunctional).And();
            TARGETING::TargetRangeFilter l_funcTargetList(
                TARGETING::targetService().begin(),
                TARGETING::targetService().end(),
                &l_functional);
            o_vector.clear();
            for (; l_funcTargetList; ++l_funcTargetList)
            {
                o_vector.push_back(*l_funcTargetList);
            }
            break;
        case UTIL_FILTER_NON_FUNCTIONAL:
            TARGETING::PredicateIsNonFunctional l_isNonFunctional(false);
            TARGETING::PredicateCTM l_CtmFilter(i_class, i_type);
            TARGETING::PredicatePostfixExpr l_nonFunctional;
            l_nonFunctional.push(&l_CtmFilter).push(&l_isNonFunctional).And();
            TARGETING::TargetRangeFilter l_nonFuncTargetList(
                TARGETING::targetService().begin(),
                TARGETING::targetService().end(),
                &l_nonFunctional);
            o_vector.clear();
            for (; l_nonFuncTargetList; ++l_nonFuncTargetList)
            {
                o_vector.push_back(*l_nonFuncTargetList);
            }
            break;
        case UTIL_FILTER_PRESENT_NON_FUNCTIONAL:
            TARGETING::PredicateIsNonFunctional l_isPresNonFunctional;
            TARGETING::PredicateCTM l_CtmFilter(i_class, i_type);
            TARGETING::PredicatePostfixExpr l_presNonFunctional;
            l_presNonFunctional.push(&l_CtmFilter).push(&l_isPresNonFunctional).And();
            TARGETING::TargetRangeFilter l_presNonFuncTargetList(
                TARGETING::targetService().begin(),
                TARGETING::targetService().end(),
                &l_presNonFunctional);
            o_vector.clear();
            for (; l_presNonFuncTargetList; ++l_presNonFuncTargetList)
            {
                o_vector.push_back(*l_presNonFuncTargetList);
            }
            break;
    }
    if (o_vector.size() > 1)
    {
        std::sort(o_vector.begin(),o_vector.end(),compareTargetHuid);
    }
}

void getEncResources( TARGETING::TargetHandleList & o_vector,
                      TYPE i_type, ResourceState i_state )
{
    getClassResources(o_vector, CLASS_ENC, i_type, i_state);
}

Target* getCurrentNodeTarget(void)
{
    TargetHandleList l_nodelist;
    getEncResources(l_nodelist, TARGETING::TYPE_NODE, TARGETING::UTIL_FILTER_FUNCTIONAL);
    Target* pTgt = l_nodelist[0];
    return pTgt;
}

uint8_t getCurrentNodePhysId(void)
{
    Target* pNodeTgt = getCurrentNodeTarget();
    EntityPath epath = pNodeTgt->getAttr<TARGETING::ATTR_PHYS_PATH>();
    const TARGETING::EntityPath::PathElement pe = epath.pathElementOfType(TARGETING::TYPE_NODE);
    return pe.instance;
}

errlHndl_t utilClosePayloadTces(void)
{
    errlHndl_t errl = nullptr;

    uint32_t token=0;
    uint8_t  nodeId = TARGETING::UTIL::getCurrentNodePhysId();

    token = Singleton<UtilTceMgr>::instance().getToken(UtilTceMgr::PAYLOAD_TOKEN);
    utilDeallocateTces(token);

    uint64_t hrmorVal = cpu_spr_value(CPU_SPR_HRMOR);
    uint64_t addr = hrmorVal - VMM_HRMOR_OFFSET + MCL_TMP_ADDR;

    SBEIO::closeUnsecureMemRegion(addr, nullptr); //Master Processor

    token = Singleton<UtilTceMgr>::instance().getToken(UtilTceMgr::HDAT_TOKEN);
    utilDeallocateTces(token);
}

errlHndl_t sendFreqAttrData()
{
    tid_t l_progTid = 0;
    TARGETING::Target * sys = nullptr;
    TARGETING::targetService().getTopLevelTarget( sys );
    TARGETING::Target* mproc = nullptr;
    TARGETING::targetService().masterProcChipTargetHandle(mproc);
    TARGETING::EntityPath epath = mproc->getAttr<TARGETING::ATTR_PHYS_PATH>();
    const TARGETING::EntityPath::PathElement pe = epath.pathElementOfType(TARGETING::TYPE_NODE);
    uint32_t nodeid = pe.instance;

    // ATTR_HB_EXISTING_IMAGE only gets set on a multi-drawer system.
    // Currently set up in host_sys_fab_iovalid_processing() which only
    // gets called if there are multiple physical nodes. It eventually
    // needs to be setup by a hb routine that snoops for multiple nodes.
    TARGETING::ATTR_HB_EXISTING_IMAGE_type hb_images = sys->getAttr<TARGETING::ATTR_HB_EXISTING_IMAGE>();

    if(0 == hb_images)
    {
        // Single node system
        return;
    }

    msg_q_t msgQ = msg_q_create();
    MBOX::msgq_register(MBOX::HB_FREQ_ATTR_DATA_MSGQ,msgQ);

    uint64_t msg_count = 0;
    freq_data freq_data_obj{0};

    freq_data_obj.nominalFreq = sys->getAttr<ATTR_NOMINAL_FREQ_MHZ>();
    freq_data_obj.floorFreq = sys->getAttr<ATTR_MIN_FREQ_MHZ>();
    freq_data_obj.ceilingFreq = sys->getAttr<ATTR_FREQ_CORE_CEILING_MHZ>();
    freq_data_obj.ultraTurboFreq = sys->getAttr<ATTR_ULTRA_TURBO_FREQ_MHZ>();
    freq_data_obj.turboFreq = sys->getAttr<ATTR_FREQ_CORE_MAX>();
    freq_data_obj.nestFreq = sys->getAttr<ATTR_FREQ_PB_MHZ>();
    freq_data_obj.powerModeNom = sys->getAttr<ATTR_SOCKET_POWER_NOMINAL>();
    freq_data_obj.powerModeTurbo = sys->getAttr<ATTR_SOCKET_POWER_TURBO>();

    TARGETING::ATTR_HB_EXISTING_IMAGE_type mask = 0x1 << ((sizeof(TARGETING::ATTR_HB_EXISTING_IMAGE_type) * 8) - 1);

    for (uint32_t l_node=0; l_node < MAX_NODES_PER_SYS; l_node++)
    {
        if(l_node == nodeid)
        {
            continue;
        }

        if(0 != ((mask >> l_node) & hb_images))
        {
            msg_t * msg = msg_allocate();
            msg->type = IPC::IPC_FREQ_ATTR_DATA;
            msg->data[0] = ((uint64_t)(l_node) << 32) | (uint64_t)(nodeid);
            msg->data[1] = freq_data_obj.freqData1;
            msg->extra_data = reinterpret_cast<uint64_t*>(freq_data_obj.freqData2);
            MBOX::send(MBOX::HB_IPC_MSGQ, msg, l_node);
            ++msg_count;
        }
    }
    l_progTid = task_create(sendFreqAttrData_timer,&msgQ);

    while(msg_count)
    {
        msg_t* response = msg_wait(msgQ);

        if(response->type == HB_FREQ_ATTR_DATA_TIMER_MSG
        && response->data[1] == CONTINUE_WAIT_FOR_MSGS)
        {
            response->data[1] = HB_FREQ_ATTR_DATA_WAITING_FOR_MSG;
            msg_respond(msgQ,response);
        }
        if (response->type == IPC::IPC_FREQ_ATTR_DATA)
        {
            --msg_count;
            msg_free(response);
        }
    }

    if (msg_count == 0)
    {
        msg_t* response = msg_wait(msgQ);
        if (response->type == HB_FREQ_ATTR_DATA_TIMER_MSG)
        {
            response->data[1] = HB_FREQ_ATTR_DATA_MSG_DONE;
            msg_respond(msgQ,response);
        }
    }

    int l_childsts = 0;
    void* l_childrc = NULL;
    tid_t l_tidretrc = task_wait_tid(l_progTid, &l_childsts, &l_childrc);
    MBOX::msgq_unregister(MBOX::HB_FREQ_ATTR_DATA_MSGQ);
    msg_q_destroy(msgQ);
    return;
}

void* call_host_runtime_setup (void *io_pArgs)
{
    TARGETING::Target * sys = nullptr;
    TARGETING::targetService().getTopLevelTarget (sys);
    sys->trySetAttr<ATTR_PM_RESET_FFDC_ENABLE> (0x01);

    sendFreqAttrData();
    RUNTIME::sendSBESystemConfig();

#ifdef CONFIG_PLDM
    if(false)
    {
#endif
        RUNTIME::verifyAndMovePayload();
#ifdef CONFIG_PLDM
    }
#endif
    RUNTIME::load_host_data();
#ifdef CONFIG_UCD_FLASH_UPDATES
    POWER_SEQUENCER::TI::UCD::call_update_ucd_flash();
#endif
    RUNTIME::populate_hbSecurebootData();
    RUNTIME::populate_hbTpmInfo();
    RUNTIME::persistent_rwAttrRuntimeCheck();
#ifdef CONFIG_NVDIMM
    NVDIMM_UPDATE::call_nvdimm_update();
#endif

        loadAndStartPMAll(HBPM::PM_LOAD);
#ifdef CONFIG_NVDIMM
        TARGETING::TargetHandleList l_nvdimmTargetList;
        TARGETING::TargetHandleList l_procList;
        TARGETING::getAllChips(l_procList, TARGETING::TYPE_PROC, true);

        for (auto l_proc : l_procList)
        {
            l_nvdimmTargetList = TARGETING::getProcNVDIMMs(l_proc);
            if (l_nvdimmTargetList.size() != 0)
            {
                NVDIMM::nvdimmArm(l_nvdimmTargetList);
            }
        }
#endif
    HTMGT::processOccStartStatus(/*i_startCompleted=*/true);

    uint32_t threadRegSize = sizeof(DUMP::hostArchRegDataHdr)
                           + 95 * sizeof(DUMP::hostArchRegDataEntry);
    uint8_t threadRegFormat = REG_DUMP_SBE_HB_STRUCT_VER;
    uint64_t capThreadArrayAddr = 0;
    uint64_t capThreadArraySize = 0;

    RUNTIME::updateHostProcDumpActual(
        RUNTIME::PROC_DUMP_AREA_TBL,
        threadRegSize,
        threadRegFormat,
        capThreadArrayAddr,
        capThreadArraySize);
    RUNTIME::writeActualCount(RUNTIME::MS_DUMP_RESULTS_TBL);
    RUNTIME::populate_hbRuntimeData();
}

// Move the OCCs to active state or log unrecoverable error and
// stay in safe mode
void HTMGT::processOccStartStatus(const bool i_startCompleted = true)
{
	errlHndl_t l_err = nullptr;

	// Calc memory throttles (once per IPL)
	l_err = calcMemThrottles();
	if (l_err) return;

	// Make sure OCCs are ready for communication
	l_err = OccManager::waitForOccCheckpoint();
	if (l_err) return;

	// Send initial poll to all OCCs to establish communication
	l_err = OccManager::sendOccPoll();
	if (l_err) return;

	// Send ALL config data
	sendOccConfigData();

	// Set the User PCAP
	l_err = sendOccUserPowerCap();
	if (l_err) return;

	// Wait for all OCCs to go to the target state
	l_err = waitForOccState();
	if (l_err) return;

	// Set active sensors for all OCCs, so BMC can start communication with OCCs
	l_err = setOccActiveSensors(true);
	if (l_err) return;
}

/**
 * Calculates the memory throttling numerator values for the OT,
 * oversubscription, and redundant power cases.  The results are
 * stored in attributes under the corresponding MBAs.
 */
errlHndl_t calcMemThrottles()
{
    Target* sys = NULL;

    targetService().getTopLevelTarget(sys);
    assert(sys != NULL);

    uint8_t min_utilization =
	sys->getAttr<ATTR_OPEN_POWER_MIN_MEM_UTILIZATION_THROTTLING>();
    if (min_utilization == 0)
    {
	// Use SAFEMODE utilization
	min_utilization = sys->getAttr
	    <ATTR_MSS_MRW_SAFEMODE_MEM_THROTTLED_N_COMMANDS_PER_PORT>();
	if (min_utilization == 0)
	{
	    // Use hardcoded utilization
	    min_utilization = 10;
	}
    }
    const uint8_t efficiency =
	sys->getAttr<ATTR_OPEN_POWER_REGULATOR_EFFICIENCY_FACTOR>();

    //Get all functional MCSs
    TargetHandleList mcs_list;
    getAllChiplets(mcs_list, TYPE_MCS, true);

    // Create a FAPI Target list for HWP
    std::vector < fapi2::Target< fapi2::TARGET_TYPE_MCS>> l_fapi_target_list;
    for(const auto & mcs_target : mcs_list)
    {
	uint32_t mcs_huid = 0xFFFFFFFF;
	uint8_t mcs_unit = 0xFF;
	mcs_target->tryGetAttr<TARGETING::ATTR_HUID>(mcs_huid);
	mcs_target->tryGetAttr<TARGETING::ATTR_CHIP_UNIT>(mcs_unit);

	// Query the functional MCAs for this MCS
	TARGETING::TargetHandleList mca_list;
	getChildAffinityTargetsByState(mca_list, mcs_target, CLASS_UNIT,
				       TYPE_MCA, UTIL_FILTER_FUNCTIONAL);
	uint8_t occ_instance = 0xFF;
	ConstTargetHandle_t proc_target = getParentChip(mcs_target);
	assert(proc_target != nullptr);
	occ_instance = proc_target->getAttr<TARGETING::ATTR_POSITION>();

	// Convert to FAPI target and add to list
	fapi2::Target<fapi2::TARGET_TYPE_MCS> l_fapiTarget(mcs_target);
	l_fapi_target_list.push_back(l_fapiTarget);
    }

    errlHndl_t err = NULL;
    do
    {
	//Calculate Throttle settings for Over Temperature
	err = memPowerThrottleOT(l_fapi_target_list,
				 min_utilization,
				 efficiency);
	if (NULL != err) break;

	//Calculate Throttle settings for Nominal/Turbo
	err = memPowerThrottleRedPower(l_fapi_target_list,
				       min_utilization,
				       efficiency);
	if (NULL != err) break;

	//Calculate Throttle settings for Power Capping
	uint8_t pcap_min_utilization;
	if (!sys->tryGetAttr<ATTR_OPEN_POWER_MIN_MEM_UTILIZATION_POWER_CAP>
	    (pcap_min_utilization))
	{
	    pcap_min_utilization = 0;
	}
	err = memPowerThrottlePowercap(l_fapi_target_list,
				       pcap_min_utilization,
				       efficiency);

    } while(0);

    calculate_system_power();

    if (err)
    {
	err->collectTrace(HTMGT_COMP_NAME);
    }

    return err;
}

OCC_RC_INIT_FAILURE             = 0xE5;
OCC_RC_OCC_INIT_CHECKPOINT      = 0xE1;
OCC_COMMAND_IN_PROGRESS         = 0xFF

const uint32_t OCC_RSP_SRAM_ADDR   = 0xFFFBF000;

const uint16_t OCC_COMM_INIT_COMPLETE = 0x0EFF;
const uint16_t OCC_INIT_FAILURE = 0xE000;

// The following header lengths include the 2 byte checksum
const uint16_t OCC_CMD_HDR_LENGTH = 6;
const uint16_t OCC_RSP_HDR_LENGTH = 7;

// Wait for all OCCs to reach communications checkpoint
void OccManager::waitForOccCheckpoint()
{
	// Wait up to 15 seconds for all OCCs to be ready (150 * 100ms = 15s)
	const size_t NS_BETWEEN_READ = 100 * NS_PER_MSEC;
	const size_t READ_RETRY_LIMIT = 150;

	uint8_t retryCount = 0;

	for( const auto & occ : iv_occArray )
	{
		bool occReady = false;

		while (!occReady && retryCount++ < READ_RETRY_LIMIT)
		{
			nanosleep(0, NS_BETWEEN_READ);

			TARGETING::ConstTargetHandle_t procTarget =
				TARGETING::getParentChip(occ->getTarget());

			// Read SRAM response buffer to check for OCC checkpoint
			const uint16_t l_length = 8;  //Note: number of bytes
			uint8_t l_sram_data[l_length] = { 0x0 };
			errlHndl_t l_err = HBOCC::readSRAM(procTarget,
						OCC_RSP_SRAM_ADDR,
						(uint64_t*)(&(l_sram_data)),
						l_length);
			if (l_err != nullptr)
			{
				return nullptr;
			}

			// Pull status from response (byte 2)
			uint8_t status = l_sram_data[2];

			// Pull checkpoint from response (byte 6-7)
			uint16_t checkpoint = l_sram_data[6]<<8 | l_sram_data[7];

			if (OCC_RC_OCC_INIT_CHECKPOINT == status &&
			    OCC_COMM_INIT_COMPLETE == checkpoint)
				occReady = true;
				break;
			}
			if (((checkpoint & OCC_INIT_FAILURE) == OCC_INIT_FAILURE) ||
				status == OCC_RC_INIT_FAILURE)
			{
				occReady = false;
				break;
			}
		}

		if (!occReady)
			die();
	}
}

/**
 * @brief Send a poll command to one or all OCCs
 *
 * @param[in]  i_flushAllErrors:
 *                 If set to true, HTMGT will send poll cmds
 *                 to each OCC that is selected as long as that OCC
 *                 continues to report errors.  If false, only one
 *                 poll will be send to each OCC.
 * @param[in] i_occTarget: The Selected OCC or NULL for all OCCs
 *
 * @return NULL on success, else error handle
 */
void OccManager::sendOccPoll(const bool i_flushAllErrors = false,
				   TARGETING::Target * i_occTarget = NULL,
				   const bool onlyIfEstablished = false)
{
	errlHndl_t l_err = nullptr;

	for( const auto & l_occ : iv_occArray )
	{
		errlHndl_t poll_err = l_occ->pollForErrors(i_flushAllErrors);
		if (poll_err != nullptr)
			die();
	}

	if (occNeedsReset())
	{
		TMGT_ERR("sendOccPoll(): OCCs need to be reset");
	}
}

enum occCommandType
{
    OCC_CMD_POLL                    = 0x00,
    OCC_CMD_CLEAR_ERROR_LOG         = 0x12,
    OCC_CMD_SET_STATE               = 0x20,
    OCC_CMD_SETUP_CFG_DATA          = 0x21,
    OCC_CMD_SET_POWER_CAP           = 0x22,
    OCC_CMD_RESET_PREP              = 0x25,
    OCC_CMD_DEBUG_PASS_THROUGH      = 0x40,
    OCC_CMD_AME_PASS_THROUGH        = 0x41,
    OCC_CMD_GET_FIELD_DEBUG_DATA    = 0x42,
    OCC_CMD_MFG_TEST                = 0x53,
}

/**
 * @brief Poll for Errors
 *
 * @param[in]  i_flushAllErrors:
 *      If set to true, HTMGT will send poll cmds
 *      to the OCC as long as the OCC continues
 *      to report errors.  If false, only one
 *      poll will be sent.
 *
 * @return NULL on success, else error handle
 */
errlHndl_t Occ::pollForErrors(const bool i_flushAllErrors = false)
{
	errlHndl_t err = nullptr;
	bool continuePolling = false;
	size_t elogCount = 10;
	do
	{
		// create 1 byte buffer for poll command data
		const uint8_t l_cmdData[1] = { 0x20 /*version*/ };

		OccCmd cmd(this,
			   OCC_CMD_POLL,
			   sizeof(l_cmdData),
			   l_cmdData);

		err = cmd.sendOccCmd();
		if (err != nullptr)
		{
			break;
		}

		// Poll succeeded, check response
		uint8_t * poll_rsp = nullptr;
		uint32_t poll_rsp_size = cmd.getResponseData(poll_rsp);
		if (poll_rsp_size >= OCC_POLL_DATA_MIN_SIZE)
		{
			if (i_flushAllErrors)
			{
				const occPollRspStruct_t *currentPollRsp =
					(occPollRspStruct_t *) poll_rsp;
				if (currentPollRsp->errorId != 0)
				{
					if (--elogCount > 0)
					{
						// An error was returned, keep polling OCC
						continuePolling = true;
					}
					else
					{
						// Limit number of elogs retrieved so
						// we do not get stuck in loop
						TMGT_INF("pollForErrors: OCC%d still has "
							"more errors to report. "
							"(ID 0x%02X)",
							iv_instance,
							currentPollRsp->errorId);
						continuePolling = false;
					}
				}
				else
				{
					continuePolling = false;
				}
			}
			pollRspHandler(poll_rsp, poll_rsp_size);
		}
		else
		{
			die("Invalid data length");
		}
	}
	while (continuePolling);

	return err;
}

struct occPollRspStruct_t
{
    uint8_t   status;
    uint8_t   extStatus;
    uint8_t   occsPresent;
    uint8_t   requestedCfg;
    uint8_t   state;
    uint8_t   mode;
    uint8_t   IPSStatus;
    uint8_t   errorId;
    uint32_t  errorAddress;
    uint16_t  errorLength;
    uint8_t   errorSource;
    uint8_t   gpuCfg;
    uint8_t   codeLevel[16];
    uint8_t   sensor[6];
    uint8_t   numBlocks;
    uint8_t   version;
    uint8_t   sensorData[4049];
}  __attribute__((packed));

// Handle OCC poll response
void Occ::pollRspHandler(const uint8_t * i_pollResponse,
			 const uint16_t i_pollResponseSize)
{
    static uint32_t L_elog_retry_count = 0;

    const occPollRspStruct_t *pollRsp =
	(occPollRspStruct_t *) i_pollResponse;
    const occPollRspStruct_t *lastPollRsp =
	(occPollRspStruct_t *) iv_lastPollResponse;

    do
    {
	if (!iv_commEstablished)
	{
	    // 1st poll response, so comm has been established for this OCC
	    iv_commEstablished = true;
	}

	// Check for Error Logs
	if (pollRsp->errorId != 0)
	{
	    if ((pollRsp->errorId != lastPollRsp->errorId) ||
		(pollRsp->errorSource != lastPollRsp->errorSource) ||
		(L_elog_retry_count < 3))

	    {
		if ((pollRsp->errorId == lastPollRsp->errorId) &&
		    (pollRsp->errorSource == lastPollRsp->errorSource))
		{
		    // Only retry same errorId a few times...
		    L_elog_retry_count++;
		}
		else
		{
		    L_elog_retry_count = 0;
		}

		// Handle a new error log from the OCC
		occProcessElog(pollRsp->errorId,
			       pollRsp->errorAddress,
			       pollRsp->errorLength,
			       pollRsp->errorSource);
		if (iv_needsReset)
		{
		    // Update state if changed...
		    // (since dropping out of poll rsp handler)
		    if (iv_state != pollRsp->state)
		    {
			iv_state = (occStateId)pollRsp->state;
		    }
		    break;
		}
	    }
	}

	if ((OCC_STATE_ACTIVE == pollRsp->state) ||
	    (OCC_STATE_OBSERVATION == pollRsp->state) ||
	    (OCC_STATE_CHARACTERIZATION == pollRsp->state))
	{
	    errlHndl_t l_err = nullptr;

	    // Check role status
	    if (((OCC_ROLE_SLAVE == iv_role) &&
		 ((pollRsp->status & OCC_STATUS_MASTER) != 0)) ||
		((OCC_ROLE_MASTER == iv_role) &&
		 ((pollRsp->status & OCC_STATUS_MASTER) == 0)))
	    {
		iv_needsReset = true;
		break;
	    }

	    if (pollRsp->occsPresent != iv_occsPresent)
	    {
		iv_needsReset = true;
		if (iv_resetReason == OCC_RESET_REASON_NONE)
		{
		    iv_resetReason = OCC_RESET_REASON_ERROR;
		}
	    }
	}

	// Check for state change
	if (iv_state != pollRsp->state)
	{
	    iv_state = (occStateId)pollRsp->state;
	}

	// Check GPU config
	if (iv_gpuCfg != pollRsp->gpuCfg)
	{
	    iv_gpuCfg = pollRsp->gpuCfg;
	}

	// Copy rspData to lastPollResponse
	memcpy(iv_lastPollResponse, pollRsp, OCC_POLL_DATA_MIN_SIZE);
	iv_lastPollValid = true;
    }
    while(0);
}

const uint32_t OCC_MAX_DATA_LENGTH = 0x00001000;

enum cfgTargets
{
    TARGET_ALL    = 0x00,
    TARGET_MASTER = 0x01,
};

enum cfgSupStates
{
    CFGSTATE_ALL     = 0x00,
    CFGSTATE_STANDBY = 0x01,
    CFGSTATE_SBYOBS  = 0x02
};

const uint32_t TO_20SEC = 20;

struct occCfgDataTable_t
{
    occCfgDataFormat    format;
    cfgTargets          targets;
    uint16_t            timeout; // in seconds
    cfgSupStates        supportedStates;

    bool operator==(const occCfgDataFormat& i_format) const
    {
	return (i_format == format);
    }
};

const occCfgDataTable_t occCfgDataTable[] =
{
    { OCC_CFGDATA_SYS_CONFIG,     TARGET_ALL,    TO_20SEC, CFGSTATE_ALL },
    { OCC_CFGDATA_APSS_CONFIG,    TARGET_ALL,    TO_20SEC, CFGSTATE_ALL },
    { OCC_CFGDATA_OCC_ROLE,       TARGET_ALL,    TO_20SEC, CFGSTATE_STANDBY },
    { OCC_CFGDATA_FREQ_POINT,     TARGET_MASTER, TO_20SEC, CFGSTATE_SBYOBS },
    { OCC_CFGDATA_MEM_CONFIG,     TARGET_ALL,    TO_20SEC, CFGSTATE_ALL },
    { OCC_CFGDATA_PCAP_CONFIG,    TARGET_MASTER, TO_20SEC, CFGSTATE_ALL },
    { OCC_CFGDATA_MEM_THROTTLE,   TARGET_ALL,    TO_20SEC, CFGSTATE_ALL },
    { OCC_CFGDATA_TCT_CONFIG,     TARGET_ALL,    TO_20SEC, CFGSTATE_ALL },
    { OCC_CFGDATA_AVSBUS_CONFIG,  TARGET_ALL,    TO_20SEC, CFGSTATE_ALL },
    // GPU config packet MUST be sent after APSS config
    { OCC_CFGDATA_GPU_CONFIG,     TARGET_ALL,    TO_20SEC, CFGSTATE_ALL },
};
const size_t OCC_CONFIG_TABLE_SIZE = sizeof(occCfgDataTable) / sizeof(occCfgDataTable_t);

// Send config format data to all OCCs
void sendOccConfigData(const occCfgDataFormat i_requestedFormat = OCC_CFGDATA_CLEAR_ALL)
{
    uint8_t cmdData[OCC_MAX_DATA_LENGTH] = {0};

    const occCfgDataTable_t* start = &occCfgDataTable[0];
    const occCfgDataTable_t* end = &occCfgDataTable[OCC_CONFIG_TABLE_SIZE];

    // Loop through all functional OCCs
    for (Occ * occ : OccManager::getOccArray())
    {
        const uint8_t occInstance = occ->getInstance();
        const occRole role = occ->getRole();

        // Loop through all config data types
        for (const occCfgDataTable_t *itr = start; itr < end; ++itr)
        {
		const occCfgDataFormat format = itr->format;
		bool sendData = true;

		// Make sure format is supported by this OCC
		if (TARGET_MASTER == itr->targets)
		{
			if (OCC_ROLE_MASTER != role)
			{
				sendData = false;
			}
		}

		// Make sure data is supported in the current state
		const occStateId state = occ->getState();
		// `state ` should be OCC_STATE_STANDBY here.
		if (CFGSTATE_STANDBY == itr->supportedStates)
		{
			if (OCC_STATE_STANDBY != state)
			{
				sendData = false;
			}
		}
		else if (CFGSTATE_SBYOBS == itr->supportedStates)
		{
			if ((OCC_STATE_STANDBY != state) &&
			    (OCC_STATE_OBSERVATION != state))
			{
				sendData = false;
			}
		}

		if (sendData)
		{
			uint64_t cmdDataLen = 0;
			switch(format)
			{
				case OCC_CFGDATA_FREQ_POINT:
					getFrequencyPointMessageData(cmdData,
								     cmdDataLen);
					break;

				case OCC_CFGDATA_OCC_ROLE:
					getOCCRoleMessageData(OCC_ROLE_MASTER ==
							      occ->getRole(),
							      OCC_ROLE_FIR_MASTER ==
							      occ->getRole(),
							      cmdData, cmdDataLen);
					break;

				case OCC_CFGDATA_APSS_CONFIG:
					getApssMessageData(cmdData, cmdDataLen);
					break;

				case OCC_CFGDATA_MEM_CONFIG:
					getMemConfigMessageData(occ->getTarget(),
								cmdData, cmdDataLen);
					break;

				case OCC_CFGDATA_PCAP_CONFIG:
					getPowerCapMessageData(cmdData, cmdDataLen);
					break;

				case OCC_CFGDATA_SYS_CONFIG:
					getSystemConfigMessageData(occ->getTarget(),
								   cmdData, cmdDataLen);
					break;

				case OCC_CFGDATA_MEM_THROTTLE:
					if (!int_flags_set(FLAG_DISABLE_MEM_CONFIG))
					{
						getMemThrottleMessageData(occ->getTarget(),
									  occInstance, cmdData, cmdDataLen);
					}
					break;

				case OCC_CFGDATA_TCT_CONFIG:
					getThermalControlMessageData(cmdData,
								     cmdDataLen);
					break;

				case OCC_CFGDATA_AVSBUS_CONFIG:
					getAVSBusConfigMessageData( occ->getTarget(),
								    cmdData,
								    cmdDataLen );
					break;

				case OCC_CFGDATA_GPU_CONFIG:
					getGPUConfigMessageData(occ->getTarget(),
								cmdData,
								cmdDataLen);
					break;
			}

			if (cmdDataLen > 0)
			{
				OccCmd cmd(occ, OCC_CMD_SETUP_CFG_DATA, cmdDataLen, cmdData);
				errlHndl_t l_err = cmd.sendOccCmd();
				if (l_err == nullptr && OCC_RC_SUCCESS != cmd.getRspStatus())
				{
					die();
				}

				// Send poll between config packets to flush errors
				l_err = OccManager::sendOccPoll();
				if (l_err)
				{
					ERRORLOG::errlCommit(l_err, HTMGT_COMP_ID);
				}
			}
		} // if (sendData)

		if (OccManager::occNeedsReset())
		{
			TMGT_ERR("sendOccConfigData(): OCCs need to be reset");
		}
        } // for each config format
    } // for each OCC
}

void getFrequencyPointMessageData(uint8_t* o_data,
				  uint64_t & o_size)
{
    uint64_t index   = 0;
    uint16_t min     = 0;
    uint16_t turbo   = 0;
    uint16_t ultra   = 0;
    uint16_t nominal = 0;
    Target* sys = nullptr;

    targetService().getTopLevelTarget(sys);
    assert(sys != nullptr);
    assert(o_data != nullptr);


    o_data[index++] = OCC_CFGDATA_FREQ_POINT;
    o_data[index++] = OCC_CFGDATA_FREQ_POINT_VERSION;

    check_wof_support(nominal, turbo, ultra);
    if (turbo == 0)
    {

	// If turbo not supported, send nominal for turbo
	// and reason code for ultra-turbo (no WOF support)
	turbo = nominal;
	ultra = WOF_UNSUPPORTED_FREQ;
    }

    //Nominal Frequency in MHz
    memcpy(&o_data[index], &nominal, 2);
    index += 2;

    //Turbo Frequency in MHz
    memcpy(&o_data[index], &turbo, 2);
    index += 2;

    //Minimum Frequency in MHz
    min = sys->getAttr<ATTR_MIN_FREQ_MHZ>();
    Target* proc = nullptr;
    targetService().masterProcChipTargetHandle(proc);
    if (proc != nullptr)
    {
	// Check if min frequency needs to be biased
	int8_t bias = proc->getAttr<ATTR_FREQ_BIAS_POWERSAVE>();
	if (bias != 0)
	{
	    // Calculate biased Minimum frequency
	    // (bias values are signed integers in units of 0.5 percent steps)
	    min *= 1 + (bias/200.0);
	}
    }
    memcpy(&o_data[index], &min, 2);
    index += 2;

    //Ultra Turbo Frequency in MHz
    memcpy(&o_data[index], &ultra, 2);
    index += 2;

    // Reserved (Static Power Save in PowerVM)
    memset(&o_data[index], 0, 2);
    index += 2;

    // Reserved (FFO in PowerVM)
    memset(&o_data[index], 0, 2);
    index += 2;

    o_size = index;
}

void getOCCRoleMessageData(bool i_master, bool i_firMaster,
			   uint8_t* o_data, uint64_t & o_size)
{
    assert(o_data != nullptr);

    o_data[0] = OCC_CFGDATA_OCC_ROLE;

    o_data[1] = OCC_ROLE_SLAVE;

    if (i_master)
    {
	o_data[1] = OCC_ROLE_MASTER;
    }

    if (i_firMaster)
    {
	o_data[1] |= OCC_ROLE_FIR_MASTER;
    }

    o_size = 2;
}

void getApssMessageData(uint8_t* o_data, uint64_t & o_size)
{
    Target* sys = nullptr;
    targetService().getTopLevelTarget(sys);

    ATTR_ADC_CHANNEL_FUNC_IDS_type function;
    sys->tryGetAttr<ATTR_ADC_CHANNEL_FUNC_IDS>(function);

    ATTR_ADC_CHANNEL_GNDS_type ground;
    sys->tryGetAttr<ATTR_ADC_CHANNEL_GNDS>(ground);

    ATTR_ADC_CHANNEL_GAINS_type gain;
    sys->tryGetAttr<ATTR_ADC_CHANNEL_GAINS>(gain);

    ATTR_ADC_CHANNEL_OFFSETS_type offset;
    sys->tryGetAttr<ATTR_ADC_CHANNEL_OFFSETS>(offset);

    CPPASSERT(sizeof(function) == sizeof(ground));
    CPPASSERT(sizeof(function) == sizeof(gain));
    CPPASSERT(sizeof(function) == sizeof(offset));

    //The APSS function below hardcodes 16 channels,
    //so everything better agree.
    CPPASSERT(sizeof(function) == 16);

    o_data[0] = OCC_CFGDATA_APSS_CONFIG;
    o_data[1] = OCC_CFGDATA_APSS_VERSION;
    o_data[2] = 0;
    o_data[3] = 0;
    uint64_t idx = 4;

    for(uint64_t channel = 0; channel < sizeof(function); ++channel)
    {
	o_data[idx] = function[channel]; // ADC Channel assignement
	idx += sizeof(uint8_t);

	uint32_t sensorId = 0;
	memcpy(o_data+idx,&sensorId,sizeof(uint32_t)); // Sensor ID
	idx += sizeof(uint32_t);

	o_data[idx] = ground[channel];   // Ground Select
	idx += sizeof(uint8_t);

	INT32_PUT(o_data+idx, gain[channel]);
	idx += sizeof(int32_t);

	INT32_PUT(o_data+idx, offset[channel]);
	idx += sizeof(int32_t);
    }

    ATTR_APSS_GPIO_PORT_MODES_type gpioMode;
    sys->tryGetAttr<ATTR_APSS_GPIO_PORT_MODES>(gpioMode);

    ATTR_APSS_GPIO_PORT_PINS_type gpioPin;
    sys->tryGetAttr<ATTR_APSS_GPIO_PORT_PINS>(gpioPin);

    uint64_t pinsPerPort = sizeof(ATTR_APSS_GPIO_PORT_PINS_type) /
	sizeof(ATTR_APSS_GPIO_PORT_MODES_type);
    uint64_t pinIdx = 0;

    for(uint64_t port = 0; port < sizeof(gpioMode); ++port)
    {
	o_data[idx] = gpioMode[port];
	idx += sizeof(uint8_t);
	o_data[idx] = 0;
	idx += sizeof(uint8_t);
	memcpy(o_data + idx, gpioPin+pinIdx, pinsPerPort);
	idx += pinsPerPort;
	pinIdx += pinsPerPort;
    }

    o_size = idx;
}

OCC_CFGDATA_MEM_CONFIG     = 0x05,  // Memory Configuration

void getMemConfigMessageData(const TargetHandle_t i_occ,
			     uint8_t* o_data, uint64_t & o_size)
{
    uint64_t index = 0;

    o_data[index++] = OCC_CFGDATA_MEM_CONFIG;
    o_data[index++] = 0x21; // version

    // if OPAL then no "Power Control Default" support.
    //Byte 3:   Memory Power Control Default.
    o_data[index++] = 0xFF;
    //Byte 4:   Idle Power Memory Power Control.
    o_data[index++] = 0xFF;

    //Byte 5:   Number of data sets.
    size_t numSetsOffset = index++; //Will fill in numSets at the end

    if (!int_flags_set(FLAG_DISABLE_MEM_CONFIG))
    {
	TargetHandleList dimms;
	uint8_t numSets = 0;

	ConstTargetHandle_t proc = getParentChip(i_occ);
	assert(proc != nullptr);

	// DIMMs are wired directly to the proc in Nimbus
	dimms.clear();
	getChildAffinityTargets( dimms,
				 proc,
				 CLASS_LOGICAL_CARD,
				 TYPE_DIMM );

	for( const auto & dimm : dimms )
	{
	    numSets++;

	    // Get PIB I2C Master engine for this dimm
	    ATTR_TEMP_SENSOR_I2C_CONFIG_type tempI2cCfgData =
	        dimm->getAttr<ATTR_TEMP_SENSOR_I2C_CONFIG>();

	    // Fill in the DIMM entry
	    writeMemConfigData( o_data,
	    		dimm,
	    		SENSOR_NAME_DIMM_STATE,//Bytes 0-3:HW sensor ID
	    		SENSOR_NAME_DIMM_TEMP, //Bytes 4-7:TMP sensor ID
	    		0xFF,                  //Bytes 8:MEM Nimbus,
	    		tempI2cCfgData.engine, //Byte 9: DIMM Info byte1
	    		tempI2cCfgData.port,   //Byte 10:DIMM Info byte2
	    		tempI2cCfgData.devAddr,//Byte 11:DIMM Info byte3
	    		index );
	}

	o_data[numSetsOffset] = numSets;
    }
    else
    {
	//A zero in byte 5 (numSets) means monitoring is disabled
	o_data[numSetsOffset] = 0;
    }

    o_size = index;
}

void getPowerCapMessageData(uint8_t* o_data, uint64_t & o_size)
{
    uint64_t index = 0;

    Target* sys = nullptr;
    targetService().getTopLevelTarget(sys);

    o_data[index++] = OCC_CFGDATA_PCAP_CONFIG;
    o_data[index++] = OCC_CFGDATA_PCAP_CONFIG_VERSION;

    // Minimum HARD Power Cap
    ATTR_OPEN_POWER_MIN_POWER_CAP_WATTS_type min_pcap =
	sys->getAttr<ATTR_OPEN_POWER_MIN_POWER_CAP_WATTS>();

    // Minimum SOFT Power Cap
    ATTR_OPEN_POWER_SOFT_MIN_PCAP_WATTS_type soft_pcap;
    if ( ! sys->tryGetAttr
	    <ATTR_OPEN_POWER_SOFT_MIN_PCAP_WATTS>(soft_pcap))
    {
	// attr does not exist (use min)
	soft_pcap = min_pcap;
    }
    UINT16_PUT(&o_data[index], soft_pcap);
    index += 2;

    // Minimum Hard Power Cap
    UINT16_PUT(&o_data[index], min_pcap);
    index += 2;

    // System Maximum Power Cap
    bool is_redundant;
    const uint16_t max_pcap = getMaxPowerCap(sys, is_redundant);
    UINT16_PUT(&o_data[index], max_pcap);
    index += 2;

    // Quick Power Drop Power Cap
    ATTR_OPEN_POWER_N_BULK_POWER_LIMIT_WATTS_type qpd_pcap;
    if ( ! sys->tryGetAttr
	 <ATTR_OPEN_POWER_N_BULK_POWER_LIMIT_WATTS>(qpd_pcap))
    {
	// attr does not exist, so disable by sending 0
	qpd_pcap = 0;
    }
    UINT16_PUT(&o_data[index], qpd_pcap);
    index += 2;

    o_size = index;
}

/**
 * Return the maximum power cap for the system.
 *
 * Value is read from the MRW based on the Current Power Supply
 * Redundancy Policy sensor in the BMC
 *
 * @param[in]  i_sys - pointer to system target
 * @param[out] o_is_redundant - true if power supplies should be redundant
 * @returns  maximum power cap in watts
 */
uint16_t getMaxPowerCap(Target *i_sys, bool & o_is_redundant)
{
    uint16_t o_maxPcap = 0;
    o_is_redundant = true;

    if (o_is_redundant)
    {
	// Read the default N+1 bulk power limit (redundant PS policy)
	o_maxPcap = i_sys->
	    getAttr<ATTR_OPEN_POWER_N_PLUS_ONE_BULK_POWER_LIMIT_WATTS>();
	TMGT_INF("getMaxPowerCap: maximum power cap = %dW "
		 "(redundant PS bulk power limit)", o_maxPcap);
    }

    return o_maxPcap;

} // end getMaxPowerCap()

OCC_CFGDATA_SYS_CONFIG     = 0x0F,  // System Configuration
OCC_CFGDATA_SYS_CONFIG_VERSION    = 0x21,

OCC_CFGDATA_OPENPOWER_OPALVM  = 0x81,
uint8_t G_system_type = OCC_CFGDATA_OPENPOWER_OPALVM;

void getSystemConfigMessageData(const TargetHandle_t i_occ, uint8_t* o_data,
				uint64_t & o_size)
{
    uint64_t index = 0;
    uint32_t SensorID1 = 0;
    uint32_t SensorID2 = 0;

    TargetHandle_t sys = nullptr;
    TargetHandleList nodes;
    targetService().getTopLevelTarget(sys);
    assert(sys != nullptr);
    getChildAffinityTargets(nodes, sys, CLASS_ENC, TYPE_NODE);
    assert(!nodes.empty());
    TargetHandle_t node = nodes[0];

    o_data[index++] = OCC_CFGDATA_SYS_CONFIG;
    o_data[index++] = OCC_CFGDATA_SYS_CONFIG_VERSION;

    //System Type
    uint8_t l_throttle_below_nominal = 0;
    //0=OCC report throttling when max frequency lowered below turbo
    G_system_type &= ~OCC_REPORT_THROTTLE_BELOW_NOMINAL;
    // Power supply policy is redundant
    G_system_type &= ~OCC_CFGDATA_NON_REDUNDANT_PS;
    o_data[index++] = G_system_type;

    //processor Callout Sensor ID
    ConstTargetHandle_t proc = getParentChip(i_occ);
    SensorID1 = UTIL::getSensorNumber(proc, SENSOR_NAME_PROC_STATE);
    memcpy(&o_data[index], &SensorID1, 4);
    index += 4;

    //Next 12*4 bytes are for core sensors.
    //If a new processor with more cores comes along,
    //this command will have to change.
    TargetHandleList cores;
    getChildChiplets(cores, proc, TYPE_CORE, false);

    for (uint64_t core=0; core<CFGDATA_CORES; core++)
    {
	SensorID1 = 0;
	SensorID2 = 0;

	if ( core < cores.size() )
	{
	    SensorID1 = UTIL::getSensorNumber(cores[core],     //Temp Sensor
					       SENSOR_NAME_CORE_TEMP);

	    SensorID2 = UTIL::getSensorNumber(cores[core],     //Freq Sensor
					       SENSOR_NAME_CORE_FREQ);
	}

	//Core Temp Sensor ID
	memcpy(&o_data[index], &SensorID1, 4);
	index += 4;

	//Core Frequency Sensor ID
	memcpy(&o_data[index], &SensorID2, 4);
	index += 4;
    }

    //Backplane Callout Sensor ID
    SensorID1 = UTIL::getSensorNumber(node, SENSOR_NAME_BACKPLANE_FAULT);
    memcpy(&o_data[index], &SensorID1, 4);
    index += 4;

    //APSS Callout Sensor ID
    SensorID1 = UTIL::getSensorNumber(node, SENSOR_NAME_APSS_FAULT);
    memcpy(&o_data[index], &SensorID1, 4);
    index += 4;

    //Format 21 - VRM VDD Callout Sensor ID
    SensorID1 = UTIL::getSensorNumber(node, SENSOR_NAME_VRM_VDD_FAULT);
    memcpy(&o_data[index], &SensorID1, 4);
    index += 4;

    //Format 21 - VRM VDD Temperature Sensor ID
    SensorID1 = UTIL::getSensorNumber(node, SENSOR_NAME_VRM_VDD_TEMP);
    memcpy(&o_data[index], &SensorID1, 4);
    index += 4;

    o_size = index;
}

void getMemThrottleMessageData(const TargetHandle_t i_occ,
			       const uint8_t i_occ_instance,
			       uint8_t* o_data, uint64_t & o_size)
{
    uint8_t numSets = 0;
    uint64_t index = 0;

    ConstTargetHandle_t proc = getParentChip(i_occ);
    assert(proc != nullptr);
    assert(o_data != nullptr);

    //Get all functional MCSs
    TargetHandleList mcs_list;
    getAllChiplets(mcs_list, TYPE_MCS, true);

    o_data[index++] = OCC_CFGDATA_MEM_THROTTLE;
    o_data[index++] = 0x20; // version;

    //Byte 3:   Number of memory throttling data sets.
    size_t numSetsOffset = index++; //Will fill in numSets at the end

    //Next, the following format repeats per set/MBA:
    //Byte 0:       Cumulus: Centaur position 0-7
    //              Nimbus : Memory Controller
    //Byte 1:       Cumulus: MBA Position 0-1
    //              Nimbus : Memory Controller's physical Port # 0-3
    //Bytes 2-3:    min N_PER_MBA
    //Bytes 4-5:    Max mem power with throttle @Min
    //Bytes 6-7:    Turbo N_PER_MBA
    //Bytes 8-9:    Turbo N_PER_CHIP
    //Bytes 10-11:  Max mem power with throttle @Turbo
    //Bytes 12-13:  Power Capping N_PER_MBA
    //Bytes 14-15:  Power Capping N_PER_CHIP
    //Bytes 16-17:  Max mem power with throttle @PowerCapping
    //Bytes 18-19:  Nominal Power N_PER_MBA
    //Bytes 20-21:  Nominal Power N_PER_CHIP
    //Bytes 22-23:  Max mem power with throttle @Nominal
    //Bytes 24-29:  Reserved

    for(const auto & mcs_target : mcs_list)
    {
	uint8_t mcs_unit = 0xFF;
	if (!mcs_target->tryGetAttr<TARGETING::ATTR_CHIP_UNIT>(mcs_unit))
	{
	    continue;
	}
	ConstTargetHandle_t proc_target = getParentChip(mcs_target);
	assert(proc_target != nullptr);

	// Make sure this MCS is for the current OCC/Proc
	if (i_occ_instance == proc_target->getAttr<TARGETING::ATTR_POSITION>())
	{
	    // Read the throttle and power values for this MCS
	    ATTR_OT_MIN_N_PER_MBA_type npm_min;
	    ATTR_OT_MEM_POWER_type power_min;
	    mcs_target->tryGetAttr<ATTR_OT_MIN_N_PER_MBA>(npm_min);
	    mcs_target->tryGetAttr<ATTR_OT_MEM_POWER>(power_min);
	    ATTR_N_PLUS_ONE_N_PER_MBA_type npm_redun;
	    ATTR_N_PLUS_ONE_N_PER_CHIP_type npc_redun;
	    ATTR_N_PLUS_ONE_MEM_POWER_type power_redun;
	    mcs_target->tryGetAttr<ATTR_N_PLUS_ONE_N_PER_MBA>(npm_redun);
	    mcs_target->tryGetAttr<ATTR_N_PLUS_ONE_N_PER_CHIP>(npc_redun);
	    mcs_target->tryGetAttr<ATTR_N_PLUS_ONE_MEM_POWER>(power_redun);
	    ATTR_POWERCAP_N_PER_MBA_type npm_pcap;
	    ATTR_POWERCAP_N_PER_CHIP_type npc_pcap;
	    ATTR_POWERCAP_MEM_POWER_type power_pcap;
	    mcs_target->tryGetAttr<ATTR_POWERCAP_N_PER_MBA>(npm_pcap);
	    mcs_target->tryGetAttr<ATTR_POWERCAP_N_PER_CHIP>(npc_pcap);
	    mcs_target->tryGetAttr<ATTR_POWERCAP_MEM_POWER>(power_pcap);

	    // Query the functional MCAs for this MCS
	    TARGETING::TargetHandleList mca_list;
	    getChildAffinityTargetsByState(mca_list, mcs_target, CLASS_UNIT,
					   TYPE_MCA, UTIL_FILTER_FUNCTIONAL);
	    for(const auto & mca_target : mca_list)
	    {
		// unit identifies unique MCA under a processor
		uint8_t mca_unit = 0xFF;
		mca_target->tryGetAttr<TARGETING::ATTR_CHIP_UNIT>(mca_unit);
		const uint8_t mca_rel_pos = mca_unit % 2;
		if ((npm_min[mca_rel_pos] == 0) ||
		    (npm_redun[mca_rel_pos] == 0) ||
		    (npm_pcap[mca_rel_pos] == 0))
		{
		    continue;
		}
		if (mca_rel_pos >= TMGT_MAX_MCA_PER_MCS)
		{
		    continue;
		}
		// OCC expects phyMC=0 for (MCS0-1) and 1 for (MCS2-3)
		//  MCS   MCA  MCA    OCC   OCC
		// unit  unit relPos phyMC phyPort
		//   0     0    0      0     0
		//   0     1    1      0     1
		//   1     2    0      0     2
		//   1     3    1      0     3
		//   2     4    0      1     0
		//   2     5    1      1     1
		//   3     6    0      1     2
		//   3     7    1      1     3
		o_data[index] = mcs_unit >> 1; // MC (0-1)
		o_data[index+1] = mca_unit % 4; // Phy Port (0-3)
		// Minimum
		UINT16_PUT(&o_data[index+ 2], npm_min[mca_rel_pos]);
		UINT16_PUT(&o_data[index+ 4], power_min[mca_rel_pos]);
		// Turbo
		UINT16_PUT(&o_data[index+ 6], npm_redun[mca_rel_pos]);
		UINT16_PUT(&o_data[index+ 8], npc_redun[mca_rel_pos]);
		UINT16_PUT(&o_data[index+10], power_redun[mca_rel_pos]);
		// Power Capping
		UINT16_PUT(&o_data[index+12], npm_pcap[mca_rel_pos]);
		UINT16_PUT(&o_data[index+14], npc_pcap[mca_rel_pos]);
		UINT16_PUT(&o_data[index+16], power_pcap[mca_rel_pos]);
		// Nominal (same as Turbo)
		UINT16_PUT(&o_data[index+18], npm_redun[mca_rel_pos]);
		UINT16_PUT(&o_data[index+20], npc_redun[mca_rel_pos]);
		UINT16_PUT(&o_data[index+22], power_redun[mca_rel_pos]);
		index += 30;
		++numSets ;
	    }
	}
    }

    o_data[numSetsOffset] = numSets;

    o_size = index;
}

OCC_CFGDATA_TCT_CONFIG     = 0x13,  // Thermal Control Treshold

CFGDATA_FRU_TYPE_PROC       = 0x00,
CFGDATA_FRU_TYPE_MEMBUF     = 0x01,
CFGDATA_FRU_TYPE_DIMM       = 0x02,
CFGDATA_FRU_TYPE_VRM        = 0x03,
CFGDATA_FRU_TYPE_GPU_CORE   = 0x04,
CFGDATA_FRU_TYPE_GPU_MEMORY = 0x05,
CFGDATA_FRU_TYPE_VRM_VDD    = 0x06,

OCC_NOT_DEFINED = 0xFF,

void getThermalControlMessageData(uint8_t* o_data, uint64_t & o_size)
{
    uint64_t index = 0;
    uint8_t l_numSets = 0;
    Target* l_sys = nullptr;
    targetService().getTopLevelTarget(l_sys);

    assert(l_sys != nullptr);

    o_data[index++] = OCC_CFGDATA_TCT_CONFIG;
    o_data[index++] = OCC_CFGDATA_TCT_CONFIG_VERSION;

    // Processor Core Weight
    ATTR_OPEN_POWER_PROC_WEIGHT_type l_proc_weight;
    if ( ! l_sys->tryGetAttr          //if attr does not exists.
	   <ATTR_OPEN_POWER_PROC_WEIGHT>(l_proc_weight))
    {
	l_proc_weight = OCC_PROC_QUAD_DEFAULT_WEIGHT;
    }
    if(l_proc_weight == 0x0)
    {
	l_proc_weight = OCC_PROC_QUAD_DEFAULT_WEIGHT;
    }
    o_data[index++] = l_proc_weight;

    // Processor Quad Weight
    ATTR_OPEN_POWER_QUAD_WEIGHT_type l_quad_weight;
    if ( ! l_sys->tryGetAttr          //if attr does not exists.
	   <ATTR_OPEN_POWER_QUAD_WEIGHT>(l_quad_weight))
    {
	l_quad_weight = OCC_PROC_QUAD_DEFAULT_WEIGHT;
    }
    if(l_quad_weight == 0x0)
    {
	l_quad_weight = OCC_PROC_QUAD_DEFAULT_WEIGHT;
    }
    o_data[index++] = l_quad_weight;


    // data sets following (proc, Centaur(Cumulus only), DIMM), and
    // each will get a FRU type, DVS temp, error temp,
    // and max read timeout
    size_t l_numSetsOffset = index++;

    // Note: Bytes 4 and 5 of each data set represent the PowerVM DVFS and ERROR
    // Resending the regular DVFS and ERROR for now

    // Processor
    o_data[index++] = CFGDATA_FRU_TYPE_PROC;
    uint8_t l_DVFS_temp =l_sys->getAttr<ATTR_OPEN_POWER_PROC_DVFS_TEMP_DEG_C>();
    uint8_t l_ERR_temp =l_sys->getAttr<ATTR_OPEN_POWER_PROC_ERROR_TEMP_DEG_C>();
    uint8_t l_timeout = l_sys->getAttr<ATTR_OPEN_POWER_PROC_READ_TIMEOUT_SEC>();
    if(l_DVFS_temp == 0x0)
    {
	l_DVFS_temp = OCC_PROC_DEFAULT_DVFS_TEMP;
	l_ERR_temp  = OCC_PROC_DEFAULT_ERR_TEMP;
	l_timeout   = OCC_PROC_DEFAULT_TIMEOUT;
    }
    o_data[index++] = l_DVFS_temp;
    o_data[index++] = l_ERR_temp;
    o_data[index++] = OCC_NOT_DEFINED;     //PM_DVFS
    o_data[index++] = OCC_NOT_DEFINED;     //PM_ERROR
    o_data[index++] = l_timeout;
    l_numSets++;

    // DIMM
    o_data[index++] = CFGDATA_FRU_TYPE_DIMM;
    l_DVFS_temp =l_sys->getAttr<ATTR_OPEN_POWER_DIMM_THROTTLE_TEMP_DEG_C>();
    l_ERR_temp =l_sys->getAttr<ATTR_OPEN_POWER_DIMM_ERROR_TEMP_DEG_C>();
    l_timeout = l_sys->getAttr<ATTR_OPEN_POWER_DIMM_READ_TIMEOUT_SEC>();
    if(l_DVFS_temp == 0x0)
    {
	l_DVFS_temp = OCC_DIMM_DEFAULT_DVFS_TEMP;
	l_ERR_temp  = OCC_DIMM_DEFAULT_ERR_TEMP;
	l_timeout   = OCC_DIMM_DEFAULT_TIMEOUT;
    }
    o_data[index++] = l_DVFS_temp;
    o_data[index++] = l_ERR_temp;
    o_data[index++] = OCC_NOT_DEFINED;     //PM_DVFS
    o_data[index++] = OCC_NOT_DEFINED;     //PM_ERROR
    o_data[index++] = l_timeout;
    l_numSets++;

    // VRM
    if (!l_sys->tryGetAttr<ATTR_OPEN_POWER_VRM_READ_TIMEOUT_SEC>(l_timeout))
	l_timeout = 0;
    if (l_timeout != 0)
    {
	o_data[index++] = CFGDATA_FRU_TYPE_VRM;
	o_data[index++] = 0xFF;
	o_data[index++] = 0xFF;
	o_data[index++] = 0xFF;
	o_data[index++] = 0xFF;
	o_data[index++] = l_timeout;
	l_numSets++;
    }

    // GPU Cores
    if (!l_sys->tryGetAttr<ATTR_OPEN_POWER_GPU_READ_TIMEOUT_SEC>(l_timeout))
	l_timeout = 0xFF;
    if (l_timeout == 0)
    {
	l_timeout = 0xFF;
    }
    if (!l_sys->
	tryGetAttr<ATTR_OPEN_POWER_GPU_ERROR_TEMP_DEG_C>(l_ERR_temp))
	l_ERR_temp = OCC_NOT_DEFINED;
    if (l_ERR_temp == 0)
    {
	l_ERR_temp = OCC_NOT_DEFINED;
    }
    o_data[index++] = CFGDATA_FRU_TYPE_GPU_CORE;
    o_data[index++] = OCC_NOT_DEFINED;      //DVFS
    o_data[index++] = l_ERR_temp;           //ERROR
    o_data[index++] = OCC_NOT_DEFINED;      //PM_DVFS
    o_data[index++] = OCC_NOT_DEFINED;      //PM_ERROR
    o_data[index++] = l_timeout;
    l_numSets++;

    // GPU Memory
    if (!l_sys->
	tryGetAttr<ATTR_OPEN_POWER_GPU_MEM_READ_TIMEOUT_SEC>(l_timeout))
	l_timeout = 0xFF;
    if (l_timeout == 0)
    {
	l_timeout = 0xFF;
    }
    if (!l_sys->
	tryGetAttr<ATTR_OPEN_POWER_GPU_MEM_ERROR_TEMP_DEG_C>(l_ERR_temp))
	l_ERR_temp = OCC_NOT_DEFINED;
    if (l_ERR_temp == 0)
    {
	l_ERR_temp = OCC_NOT_DEFINED;
    }
    o_data[index++] = CFGDATA_FRU_TYPE_GPU_MEMORY;
    o_data[index++] = OCC_NOT_DEFINED;      //DVFS
    o_data[index++] = l_ERR_temp;           //ERROR
    o_data[index++] = OCC_NOT_DEFINED;      //PM_DVFS
    o_data[index++] = OCC_NOT_DEFINED;      //PM_ERROR
    o_data[index++] = l_timeout;
    l_numSets++;

    // VRM Vdd
    if(!l_sys->tryGetAttr<ATTR_OPEN_POWER_VRM_VDD_DVFS_TEMP_DEG_C>(l_DVFS_temp))
	l_DVFS_temp = OCC_NOT_DEFINED;
    if (l_DVFS_temp == 0)
    {
	l_DVFS_temp = OCC_NOT_DEFINED;
    }
    if(!l_sys->tryGetAttr<ATTR_OPEN_POWER_VRM_VDD_ERROR_TEMP_DEG_C>(l_ERR_temp))
	l_ERR_temp = OCC_NOT_DEFINED;
    if (l_ERR_temp == 0)
    {
	l_ERR_temp = OCC_NOT_DEFINED;
    }
    if(!l_sys->tryGetAttr<ATTR_OPEN_POWER_VRM_VDD_READ_TIMEOUT_SEC>(l_timeout))
	l_timeout = OCC_NOT_DEFINED;
    if(l_timeout == 0)
    {
	l_timeout = OCC_NOT_DEFINED;
    }
    o_data[index++] = CFGDATA_FRU_TYPE_VRM_VDD;
    o_data[index++] = l_DVFS_temp;          //DVFS
    o_data[index++] = l_ERR_temp;           //ERROR
    o_data[index++] = OCC_NOT_DEFINED;      //PM_DVFS
    o_data[index++] = OCC_NOT_DEFINED;      //PM_ERROR
    o_data[index++] = l_timeout;
    l_numSets++;

    o_data[l_numSetsOffset] = l_numSets;
    o_size = index;
}

void getAVSBusConfigMessageData( const TargetHandle_t i_occ,
				 uint8_t * o_data,
				 uint64_t & o_size )
{
    uint64_t index      = 0;
    uint8_t version = 0x01;
    o_size = 0;

    Target* l_sys = nullptr;
    targetService().getTopLevelTarget(l_sys);
    assert(l_sys != nullptr);

    // Get the parent processor
    ConstTargetHandle_t l_proc = getParentChip( i_occ );
    assert( l_proc != nullptr );

    // Populate the data
    o_data[index++] = OCC_CFGDATA_AVSBUS_CONFIG;
    const uint64_t version_index = index++; // version updated later
    o_data[index++] = l_proc->getAttr<ATTR_VDD_AVSBUS_BUSNUM>();//Vdd Bus
    o_data[index++] = l_proc->getAttr<ATTR_VDD_AVSBUS_RAIL>();  //Vdd Rail Sel
    o_data[index++] = 0xFF;                                     //reserved
    o_data[index++] = 0xFF;                                     //reserved
    o_data[index++] = l_proc->getAttr<ATTR_VDN_AVSBUS_BUSNUM>();//Vdn Bus
    o_data[index++] = l_proc->getAttr<ATTR_VDN_AVSBUS_RAIL>();  //Vdn Rail sel

    ATTR_NO_APSS_PROC_POWER_VCS_VIO_WATTS_type PowerAdder = 0;
    if (l_proc->tryGetAttr          //if attr exists populate Proc Power Adder.
	<ATTR_NO_APSS_PROC_POWER_VCS_VIO_WATTS>(PowerAdder))
    {
	o_data[index++] = ((PowerAdder>>8)&0xFF);
	o_data[index++] = ((PowerAdder)&0xFF);
    }
    else                            //else attr not def. set to 0x0000.
    {
	o_data[index++] = 0x00;
	o_data[index++] = 0x00;
    }

    ATTR_VDD_CURRENT_OVERFLOW_WORKAROUND_ENABLE_type overflow_enable = 0;
    ATTR_MAX_VDD_CURRENT_READING_type max_vdd_current = 0;
    if ((l_sys->tryGetAttr          //if attr exists populate overflow_enable
	 <ATTR_VDD_CURRENT_OVERFLOW_WORKAROUND_ENABLE>(overflow_enable)) &&
	(l_sys->tryGetAttr          //if attr exists populate max_vdd_current
	 <ATTR_MAX_VDD_CURRENT_READING>(max_vdd_current)))
    {
	if (overflow_enable == 1)
	{
	    // Additional config info for Vdd Current overflow workaround
	    version = 0x02;
	    o_data[index++] = 0x7F; // Hardcode Vdd Current Rollover Point
	    o_data[index++] = 0xFF;
	    o_data[index++] = (max_vdd_current>>8) & 0xFF;
	    o_data[index++] = max_vdd_current & 0xFF;
	}
    }

    o_data[version_index] = version; // Version
    o_size = index;
}

void calculate_system_power()
{
    Target* sys = NULL;
    targetService().getTopLevelTarget(sys);
    assert(sys != NULL);

    TARGETING::TargetHandleList proc_list;
    // Get all processor chips (do not have to be functional)
    getAllChips(proc_list, TARGETING::TYPE_PROC, false);
    const uint8_t num_procs = proc_list.size();

    const uint16_t proc_socket_power =
	sys->getAttr<ATTR_PROC_SOCKET_POWER_WATTS>();
    TMGT_INF("calculate_system_power: proc socket power: %5dW (%d procs)",
	     proc_socket_power, num_procs);
    const uint16_t misc_power =
	sys->getAttr<ATTR_MISC_SYSTEM_COMPONENTS_MAX_POWER_WATTS>();
    TMGT_INF("calculate_system_power: misc power: %5dW", misc_power);

    // Calculate Total non-GPU maximum power (Watts):
    //   Maximum system power excluding GPUs when CPUs are at maximum frequency
    //   (ultra turbo) and memory at maximum power (least throttled) plus
    //   everything else (fans...) excluding GPUs.
    uint32_t power_max = proc_socket_power * num_procs;
    TMGT_INF("calculate_system_power: power(max)  proc: %5dW, mem: %5dW",
		 power_max, G_mem_power_min_throttles);
    power_max += G_mem_power_min_throttles + misc_power;
    TMGT_INF("calculate_system_power: max proc/mem/misc power (no GPUs): %5dW",
	     power_max);
    sys->setAttr<ATTR_CALCULATED_MAX_SYS_POWER_EXCLUDING_GPUS>(power_max);

    // Calculate Total Processor/Memory Power Drop (Watts):
    //   The max non-GPU power can be reduced (with proc/mem)
    //   calculates this as the CPU power at minimum frequency plus memory at
    //   minimum power (most throttled)
    const uint16_t freq_min = sys->getAttr<ATTR_MIN_FREQ_MHZ>();
    // Minimum Frequency biasing (ATTR_FREQ_BIAS_POWERSAVE) will be ignored here
    uint16_t freq_nominal, freq_turbo, freq_ultra;
    check_wof_support(freq_nominal, freq_turbo, freq_ultra);
    if (freq_turbo == 0)
    {
	freq_turbo = sys->getAttr<ATTR_FREQ_CORE_MAX>();
	// Turbo Frequency biasing (ATTR_FREQ_BIAS_TURBO) will be ignored here
	if (freq_turbo == 0)
	{
	    // If no turbo point, then use nominal...
	    TMGT_ERR("calculate_system_power: No turbo frequency to calculate "
		     "power drop.  Using nominal");
	    freq_turbo = freq_nominal;
	}
    }
    const uint16_t mhz_per_watt = sys->getAttr<ATTR_PROC_MHZ_PER_WATT>();
    // Drop always calculated from Turbo to Min (not ultra)
    uint32_t proc_drop = ((freq_turbo - freq_min) / mhz_per_watt);
    TMGT_INF("calculate_system_power: Processor Power Drop: %dMHz (%dMHz/W) "
	     "-> %dW/proc",
	     freq_turbo - freq_min, mhz_per_watt, proc_drop);
    proc_drop *= num_procs;
    const uint32_t memory_drop =
	G_mem_power_min_throttles - G_mem_power_max_throttles;
    TMGT_INF("calculate_system_power: Memory Power Drop: %d - %d = %dW",
	     G_mem_power_min_throttles, G_mem_power_max_throttles,
	     G_mem_power_min_throttles - G_mem_power_max_throttles);
    const uint32_t power_drop = proc_drop + memory_drop;
    TMGT_INF("calculate_system_power: Proc/Mem Power Drop: %d + %d = %dW",
	     proc_drop, memory_drop, power_drop);
    sys->setAttr<ATTR_CALCULATED_PROC_MEMORY_POWER_DROP>(power_drop);

} // end calculate_system_power()

// Send config data required by OCC for GPU handling.
// The OCC will determine which GPUs are present from the APSS GPIOs.
void getGPUConfigMessageData(const TargetHandle_t i_occ,
			     uint8_t * o_data,
			     uint64_t & o_size)
{
    unsigned int index = 0;

    // Get system and proc target
    Target* sys = nullptr;
    targetService().getTopLevelTarget(sys);
    assert(sys != nullptr);
    ConstTargetHandle_t proc = getParentChip(i_occ);
    assert(proc != nullptr);

    // Populate the data
    o_data[index++] = OCC_CFGDATA_GPU_CONFIG;
    o_data[index++] = 0x01;             // GPU Config Version

    uint16_t power = 0;
    power = sys->getAttr<ATTR_CALCULATED_MAX_SYS_POWER_EXCLUDING_GPUS>();
    UINT16_PUT(&o_data[index], power);   // Total non-GPU max power (W)
    index += 2;

    power = sys->getAttr<ATTR_CALCULATED_PROC_MEMORY_POWER_DROP>();
    UINT16_PUT(&o_data[index], power);   // Total proc/mem power drop (W)
    index += 2;
    o_data[index++] = 0;                // reserved
    o_data[index++] = 0;

    uint32_t gpu_func_sensors[MAX_GPUS] = {0};
    uint32_t gpu_temp_sensors[MAX_GPUS] = {0};
    uint32_t gpu_memtemp_sensors[MAX_GPUS] = {0};
    // Read GPU sensor numbers
    uint8_t num_sensors = 0;
    errlHndl_t err = nullptr;
    err = SENSOR::getGpuSensors(const_cast<TARGETING::TargetHandle_t>(proc),
				HWAS::GPU_FUNC_SENSOR,
				num_sensors, gpu_func_sensors);
    if (err)
    {
	memset(gpu_func_sensors, 0, sizeof(gpu_func_sensors));
    }
    err = SENSOR::getGpuSensors(const_cast<TARGETING::TargetHandle_t>(proc),
				HWAS::GPU_TEMPERATURE_SENSOR,
				num_sensors, gpu_temp_sensors);
    if (err)
    {
	memset(gpu_temp_sensors, 0, sizeof(gpu_temp_sensors));
    }
    err = SENSOR::getGpuSensors(const_cast<TARGETING::TargetHandle_t>(proc),
				HWAS::GPU_MEMORY_TEMP_SENSOR,
				num_sensors, gpu_memtemp_sensors);
    if (err)
    {
	memset(gpu_memtemp_sensors, 0, sizeof(gpu_memtemp_sensors));
    }
    for (unsigned int index = 0; index < MAX_GPUS; ++index)
    {
	if (gpu_func_sensors[index] == TARGETING::UTIL::INVALID_IPMI_SENSOR)
	    gpu_func_sensors[index] = 0;
	if (gpu_temp_sensors[index] == TARGETING::UTIL::INVALID_IPMI_SENSOR)
	    gpu_temp_sensors[index] = 0;
	if (gpu_memtemp_sensors[index] == TARGETING::UTIL::INVALID_IPMI_SENSOR)
	    gpu_memtemp_sensors[index] = 0;
    }

    // GPU0
    UINT32_PUT(&o_data[index], gpu_temp_sensors[0]);
    index += 4;
    UINT32_PUT(&o_data[index], gpu_memtemp_sensors[0]);
    index += 4;
    UINT32_PUT(&o_data[index], gpu_func_sensors[0]);
    index += 4;

    // GPU1
    UINT32_PUT(&o_data[index], gpu_temp_sensors[1]);
    index += 4;
    UINT32_PUT(&o_data[index], gpu_memtemp_sensors[1]);
    index += 4;
    UINT32_PUT(&o_data[index], gpu_func_sensors[1]);
    index += 4;

    // GPU2
    UINT32_PUT(&o_data[index], gpu_temp_sensors[2]);
    index += 4;
    UINT32_PUT(&o_data[index], gpu_memtemp_sensors[2]);
    index += 4;
    UINT32_PUT(&o_data[index], gpu_func_sensors[2]);
    index += 4;

    o_size = index;
}

//Sends the user selected power limit to the master OCC
errlHndl_t sendOccUserPowerCap()
{
	Target* sys = NULL;
	bool active = false;
	uint16_t limit = 0;
	uint16_t min = 0;
	uint16_t max = 0;
	targetService().getTopLevelTarget(sys);
	assert(sys != NULL);

	if (active)
	{
		//Make sure this value is between the min & max allowed
		bool is_redundant;
		min = sys->getAttr<ATTR_OPEN_POWER_MIN_POWER_CAP_WATTS>();
		max = getMaxPowerCap(sys, is_redundant);
		if ((limit != 0) && (limit < min))
		{
			limit = min;
		}
		else if (limit > max)
		{
			limit = max;
		}
		else if (limit == 0)
		{
			active = false;
		}
	}
	else
	{
		// The OCC knows cap isn't active by getting a value of 0.
		limit = 0;
	}


	Occ* occ = occMgr::instance().getMasterOcc();
	if (occ)
	{
		uint8_t data[2];
		data[0] = limit >> 8;
		data[1] = limit & 0xFF;

		OccCmd cmd(occ, OCC_CMD_SET_POWER_CAP, 2, data);

		errlHndl_t err = cmd.sendOccCmd();
		if (err)
		{
			return err;
		}
	}

	return NULL;
}

// Wait for all OCCs to reach target state
errlHndl_t waitForOccState()
{
    errlHndl_t l_err = NULL;

    // Wait for all OCCs to be ready for active state
    l_err = waitForOccReady();
    if (NULL == l_err)
    {
        // Send Set State command to master OCC.
        // The master will use the target state (default = ACTIVE)
        l_err = OccManager::setOccState();
    }

    return l_err;
}

const uint8_t OCC_STATUS_ACTIVE_READY   = 0x01;
const uint8_t OCC_STATUS_OBS_READY      = 0x02;

enum occStateId
{
    OCC_STATE_NO_CHANGE                = 0x00,
    OCC_STATE_STANDBY                  = 0x01,
    OCC_STATE_OBSERVATION              = 0x02,
    OCC_STATE_ACTIVE                   = 0x03,
    OCC_STATE_SAFE                     = 0x04,
    OCC_STATE_CHARACTERIZATION         = 0x05,
    // the following states are internal to TMGT
    OCC_STATE_RESET                    = 0x85,
    OCC_STATE_IN_TRANSITION            = 0x87,
    OCC_STATE_LOADING                  = 0x88,
    OCC_STATE_UNKNOWN                  = 0x89,
};

// Wait for all OCCs to reach ready state
errlHndl_t waitForOccReady()
{
    const uint8_t OCC_NONE = 0xFF;

    uint8_t waitingForInstance = OCC_NONE;
    const size_t MAX_POLL = 40;
    const size_t MSEC_BETWEEN_POLLS = 250;
    size_t numPolls = 0;
    std::vector<Occ*> occList = OccManager::getOccArray();

    // Determine which bit to check
    uint8_t targetBit = OCC_STATUS_ACTIVE_READY;
    // should evaluate to `false`, we're going to "Active" state
    if (OCC_STATE_OBSERVATION == OccManager::getTargetState())
    {
	targetBit = OCC_STATUS_OBS_READY;
    }

    do
    {
	// Poll all OCCs
	errlHndl_t l_err = OccManager::sendOccPoll();
	++numPolls;
	if (NULL != l_err)
	{
		return l_err;
	}

	// Check each OCC for ready state
	waitingForInstance = OCC_NONE;
	for (Occ * occ : occList)
	{
	    if (!occ->statusBitSet(targetBit))
	    {
		waitingForInstance = occ->getInstance();
		break;
	    }
	}

	if ((OCC_NONE != waitingForInstance) && (numPolls < MAX_POLL))
	{
	    // Still waiting for at least one OCC, delay and try again
	    nanosleep(0,  NS_PER_MSEC * MSEC_BETWEEN_POLLS);
	}
    } while ((OCC_NONE != waitingForInstance) && (numPolls < MAX_POLL));

    if (OCC_NONE != waitingForInstance)
    {
	die("waitForOccReady: OCC%d is not in ready state", waitingForInstance);
    }

    return l_err;
}

// Set the OCC state
errlHndl_t OccManager::setOccState(const occStateId i_state = OCC_STATE_NO_CHANGE )
{
	errlHndl_t l_err = nullptr;

	occStateId requestedState = i_state;
	if (OCC_STATE_NO_CHANGE == i_state)
	{
		// If no state was requested use the target state, which is OCC_STATE_ACTIVE
		// by default.
		requestedState = OCC_STATE_ACTIVE;
	}

	l_err = _buildOccs(); // if not already built.

	// Send poll cmd to confirm comm has been established.
	// Flush old errors to ensure any new errors will be collected
	l_err = sendOccPoll(true, nullptr);
	if (l_err)
	{
		TMGT_ERR("setOccState: Poll OCCs failed.");
		// Proceed with reset even if failed
		ERRORLOG::errlCommit(l_err, HTMGT_COMP_ID);
	}

	const uint8_t occInstance = iv_occMaster->getInstance();
	bool needsRetry = false;
	do
	{
		l_err = iv_occMaster->setState(requestedState);
		if (nullptr == l_err)
		{
			needsRetry = false;
		}
		else
		{
			if (false == needsRetry)
			{
				ERRORLOG::errlCommit(l_err, HTMGT_COMP_ID);
				needsRetry = true;
			}
			else
			{
				// Only one retry, return error handle
				needsRetry = false;
			}
		}
	}
	while (needsRetry);

	if (l_err)
		return l_err;

	// Send poll to query state of all OCCs
	// and flush any errors reported by the OCCs
	l_err = sendOccPoll(true);
	if (l_err)
	{
		TMGT_ERR("setOccState: Poll all OCCs failed");
		ERRORLOG::errlCommit(l_err, HTMGT_COMP_ID);
	}

	// Make sure all OCCs went to active state
	for( const auto & occ : iv_occArray )
	{
		if (requestedState == occ->getState())
		{
			// Update GPU present status
			occ->updateGpuPresence();
		}
		else
		{
			die();
		}
	}

	return l_err;
}

// Set state of the OCC
errlHndl_t Occ::setState(const occStateId i_state)
{
	errlHndl_t l_err = nullptr;

	const uint8_t l_cmdData[3] =
	{
		0x00, // version
		i_state,
		0x00 // reserved
	};

	OccCmd cmd(this, OCC_CMD_SET_STATE, sizeof(l_cmdData), l_cmdData);
	l_err = cmd.sendOccCmd();
	if (l_err != nullptr)
	{
		die();
	}
	else
	{
		if (OCC_RC_SUCCESS != cmd.getRspStatus())
		{
			die();
		}
	}

	return l_err;

} // end Occ::setState()

// Send command to OCC
errlHndl_t OccCmd::sendOccCmd()
{
	errlHndl_t l_errlHndl = NULL;
	iv_OccRsp.returnStatus = OCC_COMMAND_IN_PROGRESS;

	// Only allow commands if comm has been established,
	// or this is a poll command
	const bool l_commEstablished = iv_Occ->iv_commEstablished;
	if (l_commEstablished || OCC_CMD_POLL == iv_OccCmd.cmdType)
	{
		iv_RetryCmd = false;
		do
		{
			// Send the command and receive the response
			l_errlHndl = writeOccCmd();

			// process response if OCC did not hit an exception
			if (0 == iv_Occ->iv_exceptionLogged)
			{
				processOccResponse(l_errlHndl);
			}
			// skip retry if an exception was logged
		} while (iv_RetryCmd && (0 == iv_Occ->iv_exceptionLogged));
	}

	return l_errlHndl;
}

struct occCircBufferCmd_t
{
    uint8_t senderId;
    uint8_t commandType;
    uint8_t reserved[6];
}__attribute__((packed));

// Build OCC command buffer in HOMER, notify OCC and wait for the response or timeout
errlHndl_t OccCmd::writeOccCmd()
{
    errlHndl_t l_err = NULL;

    // Write the command to HOMER
    buildOccCmdBuffer();

    // Notify OCC that command is available (via circular buffer)
    const uint32_t l_bitsToSend = sizeof(occCircBufferCmd_t) * 8;

    const occCircBufferCmd_t tmgtDataWriteAttention = {
	0x10,   // sender: HTMGT
	0x01,   // command: Command Write Attention
	{0, 0, 0, 0, 0, 0} // reserved
    };

    fapi2::buffer<uint64_t> l_circ_buffer;
    l_circ_buffer.insert((*(uint64_t*)&tmgtDataWriteAttention),0, l_bitsToSend);

    l_err = HBOCC::writeCircularBuffer(iv_Occ->iv_target, l_circ_buffer.pointer());
    if (NULL != l_err)
    {
	// Continue to try to read response buffer, in case an
	// exception happened...
    }

    // Wait for response from the OCC
    const uint8_t l_index = getCmdIndex(iv_OccCmd.cmdType);
    const uint16_t l_read_timeout = cv_occCommandTable[l_index].timeout;

    // Wait for OCC to process command and send response
    waitForOccRsp(l_read_timeout);

    // Parse the OCC response (called even on timeout to collect
    // rsp buffer)
    l_err = parseOccResponse();

    if (OCC_COMMAND_IN_PROGRESS != iv_OccRsp.returnStatus)
    {
	// Status of 0xE0-EF are reserved for OCC exceptions,
	// must collect data for these
	if (0xE0 == (iv_OccRsp.returnStatus & 0xF0))
	{
	    handleOccException();
	    die();
	}
	else if (iv_OccRsp.sequenceNumber != iv_OccCmd.sequenceNumber)
	{
	    // Sequence number mismatch
	    die();
	}
    }
    else
    {
	// OCC must not have completed processing the command before
	// timeout
	die();
    }

    return l_err;
}

const uint32_t OCC_CMD_ADDR        = 0x000E0000;
const uint32_t OCC_RSP_ADDR        = 0x000E1000;

// Copy OCC command into command buffer in HOMER
uint16_t OccCmd::buildOccCmdBuffer()
{
    uint8_t * const cmdBuffer = iv_Occ->iv_homer + OCC_CMD_ADDR;
    uint16_t l_send_length = 0;

    if (0 == ++iv_Occ->iv_seqNumber)
    {
	// Do not use 0 for sequence number
	++iv_Occ->iv_seqNumber;
    }
    iv_OccCmd.sequenceNumber = iv_Occ->iv_seqNumber;
    cmdBuffer[l_send_length++] = iv_OccCmd.sequenceNumber;
    cmdBuffer[l_send_length++] = iv_OccCmd.cmdType;
    cmdBuffer[l_send_length++] = (iv_OccCmd.dataLength >> 8) & 0xFF;
    cmdBuffer[l_send_length++] = iv_OccCmd.dataLength & 0xFF;
    memcpy(&cmdBuffer[l_send_length], iv_OccCmd.cmdData,
	   iv_OccCmd.dataLength);
    l_send_length += iv_OccCmd.dataLength;

    // Calculate checksum
    iv_OccCmd.checksum = 0;
    for (uint16_t l_index = 0; l_index < l_send_length; l_index++)
    {
	iv_OccCmd.checksum += cmdBuffer[l_index];
    }
    cmdBuffer[l_send_length++] = (iv_OccCmd.checksum >> 8) & 0xFF;
    cmdBuffer[l_send_length++] = iv_OccCmd.checksum & 0xFF;

    // When the P8 processor writes to memory (such as the HOMER) there is
    // no certainty that the writes happen in order or that they have
    // actually completed by the time the instructions complete. 'sync'
    // is a memory barrier to ensure the HOMER data has actually been made
    // consistent with respect to memory, so that if the OCC were to read
    // it they would see all of the data. Otherwise, there is potential
    // for them to get stale or incomplete data.
    sync();

    return l_send_length;
}

// Write OCC Circular Buffer
errlHndl_t writeCircularBuffer(const TARGETING::Target * i_pTarget,
			       uint64_t * i_dataBuf)
{
    errlHndl_t l_errl = nullptr;
    l_errl = accessOCBIndirectChannel(ACCESS_OCB_WRITE_CIRCULAR,
					i_pTarget,
					0,
					i_dataBuf,
					CIRCULAR_OCB_DATA_SIZE);
    return l_errl;
}

/*
 * @brief Interface for communicating with the OCC via OCB channels
 *
 * @param[in] i_cmd - OCB Command type
 * @param[in] i_pTarget - The OCC Target
 * @param[in] i_addr - The address to read from/write to
 * @param[in/out] io_dataBuf - The input/output buffer
 * @param[in] i_dataLen - The length of the buffer in bytes
 *
 * @return - nullptr on success, error log handle on failure
 */
errlHndl_t accessOCBIndirectChannel(accessOCBIndirectCmd i_cmd = ACCESS_OCB_WRITE_CIRCULAR,
				    const TARGETING::Target * i_pTarget,
				    const uint32_t i_addr = 0,
				    uint64_t * io_dataBuf,
				    size_t i_dataLen )
{
    errlHndl_t l_errl = nullptr;
    uint32_t   l_len  = 0;
    TARGETING::Target* l_pChipTarget = nullptr;

    p9ocb::PM_OCB_CHAN_NUM   l_channel = p9ocb::OCB_CHAN1;  // OCB channel (0,1,2,3)
    p9ocb::PM_OCB_ACCESS_OP   l_operation = p9ocb::OCB_PUT;  // Operation(Get, Put)
    bool       l_ociAddrValid = false;  // use oci_address

    do
    {
	l_errl = getChipTarget(i_pTarget,l_pChipTarget);
	if (l_errl)
	{
	    break; //exit with error
	}

	fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_fapiTarget( l_pChipTarget );

	// buffer must be multiple of 8 bytes
	if( i_dataLen%8 != 0)
	{
	    break; // return with error
	}

	// perform operation
	p9_pm_ocb_indir_access(l_fapiTarget,
			 l_channel,
			 l_operation,
			 i_dataLen/8, // Number of 8-byte blocks
			 l_ociAddrValid,
			 i_addr,
			 l_len,
			 io_dataBuf);

	if(l_errl)
	{
	    break; // return with error
	}
    }
    while (0);

    return l_errl;
}

fapi2::ReturnCode 9_pm_ocb_indir_access(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9ocb::PM_OCB_CHAN_NUM  i_ocb_chan = p9ocb::OCB_CHAN1,
    const p9ocb::PM_OCB_ACCESS_OP i_ocb_op,
    const uint32_t                i_ocb_req_length,
    const bool                    i_oci_address_valid = false,
    const uint32_t                i_oci_address,
    uint32_t&                     o_ocb_act_length,
    uint64_t*                     io_ocb_buffer)
{
    uint64_t l_OCBAR_address   = PU_OCB_PIB_OCBAR1;
    uint64_t l_OCBDR_address   = PU_OCB_PIB_OCBDR1;
    uint64_t l_OCBCSR_address  = PU_OCB_PIB_OCBCSR1_RO;
    uint64_t l_OCBSHCS_address = PU_OCB_OCI_OCBSHCS1_SCOM;
    o_ocb_act_length = 0;

    // PUT Operation: Write data to the SRAM in the given location
    //                via the OCB channel
    if ( i_ocb_op == p9ocb::OCB_PUT )
    {
	fapi2::buffer<uint64_t> l_data64;
	fapi2::getScom(i_target, l_OCBCSR_address, l_data64);

	// The following check for circular mode is an additional check
	// performed to ensure a valid data access.
	if (l_data64.getBit<4>() && l_data64.getBit<5>())
	{
	    FAPI_DBG("Circular mode detected.");
	    // Check if push queue is enabled. If not, let the store occur
	    // anyway to let the PIB error response return occur. (that is
	    // what will happen if this checking code were not here)
	    fapi2::getScom(i_target, l_OCBSHCS_address, l_data64);

	    if (l_data64.getBit<31>())
	    {
		FAPI_DBG("Poll for a non-full condition to a push queue to "
			 "avoid data corruption problem");
		bool l_push_ok_flag = false;
		uint8_t l_counter = 0;

		do
		{
		    // If the OCB_OCI_OCBSHCS0_PUSH_FULL bit (bit 0) is clear,
		    // proceed. Otherwise, poll
		    if (!l_data64.getBit<0>())
		    {
			l_push_ok_flag = true;
			FAPI_DBG("Push queue not full. Proceeding");
			break;
		    }

		    // Delay, before next polling.
		    fapi2::delay(OCB_FULL_POLL_DELAY_HDW, OCB_FULL_POLL_DELAY_SIM);

		    fapi2::getScom(i_target, l_OCBSHCS_address, l_data64);
		    l_counter++;
		}
		while (l_counter < OCB_FULL_POLL_MAX);

		FAPI_ASSERT((true == l_push_ok_flag),
			    fapi2::PM_OCB_PUT_DATA_POLL_NOT_FULL_ERROR().
			    set_CHANNEL(i_ocb_chan).
			    set_DATA_SIZE(i_ocb_req_length).
			    set_TARGET(i_target),
			    "Polling timeout waiting on push non-full");
	    }
	}

	// Walk the input buffer (io_ocb_buffer) 8B (64bits) at a time to write
	// the channel data register
	for(uint32_t l_index = 0; l_index < i_ocb_req_length; l_index++)
	{
	    l_data64.insertFromRight(io_ocb_buffer[l_index], 0, 64);
	    /* The data read is done via this getscom operation.
	     * A data write failure will be logged off as a simple scom failure.
	     * Need to find a way to distiniguish this error and collect
	     * additional information incase of a failure.*/
	    fapi2::putScom(i_target, l_OCBDR_address, l_data64);
	    o_ocb_act_length++;
	}
    }
}

// Returns true if timeout waiting for response
bool OccCmd::waitForOccRsp(uint32_t i_timeout)
{
    const uint8_t * const rspBuffer = iv_Occ->iv_homer + OCC_RSP_ADDR;
    uint16_t rspLength = 0;

    bool l_time_expired = true;
    const int64_t OCC_RSP_SAMPLE_TIME = 100; // in milliseconds
    int64_t l_msec_remaining =
	std::max(int64_t(i_timeout * 1000), OCC_RSP_SAMPLE_TIME);
    while (l_msec_remaining >= 0)
    {
	// 1. When OCC receives the command, it will set the status to
	//    COMMAND_IN_PROGRESS.
	// 2. When the response is ready OCC will update the full
	//    response buffer (except the status)
	// 3. The status field is updated last to indicate response ready
	//
	// Note: Need to check the sequence number to be sure we are
	//       processing the expected response
	if ((OCC_COMMAND_IN_PROGRESS != rspBuffer[2]) &&
	    (iv_Occ->iv_seqNumber == rspBuffer[0]))
	{
	    // Need an 'isync' here to ensure that previous instructions
	    // have completed before the code continues on. This is a type
	    // of read-barrier.  Without this the processor can do
	    // speculative reads of the HOMER data and you can actually
	    // get stale data as part of the instructions that happen
	    // afterwards. Another 'weak consistency' issue.
	    isync();

	    // OCC must have processed the command
	    const uint16_t rspDataLen = UINT16_GET(&rspBuffer[3]);
	    rspLength = OCC_RSP_HDR_LENGTH + rspDataLen;
	    l_time_expired = false;
	    break;
	}

	if (l_msec_remaining > 0)
	{
	    // delay before next check
	    const int64_t l_sleep_msec = std::min(l_msec_remaining,
						  OCC_RSP_SAMPLE_TIME);
	    nanosleep( 0, NS_PER_MSEC * l_sleep_msec );
	    l_msec_remaining -= l_sleep_msec;
	}
	else
	{
	    // time expired
	    l_msec_remaining = -1;

	    // Read SRAM response buffer to check for exception
	    // (On exception, data may not be copied to HOMER)
	    handleOccException();
	    die();
	}
    }

    return l_time_expired;
}

// Copy response into object
errlHndl_t OccCmd::parseOccResponse()
{
	errlHndl_t l_errlHndl = NULL;
	uint16_t l_index = 0;
	const uint8_t * const rspBuffer = iv_Occ->iv_homer + OCC_RSP_ADDR;

	iv_OccRsp.sequenceNumber = rspBuffer[l_index++];
	iv_OccRsp.cmdType = (enum occCommandType)rspBuffer[l_index++];
	iv_OccRsp.returnStatus = (occReturnCodes)rspBuffer[l_index++];

	iv_OccRsp.dataLength = UINT16_GET(&rspBuffer[l_index]);
	l_index += 2;

	if (iv_OccRsp.dataLength > 0)
	{
		if (iv_OccRsp.dataLength > OCC_MAX_DATA_LENGTH)
		{
			// truncating data
			iv_OccRsp.dataLength = OCC_MAX_DATA_LENGTH;
		}
		memcpy(iv_OccRsp.rspData, &rspBuffer[l_index],
			iv_OccRsp.dataLength);
		l_index += iv_OccRsp.dataLength;
	}

	iv_OccRsp.checksum = UINT16_GET(&rspBuffer[l_index]);

	return l_errlHndl;
}

// Check for an OCC exception in SRAM.  If found:
// create/commit an error log with the OCC exception data
void OccCmd::handleOccException(void);

// Process the OCC response and determine if retry is required
void OccCmd::processOccResponse(errlHndl_t & io_errlHndl)
{
	const uint8_t l_instance = iv_Occ->iv_instance;
	const bool alreadyRetriedOnce = iv_RetryCmd;
	iv_RetryCmd = false;

	if (io_errlHndl != NULL)
		die();

	// A response was received
	io_errlHndl = checkOccResponse();
	if (io_errlHndl != NULL)
	{
		// Error checking on response failed...
		if (!alreadyRetriedOnce &&
		    OCC_RC_PRESENT_STATE_PROHIBITS != iv_OccRsp.returnStatus)
		{
			// A retry has not been sent yet, commit the error
			// and retry.
			iv_RetryCmd = true;
			// Clear/init the response data structure
			memset(&iv_OccRsp, 0x00, sizeof(occResponseStruct_t));
			iv_OccRsp.returnStatus = OCC_COMMAND_IN_PROGRESS;
		}
		else
		{
			die();
		}
	}
}

struct occCommandTable_t
{
    occCommandType    cmdType;
    uint8_t            supported;
    occCheckRspLengthType checkRspLength;
    uint16_t           rspLength;
    uint32_t           timeout;
    uint16_t           maxBytesRead;
    occCmdTraceEnum   traceCmd;

    bool operator== (const occCommandType i_cmd)
    {
	return (cmdType == i_cmd);
    }
};

const occCommandTable_t OccCmd::cv_occCommandTable[] =
{
    // Command                   Support  RspCheck
    //   RspLen  Timeout  ReadMax  Tracing
    {OCC_CMD_POLL,                 0xE0,  OCC_CHECK_RSP_LENGTH_GREATER,
	0x0028, TO_20SEC,  0x0017, OCC_TRACE_EXTENDED},
    {OCC_CMD_CLEAR_ERROR_LOG,      0xC0,  OCC_CHECK_RSP_LENGTH_EQUALS,
	0x0000, TO_20SEC,  0x0008, OCC_TRACE_EXTENDED},
    {OCC_CMD_SET_STATE,            0xE0,  OCC_CHECK_RSP_LENGTH_EQUALS,
	0x0000, TO_20SEC,  0x0008, OCC_TRACE_ALWAYS},
    {OCC_CMD_SETUP_CFG_DATA,       0x80,  OCC_CHECK_RSP_LENGTH_EQUALS,
	0x0000, TO_20SEC,  0x0008, OCC_TRACE_CONDITIONAL},
    {OCC_CMD_SET_POWER_CAP,        0x80,  OCC_CHECK_RSP_LENGTH_NONE,
	0x0000, TO_20SEC,  0x0090, OCC_TRACE_EXTENDED},
    {OCC_CMD_RESET_PREP,           0x80,  OCC_CHECK_RSP_LENGTH_GREATER,
	0x0000, TO_20SEC,  0x0190, OCC_TRACE_ALWAYS},
    {OCC_CMD_DEBUG_PASS_THROUGH,   0xF0,  OCC_CHECK_RSP_LENGTH_NONE,
	0x0000, TO_20SEC,  RD_MAX, OCC_TRACE_EXTENDED},
    {OCC_CMD_AME_PASS_THROUGH,     0xF0,  OCC_CHECK_RSP_LENGTH_NONE,
	0x0000, TO_20SEC,  RD_MAX, OCC_TRACE_EXTENDED},
    {OCC_CMD_GET_FIELD_DEBUG_DATA, 0x80,  OCC_CHECK_RSP_LENGTH_GREATER,
	0x0001, TO_20SEC,  RD_MAX, OCC_TRACE_NEVER},
    {OCC_CMD_MFG_TEST,             0xF0,  OCC_CHECK_RSP_LENGTH_NONE,
	0x0001, TO_20SEC,  RD_MAX, OCC_TRACE_ALWAYS},

    // If command not found, use this last entry
    {OCC_CMD_END_OF_TABLE,         0xE0,  OCC_CHECK_RSP_LENGTH_NONE,
	0x0000, TO_20SEC,  RD_MAX, OCC_TRACE_NEVER}
};

// Verify status, checksum and length of OCC response
errlHndl_t OccCmd::checkOccResponse()
{
	errlHndl_t l_errlHndl = NULL;
	uint16_t l_calc_checksum = 0, l_index = 0;

	// Calculate checksum on response
	l_calc_checksum += iv_OccRsp.sequenceNumber;
	l_calc_checksum += iv_OccRsp.cmdType;
	l_calc_checksum += iv_OccRsp.returnStatus;
	l_calc_checksum += (iv_OccRsp.dataLength >> 8) & 0xFF;
	l_calc_checksum += iv_OccRsp.dataLength & 0xFF;
	for (l_index = 0; l_index < iv_OccRsp.dataLength; l_index++)
	{
		l_calc_checksum += iv_OccRsp.rspData[l_index];
	}

	if (l_calc_checksum != iv_OccRsp.checksum)
		die();

	if (iv_OccRsp.returnStatus != OCC_RC_SUCCESS)
		die();

	occCheckRspLengthType l_check_rsp_length;
	uint16_t l_rsp_length = 0;

	// Verify response length and log errors if bad
	l_index = getCmdIndex(iv_OccRsp.cmdType);
	// l_index should be valid since validation was done
	// in sendOccCmd()
	l_check_rsp_length = cv_occCommandTable[l_index].checkRspLength;
	l_rsp_length = cv_occCommandTable[l_index].rspLength;

	if (OCC_CHECK_RSP_LENGTH_EQUALS == l_check_rsp_length)
	{
		if ( iv_OccRsp.dataLength != l_rsp_length )
		{
			die();
		}
	}
	else if (OCC_CHECK_RSP_LENGTH_GREATER == l_check_rsp_length)
	{
		if ( iv_OccRsp.dataLength < l_rsp_length )
		{
			die();
		}
	}
	else if (OCC_CHECK_RSP_LENGTH_NONE != l_check_rsp_length)
	{
		die();
	}

	return(l_errlHndl);
}

// Set active/inactive sensors for all OCCs so BMC can start communication
errlHndl_t setOccActiveSensors(bool i_activate = true)
{
    errlHndl_t l_err = NULL;

    for (Occ * occ : OccManager::getOccArray())
    {
	l_err = occ->ipmiSensor(i_activate);
    }

    return l_err;
}

StopReturnCode_t p9_stop_save_scom( void* const   i_pImage,
                                    const uint32_t i_scomAddress,
                                    const uint64_t i_scomData,
                                    const ScomOperation_t i_operation,
                                    const ScomSection_t i_section )
{
    return proc_stop_save_scom(i_pImage, i_scomAddress, i_scomData, i_operation, i_section);
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
                if(PPE_EP05_CHIPLET_ID >= l_scom.get_chiplet_id()
                && l_scom.get_chiplet_id() >= PPE_EP00_CHIPLET_ID)
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
                if(EP05_CHIPLET_ID >= l_scom.get_chiplet_id()
                && l_scom.get_chiplet_id() >= EP00_CHIPLET_ID)
                {
                    l_scom.set_chiplet_id(EP00_CHIPLET_ID + (i_chipUnitNum / 2));
                    uint8_t l_ringId = (l_scom.get_ring() & 0xF);
                    l_ringId = l_ringId - l_ringId % 2 + i_chipUnitNum % 2;
                    l_scom.set_ring(l_ringId & 0xF);
                }
                else if (EC23_CHIPLET_ID >= l_scom.get_chiplet_id()
                && l_scom.get_chiplet_id() >= EC00_CHIPLET_ID)
                {
                    l_scom.set_chiplet_id(
                        EC00_CHIPLET_ID
                      + l_scom.get_chiplet_id() % 2
                      + i_chipUnitNum * 2);
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

                    if((l_scom.get_ring() & 0xF) == MC_MC01_0_RING_ID)
                    {
                        l_scom.set_sat_id(
                            l_scom.get_sat_id()
                          - l_scom.get_sat_id() % 4
                          + i_chipUnitNum % 4);
                    }
                    else
                    {
                        l_scom.set_ring( (MC_IOM01_0_RING_ID + (i_chipUnitNum % 4)) & 0xF );
                    }
                }
                else
                {
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
                l_scom.set_chiplet_id(N3_CHIPLET_ID - (2 * (i_chipUnitNum / 2)));
                l_scom.set_sat_id(2 * (i_chipUnitNum % 2));
                break;
            case PU_DMI_CHIPUNIT:
                if(l_chiplet_id == N3_CHIPLET_ID || l_chiplet_id == N1_CHIPLET_ID)
                {
                    l_scom.set_chiplet_id(N3_CHIPLET_ID - (2 * (i_chipUnitNum / 4)));
                    l_scom.set_sat_id(2 * ((i_chipUnitNum / 2) % 2));
                    l_sat_offset = (l_sat_offset & 0xF) + ((2 + (i_chipUnitNum % 2)) << 4);
                    l_scom.set_sat_offset(l_sat_offset);
                }

                if (l_chiplet_id == MC01_CHIPLET_ID || l_chiplet_id == MC23_CHIPLET_ID)
                {
                    if (l_ring == P9C_MC_CHAN_RING_ID)
                    {
                        l_scom.set_chiplet_id(MC01_CHIPLET_ID + (i_chipUnitNum / 4));
                        uint8_t l_msat = l_scom.get_sat_id();
                        l_msat = l_msat & 0xC;
                        l_scom.set_sat_id(l_msat + i_chipUnitNum % 4);
                    }
                    if (l_ring == P9C_MC_BIST_RING_ID)
                    {
                        l_scom.set_chiplet_id(MC01_CHIPLET_ID + (i_chipUnitNum / 4));
                        l_sat_offset = (l_sat_offset & 0xF) + ((i_chipUnitNum % 4) << 4);
                        l_scom.set_sat_offset(l_sat_offset);
                    }
                    if (l_ring == P9C_MC_IO_RING_ID)
                    {
                        l_scom.set_chiplet_id(MC01_CHIPLET_ID + (i_chipUnitNum / 4));
                        uint8_t l_rxtx_grp = l_scom.get_rxtx_group_id();
                        l_rxtx_grp = l_rxtx_grp & 0xF0;

                        switch(i_chipUnitNum % 4)
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
                        }
                        l_scom.set_rxtx_group_id(l_rxtx_grp);
                    }
                }
                break;
            case PU_MCC_CHIPUNIT:
                if (l_chiplet_id == N3_CHIPLET_ID || l_chiplet_id == N1_CHIPLET_ID)
                {
                    l_scom.set_chiplet_id(N3_CHIPLET_ID - (2 * (i_chipUnitNum / 4)));
                    l_scom.set_sat_id(2 * ((i_chipUnitNum / 2) % 2));
                    uint8_t l_satoff = (l_sat_offset & 0xF) + ((2 + (i_chipUnitNum % 2)) << 4);
                    l_scom.set_sat_offset(l_satoff);
                }
                if(l_chiplet_id == MC01_CHIPLET_ID || l_chiplet_id == MC23_CHIPLET_ID)
                {
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
                uint8_t l_chipUnitNum = P9_SCOMINFO_OMI_UNSWIZZLE[i_chipUnitNum];
                l_scom.set_chiplet_id(MC01_CHIPLET_ID + (l_chipUnitNum / 8));
                if (P9A_MC_OMIC0_RING_ID <= l_ring && l_ring <= P9A_MC_OMIC2_RING_ID)
                {
                    l_scom.set_ring(P9A_MC_OMIC0_RING_ID + ((l_chipUnitNum % 8) / 3));
                    uint8_t l_lane = l_scom.get_lane_id();
                    l_lane = l_lane % 8;
                    uint8_t l_chipnum = l_chipUnitNum;

                    if (l_chipnum >= 8)
                    {
                        l_chipnum++;
                    }

                    l_scom.set_lane_id((l_chipnum % 3) * 8 + l_lane);
                }
                if (l_ring == P9A_MC_OMI_DL_RING_ID)
                {
                    l_scom.set_sat_id(P9A_MC_DL_REG0_SAT_ID + ((l_chipUnitNum % 8) / 3));
                    uint8_t l_satoff = l_sat_offset % 16;
                    uint8_t l_chipnum = l_chipUnitNum;

                    if (l_chipnum >= 8)
                    {
                        l_chipnum++;
                    }

                    l_scom.set_sat_offset(
                        (l_chipnum % 3) * 16
                        + P9A_MC_DL_OMI0_FRST_REG
                        + l_satoff);
                }
                break;
            case PU_NV_CHIPUNIT:
                if (i_mode == P9N_DD1_SI_MODE)
                {
                    l_scom.set_sat_id(l_scom.get_sat_id() % 4 + (i_chipUnitNum / 2) * 4);
                    l_scom.set_sat_offset(
                        l_scom.get_sat_offset() % 32
                        + 32 * (i_chipUnitNum % 2));
                }
                else if (i_mode != P9A_DD1_SI_MODE && i_mode != P9A_DD2_SI_MODE)
                {
                    uint64_t l_sa = i_scomAddr;
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

                    if (l_eo > 5 && i_chipUnitNum % 2 == 0)
                    {
                        l_sa -= 0x20ULL;
                    }

                    if (l_eo <= 5 && i_chipUnitNum % 2 == 1)
                    {
                        l_sa += 0x20ULL;
                    }
                    l_scom.set_addr(l_sa);
                }
                else
                {
                    l_scom.set_addr(FAILED_TRANSLATION);
                }
                break;
            case PU_PEC_CHIPUNIT:
                if (l_scom.get_chiplet_id() == N2_CHIPLET_ID)
                {
                    l_scom.set_ring( (N2_PCIS0_0_RING_ID + i_chipUnitNum) & 0xF);
                }
                else
                {
                    l_scom.set_chiplet_id(PCI0_CHIPLET_ID + i_chipUnitNum);
                }
                break;
            case PU_PHB_CHIPUNIT:
                if (l_scom.get_chiplet_id() == N2_CHIPLET_ID)
                {
                    if (i_chipUnitNum == 0)
                    {
                        l_scom.set_ring(N2_PCIS0_0_RING_ID & 0xF);
                        l_scom.set_sat_id((l_scom.get_sat_id() < 4) ? 1 : 4);
                    }
                    else
                    {
                        l_scom.set_ring( (N2_PCIS0_0_RING_ID + (i_chipUnitNum / 3) + 1) & 0xF);
                        l_scom.set_sat_id(
                            (l_scom.get_sat_id() < 4) ? 1 : 4
                            + (i_chipUnitNum % 2) ? 0 : 1
                            + 2 * (i_chipUnitNum / 5));
                    }
                }
                else
                {
                    if (i_chipUnitNum == 0)
                    {
                        l_scom.set_chiplet_id(PCI0_CHIPLET_ID);
                        l_scom.set_sat_id((l_scom.get_sat_id() < 4) ? 1 : 4);
                    }
                    else
                    {
                        l_scom.set_chiplet_id(PCI0_CHIPLET_ID + (i_chipUnitNum / 3) + 1);
                        l_scom.set_sat_id(
                            (l_scom.get_sat_id() < 4) ? 1 : 4
                            + (i_chipUnitNum % 2) ? 0 : 1
                            + 2 * (i_chipUnitNum / 5));
                    }
                }

                break;

            case PU_OBUS_CHIPUNIT:
                l_scom.set_chiplet_id(OB0_CHIPLET_ID + i_chipUnitNum);
                break;

            case PU_XBUS_CHIPUNIT:
                l_ring &= 0xF;
                if (XB_IOX_2_RING_ID >= l_ring
                && l_ring >= XB_IOX_0_RING_ID)
                {
                    l_scom.set_ring( (XB_IOX_0_RING_ID + i_chipUnitNum) & 0xF);
                }

                else if(XB_PBIOX_2_RING_ID >= l_ring
                && l_ring >= XB_PBIOX_0_RING_ID)
                {
                    l_scom.set_ring( (XB_PBIOX_0_RING_ID + i_chipUnitNum) & 0xF);
                }
                break;

            case PU_SBE_CHIPUNIT:
                l_scom.set_chiplet_id(i_chipUnitNum);
                break;

            case PU_PPE_CHIPUNIT:
                if(i_chipUnitNum == PPE_SBE_CHIPUNIT_NUM)
                {
                    l_scom.set_chiplet_id(PIB_CHIPLET_ID);
                    l_scom.set_port(SBE_PORT_ID);
                    l_scom.set_ring(PPE_SBE_RING_ID);
                    l_scom.set_sat_id(PPE_SBE_SAT_ID);
                    l_scom.set_sat_offset(0x0F & l_scom.get_sat_offset());
                    break;
                }

                if (l_scom.get_port() == SBE_PORT_ID)
                {
                    l_scom.set_sat_offset(l_scom.get_sat_offset() | 0x10);
                }

                if(i_chipUnitNum >= PPE_GPE0_CHIPUNIT_NUM
                && i_chipUnitNum <= PPE_GPE3_CHIPUNIT_NUM)
                {
                    l_scom.set_chiplet_id(PIB_CHIPLET_ID);
                    l_scom.set_port(GPE_PORT_ID);
                    l_scom.set_ring((i_chipUnitNum - PPE_GPE0_CHIPUNIT_NUM) * 8);
                    l_scom.set_sat_id(PPE_GPE_SAT_ID);
                }
                else if(i_chipUnitNum >= PPE_EQ0_CME0_CHIPUNIT_NUM
                && i_chipUnitNum <= PPE_EQ5_CME1_CHIPUNIT_NUM)
                {
                    if (i_chipUnitNum >= PPE_EQ0_CME1_CHIPUNIT_NUM)
                    {
                        l_scom.set_chiplet_id(EP00_CHIPLET_ID + (i_chipUnitNum % PPE_EQ0_CME1_CHIPUNIT_NUM));
                    }
                    else
                    {
                        l_scom.set_chiplet_id(EP00_CHIPLET_ID + (i_chipUnitNum % PPE_EQ0_CME0_CHIPUNIT_NUM));
                    }

                    l_scom.set_port(UNIT_PORT_ID);
                    l_scom.set_ring(((i_chipUnitNum / PPE_EQ0_CME1_CHIPUNIT_NUM) + 8) & 0xF);
                    l_scom.set_sat_id(PPE_CME_SAT_ID);
                }
                else if(i_chipUnitNum >= PPE_IO_XBUS_CHIPUNIT_NUM
                && i_chipUnitNum <= PPE_IO_OB3_CHIPUNIT_NUM)
                {
                    l_scom.set_chiplet_id(
                        XB_CHIPLET_ID
                        + i_chipUnitNum % PPE_IO_XBUS_CHIPUNIT_NUM
                        + (i_chipUnitNum / PPE_IO_OB0_CHIPUNIT_NUM) * 2);
                    l_scom.set_port(UNIT_PORT_ID);

                    if(i_chipUnitNum == PPE_IO_XBUS_CHIPUNIT_NUM)
                    {
                        l_scom.set_ring(XB_IOPPE_0_RING_ID & 0xF);
                    }
                    else
                    {
                        l_scom.set_ring(OB_PPE_RING_ID & 0xF);
                    }
                    l_scom.set_sat_id(OB_PPE_SAT_ID);
                }
                else if(i_chipUnitNum >= PPE_IO_DMI0_CHIPUNIT_NUM
                && i_chipUnitNum <= PPE_IO_DMI1_CHIPUNIT_NUM)
                {
                    l_scom.set_chiplet_id(MC01_CHIPLET_ID + (i_chipUnitNum - PPE_IO_DMI0_CHIPUNIT_NUM));
                    l_scom.set_ring(MC_IOM01_1_RING_ID);
                    l_scom.set_port(UNIT_PORT_ID);
                    l_scom.set_sat_id(P9C_MC_PPE_SAT_ID);
                }
                else if(i_chipUnitNum >= PPE_PB0_CHIPUNIT_NUM
                && i_chipUnitNum <= PPE_PB2_CHIPUNIT_NUM)
                {
                    l_scom.set_chiplet_id(N3_CHIPLET_ID);
                    l_scom.set_port(UNIT_PORT_ID);
                    l_scom.set_ring(N3_PB_3_RING_ID & 0xF);
                    l_scom.set_sat_id(PPE_PB_SAT_ID);
                }
                else
                {
                    l_scom.set_addr(FAILED_TRANSLATION);
                }
                break;

            case PU_NPU_CHIPUNIT:
                l_chiplet_id = (2 == i_chipUnitNum) ? N1_CHIPLET_ID : N3_CHIPLET_ID;
                l_scom.set_chiplet_id( l_chiplet_id );

                if(N3_NPU_0_RING_ID  == l_ring
                || N3_NPU_1_RING_ID  == l_ring
                || P9A_NPU_2_RING_ID == l_ring)
                {
                    if(N3_CHIPLET_ID == l_chiplet_id)
                    {
                        l_scom.set_ring(N3_NPU_0_RING_ID + i_chipUnitNum);
                    }
                    else if (N1_CHIPLET_ID == l_chiplet_id)
                    {
                        l_scom.set_ring(P9A_NPU_2_RING_ID);
                    }
                    else
                    {
                        l_scom.set_addr(FAILED_TRANSLATION);
                    }
                }
                else if(P9A_NPU_0_FIR_RING_ID == l_ring
                || P9A_NPU_2_FIR_RING_ID == l_ring)
                {
                    if(N3_CHIPLET_ID == l_chiplet_id)
                    {
                        l_scom.set_ring(P9A_NPU_0_FIR_RING_ID);
                        l_scom.set_sat_id((l_sat_id % 3) + (3 * i_chipUnitNum));
                    }
                    else if(N1_CHIPLET_ID == l_chiplet_id)
                    {
                        l_scom.set_ring( P9A_NPU_2_FIR_RING_ID );
                        l_scom.set_sat_id( l_sat_id % 3);
                    }
                    else
                    {
                        l_scom.set_addr(FAILED_TRANSLATION);
                    }
                }
                else
                {
                    l_scom.set_addr(FAILED_TRANSLATION);
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

    if((l_chiplet_id == N3_CHIPLET_ID || l_chiplet_id == N1_CHIPLET_ID)
    && l_port == UNIT_PORT_ID
    && l_ring == N3_MC01_0_RING_ID
    && (l_sat_id == P9_N3_MCS01_SAT_ID || l_sat_id == P9_N3_MCS23_SAT_ID))
    {
        if((i_low0 <= l_sat_offset && l_sat_offset <= 0x2B)
        || (i_low1 <= l_sat_offset && l_sat_offset <= 0x3B))
        {
            uint8_t l_off_nib0 = l_sat_offset >> 4;
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(
                p9_chipUnitPairing_t(
                    mcc_dmi,
                    ((l_chiplet_id == N3_CHIPLET_ID) ? 0 : 4)
                    + l_off_nib0 - 2 + l_sat_id));
        }
        else
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(
                p9_chipUnitPairing_t(
                    PU_MI_CHIPUNIT,
                    (l_sat_id / 2) + ((l_chiplet_id == N3_CHIPLET_ID) ? 0 : 2)));
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
                    ((l_chiplet_id - PPE_EP00_CHIPLET_ID) * 2) + (l_port - 1)));

        }

    }
    else if (l_scom.is_unicast())
    {
        if (l_port == GPREG_PORT_ID
        || (l_port >= CME_PORT_ID && l_port <= CPM_PORT_ID)
        || l_port == PCBSLV_PORT_ID
        || (l_port == UNIT_PORT_ID && l_ring == EC_PSCM_RING_ID)
        || (l_port == UNIT_PORT_ID && l_ring == EC_PERV_RING_ID && l_sat_id == PERV_DBG_SAT_ID))
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
                        if ((l_ind_reg & 0x100) == 0)
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

                if(P9A_MC_OMIC0_PPE_RING_ID <= l_ring
                && l_ring <= P9A_MC_OMIC2_PPE_RING_ID
                && l_port == UNIT_PORT_ID)
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

        if(l_chiplet_id >= PCI0_CHIPLET_ID
        && l_chiplet_id <= PCI2_CHIPLET_ID
        && l_port == UNIT_PORT_ID
        && l_ring == PCI_PE_0_RING_ID
        && ((l_sat_id >= 1 && l_sat_id <= (l_chiplet_id - PCI0_CHIPLET_ID + 1))
          || (l_sat_id >= 4 && l_sat_id <= (l_chiplet_id - PCI0_CHIPLET_ID + 4))))
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(
                p9_chipUnitPairing_t(
                    PU_PHB_CHIPUNIT,
                    ((l_chiplet_id != PCI0_CHIPLET_ID)
                    ? ((l_chiplet_id - PCI0_CHIPLET_ID) * 2) - 1
                    : 0)
                    + l_sat_id
                    - ((l_sat_id >= 4) ? 4 : 1)));
        }

        if (((l_chiplet_id >= OB0_CHIPLET_ID) && (l_chiplet_id <= OB3_CHIPLET_ID)))
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_OBUS_CHIPUNIT, (l_chiplet_id - OB0_CHIPLET_ID)));
        }

        if(l_chiplet_id == XB_CHIPLET_ID
        && l_port == UNIT_PORT_ID
        && ((l_ring >= XB_IOX_0_RING_ID && l_ring <= XB_IOX_2_RING_ID && l_sat_id == XB_IOF_SAT_ID)
        || (l_ring >= XB_PBIOX_0_RING_ID && l_ring <= XB_PBIOX_2_RING_ID && l_sat_id == XB_PB_SAT_ID)))
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_XBUS_CHIPUNIT, l_ring % 3));
        }

        if(l_port == SBE_PORT_ID
        && l_chiplet_id == PIB_CHIPLET_ID
        && l_ring == PPE_SBE_RING_ID
        && l_sat_id == PPE_SBE_SAT_ID)
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_SBE_CHIPUNIT, l_chiplet_id));
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_PPE_CHIPUNIT, l_chiplet_id));
        }

        if(l_port == GPE_PORT_ID
        && l_chiplet_id == PIB_CHIPLET_ID
        && l_sat_id == PPE_GPE_SAT_ID
        && (l_ring == PPE_GPE0_RING_ID
          || l_ring == PPE_GPE1_RING_ID
          || l_ring == PPE_GPE2_RING_ID
          || l_ring == PPE_GPE3_RING_ID))
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_PPE_CHIPUNIT, PPE_GPE0_CHIPUNIT_NUM + (l_ring / 8)));
        }

        if(l_port == UNIT_PORT_ID
        && l_chiplet_id >= EP00_CHIPLET_ID
        && l_chiplet_id <= EP05_CHIPLET_ID
        && (l_ring == EQ_CME_0_RING_ID || l_ring == EQ_CME_1_RING_ID)
        && l_sat_id == PPE_CME_SAT_ID)
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(
                p9_chipUnitPairing_t(
                    PU_PPE_CHIPUNIT,
                    (l_chiplet_id - EP00_CHIPLET_ID) + PPE_EQ0_CME0_CHIPUNIT_NUM + ((l_ring % 8) * 10)));
        }

        if(l_port == UNIT_PORT_ID
        && l_chiplet_id == N3_CHIPLET_ID
        && l_ring == N3_PB_3_RING_ID
        && l_sat_id == PPE_PB_SAT_ID)
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_PPE_CHIPUNIT, PPE_PB0_CHIPUNIT_NUM));
        }

        if(l_port == UNIT_PORT_ID
        && l_chiplet_id == XB_CHIPLET_ID
        && l_ring == XB_IOPPE_0_RING_ID
        && l_sat_id == XB_PPE_SAT_ID)
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_PPE_CHIPUNIT, PPE_IO_XBUS_CHIPUNIT_NUM));
        }

        if ( (l_port == UNIT_PORT_ID) &&
                ((l_chiplet_id >= OB0_CHIPLET_ID) && (l_chiplet_id <= OB3_CHIPLET_ID)) &&
                (l_ring == OB_PPE_RING_ID) &&
                (l_sat_id == OB_PPE_SAT_ID) )
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_PPE_CHIPUNIT, (l_chiplet_id - OB0_CHIPLET_ID) + PPE_IO_OB0_CHIPUNIT_NUM));
        }

        if(l_port == UNIT_PORT_ID
        && l_chiplet_id == N3_CHIPLET_ID
        && N3_NPU_0_RING_ID <= l_ring
        && l_ring <= N3_NPU_1_RING_ID)
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_NPU_CHIPUNIT, l_ring - N3_NPU_0_RING_ID));
        }

        if(l_port == UNIT_PORT_ID
        && l_chiplet_id == N3_CHIPLET_ID
        && l_ring == P9A_NPU_0_FIR_RING_ID)
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_NPU_CHIPUNIT, l_sat_id / 3));
        }

        if(l_port == UNIT_PORT_ID
        && l_chiplet_id == N1_CHIPLET_ID
        && (l_ring == P9A_NPU_2_RING_ID || l_ring == P9A_NPU_2_FIR_RING_ID))
        {
            o_chipUnitRelated = true;
            o_chipUnitPairing.push_back(p9_chipUnitPairing_t(PU_NPU_CHIPUNIT, 2));
        }
    }
    return !l_scom.is_valid();
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
        uint64_t l_scomAddr = C_CORE_ACTION0;
        bool l_needsWakeup = false;

        l_errl = SCOM::scomTranslate( l_core, l_scomAddr, l_needsWakeup );
        if( l_errl )
        {
            break;
        }

        stopImageSection::StopReturnCode_t l_srErrl =
            p9_stop_save_scom( (void *)l_homerAddr,
            l_scomAddr, l_action0,
            stopImageSection::P9_STOP_SCOM_REPLACE,
            stopImageSection::P9_STOP_SECTION_CORE_SCOM );

        if( l_srErrl != stopImageSection::StopReturnCode_t::STOP_SAVE_SUCCESS )
            break;

        l_scomAddr = C_CORE_ACTION1;

        l_errl = SCOM::scomTranslate(l_core, l_scomAddr, l_needsWakeup);
        if( l_errl )
            break;

        l_srErrl = p9_stop_save_scom( (void *)l_homerAddr,
                            l_scomAddr, l_action1,
                            stopImageSection::P9_STOP_SCOM_REPLACE,
                            stopImageSection::P9_STOP_SECTION_CORE_SCOM );
        if( l_srErrl != stopImageSection::StopReturnCode_t::STOP_SAVE_SUCCESS )
            break;
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

errlHndl_t loadAndStartPMAll(loadPmMode i_mode = HBPM::PM_LOAD)
{
    errlHndl_t l_errl = nullptr;

    TARGETING::Target * l_sys = nullptr;
    TARGETING::targetService().getTopLevelTarget( l_sys );
    assert(l_sys != nullptr);

    TargetHandleList l_procChips;
    getAllChips(l_procChips, TYPE_PROC, true);

    uint64_t l_homerPhysAddr = 0x0;
    uint64_t l_commonPhysAddr = 0x0;

    for (const auto & l_procChip: l_procChips)
    {
        // This attr was set during istep15 HCODE build
        l_homerPhysAddr = l_procChip->getAttr<TARGETING::ATTR_HOMER_PHYS_ADDR>();
        l_commonPhysAddr = l_sys->getAttr<TARGETING::ATTR_OCC_COMMON_AREA_PHYS_ADDR>();

        loadPMComplex( // common, but control flow differs
            l_procChip,
            l_homerPhysAddr,
            l_commonPhysAddr,
            i_mode);
        startPMComplex(l_procChip);
    }

    core_checkstop_helper_homer();
}

void startPMComplex(Target* i_target)
{
    uint64_t l_homerPhysAddr = i_target->getAttr<TARGETING::ATTR_HOMER_PHYS_ADDR>();
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_fapiTarg(i_target);
    pm_init(l_fapiTarg, (void *)l_homerPhysAddr);
}

/** Offset from HOMER to OCC Image */
#define HOMER_OFFSET_TO_OCC_IMG (0*KILOBYTE)
/** Offset from HOMER to OCC Host Data Area */
#define HOMER_OFFSET_TO_OCC_HOST_DATA (768*KILOBYTE)

static void loadPMComplex(
    TARGETING::Target * i_target,
    uint64_t i_homerPhysAddr,
    uint64_t i_commonPhysAddr)
{
    resetPMComplex(i_target);

    uint64_t l_occImgPaddr = i_homerPhysAddr + HOMER_OFFSET_TO_OCC_IMG;
    loadOCCSetup(i_target, l_occImgPaddr, i_commonPhysAddr);
    loadOCCImageToHomer(i_target, l_occImgPaddr);
    loadHostDataToHomer(i_target, l_occImgPaddr + HOMER_OFFSET_TO_OCC_HOST_DATA);
    loadHcode(i_target, (void *)i_homerPhysAddr, HBPM::PM_LOAD);
}

/**
 * @brief Host config data consumed by OCC
 */
struct occHostConfigDataArea_t
{
    uint32_t version;

    //For computation of timebase frequency
    uint32_t nestFrequency;

    // For determining the interrupt type to Host
    //  0x00000000 = Use FSI2HOST Mailbox
    //  0x00000001 = Use OCC interrupt line through PSIHB complex
    uint32_t interruptType;

    // For informing OCC if it is the FIR master:
    //  0x00000000 = Default
    //  0x00000001 = FIR Master
    uint32_t firMaster;

    // FIR collection configuration data needed by FIR Master
    //  OCC in the event of a checkstop
    uint8_t firdataConfig[3072];

    // For informing OCC if SMF mode is enabled:
    //  0x00000000 = Default (SMF disabled)
    //  0x00000001 = SMF mode is enabled
    uint32_t smfMode;
};

// the one from src/usr/isteps/pm/pm_common.H
OccHostDataVersion = 0x00000090,

USE_PSIHB_COMPLEX = 0x00000001,

/**
 * @brief Sets up OCC Host data in Homer
 */
void loadHostDataToHomer(TARGETING::Target* i_proc,
                         void* i_occHostDataVirtAddr /*destination*/)
{
    //Treat virtual address as starting pointer
    //for config struct
    occHostConfigDataArea_t * l_config_data =
        reinterpret_cast<occHostConfigDataArea_t *>
        (i_occHostDataVirtAddr);

    // Get top level system target
    TARGETING::TargetService & tS = TARGETING::targetService();
    TARGETING::Target * sysTarget = nullptr;
    tS.getTopLevelTarget( sysTarget );
    assert( sysTarget != nullptr );

    l_config_data->version = OccHostDataVersion;
    l_config_data->nestFrequency = sysTarget->getAttr<ATTR_FREQ_PB_MHZ>();

    l_config_data->interruptType = USE_PSIHB_COMPLEX;
    l_config_data->firMaster = false;
    l_config_data->smfMode = false;
}

/**
 * @brief Sets up Hcode in Homer
 */
errlHndl_t loadHcode( TARGETING::Target* i_target,
                      void* i_pImageOut,
                      loadPmMode i_mode = PM_LOAD)
{
    errlHndl_t l_errl = nullptr;

    // cast OUR type of target to a FAPI type of target.
    // figure out homer offsets
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_fapiTarg(i_target);

    void *l_buffer1 = (void*)malloc(HW_IMG_RING_SIZE);
    void *l_buffer2 = (void*)malloc(MAX_RING_BUF_SIZE);
    void *l_buffer3 = (void*)malloc(MAX_RING_BUF_SIZE);
    void *l_buffer4 = (void*)malloc(MAX_RING_BUF_SIZE);

    do
    {
        if(g_pHcodeLidMgr.get() == nullptr)
        {
            g_pHcodeLidMgr = std::shared_ptr<UtilLidMgr>
                             (new UtilLidMgr(Util::NIMBUS_HCODE_LIDID));
        }
        void* l_pImageIn = nullptr;
        size_t l_lidImageSize = 0;

        l_errl = g_pHcodeLidMgr->getStoredLidImage(l_pImageIn,
                                                   l_lidImageSize);
        if (l_errl)
            break;

        // The ref image may still include the 4K header which the HWP is
        // not expecting, so move the image pointer past the header
        if( *(reinterpret_cast<uint64_t*>(l_pImageIn)) == VER_EYECATCH)
        {
             l_pImageIn = reinterpret_cast<void*>
                (reinterpret_cast<uint8_t*>(l_pImageIn) + PAGESIZE);
        }

        // Pull build information from XIP header and trace it
        Util::imageBuild_t l_imageBuild;
        Util::pullTraceBuildInfo(l_pImageIn,
                                 l_imageBuild,
                                 ISTEPS_TRACE::g_trac_isteps_trace);

        ImageType_t l_imgType;

        // Check if we have a valid ring override section and include it in if so
        void* l_ringOverrides = nullptr;
        l_errl = HBPM::getRingOvd(l_ringOverrides);
        if (l_errl)
        {
		// this isn't a fatal error
        }

	l_errl = p9_hcode_image_build( l_fapiTarg,
                         l_pImageIn, //reference image
                         i_pImageOut, //homer image buffer
                         l_ringOverrides,
                         PHASE_IPL,
                         l_imgType,
                         l_buffer1,
                         HW_IMG_RING_SIZE,
                         l_buffer2,
                         MAX_RING_BUF_SIZE,
                         l_buffer3,
                         MAX_RING_BUF_SIZE,
                         l_buffer4,
                         MAX_RING_BUF_SIZE);

        if (l_errl)
            break;
    } while(0);

    free(l_buffer1);
    free(l_buffer2);
    free(l_buffer3);
    free(l_buffer4);

    return l_errl;
} // loadHcode

errlHndl_t resetPMComplex(TARGETING::Target * i_target)
{
    uint64_t l_homerPhysAddr = i_target->getAttr<TARGETING::ATTR_HOMER_PHYS_ADDR>();
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>l_fapiTarg(i_target);
    ATTR_HB_INITIATED_PM_RESET_type l_chipResetState = i_target->getAttr<TARGETING::ATTR_HB_INITIATED_PM_RESET>();
    if (HB_INITIATED_PM_RESET_COMPLETE == l_chipResetState)
    {
        i_target->setAttr<ATTR_HB_INITIATED_PM_RESET>(HB_INITIATED_PM_RESET_INACTIVE);
        return;
    }

    p9_pm_reset(l_fapiTarg, (void*)l_homerPhysAddr);
}

uint64_t PHYSICAL_ADDR_MASK = 0x7FFFFFFFFFFFFFFF;

/**
 * @brief Execute procedures and steps required to setup for loading
 *        the OCC image in a specified processor
 */
errlHndl_t loadOCCSetup(
    TARGETING::Target* i_target,
    uint64_t i_occImgPaddr,
    uint64_t i_commonPhysAddr)
{
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_fapiTarg(i_target);

    uint64_t l_occ_addr = i_occImgPaddr & PHYSICAL_ADDR_MASK;
    p9_pm_pba_bar_config(l_fapiTarg, 0, l_occ_addr); // analyzed

    TARGETING::Target* sys = nullptr;
    TARGETING::targetService().getTopLevelTarget(sys);
    sys->setAttr<ATTR_OCC_COMMON_AREA_PHYS_ADDR>(i_commonPhysAddr);

    uint64_t l_common_addr = i_commonPhysAddr & PHYSICAL_ADDR_MASK;
    p9_pm_pba_bar_config(l_fapiTarg, 2, l_common_addr); // analyzed
}

REG64(PU_PBABAR0   , RULL(0x05012B00), SH_UNT, SH_ACS_SCOM_RW);
REG64(PU_PBABAR1   , RULL(0x05012B01), SH_UNT, SH_ACS_SCOM_RW);
REG64(PU_PBABAR2   , RULL(0x05012B02), SH_UNT, SH_ACS_SCOM_RW);
REG64(PU_PBABAR3   , RULL(0x05012B03), SH_UNT, SH_ACS_SCOM_RW);

REG64(PU_PBABARMSK0, RULL(0x05012B04), SH_UNT, SH_ACS_SCOM_RW);
REG64(PU_PBABARMSK1, RULL(0x05012B05), SH_UNT, SH_ACS_SCOM_RW);
REG64(PU_PBABARMSK2, RULL(0x05012B06), SH_UNT, SH_ACS_SCOM_RW);
REG64(PU_PBABARMSK3, RULL(0x05012B07), SH_UNT, SH_ACS_SCOM_RW);

const uint64_t PBA_BARs[4] =
{
    PU_PBABAR0,
    PU_PBABAR1,
    PU_PBABAR2,
    PU_PBABAR3
};

const uint64_t PBA_BARMSKs[4] =
{
    PU_PBABARMSK0,
    PU_PBABARMSK1,
    PU_PBABARMSK2,
    PU_PBABARMSK3
};

static void p9_pm_pba_bar_config (
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const uint32_t i_index,
    const uint64_t i_pba_bar_addr)
{
    uint64_t l_bar64 = i_pba_bar_addr;
    l_bar64.insertFromRight<0, 3>(0);
    fapi2::putScom(i_target, PBA_BARs[i_index], l_bar64);
    fapi2::putScom(i_target, PBA_BARMSKs[i_index], 0x300000);
}

/**
 * @brief Execute procedures and steps required to load
 *        OCC image in a specified processor
 */
errlHndl_t loadOCCImageToHomer(TARGETING::Target* i_target,
                                uint64_t i_occImgVaddr // dest)
{
    if(g_pOccLidMgr.get() == nullptr)
    {
        g_pOccLidMgr = std::shared_ptr<UtilLidMgr>
                        (new UtilLidMgr(Util::OCC_LIDID));
    }
    void* l_pLidImage = nullptr;
    size_t l_lidImageSize = 0;

    // Image is "OCC" partition of PNOR
    g_pOccLidMgr->getStoredLidImage(l_pLidImage, l_lidImageSize);

    void* l_occVirt = reinterpret_cast<void *>(i_occImgVaddr);
    memcpy(l_occVirt, l_pLidImage, l_lidImageSize);
}

/**
 * @brief enumerates all platforms which request special wakeup.
 */
enum PROC_SPCWKUP_ENTITY
{
    OTR = 0,
    FSP = 1,
    OCC = 2,
    HYP = 3,
    HOST = HYP,
    SPW_ALL
};

/**
 * @brief enumerates types of special wakeup.
 */
enum PROC_SPCWKUP_TYPE
{
    SPW_CORE    = 0,
    SPW_EQ      = 1,
    SPW_EX      = 2
};

REG64( C_PPM_SPWKUP_OTR                                        , RULL(0x200F010A), SH_UNT_C        , SH_ACS_SCOM_RW   );
REG64( EQ_PPM_SPWKUP_OTR                                       , RULL(0x100F010A), SH_UNT_EQ       , SH_ACS_SCOM_RW   );
REG64( C_PPM_SPWKUP_FSP                                        , RULL(0x200F010B), SH_UNT_C        , SH_ACS_SCOM_RW   );
REG64( EQ_PPM_SPWKUP_FSP                                       , RULL(0x100F010B), SH_UNT_EQ       , SH_ACS_SCOM_RW   );


static const uint32_t NUM_ENTITIES = 4;
static const uint32_t NUM_CHIPLET_TYPES = 2;
static const uint64_t SPCWKUP_ADDR[NUM_ENTITIES][NUM_CHIPLET_TYPES] =
{
    {C_PPM_SPWKUP_OTR, EQ_PPM_SPWKUP_OTR},
    {C_PPM_SPWKUP_FSP, EQ_PPM_SPWKUP_FSP},
    {C_PPM_SPWKUP_OCC, EQ_PPM_SPWKUP_OCC},
    {C_PPM_SPWKUP_HYP, EQ_PPM_SPWKUP_HYP}
};

fapi2::ReturnCode pm_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    void* i_pHomerImage)
{
    fapi2::buffer<uint64_t> l_data64       = 0;

    pm_corequad_init(i_target);
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

    p9_check_proc_config(i_target, i_pHomerImage);
    clear_occ_special_wakeups(i_target);
    special_wakeup_all(i_target, false);
    p9_pm_occ_control(
        i_target,
        p9occ_ctrl::PPC405_START,// Operation on PPC405
        p9occ_ctrl::PPC405_BOOT_MEM, // PPC405 boot location
        0); //Jump to 405 main instruction - not used here

    l_data64.flush<0>().setBit<p9hcd::STOP_RECOVERY_TRIGGER_ENABLE>();
    fapi2::putScom(i_target, PU_OCB_OCI_OCCFLG2_CLEAR, l_data64);
}

EQ_QPPM_ERR = 0x100F0121
EQ_QPPM_ERRMSK = 0x100F0122

C_CPPM_CMEDB0_CLEAR = 0x200F0191
C_CPPM_CMEDB1_CLEAR = 0x200F0195
C_CPPM_CMEDB2_CLEAR = 0x200F0199
C_CPPM_CMEDB3_CLEAR = 0x200F019D

const uint64_t CME_DOORBELL_CLEAR[4] = {C_CPPM_CMEDB0_CLEAR,
					C_CPPM_CMEDB1_CLEAR,
					C_CPPM_CMEDB2_CLEAR,
					C_CPPM_CMEDB3_CLEAR
				       };

const uint8_t DOORBELLS_COUNT = 4;

CPPM_CSAR_FIT_HCODE_ERROR_INJECT                = 27,
CPPM_CSAR_ENABLE_PSTATE_REGISTRATION_INTERLOCK  = 28,
CPPM_CSAR_DISABLE_CME_NACK_ON_PROLONGED_DROOP   = 29,
CPPM_CSAR_PSTATE_HCODE_ERROR_INJECT             = 30,
CPPM_CSAR_STOP_HCODE_ERROR_INJECT               = 31

fapi2::ReturnCode pm_corequad_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    fapi2::buffer<uint64_t> l_data64;

    uint64_t l_address = 0;
    uint32_t l_errMask = 0;

    auto l_eqChiplets = i_target.getChildren<fapi2::TARGET_TYPE_EQ>
                        (fapi2::TARGET_STATE_FUNCTIONAL);

    // For each functional EQ chiplet
    for (auto l_quad_chplt : l_eqChiplets)
    {
        // Fetch the position of the EQ target
        fapi2::ATTR_CHIP_UNIT_POS_Type l_chpltNumber = 0;
        FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, l_quad_chplt, l_chpltNumber);

        // Setup the Quad PPM Mode Register
        // Clear the following bits:
        // 0          : Force FSAFE
        // 1  - 11    : FSAFE
        // 12         : Enable FSAFE on heartbeat loss
        // 13         : Enable DROOP protect upon heartbeat loss
        // 14         : Enable PFETs upon iVRMs dropout
        // 18 - 19    : PCB interrupt
        // 20,22,24,26: InterPPM Ivrm/Aclk/Vdata/Dpll enable
        FAPI_INF("Clear Quad PPM Mode register");

        l_data64.flush<0>()
                .setBit<EQ_QPPM_QPMMR_FORCE_FSAFE>()
                .setBit<EQ_QPPM_QPMMR_FSAFE, EQ_QPPM_QPMMR_FSAFE_LEN>()
                .setBit<EQ_QPPM_QPMMR_ENABLE_FSAFE_UPON_HEARTBEAT_LOSS>()
                .setBit<EQ_QPPM_QPMMR_ENABLE_DROOP_PROTECT_UPON_HEARTBEAT_LOSS>()
                .setBit< EQ_QPPM_QPMMR_ENABLE_PFETS_UPON_IVRM_DROPOUT>()
                .setBit< EQ_QPPM_QPMMR_ENABLE_PCB_INTR_UPON_HEARTBEAT_LOSS>()
                .setBit< EQ_QPPM_QPMMR_ENABLE_PCB_INTR_UPON_IVRM_DROPOUT>()
                .setBit< EQ_QPPM_QPMMR_ENABLE_PCB_INTR_UPON_LARGE_DROOP>()
                .setBit< EQ_QPPM_QPMMR_ENABLE_PCB_INTR_UPON_EXTREME_DROOP>()
        // @todo RTC 179958 IVRM enablement - only based on ATTR_SYSTEM_IVRM_DISABLE
        //      .setBit<EQ_QPPM_QPMMR_CME_INTERPPM_IVRM_ENABLE>()
                .setBit<EQ_QPPM_QPMMR_CME_INTERPPM_ACLK_ENABLE>()
                .setBit<EQ_QPPM_QPMMR_CME_INTERPPM_VDATA_ENABLE>()
                .setBit<EQ_QPPM_QPMMR_CME_INTERPPM_DPLL_ENABLE>();

        l_address = EQ_QPPM_QPMMR_CLEAR;
        fapi2::putScom(l_quad_chplt, l_address, l_data64));

        // "Clear QUAD PPM ERROR Register");
        l_data64.flush<0>();
        l_address = EQ_QPPM_ERR;
        fapi2::putScom(l_quad_chplt, l_address, l_data64);

        // Restore Quad PPM Error Mask
        FAPI_ATTR_GET(fapi2::ATTR_QUAD_PPM_ERRMASK, l_quad_chplt, l_errMask));
        l_data64.flush<0>().insertFromRight<0, 32>(l_errMask);
        l_address = EQ_QPPM_ERRMSK;
        fapi2::putScom(l_quad_chplt, l_address, l_data64);

        auto l_coreChiplets =
            l_quad_chplt.getChildren<fapi2::TARGET_TYPE_CORE>
            (fapi2::TARGET_STATE_FUNCTIONAL);

        // For each core target
        for (auto l_core_chplt : l_coreChiplets)
        {
            // Fetch the position of the Core target
            FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, l_core_chplt, l_chpltNumber));

            // Clear the Core PPM CME DoorBells
            l_data64.flush<1>();

            for (uint8_t l_dbLoop = 0; l_dbLoop < DOORBELLS_COUNT; l_dbLoop++)
            {
                l_address = CME_DOORBELL_CLEAR[l_dbLoop];
                fapi2::putScom(l_core_chplt, l_address, l_data64);
            }

            // Setup Core PPM Mode register
            // Clear the following bits:
            // 1      : PPM Write control override
            // 11     : Block interrupts
            // 12     : PPM response for CME error
            // 14     : enable pece
            // 15     : cme spwu done dis

            // Other bits are Init or Reset by STOP Hcode and, thus, not touched
            // here
            // 0      : PPM Write control
            // 9      : FUSED_CORE_MODE
            // 10     : STOP_EXIT_TYPE_SEL
            // 13     : WKUP_NOTIFY_SELECT

            // Clearing Core PPM Mode register ...");
            l_data64.flush<0>()
                    .setBit<EX_CPPM_CPMMR_PPM_WRITE_OVERRIDE>()
                    .setBit<EX_CPPM_CPMMR_BLOCK_INTR_INPUTS>()
                    .setBit<EX_CPPM_CPMMR_CME_ERR_NOTIFY_DIS>()
                    .setBit<EX_CPPM_CPMMR_ENABLE_PECE>()
                    .setBit<EX_CPPM_CPMMR_CME_SPECIAL_WKUP_DONE_DIS>();
            l_address = C_CPPM_CPMMR_CLEAR;
            fapi2::putScom(l_core_chplt, l_address, l_data64);

            // Clear Core PPM Errors
            l_data64.flush<0>();
            l_address = C_CPPM_ERR;
            fapi2::putScom(l_core_chplt, l_address, l_data64);

            // Clearing Hcode Error Injection and other CSAR settings ...");
            l_data64.flush<0>()
                    .setBit<p9hcd::CPPM_CSAR_FIT_HCODE_ERROR_INJECT>()
                    .setBit<p9hcd::CPPM_CSAR_ENABLE_PSTATE_REGISTRATION_INTERLOCK>()
                    .setBit<p9hcd::CPPM_CSAR_PSTATE_HCODE_ERROR_INJECT>()
                    .setBit<p9hcd::CPPM_CSAR_STOP_HCODE_ERROR_INJECT>();
            // Note:  CPPM_CSAR_DISABLE_CME_NACK_ON_PROLONGED_DROOP is NOT
            //        cleared as this is a persistent, characterization setting
            l_address =  C_CPPM_CSAR_CLEAR;
            fapi2::putScom(l_core_chplt, l_address, l_data64));

            // Restore CORE PPM Error Mask
            FAPI_ATTR_GET(fapi2::ATTR_CORE_PPM_ERRMASK, l_core_chplt, l_errMask));
            l_data64.flush<0>().insertFromRight<0, 32>(l_errMask);
            l_address = C_CPPM_ERRMSK;
            fapi2::putScom(l_core_chplt, l_address, l_data64);
        }
    }

fapi_try_exit:
    return fapi2::current_err;
}

PU_SRAM_SRBV0_SCOM = 0x0006A004
PU_SRAM_SRBV1_SCOM = 0x0006A005
PU_SRAM_SRBV2_SCOM = 0x0006A006
PU_SRAM_SRBV3_SCOM = 0x0006A007

PU_JTG_PIB_OJCFG_AND = 0x0006D005
PU_OCB_PIB_OCR_CLEAR = 0x0006D001
PU_OCB_PIB_OCR_OR    = 0x0006D002

// OCR Register Bits
static const uint32_t OCB_PIB_OCR_CORE_RESET_BIT = 0;

// OCC JTAG Register Bits
static const uint32_t JTG_PIB_OJCFG_DBG_HALT_BIT = 6;

fapi2::ReturnCode p9_pm_occ_control
(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
 const p9occ_ctrl::PPC_CONTROL i_ppc405_reset_ctrl = p9occ_ctrl::PPC405_START,
 const p9occ_ctrl::PPC_BOOT_CONTROL i_ppc405_boot_ctrl = p9occ_ctrl::PPC405_BOOT_MEM,
 const uint64_t i_ppc405_jump_to_main_instr)
{
    fapi2::buffer<uint64_t> l_data64;

    // Set up Boot Vector Registers in SRAM
    //    - set bv0-2 to all 0's (illegal instructions)
    //    - set bv3 to proper branch instruction
    fapi2::putScom(i_target, PU_SRAM_SRBV0_SCOM, l_data64);
    fapi2::putScom(i_target, PU_SRAM_SRBV1_SCOM, l_data64);
    fapi2::putScom(i_target, PU_SRAM_SRBV2_SCOM, l_data64);
    bootMemory(i_target, l_data64);
    fapi2::putScom(i_target, PU_SRAM_SRBV3_SCOM, l_data64);

    // Clear the halt bit
    fapi2::putScom(i_target, PU_JTG_PIB_OJCFG_AND, ~BIT(JTG_PIB_OJCFG_DBG_HALT_BIT));
    // Set the reset bit
    fapi2::putScom(i_target, PU_OCB_PIB_OCR_OR, BIT(OCB_PIB_OCR_CORE_RESET_BIT));
    // Clear the reset bit
    fapi2::putScom(i_target, PU_OCB_PIB_OCR_CLEAR, BIT(OCB_PIB_OCR_CORE_RESET_BIT));
}

OCC_BOOT_OFFSET = 0x40
CTR = 9
OCC_SRAM_BOOT_ADDR = 0xFFF40000
OCC_SRAM_BOOT_ADDR2 = 0xFFF40002

///
/// @brief Creates and loads the OCC memory boot launcher
/// @param[in] i_target  Chip target
/// @param[in] i_data64  32 bit instruction representing the branch
///                    instruction to the SRAM boot loader
/// @return FAPI2_RC_SUCCESS on success, else error
///
fapi2::ReturnCode bootMemory(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    fapi2::buffer<uint64_t>& i_data64)
{
    static const uint32_t SRAM_PROGRAM_SIZE = 2;  // in double words
    uint64_t l_sram_program[SRAM_PROGRAM_SIZE];
    fapi2::ReturnCode l_rc;
    uint32_t l_ocb_length_act = 0;

    // Setup use OCB channel 1 for placing instruction in SRAM
    // Channel will be returned to Linear Stream, Circular upon exit
    l_rc = p9_pm_ocb_indir_setup_linear(i_target,
                  p9ocb::OCB_CHAN1,
                  p9ocb::OCB_TYPE_LINSTR,
                  OCC_SRAM_BOOT_ADDR);   // Bar
    FAPI_TRY(l_rc);

    // lis r1, 0x8000
    l_sram_program[0] = ((uint64_t)ppc_lis(1, 0x8000) << 32);

    // ori r1, r1, OCC_BOOT_OFFSET
    l_sram_program[0] |= (ppc_ori(1, 1, OCC_BOOT_OFFSET));

    // mtctr (mtspr r1, CTR )
    l_sram_program[1] = ((uint64_t)ppc_mtspr(1, CTR) << 32);

    // bctr
    l_sram_program[1] |= ppc_bctr();

    // Write to SRAM
    l_rc = p9_pm_ocb_indir_access(i_target,
                  p9ocb::OCB_CHAN1,
                  p9ocb::OCB_PUT,
                  SRAM_PROGRAM_SIZE,
                  false,
                  0,
                  l_ocb_length_act,
                  l_sram_program);

    // b OCC_SRAM_BOOT_ADDR2
    i_data64.insertFromRight<0, 32>(ppc_b(OCC_SRAM_BOOT_ADDR2));

fapi_try_exit:
    // Channel 1 returned to Linear Stream, Circular upon exit
    l_rc = p9_pm_ocb_indir_setup_circular(i_target,
                  p9ocb::OCB_CHAN1,
                  p9ocb::OCB_TYPE_CIRC,
                  0,   // Bar
                  0,   // Length
                  p9ocb::OCB_Q_OUFLOW_NULL,
                  p9ocb::OCB_Q_ITPTYPE_NULL);
}

fapi2::ReturnCode p9_pm_ocb_indir_setup_circular(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9ocb::PM_OCB_CHAN_NUM i_ocb_chan,
    const p9ocb::PM_OCB_CHAN_TYPE i_ocb_type,
    const uint32_t i_ocb_bar,
    const uint8_t i_ocb_q_len,
    const p9ocb::PM_OCB_CHAN_OUFLOW i_ocb_flow,
    const p9ocb::PM_OCB_ITPTYPE i_ocb_itp)
{
    return p9_pm_ocb_init(i_target,
                  p9pm::PM_SETUP_ALL,
                  i_ocb_chan,
                  i_ocb_type,
                  i_ocb_bar,
                  i_ocb_q_len,
                  i_ocb_flow,
                  i_ocb_itp);
    return l_rc;
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

/**
 * @brief   checks Memory buf configuration and updates config vector buffer.
 * @param[in]   i_procTgt           fapi2 target for P9
 * @param[in]   io_configVector     Unit Avaialability Vector
 * @param[in]   i_oBusStartPos      start bit position for OBUS
 * @param[in]   i_nvLinkPos         start bit position for NV Link
 * @return      fapi2 return code
 */
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

enum {
    INIT_CONFIG_VALUE       =       0x8000000C09800000ull,
    QPMR_PROC_CONFIG_POS    =       0xBFC18,
};

/**
 * @brief   bit position for various chiplets in config vector.
 */
enum {
    MCS_POS             =   1,
    MBA_POS             =   9,
    MEM_BUF_POS         =   17,
    XBUS_POS            =   25,
    PHB_POS             =   30,
    CAPP_POS            =   37,
    OBUS_POS            =   41,
    ABUS_POS            =   41,
    NVLINK_POS          =   45,
    OBUS_BRICK_0_POS    =   0,
    OBUS_BRICK_1_POS    =   1,
    OBUS_BRICK_2_POS    =   2,
    OBUS_BRICK_9_POS    =   9,
    OBUS_BRICK_10_POS   =   10,
    OBUS_BRICK_11_POS   =   11,
};

/// @brief    builds a STOP image using a reference image as input.
/// @param[in]   i_procTgt        fapi2 target for processor chip.
/// @param[in]   i_pHomerImage    pointer to the beginning of the HOMER image buffer
/// @return   fapi2 return code
fapi2::ReturnCode p9_check_proc_config ( CONST_FAPI2_PROC& i_procTgt, void* i_pHomerImage )
{
    uint64_t l_configVectVal = INIT_CONFIG_VALUE;
    uint8_t* pHomer = (uint8_t*)i_pHomerImage + QPMR_HOMER_OFFSET + QPMR_PROC_CONFIG_POS;

    checkChiplet<fapi2::TARGET_TYPE_MCS >( i_procTgt, fapi2::TARGET_TYPE_MCS, l_configVectVal, MCS_POS );
    checkChiplet<fapi2::TARGET_TYPE_XBUS>( i_procTgt, fapi2::TARGET_TYPE_XBUS, l_configVectVal, XBUS_POS );
    checkChiplet<fapi2::TARGET_TYPE_PHB>( i_procTgt, fapi2::TARGET_TYPE_PHB, l_configVectVal, PHB_POS );
    checkChiplet<fapi2::TARGET_TYPE_CAPP>( i_procTgt, fapi2::TARGET_TYPE_CAPP, l_configVectVal, CAPP_POS );

    checkObusChipletHierarchy(i_procTgt, l_configVectVal, OBUS_POS, NVLINK_POS);

    checkChiplet<fapi2::TARGET_TYPE_MCA>(i_procTgt, fapi2::TARGET_TYPE_MCA, l_configVectVal, MBA_POS);
    *(uint64_t*)pHomer = htobe64(l_configVectVal);
}
```
