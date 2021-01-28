```cpp

template< fapi2::TargetType T = fapi2::TARGET_TYPE_MCA, typename TT = portTraits<mss::mc_type::NIMBUS> >
void get_wrdone_delay( const fapi2::buffer<uint64_t>& i_data, uint64_t& o_delay )
{
    i_data.template extractToRight<24, 6>(o_delay);
}

template< fapi2::TargetType T = fapi2::TARGET_TYPE_MCA, typename TT = portTraits<mss::mc_type::NIMBUS> >
void get_rdtag_delay( const fapi2::buffer<uint64_t>& i_data, uint64_t& o_delay )
{
    i_data.template extractToRight<36, 6>(o_delay);
}

template< fapi2::TargetType T, typename TT = portTraits<mss::mc_type::NIMBUS> >
fapi2::ReturnCode read_farb0q_register( const fapi2::Target<T>& i_target, fapi2::buffer<uint64_t>& o_time )
{
    mss::getScom(i_target, 0x7010913, o_time);
}

template< fapi2::TargetType T, typename TT = portTraits<mss::mc_type::NIMBUS> >
fapi2::ReturnCode write_farb0q_register( const fapi2::Target<T>& i_target, const fapi2::buffer<uint64_t> i_time )
{
    mss::putScom(i_target, 0x7010913, i_time);
}

template< fapi2::TargetType T = fapi2::TARGET_TYPE_MCA, typename TT = portTraits<mss::mc_type::NIMBUS> >
void set_rcd_protect_time( const uint64_t i_time, fapi2::buffer<uint64_t>& io_data )
{
     io_data.template insertFromRight<48, 6>(i_time);
}

template< fapi2::TargetType T, typename TT = portTraits<mss::mc_type::NIMBUS> >
fapi2::ReturnCode change_rcd_protect_time( const fapi2::Target<T>& i_target, const uint64_t i_time )
{
    fapi2::buffer<uint64_t> l_data;
    read_farb0q_register(i_target, l_data);
    set_rcd_protect_time(i_time, l_data);
    write_farb0q_register(i_target, l_data);
}

///
/// @brief Unmask and setup actions for memdiags related FIR
/// @param[in] i_target the fapi2::Target MCBIST
/// @return fapi2::ReturnCode FAPI2_RC_SUCCESS iff ok
///
template<>
fapi2::ReturnCode after_memdiags<mss::mc_type::NIMBUS>( const fapi2::Target<fapi2::TARGET_TYPE_MCBIST>& i_target )
{
    fapi2::ReturnCode l_rc1, l_rc2;
    fapi2::buffer<uint64_t> dsm0_buffer;
    fapi2::buffer<uint64_t> l_mnfg_buffer;
    uint64_t rd_tag_delay = 0;
    uint64_t wr_done_delay = 0;
    fapi2::buffer<uint64_t> l_aue_buffer;
    fapi2::ATTR_CHIP_EC_FEATURE_HW414700_Type l_checkstop_flag;

    // Broadcast mode workaround for UEs causing out of sync
    mss::workarounds::mcbist::broadcast_out_of_sync(i_target, mss::ON);

    for (const auto& p : mss::find_targets<TARGET_TYPE_MCA>(i_target))
    {
        fir::reg<MCA_FIR> l_ecc64_fir_reg(p, l_rc1);
        fir::reg<MCA_MBACALFIRQ> l_cal_fir_reg(p, l_rc2);
        uint64_t rcd_protect_time = 0;
        const auto l_chip_target = mss::find_target<fapi2::TARGET_TYPE_PROC_CHIP>(i_target);

        // Read out the wr_done and rd_tag delays and find min
        // and set the RCD Protect Time to this value
        mss::read_dsm0q_register(p, dsm0_buffer);
        mss::get_wrdone_delay(dsm0_buffer, wr_done_delay);
        mss::get_rdtag_delay(dsm0_buffer, rd_tag_delay);
        rcd_protect_time = min(wr_done_delay, rd_tag_delay);
        mss::change_rcd_protect_time(p, rcd_protect_time);

        l_ecc64_fir_reg.checkstop<MCA_FIR_MAINLINE_AUE>()
          .recoverable_error<MCA_FIR_MAINLINE_UE>()
          .checkstop<MCA_FIR_MAINLINE_IAUE>()
          .recoverable_error<MCA_FIR_MAINLINE_IUE>();

        l_cal_fir_reg.recoverable_error<MCA_MBACALFIRQ_PORT_FAIL>();

        // If ATTR_CHIP_EC_FEATURE_HW414700 is enabled set checkstops
        FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_HW414700, l_chip_target, l_checkstop_flag);

        // If the system is running DD2 chips override some recoverable firs with checkstop
        // Due to a known hardware defect with DD2 certain errors are not handled properly
        // As a result, these firs are marked as checkstop for DD2 to avoid any mishandling
        if (l_checkstop_flag)
        {
            l_ecc64_fir_reg
              .checkstop<MCA_FIR_MAINLINE_UE>()
              .checkstop<MCA_FIR_MAINLINE_RCD>();
            l_cal_fir_reg.checkstop<MCA_MBACALFIRQ_PORT_FAIL>();
        }

        // If MNFG FLAG Threshhold is enabled skip IUE unflagging
        mss::mnfg_flags(l_mnfg_buffer);

        if (!(l_mnfg_buffer.getBit<63>()))
        {
            l_ecc64_fir_reg.recoverable_error<MCA_FIR_MAINTENANCE_IUE>();
        }

        l_ecc64_fir_reg.write();
        l_cal_fir_reg.write();

        // Change Maint AUE and IAUE to checkstop without unmasking
        // Normal setup modifies masked bits in addition to setting checkstop
        // This causes issues if error has occured, manually scoming to avoid this
        mss::getScom(p, MCA_ACTION1, l_aue_buffer);
        l_aue_buffer.clearBit<MCA_FIR_MAINTENANCE_AUE>();
        l_aue_buffer.clearBit<MCA_FIR_MAINTENANCE_IAUE>();
        mss::putScom(p, MCA_ACTION1, l_aue_buffer);

        // Note: We also want to include the following setup RCD recovery and port fail
        mss::change_port_fail_disable(p, mss::LOW);
        mss::change_rcd_recovery_disable(p, mss::LOW);
    }
}


errlHndl_t runStep(const TargetHandleList & i_targetList)
{
    // memory diagnostics ipl step entry point
    errlHndl_t err = nullptr;
    Globals globals;
    TargetHandle_t top = nullptr;
    targetService().getTopLevelTarget(top);

    if(top)
    {
        globals.mfgPolicy = top->getAttr<ATTR_MNFG_FLAGS>();

        // by default 0
        // see hostboot/src/usr/targeting/common/xmltohb/attribute_types.xml:7292
        uint8_t maxMemPatterns =
            top->getAttr<ATTR_RUN_MAX_MEM_PATTERNS>();


        // This registry / attr is the same as the
        // exhaustive mnfg one
        if(maxMemPatterns)
        {
            globals.mfgPolicy |=
              MNFG_FLAG_ENABLE_EXHAUSTIVE_PATTERN_TEST;
        }
        globals.simicsRunning = Util::isSimicsRunning();
    }

    // get the workflow for each target mba passed in.
    // associate each workflow with the target handle.
    WorkFlowAssocMap list;
    TargetHandleList::const_iterator tit;
    DiagMode mode;
    for(tit = i_targetList.begin(); tit != i_targetList.end(); ++tit)
    {
        // mode = 0 (ONE_PATTERN) is the default output
        err = getDiagnosticMode(globals, *tit, mode);
        // create a list with patterns
        // for ONE_PATTERN the list is as follows
        // [0] = 12 (START_SCRUB)
        // [1] = 0 (START_PATTERN_0)
        err = getWorkFlow(mode, list[*tit], globals);
    }

    if(nullptr == err)
    {
        // set global data
        Singleton<StateMachine>::instance().setGlobals(globals);
        err = Singleton<StateMachine>::instance().run(list);
    }

    // ensure threads and pools are shutdown when finished
    if(nullptr == err)
    {
        err = doStepCleanup(globals);
    }

    // If this step completes without the need for a reconfig due to an RCD
    // parity error, clear all RCD parity error counters.
    ATTR_RECONFIGURE_LOOP_type attr = top->getAttr<ATTR_RECONFIGURE_LOOP>();
    if (0 == (attr & RECONFIGURE_LOOP_RCD_PARITY_ERROR))
    {
        TargetHandleList trgtList; getAllChiplets( trgtList, TYPE_MCA );
        for (auto & trgt : trgtList)
        {
            if (0 != trgt->getAttr<ATTR_RCD_PARITY_RECONFIG_LOOP_COUNT>())
                trgt->setAttr<ATTR_RCD_PARITY_RECONFIG_LOOP_COUNT>(0);
        }
    }
    return err;
}

errlHndl_t __runMemDiags(TargetHandleList i_trgtList)
{
    ATTN::startService();
    MDIA::runStep(i_trgtList);
    ATTN::stopService();
}

template< mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = portTraits<MC> >
inline fapi2::ReturnCode reset_reorder_queue_settings(const fapi2::Target<T>& i_target)
{
    uint8_t l_reorder_queue = 0;
    reorder_queue_setting<MC>(i_target, l_reorder_queue);

    // Changes the reorder queue settings
    // Two settings are FIFO and REORDER.  FIFO is a 1 in the registers, while reorder is a 0 state
    const mss::states l_state = ((l_reorder_queue == fapi2::ENUM_ATTR_MEM_REORDER_QUEUE_SETTING_FIFO) ?
                                  mss::states::ON : mss::states::OFF);
    configure_rrq(i_target, l_state);
    configure_wrq(i_target, l_state);
}

void* call_mss_memdiag (void* io_pArgs)
{
    TARGETING::Target* masterproc = nullptr;
    TARGETING::targetService().masterProcChipTargetHandle(masterproc);

#ifdef CONFIG_IPLTIME_CHECKSTOP_ANALYSIS
    errl = HBOCC::loadHostDataToSRAM(masterproc, PRDF::ALL_PROC_MEM_MASTER_CORE);
#endif

      TargetHandleList trgtList; getAllChiplets( trgtList, TYPE_MCBIST );
      // @todo RTC 179458  Intermittent SIMICs action file issues
      if ( Util::isSimicsRunning() == false )
      {
        // Start Memory Diagnostics.
        errl = __runMemDiags(trgtList);
      }

      for(auto & tt : trgtList)
      {
        fapi2::Target<fapi2::TARGET_TYPE_MCBIST> ft ( tt );
        // Unmask mainline FIRs.
        mss::unmask::after_memdiags(ft);
        // Turn off FIFO mode to improve performance.
        mss::reset_reorder_queue_settings(ft);
      }
}
```
