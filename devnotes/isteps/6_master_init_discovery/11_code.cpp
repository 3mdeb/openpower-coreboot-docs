void* host_start_occ_xstop_handler( void *io_pArgs )
{
    ISTEP_ERROR::IStepError l_stepError;
    errlHndl_t l_err = nullptr;
    TARGETING::Target* masterproc = NULL;
    TARGETING::targetService().masterProcChipTargetHandle(masterproc);


    // If we have nothing external (FSP or OCC) to handle checkstops we are
    //  better off just crashing and having a chance to pull the HB
    //  traces off the system live

    TARGETING::Target * l_sys = nullptr;
    TARGETING::targetService().getTopLevelTarget( l_sys );

#ifndef CONFIG_HANG_ON_MFG_SRC_TERM
    //When in MNFG_FLAG_SRC_TERM mode enable reboots to allow HB
    //to analyze now that the OCC is up and alive
    auto l_mnfgFlags = l_sys->getAttr<TARGETING::ATTR_MNFG_FLAGS>();

    // Check to see if SRC_TERM bit is set in MNFG flags
    if ((l_mnfgFlags & TARGETING::MNFG_FLAG_SRC_TERM) &&
        !(l_mnfgFlags & TARGETING::MNFG_FLAG_IMMEDIATE_HALT))
    {
        l_err = nullptr;

        //If HB_VOLATILE MFG_TERM_REBOOT_ENABLE flag is set at this point
        //Create errorlog to terminate the boot.
        Util::semiPersistData_t l_semiData;
        Util::readSemiPersistData(l_semiData);
        if (l_semiData.mfg_term_reboot == Util::MFG_TERM_REBOOT_ENABLE)
        {
            /*@
                * @errortype
                * @moduleid    ISTEP::MOD_OCC_XSTOP_HANDLER
                * @reasoncode  ISTEP::RC_PREVENT_REBOOT_IN_MFG_TERM_MODE
                * @devdesc     System rebooted without xstop in MFG TERM mode.
                * @custdesc    A problem occurred during the IPL of the system.
                */
            l_err = new ERRORLOG::ErrlEntry
                (ERRORLOG::ERRL_SEV_CRITICAL_SYS_TERM,
                ISTEP::MOD_OCC_XSTOP_HANDLER,
                ISTEP::RC_PREVENT_REBOOT_IN_MFG_TERM_MODE,
                0,
                0,
                true /*HB SW error*/ );
            l_stepError.addErrorDetails(l_err);
            break;
        }

        //Put a mark in HB VOLATILE
        Util::semiPersistData_t l_newSemiData;  //inits to 0s
        Util::readSemiPersistData(l_newSemiData);
        l_newSemiData.mfg_term_reboot = Util::MFG_TERM_REBOOT_ENABLE;
        Util::writeSemiPersistData(l_newSemiData);

        //Enable reboots so FIRDATA will be analyzed on XSTOP
        SENSOR::RebootControlSensor l_rbotCtl;
        l_rbotCtl.setRebootControl(SENSOR::RebootControlSensor::autoRebootSetting::ENABLE_REBOOTS);
    }
#endif

#ifdef CONFIG_IPLTIME_CHECKSTOP_ANALYSIS
    void* l_homerVirtAddrBase = reinterpret_cast<void*>(VmmManager::INITIAL_MEM_SIZE);
    uint64_t l_homerPhysAddrBase = mm_virt_to_phys(l_homerVirtAddrBase);
    uint64_t l_commonPhysAddr = l_homerPhysAddrBase + VMM_HOMER_REGION_SIZE;

    // Load the OCC directly into SRAM and start it in a special mode
    //  that only handles checkstops
    HBPM::loadPMComplex(
        masterproc,
        l_homerPhysAddrBase,
        l_commonPhysAddr,
        HBPM::PM_LOAD,
        true);
    HBOCC::startOCCFromSRAM(masterproc);
#endif
    uint64_t l_xstopXscom = XSCOM::generate_mmio_addr(
        masterproc,
        Kernel::MachineCheck::MCHK_XSTOP_FIR_SCOM_ADDR );

    set_mchk_data(
        l_xstopXscom,
        Kernel::MachineCheck::MCHK_XSTOP_FIR_VALUE );

    return l_stepError.getErrorHandle();
}

void readSemiPersistData(semiPersistData_t & o_data)
{
    memset(&o_data, 0x0, sizeof(semiPersistData_t));

    //Lock to prevent concurrent access
    mutex_lock(&g_PersistMutex);

    auto l_data = getHbVolatile();
    if(l_data)
    {
        o_data = *l_data;
    }

    mutex_unlock(&g_PersistMutex);
}

errlHndl_t HbVolatileSensor::getHbVolatile( hbVolatileSetting &o_setting )
{
    // the HB_VOLATILE sensor is defined as a discrete sensor
    getSensorReadingData l_data;

    readSensorData( l_data );

    // check if in valid range of hbVolatileSetting enums
    if(l_data.event_status == ENABLE_VOLATILE
    || l_data.event_status == DISABLE_VOLATILE )
    {
        o_setting = static_cast<hbVolatileSetting>(l_data.event_status);
    }
    return l_err;
}

virtual uint32_t getSensorNumber( )
{
    return TARGETING::UTIL::getSensorNumber(iv_target, iv_name );
};

uint32_t getSensorNumber( const TARGETING::Target* i_pTarget,
                          TARGETING::SENSOR_NAME i_name )
{

#ifdef CONFIG_BMC_IPMI
    // get the IPMI sensor number from the array, these are unique for each
    // sensor + sensor owner in an IPMI system
    return getIPMISensorNumber( i_pTarget, i_name );
#else
    // pass back the HUID - this will be the sensor number for non ipmi based
    // systems
    return get_huid( i_pTarget );

#endif

}

uint32_t getIPMISensorNumber( const TARGETING::Target*& i_targ,
        TARGETING::SENSOR_NAME i_name )
{
    // $TODO RTC:123035 investigate pre-populating some info if we end up
    // doing this multiple times per sensor
    //
    // Helper function to search the sensor data for the correct sensor number
    // based on the sensor name.
    //
    uint8_t l_sensor_number = INVALID_IPMI_SENSOR;

    const TARGETING::Target * l_targ = i_targ;

    if( i_targ == NULL )
    {
        TARGETING::Target * sys;
        // use the system target
        TARGETING::targetService().getTopLevelTarget(sys);

        // die if there is no system target
        assert(sys);

        l_targ = sys;
    }

    TARGETING::AttributeTraits<TARGETING::ATTR_IPMI_SENSORS>::Type l_sensors;

    // if there is no sensor attribute, we will return INVALID_IPMI_SENSOR(0xFF)
    if(  l_targ->tryGetAttr<TARGETING::ATTR_IPMI_SENSORS>(l_sensors) )
    {
        // get the number of rows by dividing the total size by the size of
        // the first row
        uint16_t array_rows = (sizeof(l_sensors)/sizeof(l_sensors[0]));

        // create an iterator pointing to the first element of the array
        uint16_t (*begin)[2]  = &l_sensors[0];

        // using the number entries as the index into the array will set the
        // end iterator to the correct position or one entry past the last
        // element of the array
        uint16_t (*end)[2] = &l_sensors[array_rows];

        uint16_t (*ptr)[2] =
            std::lower_bound(begin, end, i_name, &name_predicate);

        // we have not reached the end of the array and the iterator
        // returned from lower_bound is pointing to an entry which equals
        // the one we are searching for.
        if( ( ptr != end ) &&
                ( (*ptr)[TARGETING::IPMI_SENSOR_ARRAY_NAME_OFFSET] == i_name ) )
        {
            // found it
            l_sensor_number =
                (*ptr)[TARGETING::IPMI_SENSOR_ARRAY_NUMBER_OFFSET];

        }
    }
    return l_sensor_number;
}

uint32_t get_huid( const Target* i_target )
{
    uint32_t huid = 0;
    if( i_target == NULL )
    {
        huid = 0x0;
    }
    else if( i_target == MASTER_PROCESSOR_CHIP_TARGET_SENTINEL )
    {
        huid = 0xFFFFFFFF;
    }
    else
    {
        i_target->tryGetAttr<ATTR_HUID>(huid);
    }
    return huid;
}

