```cpp


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
    constexpr uint64_t MNFG_THRESHOLDS_ATTR = 63;

    // Broadcast mode workaround for UEs causing out of sync
    mss::workarounds::mcbist::broadcast_out_of_sync(i_target, mss::ON);

    for (const auto& p : mss::find_targets<TARGET_TYPE_MCA>(i_target))
    {
        fir::reg<MCA_FIR> l_ecc64_fir_reg(p, l_rc1);
        fir::reg<MCA_MBACALFIRQ> l_cal_fir_reg(p, l_rc2);
        uint64_t rcd_protect_time = 0;
        const auto l_chip_target = mss::find_target<fapi2::TARGET_TYPE_PROC_CHIP>(i_target);

        FAPI_TRY(l_rc1, "unable to create fir::reg for %d", MCA_FIR);
        FAPI_TRY(l_rc2, "unable to create fir::reg for %d", MCA_MBACALFIRQ);

        // Read out the wr_done and rd_tag delays and find min
        // and set the RCD Protect Time to this value
        mss::read_dsm0q_register(p, dsm0_buffer);
        mss::get_wrdone_delay(dsm0_buffer, wr_done_delay);
        mss::get_rdtag_delay(dsm0_buffer, rd_tag_delay);
        rcd_protect_time = std::min(wr_done_delay, rd_tag_delay);
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
            l_ecc64_fir_reg.checkstop<MCA_FIR_MAINLINE_UE>()
              .checkstop<MCA_FIR_MAINLINE_RCD>();
            l_cal_fir_reg.checkstop<MCA_MBACALFIRQ_PORT_FAIL>();
        }

        // If MNFG FLAG Threshhold is enabled skip IUE unflagging
        mss::mnfg_flags(l_mnfg_buffer);

        if (!(l_mnfg_buffer.getBit<MNFG_THRESHOLDS_ATTR>()))
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

    return fapi2::FAPI2_RC_SUCCESS;

fapi_try_exit:
    return fapi2::current_err;
}

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
    constexpr uint64_t MNFG_THRESHOLDS_ATTR = 63;

    // Broadcast mode workaround for UEs causing out of sync
    FAPI_TRY(mss::workarounds::mcbist::broadcast_out_of_sync(i_target, mss::ON));

    for (const auto& p : mss::find_targets<TARGET_TYPE_MCA>(i_target))
    {
        fir::reg<MCA_FIR> l_ecc64_fir_reg(p, l_rc1);
        fir::reg<MCA_MBACALFIRQ> l_cal_fir_reg(p, l_rc2);
        uint64_t rcd_protect_time = 0;
        const auto l_chip_target = mss::find_target<fapi2::TARGET_TYPE_PROC_CHIP>(i_target);

        FAPI_TRY(l_rc1, "unable to create fir::reg for %d", MCA_FIR);
        FAPI_TRY(l_rc2, "unable to create fir::reg for %d", MCA_MBACALFIRQ);

        // Read out the wr_done and rd_tag delays and find min
        // and set the RCD Protect Time to this value
        FAPI_TRY (mss::read_dsm0q_register(p, dsm0_buffer) );
        mss::get_wrdone_delay(dsm0_buffer, wr_done_delay);
        mss::get_rdtag_delay(dsm0_buffer, rd_tag_delay);
        rcd_protect_time = std::min(wr_done_delay, rd_tag_delay);
        FAPI_TRY (mss::change_rcd_protect_time(p, rcd_protect_time) );

        l_ecc64_fir_reg.checkstop<MCA_FIR_MAINLINE_AUE>()
        .recoverable_error<MCA_FIR_MAINLINE_UE>()
        .checkstop<MCA_FIR_MAINLINE_IAUE>()
        .recoverable_error<MCA_FIR_MAINLINE_IUE>();

        l_cal_fir_reg.recoverable_error<MCA_MBACALFIRQ_PORT_FAIL>();

        // If ATTR_CHIP_EC_FEATURE_HW414700 is enabled set checkstops
        FAPI_TRY( FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_HW414700, l_chip_target, l_checkstop_flag) );

        // If the system is running DD2 chips override some recoverable firs with checkstop
        // Due to a known hardware defect with DD2 certain errors are not handled properly
        // As a result, these firs are marked as checkstop for DD2 to avoid any mishandling
        if (l_checkstop_flag)
        {
            l_ecc64_fir_reg.checkstop<MCA_FIR_MAINLINE_UE>()
            .checkstop<MCA_FIR_MAINLINE_RCD>();
            l_cal_fir_reg.checkstop<MCA_MBACALFIRQ_PORT_FAIL>();
        }

        // If MNFG FLAG Threshhold is enabled skip IUE unflagging
        mss::mnfg_flags(l_mnfg_buffer);

        if ( !(l_mnfg_buffer.getBit<MNFG_THRESHOLDS_ATTR>()) )
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

    return fapi2::FAPI2_RC_SUCCESS;

fapi_try_exit:
    return fapi2::current_err;
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
