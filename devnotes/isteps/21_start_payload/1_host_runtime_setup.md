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
* `#undef CONFIG_IPLTIME_CHECKSTOP_ANALYSIS`
* `#undef CONFIG_PNOR_TWO_SIDE_SUPPORT`
* `#undef CONFIG_ENABLE_CHECKSTOP_ANALYSIS`
* Not in MPIPL mode (`ATTR_IS_MPIPL_HB == false`).
* Simics is not running (`ATTR_PAYLOAD_KIND != PAYLOAD_KIND_NONE`).
* Payload isn't PHYP (`ATTR_PAYLOAD_KIND != PAYLOAD_KIND_PHYP`).
* Payload is sapphire (`ATTR_PAYLOAD_KIND == PAYLOAD_KIND_SAPPHIRE`).
* Loading a payload (`TARGETING::is_no_load() == false`, `ATTR_PAYLOAD_KIND != PAYLOAD_KIND_NONE`)
* `ATTR_PM_MALF_ALERT_ENABLE == ENUM_ATTR_PM_MALF_ALERT_ENABLE_FALSE`.
* `ATTR_PM_MALF_CYCLE == ENUM_ATTR_PM_MALF_CYCLE_INACTIVE`.
* Homer address is never `NULL`.
* Virtual addresses equal physical addresses.
* Not in manufacturing mode or golden-side boot.
* SMF is not enabled.

***

HDAT -- Host data area. Collection of attributes, device information, and other
derived information used by the payload to manage the computer system.

Data (stored in HDAT):

* NACA
* SPIRA

***

Control flow of OCC start process:

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
         3. Starts OCC
      6. `putScom(PU_OCB_OCI_OCCFLG2_CLEAR, STOP_RECOVERY_TRIGGER_ENABLE)` (needed?)

***

```cpp
errlHndl_t utilDisableTces(void)
{
    return Singleton<UtilTceMgr>::instance().disableTces();
};

inline bool spBaseServicesEnabled()
{
    bool spBaseServicesEnabled = false;
    TARGETING::Target * sys = NULL;
    TARGETING::targetService().getTopLevelTarget( sys );
    TARGETING::SpFunctions spfuncs;
    if( sys &&
        sys->tryGetAttr<TARGETING::ATTR_SP_FUNCTIONS>(spfuncs) &&
        spfuncs.baseServices )
    {
        spBaseServicesEnabled = true;
    }

    return spBaseServicesEnabled;
}

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

bool utilUseTcesForDmas(void)
{
    if(INITSERVICE::spBaseServicesEnabled())
    {
        TARGETING::TargetService& tS = TARGETING::targetService();
        TARGETING::Target* sys = nullptr;
        (void) tS.getTopLevelTarget( sys );
        return sys->getAttr<TARGETING::ATTR_USE_TCES_FOR_DMAS>();
    }
    return false;
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
    if (TCE::utilUseTcesForDmas())
    {
        TCE::utilClosePayloadTces();
        closeNonMasterTces();
    }
    RUNTIME::sendSBESystemConfig();

#ifdef CONFIG_PLDM
    if(INITSERVICE::spBaseServicesEnabled())
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
#ifdef CONFIG_HTMGT
        HTMGT::processOccStartStatus(/*i_startCompleted=*/true, NULL);
#else
	HBPM::verifyOccChkptAll();
#endif

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

    if (TCE::utilUseTcesForDmas())
    {
        TCE::utilDisableTces();
    }
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

    // Figure out the interrupt type
    if( INITSERVICE::spBaseServicesEnabled() )
    {
        l_config_data->interruptType = USE_FSI2HOST_MAILBOX;
    }
    else
    {
        l_config_data->interruptType = USE_PSIHB_COMPLEX;
    }

    l_config_data->firMaster = 0;
    l_config_data->smfMode = SMF_MODE_DISABLED;
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

#if defined(__HOSTBOOT_RUNTIME) && defined(CONFIG_NVDIMM)
    NVDIMM::notifyNvdimmProtectionChange(i_target, NVDIMM::OCC_INACTIVE);
#endif

    p9_pm_reset(l_fapiTarg, (void*)l_homerPhysAddr);

#ifdef __HOSTBOOT_RUNTIME
    if(HB_INITIATED_PM_RESET_IN_PROGRESS != l_chipResetState)
    {
        i_target->setAttr<ATTR_HB_INITIATED_PM_RESET>(HB_INITIATED_PM_RESET_IN_PROGRESS);
        Singleton<ATTN::Service>::instance().handleAttentions(i_target);
        i_target->setAttr<ATTR_HB_INITIATED_PM_RESET>(l_chipResetState);
    }
#endif
}

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

fapi2::ReturnCode pm_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    void* i_pHomerImage)
{
    fapi2::buffer<uint64_t> l_data64       = 0;
    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM;

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

/// @brief    builds a STOP image using a reference image as input.
/// @param[in]   i_procTgt        fapi2 target for processor chip.
/// @param[in]   i_pHomerImage    pointer to the beginning of the HOMER image buffer
/// @return   fapi2 return code
fapi2::ReturnCode p9_check_proc_config ( CONST_FAPI2_PROC& i_procTgt, void* i_pHomerImage )
{
    uint64_t l_configVectVal = INIT_CONFIG_VALUE;
    uint8_t* pHomer = (uint8_t*)i_pHomerImage + QPMR_HOMER_OFFSET + QPMR_PROC_CONFIG_POS;
    uint8_t l_chipName = 0;

    checkChiplet<fapi2::TARGET_TYPE_MCS >( i_procTgt, fapi2::TARGET_TYPE_MCS, l_configVectVal, MCS_POS );
    checkChiplet<fapi2::TARGET_TYPE_XBUS>( i_procTgt, fapi2::TARGET_TYPE_XBUS, l_configVectVal, XBUS_POS );
    checkChiplet<fapi2::TARGET_TYPE_PHB>( i_procTgt, fapi2::TARGET_TYPE_PHB, l_configVectVal, PHB_POS );
    checkChiplet<fapi2::TARGET_TYPE_CAPP>( i_procTgt, fapi2::TARGET_TYPE_CAPP, l_configVectVal, CAPP_POS );

    FAPI_ATTR_GET_PRIVILEGED(fapi2::ATTR_NAME, i_procTgt, l_chipName );

    checkObusChipletHierarchy(i_procTgt, l_configVectVal, OBUS_POS, NVLINK_POS);

    checkChiplet<fapi2::TARGET_TYPE_MCA>(i_procTgt, fapi2::TARGET_TYPE_MCA, l_configVectVal, MBA_POS);
    *(uint64_t*)pHomer = htobe64(l_configVectVal);
}
```