errlHndl_t SensorBase::readSensorData( getSensorReadingData& o_data)
{

    // get sensor reading command only requires one byte of extra data,
    // which will be the sensor number, the command will return between
    // 3 and 5 bytes of data.
    size_t len =  1;

    // need to allocate some memory to hold the sensor number this will be
    // deleted by the IPMI transport layer
    uint8_t * l_data = new uint8_t[len];

    l_data[0] = static_cast<uint8_t>(getSensorNumber());

    IPMI::completion_code cc = IPMI::CC_UNKBAD;

    // o_data will hold the response when this returns
    errlHndl_t l_err = sendrecv(IPMI::get_sensor_reading(), cc, len,
                                l_data);

    // if we didn't get an error back from the BT interface, but see a
    // bad completion code from the BMC, process the CC to see if we
    // need to create a PEL - if an error occurs sendrcv will clean up
    // l_data for us
    if(  l_err == NULL )
    {
        l_err = processCompletionCode( cc );

        if( l_err == NULL )
        {
            // populate the output structure with the sensor data
            o_data.completion_code = cc;

            o_data.sensor_status = l_data[0];

            o_data.sensor_reading = l_data[1];

            // bytes 3-5 of the reading are optional and will be dependent
            // on the value of the sensor status byte.
            if( !( o_data.sensor_status &
                    ( SENSOR::SENSOR_DISABLED |
                    SENSOR::SENSOR_SCANNING_DISABLED )) ||
                    ( o_data.sensor_status & SENSOR::READING_UNAVAILABLE ))
            {
                // sensor reading is available
                o_data.event_status =
                            (( (uint16_t) l_data[3]) << 8  | l_data[2] );

                // spec indicates that the high order bit should be
                // ignored on a read, so lets mask it off now.
                o_data.event_status &= 0x7FFF;
            }
            else
            {
                uint32_t l_sensorNumber = getSensorNumber();

                TRACFCOMP(g_trac_ipmi,"Sensor reading not available: status = 0x%x",o_data.sensor_status);
                TRACFCOMP(g_trac_ipmi,"sensor number 0x%x, huid 0x%x",l_sensorNumber ,get_huid(iv_target));

                // something happened log an error to indicate the request
                // failed
                /*@
                    * @errortype           ERRL_SEV_UNRECOVERABLE
                    * @moduleid            IPMI::MOD_IPMISENSOR
                    * @reasoncode          IPMI::RC_SENSOR_READING_NOT_AVAIL
                    * @userdata1           sensor status indicating reason for
                    *                      reading not available
                    * @userdata2[0:31]     sensor number
                    * @userdata2[32:64]    HUID of target
                    *
                    * @devdesc             Set sensor reading command failed.
                    * @custdesc            Request to get sensor reading
                    *                      IPMI completion code can be seen
                    *                      in userdata1 field of the log.
                    */
                l_err = new ERRORLOG::ErrlEntry(
                        ERRORLOG::ERRL_SEV_UNRECOVERABLE,
                        IPMI::MOD_IPMISENSOR,
                        IPMI::RC_SENSOR_READING_NOT_AVAIL,
                        o_data.sensor_status,
                        TWO_UINT32_TO_UINT64( l_sensorNumber,
                            TARGETING::get_huid(iv_target)), true);

                l_err->collectTrace(IPMI_COMP_NAME);

            }

        }

        delete[] l_data;
    }
    return l_err;
};

errlHndl_t SensorBase::processCompletionCode( IPMI::completion_code i_rc )
{
    errlHndl_t l_err = NULL;

    IPMI::IPMIReasonCode l_reasonCode;

    if( i_rc != IPMI::CC_OK )
    {
        switch(i_rc)
        {
            case  SENSOR::CC_SENSOR_READING_NOT_SETTABLE:
            {
                /*@
                * @errortype       ERRL_SEV_UNRECOVERABLE
                * @moduleid        IPMI::MOD_IPMISENSOR
                * @reasoncode      IPMI::RC_SENSOR_NOT_SETTABLE
                * @userdata1       BMC IPMI Completion code.
                * @userdata2       bytes [0-1]sensor name
                *                  bytes [2-3]sensor number
                *                  bytes [4-7]HUID of target.
                * @devdesc         Set sensor reading command failed.
                */
                l_reasonCode = IPMI::RC_SENSOR_NOT_SETTABLE;
                break;
            }

            case  SENSOR::CC_EVENT_DATA_BYTES_NOT_SETTABLE:
            {
                /*@
                * @errortype       ERRL_SEV_UNRECOVERABLE
                * @moduleid        IPMI::MOD_IPMISENSOR
                * @reasoncode      IPMI::RC_EVENT_DATA_NOT_SETTABLE
                * @userdata1       BMC IPMI Completion code.
                * @userdata2       bytes[0-3]sensor number
                *                  bytes[4-7]HUID of target.
                * @devdesc         Set sensor reading command failed.
                */
                l_reasonCode = IPMI::RC_EVENT_DATA_NOT_SETTABLE;
                break;
            }

            case IPMI::CC_CMDSENSOR:
            {
                /*@
                * @errortype       ERRL_SEV_UNRECOVERABLE
                * @moduleid        IPMI::MOD_IPMISENSOR
                * @reasoncode      IPMI::RC_INVALID_SENSOR_CMD
                * @userdata1       BMC IPMI Completion code.
                * @userdata2       bytes [0-1]sensor name
                *                  bytes [2-3]sensor number
                *                  bytes [4-7]HUID of target.
                * @devdesc         Command not valid for this sensor.
                */
                l_reasonCode = IPMI::RC_INVALID_SENSOR_CMD;
                break;
            }

            case IPMI::CC_BADSENSOR:
            {
                /*@
                * @errortype       ERRL_SEV_UNRECOVERABLE
                * @moduleid        IPMI::MOD_IPMISENSOR
                * @reasoncode      IPMI::RC_SENSOR_NOT_PRESENT
                * @userdata1       BMC IPMI Completion code.
                * @userdata2       bytes [0-1]sensor name
                *                  bytes [2-3]sensor number
                *                  bytes [4-7]HUID of target.
                * @devdesc         Requested sensor is not present.
                */
                l_reasonCode = IPMI::RC_SENSOR_NOT_PRESENT;
                break;
            }

            default:
            {
                // lump everything else into a general failure for
                // now.
                /*@
                * @errortype       ERRL_SEV_UNRECOVERABLE
                * @moduleid        IPMI::MOD_IPMISENSOR
                * @reasoncode      IPMI::RC_SET_SENSOR_FAILURE
                * @userdata1       BMC IPMI Completion code.
                * @userdata2       bytes [0-1]sensor name
                *                  bytes [2-3]sensor number
                *                  bytes [4-7]HUID of target.
                * @devdesc         Set sensor reading command failed.
                */
                l_reasonCode = IPMI::RC_SET_SENSOR_FAILURE;
                break;
            }
        }

            // shift the sensor number into to bytes 0-3 and then
            // or in the HUID to bytes 4-7
            uint32_t sensor_number = getSensorNumber();
            uint32_t huid = TARGETING::get_huid( iv_target );


            l_err = new ERRORLOG::ErrlEntry(
                            ERRORLOG::ERRL_SEV_UNRECOVERABLE,
                            IPMI::MOD_IPMISENSOR,
                            l_reasonCode,
                            i_rc,
                            TWO_UINT32_TO_UINT64(
                                    TWO_UINT16_TO_UINT32(iv_name,
                                                        sensor_number),
                                    huid ),
                            true);

            l_err->collectTrace(IPMI_COMP_NAME);

    }
    return l_err;
}

inline uint64_t getMSR()
{
    register uint64_t msr = 0;
    asm volatile("mfmsr %0" : "=r" (msr));
    return msr;
}

inline void setMSR(uint64_t _msr)
{
    register uint64_t msr = _msr;
    asm volatile("mtmsr %0; isync" :: "r" (msr));
}

void setCheckstopData(uint64_t i_xstopAddr, uint64_t i_xstopData)
{
    g_xstopRegPtr = reinterpret_cast<uint64_t*>(i_xstopAddr | VmmManager::FORCE_PHYS_ADDR);
    g_xstopRegValue = i_xstopData;

    // Now that the machine check handler can do the xscom we
    //  can set MSR[ME]=1 to enable the regular machine check
    //  handling
    uint64_t l_msr = getMSR();
    l_msr |= 0x0000000000001000; //set bit 51
    setMSR(l_msr);
}

void SetMchkData(task_t* t)
{
    uint64_t i_xstopAddr = (uint64_t)(TASK_GETARG0(t));
    uint64_t i_xstopData = (uint64_t)(TASK_GETARG1(t));

    Kernel::MachineCheck::setCheckstopData(i_xstopAddr,i_xstopData);
}

void set_mchk_data(uint64_t i_xstopAddr, uint64_t i_xstopData)
{
    _syscall2(MISC_SETMCHKDATA,
              reinterpret_cast<void*>(i_xstopAddr),
              reinterpret_cast<void*>(i_xstopData));
}

fapi2::ReturnCode clear_occ_special_wakeups(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    fapi2::buffer<uint64_t> l_data64;
    auto l_exChiplets = i_target.getChildren<fapi2::TARGET_TYPE_EX>
                        (fapi2::TARGET_STATE_FUNCTIONAL);

    // Iterate through the EX chiplets
    for (auto l_ex_chplt : l_exChiplets)
    {
        fapi2::ATTR_CHIP_UNIT_POS_Type l_ex_num;
        FAPI_ATTR_GET( fapi2::ATTR_CHIP_UNIT_POS, l_ex_chplt, l_ex_num);
        fapi2::getScom(l_ex_chplt, EX_PPM_SPWKUP_OCC, l_data64);
        l_data64.clearBit<0>();
        fapi2::putScom(l_ex_chplt, EX_PPM_SPWKUP_OCC, l_data64);
    }

fapi_try_exit:
    return fapi2::current_err;
}

fapi2::ReturnCode pm_occ_fir_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    uint8_t firinit_done_flag = 0;
    p9pmFIR::PMFir <p9pmFIR::FIRTYPE_OCC_LFIR> l_occFir(i_target);

    FAPI_ATTR_GET(fapi2::ATTR_PM_FIRINIT_DONE_ONCE_FLAG, i_target, firinit_done_flag);

    l_occFir.get(p9pmFIR::REG_ALL);

    /* Clear all the FIR and action buffers */
    l_occFir.clearAllRegBits(p9pmFIR::REG_FIR);
    l_occFir.clearAllRegBits(p9pmFIR::REG_ACTION0);
    l_occFir.clearAllRegBits(p9pmFIR::REG_ACTION1);

    /*  Set the action and mask for the OCC LFIR bits */
    l_occFir.mask(OCC_FW0);
    l_occFir.mask(OCC_FW1);
    l_occFir.mask(CME_ERR_NOTIFY);
    l_occFir.setRecvAttn(STOP_RCV_NOTIFY_PRD);
    l_occFir.mask(OCC_HB_NOTIFY);
    l_occFir.mask(GPE0_WD_TIMEOUT);
    l_occFir.mask(GPE1_WD_TIMEOUT);
    l_occFir.mask(GPE2_WD_TIMEOUT);
    l_occFir.mask(GPE3_WD_TIMEOUT);
    l_occFir.setRecvAttn(GPE0_ERR);
    l_occFir.setRecvAttn(GPE1_ERR);
    l_occFir.mask(GPE2_ERR);
    l_occFir.mask(GPE3_ERR);
    l_occFir.mask(OCB_ERR);
    l_occFir.setRecvAttn(SRAM_UE);
    l_occFir.setRecvAttn(SRAM_CE);
    l_occFir.setRecvAttn(SRAM_READ_ERR);
    l_occFir.setRecvAttn(SRAM_WRITE_ERR);
    l_occFir.setRecvAttn(SRAM_DATAOUT_PERR);
    l_occFir.setRecvAttn(SRAM_OCI_WDATA_PARITY);
    l_occFir.setRecvAttn(SRAM_OCI_BE_PARITY_ERR);
    l_occFir.setRecvAttn(SRAM_OCI_ADDR_PARITY_ERR);
    l_occFir.mask(GPE0_HALTED);
    l_occFir.mask(GPE1_HALTED);
    l_occFir.mask(GPE2_HALTED);
    l_occFir.mask(GPE3_HALTED);
    l_occFir.mask(EXT_TRAP);
    l_occFir.mask(PPC405_CORE_RESET);
    l_occFir.mask(PPC405_CHIP_RESET);
    l_occFir.mask(PPC405_SYS_RESET);
    l_occFir.mask(PPC405_WAIT_STATE);
    l_occFir.mask(PPC405_DBGSTOPACK);
    l_occFir.setRecvAttn(OCB_DB_OCI_TIMEOUT);
    l_occFir.setRecvAttn(OCB_DB_OCI_RDATA_PARITY);
    l_occFir.setRecvAttn(OCB_DB_OCI_SLVERR);
    l_occFir.setRecvAttn(OCB_PIB_ADDR_PARITY_ERR);
    l_occFir.setRecvAttn(OCB_DB_PIB_DATA_PARITY_ERR);
    l_occFir.setRecvAttn(OCB_IDC0_ERR);
    l_occFir.setRecvAttn(OCB_IDC1_ERR);
    l_occFir.setRecvAttn(OCB_IDC2_ERR);
    l_occFir.setRecvAttn(OCB_IDC3_ERR);
    l_occFir.setRecvAttn(SRT_FSM_ERR);
    l_occFir.setRecvAttn(JTAGACC_ERR);
    l_occFir.mask(SPARE_ERR_38);
    l_occFir.setRecvIntr(C405_ECC_UE);
    l_occFir.setRecvAttn(C405_ECC_CE);
    l_occFir.setRecvAttn(C405_OCI_MC_CHK);
    l_occFir.setRecvAttn(SRAM_SPARE_DIRERR0);
    l_occFir.setRecvAttn(SRAM_SPARE_DIRERR1);
    l_occFir.setRecvAttn(SRAM_SPARE_DIRERR2);
    l_occFir.setRecvAttn(SRAM_SPARE_DIRERR3);
    l_occFir.setRecvAttn(GPE0_OCISLV_ERR);
    l_occFir.setRecvAttn(GPE1_OCISLV_ERR);
    l_occFir.setRecvAttn(GPE2_OCISLV_ERR);
    l_occFir.setRecvAttn(GPE3_OCISLV_ERR);
    l_occFir.mask(C405ICU_M_TIMEOUT);
    l_occFir.setRecvAttn(C405DCU_M_TIMEOUT);
    l_occFir.setRecvAttn(OCC_CMPLX_FAULT);
    l_occFir.setRecvAttn(OCC_CMPLX_NOTIFY);
    l_occFir.mask(SPARE_59);
    l_occFir.mask(SPARE_60);
    l_occFir.mask(SPARE_61);
    l_occFir.mask(FIR_PARITY_ERR_DUP);
    l_occFir.mask(FIR_PARITY_ERR);

    if (firinit_done_flag)
    {
        l_occFir.restoreSavedMask();
    }
    l_occFir.put();

fapi_try_exit:
    return fapi2::current_err;
}

fapi2::ReturnCode pm_occ_fir_reset(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    uint8_t firinit_done_flag = 0;
    p9pmFIR::PMFir <p9pmFIR::FIRTYPE_OCC_LFIR> l_occFir(i_target);

    FAPI_ATTR_GET(fapi2::ATTR_PM_FIRINIT_DONE_ONCE_FLAG, i_target, firinit_done_flag);

    // Here we need to read all the OCC fir registers (action0/1,mask,fir)
    // and will be stored in the respective class variable. So that below when
    // we call put function it will be read modify write.
    l_occFir.get(p9pmFIR::REG_ALL);
    if (firinit_done_flag == fapi2::ENUM_ATTR_PM_FIRINIT_DONE_ONCE_FLAG_FIRS_INITED)
    {
        /* Fetch the OCC FIR MASK; Save it to HWP attribute; clear its contents */
        l_occFir.saveMask();
    }

    l_occFir.setAllRegBits(p9pmFIR::REG_FIRMASK);
    l_occFir.setRecvIntr(OCC_HB_NOTIFY);
    l_occFir.put();
fapi_try_exit:
    return fapi2::current_err;
}

fapi2::ReturnCode p9_pm_occ_firinit(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9pm::PM_FLOW_MODE i_mode)
{
    fapi2::buffer<uint64_t> l_data64;
    fapi2::buffer<uint64_t> l_mask64;

    uint64_t l_fir;
    uint64_t l_mask;
    uint64_t l_unmaskedErrors;

    fapi2::getScom(i_target, PERV_TP_OCC_SCOM_OCCLFIR, l_data64);
    fapi2::getScom(i_target, PERV_TP_OCC_SCOM_OCCLFIRMASK, l_mask64);
    l_data64.extractToRight<0, 64>(l_fir);
    l_mask64.extractToRight<0, 64>(l_mask);
    l_unmaskedErrors = l_fir & l_mask;

    if(i_mode == p9pm::PM_RESET)
    {
        pm_occ_fir_reset(i_target);
    }
    else if(i_mode == p9pm::PM_INIT)
    {
        pm_occ_fir_init(i_target);
    }

fapi_try_exit:
    return fapi2::current_err;
}

api2::ReturnCode pm_pss_reset(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    fapi2::buffer<uint64_t> l_data64;
    uint32_t l_pollcount = 0;
    uint32_t l_max_polls;
    // timeout period is 10 millisecond. (Far longer than needed)
    const uint32_t l_pss_timeout_us = 10000;
    const uint32_t l_pss_poll_interval_us = 10;

    //  ******************************************************************
    //     - Poll status register for ongoing or no errors to give the
    //       chance for on-going operations to complete
    //  ******************************************************************

    l_max_polls = l_pss_timeout_us / l_pss_poll_interval_us;

    for (l_pollcount = 0; l_pollcount < l_max_polls; l_pollcount++)
    {
        FAPI_TRY(fapi2::getScom(i_target, PU_SPIPSS_ADC_STATUS_REG, l_data64));

        // ADC on-going complete
        if (l_data64.getBit<PU_SPIPSS_ADC_STATUS_REG_HWCTRL_ONGOING>() == 0)
        {
            break;
        }
        fapi2::delay(l_pss_poll_interval_us * 1000, 1000);
    }

    //  ******************************************************************
    //     - Poll status register for ongoing or errors to give the
    //       chance for on-going operations to complete
    //  ******************************************************************


    for (l_pollcount = 0; l_pollcount < l_max_polls; l_pollcount++)
    {
        fapi2::getScom(i_target, PU_SPIPSS_P2S_STATUS_REG, l_data64);

        //P2S On-going complete
        if (l_data64.getBit<0>() == 0)
        {
            break;
        }

        fapi2::delay(l_pss_poll_interval_us * 1000, 1000);
    }

    //  ******************************************************************
    //     - Resetting both ADC and P2S bridge
    //  ******************************************************************

    l_data64.flush<0>();
    // Need to write 01
    l_data64.setBit < PU_SPIPSS_ADC_RESET_REGISTER_HWCTRL + 1 > ();

    fapi2::putScom(i_target, PU_SPIPSS_ADC_RESET_REGISTER, l_data64);
    fapi2::putScom(i_target, PU_SPIPSS_P2S_RESET_REGISTER, l_data64);

    // Clearing reset for cleanliness
    l_data64.flush<0>();
    fapi2::putScom(i_target, PU_SPIPSS_ADC_RESET_REGISTER, l_data64);
    fapi2::putScom(i_target, PU_SPIPSS_P2S_RESET_REGISTER, l_data64);

fapi_try_exit:
    return fapi2::current_err;
}

fapi2::ReturnCode pm_pss_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{

    fapi2::buffer<uint64_t> l_data64;

    const uint8_t  l_default_apss_chip_select = 0;
    const uint8_t  l_default_spipss_frame_size = 0x20;
    const uint8_t  l_default_spipss_in_delay = 0;
    const uint8_t  l_default_spipss_clock_polarity = 0;
    const uint8_t  l_default_spipss_clock_phase = 0;
    const uint16_t l_default_attr_pm_spipss_clock_divider = 0xA;

    uint32_t l_attr_proc_pss_init_nest_frequency;
    uint8_t  l_attr_pm_apss_chip_select;
    uint8_t  l_attr_pm_spipss_frame_size;
    uint8_t  l_attr_pm_spipss_in_delay;
    uint8_t  l_attr_pm_spipss_clock_polarity;
    uint8_t  l_attr_pm_spipss_clock_phase;
    uint16_t l_attr_pm_spipss_clock_divider;
    uint32_t l_attr_pm_spipss_inter_frame_delay;

    uint32_t l_spipss_100ns_value;
    uint8_t  l_p2s_fsm_enable;
    uint8_t  l_p2s_nr_of_frames;
    uint8_t  l_hwctrl_fsm_enable;
    uint8_t  l_hwctrl_nr_of_frames;

    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> l_sysTarget =
        i_target.getParent<fapi2::TARGET_TYPE_SYSTEM>();

    FAPI_ATTR_GET(fapi2::ATTR_FREQ_PB_MHZ, l_sysTarget,
                           l_attr_proc_pss_init_nest_frequency);

    GETATTR_DEFAULT(fapi2::ATTR_PM_APSS_CHIP_SELECT,
                    "ATTR_PM_APSS_CHIP_SELECT",
                    i_target, l_attr_pm_apss_chip_select,
                    l_default_apss_chip_select);

    GETATTR_DEFAULT(fapi2::ATTR_PM_SPIPSS_FRAME_SIZE,
                    "ATTR_PM_SPIPSS_FRAME_SIZE",
                    i_target, l_attr_pm_spipss_frame_size,
                    l_default_spipss_frame_size);

    GETATTR_DEFAULT(fapi2::ATTR_PM_SPIPSS_IN_DELAY, "ATTR_PM_SPIPSS_IN_DELAY",
                    i_target, l_attr_pm_spipss_in_delay,
                    l_default_spipss_in_delay );

    GETATTR_DEFAULT(fapi2::ATTR_PM_SPIPSS_CLOCK_POLARITY,
                    "ATTR_PM_SPIPSS_CLOCK_POLARITY", i_target,
                    l_attr_pm_spipss_clock_polarity,
                    l_default_spipss_clock_polarity );

    GETATTR_DEFAULT(fapi2::ATTR_PM_SPIPSS_CLOCK_PHASE,
                    "ATTR_PM_SPIPSS_CLOCK_PHASE",
                    i_target, l_attr_pm_spipss_clock_phase,
                    l_default_spipss_clock_phase );

    GETATTR_DEFAULT(fapi2::ATTR_PM_SPIPSS_CLOCK_DIVIDER,
                    "ATTR_PM_SPIPSS_CLOCK_DIVIDER", i_target,
                    l_attr_pm_spipss_clock_divider,
                    l_default_attr_pm_spipss_clock_divider);

    FAPI_ATTR_GET(fapi2::ATTR_PM_SPIPSS_INTER_FRAME_DELAY_SETTING,
                           i_target, l_attr_pm_spipss_inter_frame_delay);

    // ------------------------------------------
    //  -- Init procedure
    // ------------------------------------------

    //  ******************************************************************
    //     - set SPIPSS_ADC_CTRL_REG0 with the values read from attributes
    //  ******************************************************************
    fapi2::getScom(i_target, PU_SPIMPSS_ADC_CTRL_REG0, l_data64);

    l_data64.insertFromRight<0, 6>(l_attr_pm_spipss_frame_size);
    l_data64.insertFromRight<12, 6>(l_attr_pm_spipss_in_delay);

    fapi2::putScom(i_target, PU_SPIMPSS_ADC_CTRL_REG0, l_data64);

    //  ******************************************************************
    //     - set SPIPSS_ADC_CTRL_REG1
    //         adc_fsm_enable = disable
    //         adc_device     = APSS
    //         adc_cpol       = 0
    //         adc_cpha       = 0
    //         adc_clock_divider = set to 10Mhz
    //         adc_nr_of_frames  = 0x16 (for auto 2 mode)
    //  ******************************************************************

    fapi2::getScom(i_target, PU_SPIPSS_ADC_CTRL_REG1, l_data64);

    l_hwctrl_fsm_enable = 0x1;
    l_hwctrl_nr_of_frames = 0x10;

    l_data64.insertFromRight<0, 1>(l_hwctrl_fsm_enable);
    l_data64.insertFromRight<1, 1>(l_attr_pm_apss_chip_select);
    l_data64.insertFromRight<2, 1>(l_attr_pm_spipss_clock_polarity);
    l_data64.insertFromRight<3, 1>(l_attr_pm_spipss_clock_phase);
    l_data64.insertFromRight<4, 10>(l_attr_pm_spipss_clock_divider);
    l_data64.insertFromRight<14, 4>(l_hwctrl_nr_of_frames);

    fapi2::putScom(i_target, PU_SPIPSS_ADC_CTRL_REG1, l_data64);

    //  ******************************************************************
    //     - set SPIPSS_ADC_CTRL_REG2
    //  ******************************************************************

    fapi2::getScom(i_target, PU_SPIPSS_ADC_CTRL_REG2, l_data64);
    l_data64.insertFromRight<0, 17>(l_attr_pm_spipss_inter_frame_delay);
    fapi2::putScom(i_target, PU_SPIPSS_ADC_CTRL_REG2, l_data64);

    //  ******************************************************************
    //     - clear SPIPSS_ADC_Wdata_REG
    //  ******************************************************************
    l_data64.flush<0>();
    fapi2::putScom(i_target, PU_SPIPSS_ADC_WDATA_REG, l_data64);

    //  ******************************************************************
    //     - set SPIPSS_P2S_CTRL_REG0
    //  ******************************************************************

    fapi2::getScom(i_target, PU_SPIPSS_P2S_CTRL_REG0, l_data64);

    l_data64.insertFromRight<0, 6>(l_attr_pm_spipss_frame_size);
    l_data64.insertFromRight<12, 6>(l_attr_pm_spipss_in_delay);

    fapi2::putScom(i_target, PU_SPIPSS_P2S_CTRL_REG0, l_data64);

    //  ******************************************************************
    //     - set SPIPSS_P2S_CTRL_REG1
    //         p2s_fsm_enable = disable
    //         p2s_device     = APSS
    //         p2s_cpol       = 0
    //         p2s_cpha       = 0
    //         p2s_clock_divider = set to 10Mhz
    //         p2s_nr_of_frames = 1 (for auto 2 mode)
    //  ******************************************************************

    fapi2::getScom(i_target, PU_SPIPSS_P2S_CTRL_REG1, l_data64);

    l_p2s_fsm_enable = 0x1;
    l_p2s_nr_of_frames = 0x1;

    l_data64.insertFromRight<0, 1>(l_p2s_fsm_enable);
    l_data64.insertFromRight<1, 1>(l_attr_pm_apss_chip_select);
    l_data64.insertFromRight<2, 1>(l_attr_pm_spipss_clock_polarity);
    l_data64.insertFromRight<3, 1>(l_attr_pm_spipss_clock_phase);
    l_data64.insertFromRight<4, 10>(l_attr_pm_spipss_clock_divider);
    l_data64.insertFromRight<17, 1>(l_p2s_nr_of_frames);

    fapi2::putScom(i_target, PU_SPIPSS_P2S_CTRL_REG1, l_data64);

    //  ******************************************************************
    //     - set SPIPSS_P2S_CTRL_REG2
    //  ******************************************************************

    fapi2::getScom(i_target, PU_SPIPSS_P2S_CTRL_REG2, l_data64);
    l_data64.insertFromRight<0, 17>(l_attr_pm_spipss_inter_frame_delay);
    fapi2::putScom(i_target, PU_SPIPSS_P2S_CTRL_REG2, l_data64);

    //  ******************************************************************
    //     - clear SPIPSS_P2S_Wdata_REG
    //  ******************************************************************
    l_data64.flush<0>();
    fapi2::putScom(i_target, PU_SPIPSS_P2S_WDATA_REG, l_data64);

    //  ******************************************************************
    //     - Set 100ns Register for Interframe delay
    //  ******************************************************************
    l_spipss_100ns_value = l_attr_proc_pss_init_nest_frequency / 40;
    fapi2::getScom(i_target, PU_SPIPSS_100NS_REG, l_data64);
    l_data64.insertFromRight<0, 32>(l_spipss_100ns_value);
    fapi2::putScom(i_target, PU_SPIPSS_100NS_REG, l_data64);

fapi_try_exit:
    return fapi2::current_err;
}

fapi2::ReturnCode p9_pm_pss_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9pm::PM_FLOW_MODE i_mode)
{
    // Initialization:  perform order or dynamic operations to initialize
    // the PMC using necessary Platform or Feature attributes.
    if (i_mode == p9pm::PM_INIT)
    {
        pm_pss_init(i_target);
    }
    // Reset:  perform reset of PSS
    else if (i_mode == p9pm::PM_RESET)
    {
        pm_pss_reset(i_target);
    }

fapi_try_exit:
    return fapi2::current_err;
}

errlHndl_t startOCCFromSRAM(TARGETING::Target* i_proc)
{

    errlHndl_t l_errl = NULL;
    uint64_t l_start405MainInstr = 0;

    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>l_target(i_proc);
    fapi2::ReturnCode l_rc;

    //  ************************************************************
    //  Issue init to OCB
    //  ************************************************************
    FAPI_INVOKE_HWP(l_errl, p9_pm_ocb_init,
                    l_target,
                    p9pm::PM_INIT,// Channel setup type
                    p9ocb::OCB_CHAN1,// Channel
                    p9ocb:: OCB_TYPE_NULL,// Channel type
                    0,// Channel base address
                    0,// Push/Pull queue length
                    p9ocb::OCB_Q_OUFLOW_NULL,// Channel flow control
                    p9ocb::OCB_Q_ITPTYPE_NULL// Channel interrupt ctrl
                    );

    //  ************************************************************
    //  Initializes P2S and HWC logic
    //  ************************************************************
    FAPI_INVOKE_HWP(l_errl, p9_pm_pss_init, l_target, p9pm::PM_INIT);

    //  ************************************************************
    //  Set the OCC FIR actions
    //  ************************************************************
    FAPI_INVOKE_HWP(l_errl, p9_pm_occ_firinit, l_target, p9pm::PM_INIT);

    // *************************************************************
    // Switch off OCC initiated special wakeup on EX to allowSTOP
    // functionality
    // *************************************************************
    FAPI_INVOKE_HWP(l_errl, clear_occ_special_wakeups, l_target);

    // Hack provided by Doug Gilbert (@dgilbert). The following six
    // scoms set up the communications between GPE0 and the 405. This
    // allows to not load and start SGPE that is responsible for setting
    // up the IRQ routing.
    // TODO: RTC 178699 come up with a more permanent solution for
    // communication setup
    uint64_t l_writeData;
    uint32_t l_writeAddress;
    size_t l_writeSize = sizeof(l_writeData);

    l_writeAddress = 0x6C040;
    l_writeData = 0x218780f800000000;
    l_errl = deviceWrite(i_proc, &l_writeData, l_writeSize,
                            DEVICE_SCOM_ADDRESS(l_writeAddress));

    l_writeAddress = 0x6C050;
    l_writeData = 0x0003d03c00000000;
    l_errl = deviceWrite(i_proc, &l_writeData, l_writeSize,
                            DEVICE_SCOM_ADDRESS(l_writeAddress));

    l_writeAddress = 0x6C044;
    l_writeData = 0x2181801800000000;
    l_errl = deviceWrite(i_proc, &l_writeData, l_writeSize,
                            DEVICE_SCOM_ADDRESS(l_writeAddress));

    l_writeAddress = 0x6C054;
    l_writeData = 0x0003d00c00000000;
    l_errl = deviceWrite(i_proc, &l_writeData, l_writeSize,
                            DEVICE_SCOM_ADDRESS(l_writeAddress));

    l_writeAddress = 0x6C048;
    l_writeData = 0x010280ac00000000;
    l_errl = deviceWrite(i_proc, &l_writeData, l_writeSize,
                            DEVICE_SCOM_ADDRESS(l_writeAddress));

    l_writeAddress = 0x6C058;
    l_writeData = 0x0001901400000000;
    l_errl = deviceWrite(i_proc, &l_writeData, l_writeSize,
                            DEVICE_SCOM_ADDRESS(l_writeAddress));

    //  ************************************************************
    //  Start OCC PPC405
    //  ************************************************************
    l_errl = makeStart405Instruction(i_proc, &l_start405MainInstr);

    FAPI_INVOKE_HWP(l_errl, p9_pm_occ_control, l_target,
                    p9occ_ctrl::PPC405_START,// Operation on PPC405
                    p9occ_ctrl::PPC405_BOOT_WITHOUT_BL, // PPC405 boot loc
                    l_start405MainInstr);

    // Set checkstop interrupt to be active-high and rising edge
    // TODO RTC:178798 Remove the following workaround
    l_writeAddress = OCB_OITR0;
    l_writeData = 0xffffffffffffffff;
    l_errl = deviceWrite(i_proc, &l_writeData, l_writeSize,
                            DEVICE_SCOM_ADDRESS(l_writeAddress));

    l_writeAddress = OCB_OIEPR0;
    l_writeData = 0xffffffffffffffff;
    l_errl = deviceWrite(i_proc, &l_writeData, l_writeSize,
                            DEVICE_SCOM_ADDRESS(l_writeAddress));

    // Need to clear out some state variables that get messed up
    //  during the PM Reset, e.g. the FIR Init flags
    l_errl = HBPM::resetPMAll(HBPM::CLEAR_ATTRIBUTES);

    return l_errl;
}

errlHndl_t makeStart405Instruction(const TARGETING::Target* i_target,
                                       uint64_t* o_instr)
{
    errlHndl_t l_errl = NULL;
    uint64_t l_epAddr;

    HBOCC::readSRAM(
        i_target,
        OCC_405_SRAM_ADDRESS + OCC_OFFSET_MAIN_EP,
        &l_epAddr,
        sizeof(uint64_t));

    // The branch instruction is of the form 0x4BXXXXX200000000, where X
    // is the address of the 405 main's entry point (alligned as shown).
    // Example: If 405 main's EP is FFF5B570, then the branch instruction
    // will be 0x4bf5b57200000000. The last two bits of the first byte of
    // the branch instruction must be '2' according to the OCC instruction
    // set manual.
    *o_instr = OCC_BRANCH_INSTR | (((uint64_t)(BRANCH_ADDR_MASK & l_epAddr)) << 32);
}

// Read OCC SRAM
errlHndl_t readSRAM(const TARGETING::Target * i_pTarget,
                             const uint32_t i_addr,
                             uint64_t * io_dataBuf,
                             size_t i_dataLen )
{
    accessOCBIndirectChannel(
        ACCESS_OCB_READ_LINEAR,
        i_pTarget,
        i_addr,
        io_dataBuf,
        i_dataLen);
}

errlHndl_t accessOCBIndirectChannel(accessOCBIndirectCmd i_cmd,
                                    const TARGETING::Target * i_pTarget,
                                    const uint32_t i_addr,
                                    uint64_t * io_dataBuf,
                                    size_t i_dataLen )
{
    errlHndl_t l_errl = nullptr;
    uint32_t   l_len  = 0;
    TARGETING::Target* l_pChipTarget = nullptr;

    p9ocb::PM_OCB_CHAN_NUM   l_channel = p9ocb::OCB_CHAN0;  // OCB channel (0,1,2,3)
    p9ocb::PM_OCB_ACCESS_OP   l_operation;  // Operation(Get, Put)
    bool       l_ociAddrValid = true;  // use oci_address
    bool       l_setup=true;           // set up linear

    switch (i_cmd)
    {
        case (ACCESS_OCB_READ_LINEAR):
            l_operation = p9ocb::OCB_GET
            break;
        case (ACCESS_OCB_WRITE_LINEAR):
            l_operation = p9ocb::OCB_PUT;
            break;
        case (ACCESS_OCB_WRITE_CIRCULAR):
            l_channel = p9ocb::OCB_CHAN1;
            l_operation = p9ocb::OCB_PUT;
            l_ociAddrValid = false;
            l_setup = false;
            break;
    }
    getChipTarget(i_pTarget,l_pChipTarget);

    fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_fapiTarget( l_pChipTarget );

    // TODO RTC: 116027 To be consistent with FSP code hwcoOCC.C,
    // p8_ocb_indir_setup_linear is always called for read and write
    // linear and not called for write circular.
    // a) linear read and write set up may only be needed once
    // b) circular write may need to be set up once
    if (l_setup)
    {

        FAPI_INVOKE_HWP(
            l_errl,
            p9_pm_ocb_indir_setup_linear,
            l_fapiTarget,
            p9ocb::OCB_CHAN0,
            p9ocb::OCB_TYPE_LINSTR,
            i_addr );
    }

    // perform operation
    FAPI_INVOKE_HWP(
        l_errl,
        p9_pm_ocb_indir_access,
        l_fapiTarget,
        l_channel,
        l_operation,
        i_dataLen/8, // Number of 8-byte blocks
        l_ociAddrValid,
        i_addr,
        l_len,
        io_dataBuf );
    return l_errl;
}

fapi2::ReturnCode p9_pm_ocb_indir_access(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9ocb::PM_OCB_CHAN_NUM  i_ocb_chan,
    const p9ocb::PM_OCB_ACCESS_OP i_ocb_op,
    const uint32_t                i_ocb_req_length,
    const bool                    i_oci_address_valid,
    const uint32_t                i_oci_address,
    uint32_t&                     o_ocb_act_length,
    uint64_t*                     io_ocb_buffer)
{
    uint64_t l_OCBAR_address   = 0;
    uint64_t l_OCBDR_address   = 0;
    uint64_t l_OCBCSR_address  = 0;
    uint64_t l_OCBSHCS_address = 0;
    o_ocb_act_length = 0;

    switch ( i_ocb_chan )
    {
        case p9ocb::OCB_CHAN0:
            l_OCBAR_address   = PU_OCB_PIB_OCBAR0;
            l_OCBDR_address   = PU_OCB_PIB_OCBDR0;
            l_OCBCSR_address  = PU_OCB_PIB_OCBCSR0_RO;
            l_OCBSHCS_address = PU_OCB_OCI_OCBSHCS0_SCOM;
            break;

        case p9ocb::OCB_CHAN1:
            l_OCBAR_address   = PU_OCB_PIB_OCBAR1;
            l_OCBDR_address   = PU_OCB_PIB_OCBDR1;
            l_OCBCSR_address  = PU_OCB_PIB_OCBCSR1_RO;
            l_OCBSHCS_address = PU_OCB_OCI_OCBSHCS1_SCOM;
            break;

        case p9ocb::OCB_CHAN2:
            l_OCBAR_address   = PU_OCB_PIB_OCBAR2;
            l_OCBDR_address   = PU_OCB_PIB_OCBDR2;
            l_OCBCSR_address  = PU_OCB_PIB_OCBCSR2_RO;
            l_OCBSHCS_address = PU_OCB_OCI_OCBSHCS2_SCOM;
            break;

        case p9ocb::OCB_CHAN3:
            l_OCBAR_address   = PU_OCB_PIB_OCBAR3;
            l_OCBDR_address   = PU_OCB_PIB_OCBDR3;
            l_OCBCSR_address  = PU_OCB_PIB_OCBCSR3_RO;
            l_OCBSHCS_address = PU_OCB_OCI_OCBSHCS3_SCOM;
            break;
    }

    if ( i_oci_address_valid )
    {
        fapi2::buffer<uint64_t> l_data64;
        l_data64.insert<0, 32>(i_oci_address);
        fapi2::putScom(i_target, l_OCBAR_address, l_data64);
    }

    if ( i_ocb_op == p9ocb::OCB_PUT )
    {
        fapi2::buffer<uint64_t> l_data64;
        fapi2::getScom(i_target, l_OCBCSR_address, l_data64);

        // The following check for circular mode is an additional check
        // performed to ensure a valid data access.
        if (l_data64.getBit<4>() && l_data64.getBit<5>())
        {
            // Check if push queue is enabled. If not, let the store occur
            // anyway to let the PIB error response return occur. (that is
            // what will happen if this checking code were not here)
            fapi2::getScom(i_target, l_OCBSHCS_address, l_data64);

            if (l_data64.getBit<31>())
            {
                bool l_push_ok_flag = false;
                uint8_t l_counter = 0;

                do
                {
                    // If the OCB_OCI_OCBSHCS0_PUSH_FULL bit (bit 0) is clear,
                    // proceed. Otherwise, poll
                    if (!l_data64.getBit<0>())
                    {
                        l_push_ok_flag = true;
                        break;
                    }

                    // Delay, before next polling.
                    fapi2::delay(OCB_FULL_POLL_DELAY_HDW,
                                 OCB_FULL_POLL_DELAY_SIM);

                    fapi2::getScom(i_target, l_OCBSHCS_address, l_data64);
                    l_counter++;
                }
                while (l_counter < OCB_FULL_POLL_MAX);
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
            // @TODO RTC 173286 - FAPI2:  FAPI_TRY (or surrogate name)
            //                    that allows access to the return code for
            //                    HWP reaction
            o_ocb_act_length++;
        }
    }
    // GET Operation: Data read from the given location in SRAM via OCB channel
    else if( i_ocb_op == p9ocb::OCB_GET )
    {
        fapi2::buffer<uint64_t> l_data64;
        uint64_t l_data = 0;

        // Read data from the Channel Data Register in blocks of 64 bits.
        for (uint32_t l_loopCount = 0; l_loopCount < i_ocb_req_length;
             l_loopCount++)
        {
            /* The data read is done via this getscom operation.
             * A data read failure will be logged off as a simple scom failure.
             * Need to find a way to distiniguish this error and collect
             * additional information incase of a failure.*/
            // @TODO RTC 173286 - FAPI2:  FAPI_TRY (or surrogate name)
            //                    that allows access to the return code for
            //                    HWP reaction
            l_data64.extract(l_data, 0, 64);
            io_ocb_buffer[l_loopCount] = l_data;
            o_ocb_act_length++;
        }

    }
fapi_try_exit:
    return fapi2::current_err;

}

fapi2::ReturnCode p9_pm_ocb_indir_setup_linear(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9ocb::PM_OCB_CHAN_NUM  i_ocb_chan,
    const p9ocb::PM_OCB_CHAN_TYPE i_ocb_type,
    const uint32_t      i_ocb_bar)
{
    FAPI_EXEC_HWP(l_rc,
                  p9_pm_ocb_init,
                  i_target,
                  p9pm::PM_SETUP_PIB,
                  i_ocb_chan,
                  i_ocb_type,
                  i_ocb_bar,
                  0, // ocb_q_len
                  p9ocb::OCB_Q_OUFLOW_NULL,
                  p9ocb::OCB_Q_ITPTYPE_NULL);
}

fapi2::ReturnCode pm_ocb_reset(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    fapi2::buffer<uint64_t> l_buf64;

    // vector of reset channels
    std::vector<uint8_t> v_reset_chan;
    v_reset_chan.push_back(1);

    // -------------------------------------------------------------------------
    // Loop over PIB Registers
    // -------------------------------------------------------------------------
    for (auto& chan : v_reset_chan)
    {
        fapi2::buffer<uint64_t> l_data64;
        // Clear out OCB Channel BAR registers
        fapi2::putScom(i_target, OCBARn[chan], 0);

        // Clear out OCB Channel control and status registers
        l_data64.flush<1>();
        fapi2::putScom(i_target, OCBCSRn_CLEAR[chan], l_data64);

        // Put channels in Circular mode
        //  - set bits 4,5 (circular mode) using OR
        l_data64.flush<0>().setBit<4>().setBit<5>();
        fapi2::putScom(i_target, OCBCSRn_OR[chan], l_data64);

        // Clear out OCB Channel Error Status registers
        fapi2::putScom(i_target, OCBESRn[chan], 0);
    }

    // -------------------------------------------------------------------------
    // Loop over OCI Registers
    // -------------------------------------------------------------------------
    for (auto& chan : v_reset_chan)
    {
        fapi2::buffer<uint64_t> l_data64;
        // Clear out Pull Base
        fapi2::putScom(i_target, OCBSLBRn[chan], 0);
        // Clear out Push Base
        fapi2::putScom(i_target, OCBSHBRn[chan], 0);
        // Clear out Pull Control & Status
        fapi2::putScom(i_target, OCBSLCSn[chan], 0);
        // Clear out Push Control & Status
        fapi2::putScom(i_target, OCBSHCSn[chan], 0);
        // Clear out Stream Error Status
        fapi2::putScom(i_target, OCBSESn[chan], 0);
        // Clear out Linear Window Control
        fapi2::putScom(i_target, OCBLWCRn[chan], 0);
        // Clear out Linear Window Base
        //  - set bits 3:9
        l_data64.setBit<3, 7>();
        fapi2::putScom(i_target, OCBLWSBRn[chan], l_data64);
    }

    // Set Interrupt Source Mask Registers 0 & 1
    //  - keep word1 0's for simics
    l_buf64.flush<0>().insertFromRight<0, 32>(INTERRUPT_SRC_MASK_REG);
    fapi2::putScom(i_target, PU_OCB_OCI_OIMR0_OR, l_buf64);

    fapi2::putScom(i_target, PU_OCB_OCI_OIMR1_OR, l_buf64);
    // Clear OCC Interrupt Type Registers 0 & 1
    l_buf64.flush<1>();
    fapi2::putScom(i_target, PU_OCB_OCI_OITR0_CLEAR, l_buf64);
    fapi2::putScom(i_target, PU_OCB_OCI_OITR1_CLEAR, l_buf64);
    // Clear OCC Interupt Edge/Polarity Registers 0 & 1
    fapi2::putScom(i_target, PU_OCB_OCI_OIEPR0_CLEAR, l_buf64);
    fapi2::putScom(i_target, PU_OCB_OCI_OIEPR1_CLEAR, l_buf64);
    // Clear OCC Interrupt Source Registers 0 & 1
    fapi2::putScom(i_target, PU_OCB_OCI_OISR0_CLEAR, l_buf64);
    fapi2::putScom(i_target, PU_OCB_OCI_OISR1_CLEAR, l_buf64);
    // Clear Interrupt Route (A, B, C) Registers 0 & 1
    fapi2::putScom(i_target, PU_OCB_OCI_OIRR0A_SCOM, 0);
    fapi2::putScom(i_target, PU_OCB_OCI_OIRR0B_SCOM, 0);
    fapi2::putScom(i_target, PU_OCB_OCI_OIRR0C_SCOM, 0);
    fapi2::putScom(i_target, PU_OCB_OCI_OIRR1A_SCOM, 0);
    fapi2::putScom(i_target, PU_OCB_OCI_OIRR1B_SCOM, 0);
    fapi2::putScom(i_target, PU_OCB_OCI_OIRR1C_SCOM, 0);
    // Clear OCC Interrupt Timer Registers 0 & 1
    //  - need bits 0&1 set to clear register
    l_buf64.flush<0>().setBit<0, 2>();
    fapi2::putScom(i_target, PU_OCB_OCI_OTR0_SCOM, l_buf64);
    fapi2::putScom(i_target, PU_OCB_OCI_OTR1_SCOM, l_buf64);

    // Clear PBA Enable Marker Acknowledgement mode to remove collisions
    // with any accesses to the OCB DCR registers (eg OSTOESR).
    // This function is only enabled by OCC firmware and is not via
    // hardware procedures.
    fapi2::getScom(i_target, PU_PBAMODE_SCOM, l_buf64);
    l_buf64.clearBit<PU_PBAMODE_EN_MARKER_ACK>();
    fapi2::putScom(i_target, PU_PBAMODE_SCOM, l_buf64);

    // Clear OCC special timeout error status register
    fapi2::putScom(i_target, PU_OCB_PIB_OSTOESR, 0);

    // Explicitly disable the OCC Heartbeat (RTC: 172638)
    // Only clearing the OCB_OCI_OCCHBR_OCC_HEARTBEAT_EN and leaving the
    // Heartbeat count intact as this may prove useful for debug later.
    fapi2::getScom(i_target, PU_OCB_OCI_OCCHBR_SCOM, l_buf64);
    l_buf64.clearBit<PU_OCB_OCI_OCCHBR_OCC_HEARTBEAT_EN>();
    fapi2::putScom(i_target, PU_OCB_OCI_OCCHBR_SCOM, l_buf64);

fapi_try_exit:
    return fapi2::current_err;
}

fapi2::ReturnCode p9_pm_ocb_init(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9pm::PM_FLOW_MODE      i_mode,
    const p9ocb::PM_OCB_CHAN_NUM     i_ocb_chan,
    const p9ocb::PM_OCB_CHAN_TYPE    i_ocb_type,
    const uint32_t                   i_ocb_bar,
    const uint8_t                    i_ocb_q_len,
    const p9ocb::PM_OCB_CHAN_OUFLOW  i_ocb_ouflow_en,
    const p9ocb::PM_OCB_ITPTYPE      i_ocb_itp_type)
{
    // -------------------------------------------------------------------------
    // RESET mode: Change the OCB channel registers to scan-0 flush state
    // -------------------------------------------------------------------------
    if (i_mode == p9pm::PM_RESET)
    {
        pm_ocb_reset(i_target);
    }
    // -------------------------------------------------------------------------
    // SETUP mode: Perform user setup of an indirect channel
    // -------------------------------------------------------------------------
    else if (i_mode == p9pm::PM_SETUP_ALL || i_mode == p9pm::PM_SETUP_PIB)
    {
        p9ocb::PM_OCB_CHAN_REG l_upd_reg = p9ocb::OCB_UPD_PIB_REG;

        if (i_mode == p9pm::PM_SETUP_ALL)
        {
            l_upd_reg = p9ocb::OCB_UPD_PIB_OCI_REG;
        }

        pm_ocb_setup(
            i_target,
            i_ocb_chan,
            i_ocb_type,
            i_ocb_bar,
            l_upd_reg,
            i_ocb_q_len,
            i_ocb_ouflow_en,
            i_ocb_itp_type);
    }

fapi_try_exit:
    return fapi2::current_err;
}

fapi2::ReturnCode pm_ocb_setup(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9ocb::PM_OCB_CHAN_NUM    i_ocb_chan,
    const p9ocb::PM_OCB_CHAN_TYPE   i_ocb_type,
    const uint32_t                  i_ocb_bar,
    const p9ocb::PM_OCB_CHAN_REG    i_ocb_upd_reg,
    const uint8_t                   i_ocb_q_len,
    const p9ocb::PM_OCB_CHAN_OUFLOW i_ocb_ouflow_en,
    const p9ocb::PM_OCB_ITPTYPE     i_ocb_itp_type)
{
    uint32_t l_ocbase = 0x0;
    fapi2::buffer<uint64_t> l_mask_or(0);
    fapi2::buffer<uint64_t> l_mask_clear(0);
    fapi2::buffer<uint64_t> l_data64;

    // -------------------------------------------------------------------------
    // Init Status and Control Register (OCBCSRn, OCBCSRn_CLEAR, OCBCSRn_OR)
    //    bit 2 => pull_read_underflow_en (0=disabled 1=enabled)
    //    bit 3 => push_write_overflow_en (0=disabled 1=enabled)
    //    bit 4 => ocb_stream_mode        (0=disabled 1=enabled)
    //    bit 5 => ocb_stream_type        (0=linear   1=circular)
    // -------------------------------------------------------------------------

    if (i_ocb_type == p9ocb::OCB_TYPE_LIN) // linear non-streaming
    {
        l_mask_clear.setBit<4, 2>();
    }
    else if (i_ocb_type == p9ocb::OCB_TYPE_LINSTR) // linear streaming
    {
        l_mask_or.setBit<4>();
        l_mask_clear.setBit<5>();
    }
    else if (i_ocb_type == p9ocb::OCB_TYPE_CIRC) // circular
    {
        l_mask_or.setBit<4, 2>();
    }
    else if (i_ocb_type == p9ocb::OCB_TYPE_PUSHQ) // push queue
    {
        l_mask_or.setBit<4, 2>();

        if (i_ocb_ouflow_en == p9ocb::OCB_Q_OUFLOW_EN)
        {
            l_mask_or.setBit<3>();
        }
        else if (i_ocb_ouflow_en == p9ocb::OCB_Q_OUFLOW_DIS)
        {
            l_mask_clear.setBit<3>();
        }
    }
    else if (i_ocb_type == p9ocb::OCB_TYPE_PULLQ) // pull queue
    {
        l_mask_or.setBit<4, 2>();

        if (i_ocb_ouflow_en == p9ocb::OCB_Q_OUFLOW_EN)
        {
            l_mask_or.setBit<2>();
        }
        else if (i_ocb_ouflow_en == p9ocb::OCB_Q_OUFLOW_DIS)
        {
            l_mask_clear.setBit<2>();
        }
    }

    // write using OR mask
    fapi2::putScom(i_target, OCBCSRn_OR[i_ocb_chan], l_mask_or);
    // write using AND mask
    fapi2::putScom(i_target, OCBCSRn_CLEAR[i_ocb_chan], l_mask_clear);

    //--------------------------------------------------------------------------
    // set address base register for linear, pull queue or push queue
    //--------------------------------------------------------------------------
    //don't update bar if type null or circular
    if (!(i_ocb_type == p9ocb::OCB_TYPE_NULL ||
          i_ocb_type == p9ocb::OCB_TYPE_CIRC))
    {
        // BAR for linear (streaming / non-streaming)
        if ((i_ocb_type == p9ocb::OCB_TYPE_LIN) ||
            (i_ocb_type == p9ocb::OCB_TYPE_LINSTR))
        {
            l_ocbase = OCBARn[i_ocb_chan];
        }
        // BAR for push queue
        else if (i_ocb_type == p9ocb::OCB_TYPE_PUSHQ)
        {
            l_ocbase = OCBSHBRn[i_ocb_chan];
        }
        // BAR for pull queue
        else
        {
            l_ocbase = OCBSLBRn[i_ocb_chan];
        }

        l_data64.flush<0>().insertFromRight<0, 32>(i_ocb_bar);
        fapi2::putScom(i_target, l_ocbase, l_data64);
    }

    // -------------------------------------------------------------------------
    // set up push queue control register
    //    bits 4:5  => push interrupt action
    //                   00=full
    //                   01=not full
    //                   10=empty
    //                   11=not empty
    //    bits 6:10 => push queue length
    //    bit  31   => push queue enable
    // -------------------------------------------------------------------------
    if ((i_ocb_type == p9ocb::OCB_TYPE_PUSHQ) &&
        (i_ocb_upd_reg == p9ocb::OCB_UPD_PIB_OCI_REG))
    {
        l_data64.flush<0>().insertFromRight<6, 5>(i_ocb_q_len);
        l_data64.insertFromRight<4, 2>(i_ocb_itp_type);
        l_data64.setBit<31>();
        fapi2::putScom(i_target, OCBSHCSn[i_ocb_chan], l_data64);
    }

    // -------------------------------------------------------------------------
    // set up pull queue control register
    //    bits 4:5  => pull interrupt action
    //                   00=full
    //                   01=not full
    //                   10=empty
    //                   11=not empty
    //    bits 6:10 => pull queue length
    //    bit  31   => pull queue enable
    // -------------------------------------------------------------------------
    if ((i_ocb_type == p9ocb::OCB_TYPE_PULLQ) &&
        (i_ocb_upd_reg == p9ocb::OCB_UPD_PIB_OCI_REG))
    {
        l_data64.flush<0>().insertFromRight<6, 5>(i_ocb_q_len);
        l_data64.insertFromRight<4, 2>(i_ocb_itp_type);
        l_data64.setBit<31>();
        fapi2::putScom(i_target, OCBSLCSn[i_ocb_chan], l_data64);
    }

fapi_try_exit:
    return fapi2::current_err;
}
