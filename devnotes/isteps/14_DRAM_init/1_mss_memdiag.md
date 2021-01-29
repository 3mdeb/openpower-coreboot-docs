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

bool StateMachine::scheduleWorkItem(WorkFlowProperties & i_wfp)
{
    // schedule work items for execution in the thread pool

    // see if the workFlow for this target is complete
    // and see if all phases have completed successfully

    if(i_wfp.workItem == getWorkFlow(i_wfp).end())
    {
        i_wfp.status = COMPLETE;
    }

    // see if the workFlow for this target is done...for better or worse
    // (failed or successful)
    // if it is, also check to see if all workFlows for all targets
    // are complete

    if(i_wfp.status != IN_PROGRESS && allWorkFlowsComplete())
    {
        // Clear BAD_DQ_BIT_SET bit
        TargetHandle_t top = NULL;
        targetService().getTopLevelTarget(top);
        ATTR_RECONFIGURE_LOOP_type reconfigAttr =
            top->getAttr<TARGETING::ATTR_RECONFIGURE_LOOP>();
        reconfigAttr &= ~RECONFIGURE_LOOP_BAD_DQ_BIT_SET;
        top->setAttr<TARGETING::ATTR_RECONFIGURE_LOOP>(reconfigAttr);

        // all workFlows are finished
        // release the init service dispatcher
        // thread waiting for completion

        MDIA_FAST("sm: all workflows finished");

        iv_done = true;
        sync_cond_broadcast(&iv_cond);
    }

    else if(i_wfp.status == IN_PROGRESS)
    {
        // still work left for this target

        // 1 - get the phase for the target,
        // 2 - create the work item
        // 3 - schedule it

        // determine the priority for the work item to be scheduled
        // the priority is the number of iterations through the memory
        uint64_t priority = getRemainingWorkItems(i_wfp);

        if(!iv_tp)
        {
            //create same number of tasks in the pool as there are cpu threads
            const size_t l_num_tasks = cpu_thread_count();
            Util::ThreadPoolManager::setThreadCount(l_num_tasks);
            MDIA_FAST("Starting threadPool with %u tasks...", l_num_tasks);
            iv_tp = new Util::ThreadPool<WorkItem>();
            iv_tp->start();
        }

        TargetHandle_t target = getTarget(i_wfp);

        MDIA_FAST("sm: dispatching work item %d for: 0x%08x, priority: %d, "
                "unit: %d", *i_wfp.workItem,
                get_huid(target),
                priority,
                i_wfp.chipUnit);

        iv_tp->insert(new WorkItem(*this, &i_wfp, priority, i_wfp.chipUnit));

        return true;
    }

    return false;
}

template<  mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T >
fapi2::ReturnCode sf_init( const fapi2::Target<T>& i_target,
                           const uint64_t i_pattern = PATTERN_0 )
{
    using ET = mss::mcbistMCTraits<MC>;

    fapi2::ReturnCode l_rc;
    constraints<MC> l_const(i_pattern);
    sf_init_operation<MC> l_init_op(i_target, l_const, l_rc);

    return l_init_op.execute();

fapi_try_exit:
    return fapi2::current_err;
}

fapi2::ReturnCode nim_sf_init( const fapi2::Target<fapi2::TARGET_TYPE_MCBIST>& i_target,
                               const uint64_t i_pattern )
{
    return mss::memdiags::sf_init<mss::mc_type::NIMBUS>(i_target, i_pattern);
}

fapi2::ReturnCode nim_sf_read( const fapi2::Target<fapi2::TARGET_TYPE_MCBIST>& i_target,
                               const mss::mcbist::stop_conditions<mss::mc_type::NIMBUS>& i_stop,
                               const mss::mcbist::address& i_address,
                               const mss::mcbist::end_boundary i_end,
                               const mss::mcbist::address& i_end_address )
{
    return mss::memdiags::sf_read<mss::mc_type::NIMBUS>(i_target, i_stop, i_address, i_end, i_end_address);
}

template<  mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = mcbistTraits<MC, T> >
inline fapi2::ReturnCode clear_errors( const fapi2::Target<T> i_target )
{
    // TK: Clear the more detailed errors checked above
    FAPI_TRY( fapi2::putScom(i_target, TT::MCBSTATQ_REG, 0) );
    FAPI_TRY( fapi2::putScom(i_target, TT::SRERR0_REG, 0) );
    FAPI_TRY( fapi2::putScom(i_target, TT::SRERR1_REG, 0) );
    FAPI_TRY( fapi2::putScom(i_target, TT::FIRQ_REG, 0) );

fapi_try_exit:
    return fapi2::current_err;
}

template< mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = mcbistTraits<MC, T> >
fapi2::ReturnCode clear_error_helper( const fapi2::Target<T>& i_target, const program<MC>& i_program )
{
    FAPI_TRY(clear_errors(i_target) );
}

template<  mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = mcbistTraits<MC, T> >
inline fapi2::ReturnCode load_fifo_mode( const fapi2::Target<T>& i_target, const mcbist::program<MC>& i_program )
{
    // Checks if FIFO mode is required by checking all subtests
    const auto l_subtest_it = std::find_if(i_program.iv_subtests.begin(),
                                           i_program.iv_subtests.end(), []( const mss::mcbist::subtest_t<MC, T, TT>& i_rhs) -> bool
    {
        return i_rhs.fifo_mode_required();
    });

    // if the FIFO load is not needed (no subtest requiring it was found), just exit out
    if(l_subtest_it == i_program.iv_subtests.end())
    {
        return fapi2::FAPI2_RC_SUCCESS;
    }

    // Turns on FIFO mode
    constexpr mss::states FIFO_ON = mss::states::ON;

    FAPI_TRY( configure_wrq(i_target, FIFO_ON) );
    FAPI_TRY( configure_rrq(i_target, FIFO_ON) );

fapi_try_exit:
    return fapi2::current_err;
}


template< mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = mcbistTraits<MC, T> >
inline fapi2::ReturnCode config_address_range( const fapi2::Target<T>& i_target,
        const uint64_t i_start,
        const uint64_t i_end,
        const uint64_t i_index )
{
    FAPI_INF("config MCBIST address range %d start: 0x%016lx (0x%016lx), end/len 0x%016lx (0x%016lx)",
             i_index,
             i_start, (i_start << mss::mcbist::address::MAGIC_PAD),
             i_end, (i_end << mss::mcbist::address::MAGIC_PAD),
             mss::c_str(i_target));
    FAPI_ASSERT( i_index < TT::ADDRESS_PAIRS,
                 fapi2::MSS_MCBIST_INVALID_ADDRESS_PAIR_INDEX().
                 set_INDEX(i_index).
                 set_MC_TYPE(MC).
                 set_TARGET(i_target),
                 "An invalid address pair index %d for %s", i_index, mss::c_str(i_target));
    FAPI_TRY( fapi2::putScom(i_target, TT::address_pairs[i_index].first, i_start << mss::mcbist::address::MAGIC_PAD) );
    FAPI_TRY( fapi2::putScom(i_target, TT::address_pairs[i_index].second, i_end << mss::mcbist::address::MAGIC_PAD) );

fapi_try_exit:
    return fapi2::current_err;
}

template<  mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = mcbistTraits<MC, T> >
inline fapi2::ReturnCode load_mcbparm( const fapi2::Target<T>& i_target, const mcbist::program<MC>& i_program )
{
    FAPI_INF("load MCBIST parameter register: 0x%016lx for %s", i_program.iv_parameters, mss::c_str(i_target));
    return fapi2::putScom(i_target, TT::MCBPARMQ_REG, i_program.iv_parameters);
}

template<  mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = mcbistTraits<MC, T> >
fapi2::ReturnCode load_mcbamr( const fapi2::Target<T>& i_target, const mcbist::program<MC>& i_program )
{
    // Vector? Can decide when we fully understand the methods to twiddle the maps themselves. BRS
    FAPI_INF("load MCBIST address map register 0: 0x%016lx for %s", i_program.iv_addr_map0, mss::c_str(i_target));
    FAPI_TRY( fapi2::putScom(i_target, TT::MCBAMR0A0Q_REG, i_program.iv_addr_map0) );

    FAPI_INF("load MCBIST address map register 1: 0x%016lx for %s", i_program.iv_addr_map1, mss::c_str(i_target));
    FAPI_TRY( fapi2::putScom(i_target, TT::MCBAMR1A0Q_REG, i_program.iv_addr_map1) );

    FAPI_INF("load MCBIST address map register 2: 0x%016lx for %s", i_program.iv_addr_map2, mss::c_str(i_target));
    FAPI_TRY( fapi2::putScom(i_target, TT::MCBAMR2A0Q_REG, i_program.iv_addr_map2) );

    FAPI_INF("load MCBIST address map register 3: 0x%016lx for %s", i_program.iv_addr_map3, mss::c_str(i_target));
    FAPI_TRY( fapi2::putScom(i_target, TT::MCBAMR3A0Q_REG, i_program.iv_addr_map3) );

fapi_try_exit:
    return fapi2::current_err;
}

template< mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = mcbistTraits<MC, T> >
inline fapi2::ReturnCode load_config( const fapi2::Target<T>& i_target, const mcbist::program<MC>& i_program )
{
    FAPI_INF("%s loading MCBIST Config 0x%016lx", mss::c_str(i_target), i_program.iv_config);

    // Copy the program's config settings - we want to modify them if we're in sim.
    fapi2::buffer<uint64_t> l_config = i_program.iv_config;

    // If we're running in Cronus, there is no interrupt so any attention bits will
    // hang something somewhere. Make sure there's nothing in this config which can
    // turn on attention bits unless we're running in hostboot
#ifndef __HOSTBOOT_MODULE

    if(TT::CFG_ENABLE_ATTN_SUPPORT == mss::states::YES)
    {
        l_config.template clearBit<TT::CFG_ENABLE_HOST_ATTN>();
        l_config.template clearBit<TT::CFG_ENABLE_SPEC_ATTN>();
    }

#endif

    FAPI_TRY( fapi2::putScom(i_target, TT::CFGQ_REG, l_config) );

fapi_try_exit:
    return fapi2::current_err;
}

template<  mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = mcbistTraits<MC, T> >
inline fapi2::ReturnCode load_thresholds( const fapi2::Target<T>& i_target, const uint64_t i_thresholds )
{
    FAPI_INF("load MCBIST threshold register: 0x%016lx for %s", i_thresholds, mss::c_str(i_target) );
    return fapi2::putScom(i_target, TT::THRESHOLD_REG, i_thresholds);
}

template<  mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = mcbistTraits<MC, T> >
fapi2::ReturnCode load_mcbmr( const fapi2::Target<T>& i_target, const mcbist::program<MC>& i_program )
{
    fapi2::current_err = fapi2::FAPI2_RC_SUCCESS;

    // Leave if there are no subtests.
    if (0 == i_program.iv_subtests.size())
    {
        FAPI_INF("no subtests, nothing to do for %s", mss::c_str(i_target));
        return fapi2::current_err;
    }

    // List of the 8 MCBIST registers - each holds 4 subtests.
    const std::vector< uint64_t > l_memory_registers =
    {
        TT::MCBMR0_REG, TT::MCBMR1_REG, TT::MCBMR2_REG, TT::MCBMR3_REG,
        TT::MCBMR4_REG, TT::MCBMR5_REG, TT::MCBMR6_REG, TT::MCBMR7_REG,
    };

    std::vector< uint64_t > l_memory_register_buffers =
    {
        0, 0, 0, 0, 0, 0, 0, 0,
    };

    ssize_t l_bin = -1;
    size_t l_register_shift = 0;

    // We'll shift this in to position to indicate which subtest is the last
    const uint64_t l_done_bit( 0x8000000000000000 >> TT::DONE );

    // For now limit MCBIST programs to 32 subtests.
    const auto l_program_size = i_program.iv_subtests.size();
    FAPI_ASSERT( l_program_size <= TT::SUBTEST_PER_PROGRAM,
                 fapi2::MSS_MCBIST_PROGRAM_TOO_BIG().
                 set_PROGRAM_LENGTH(l_program_size).
                 set_TARGET(i_target).
                 set_MC_TYPE(MC),
                 "mcbist program of length %d exceeds arbitrary maximum of %d", l_program_size, TT::SUBTEST_PER_PROGRAM );

    // Distribute the program over the 8 MCBIST subtest registers
    // We need the index, so increment thru i_program.iv_subtests.size()
    for (size_t l_index = 0; l_index < l_program_size; ++l_index)
    {
        l_bin = (l_index % TT::SUBTEST_PER_REG) == 0 ? l_bin + 1 : l_bin;
        l_register_shift = (l_index % TT::SUBTEST_PER_REG) * TT::BITS_IN_SUBTEST;

        l_memory_register_buffers[l_bin] |=
            (uint64_t(i_program.iv_subtests[l_index].iv_mcbmr) << TT::LEFT_SHIFT) >> l_register_shift;

        FAPI_DBG("putting subtest %d (0x%x) in MCBMR%dQ shifted %d 0x%016llx",
                 l_index, i_program.iv_subtests[l_index].iv_mcbmr, l_bin,
                 l_register_shift, l_memory_register_buffers[l_bin]);
    }

    // l_bin and l_register_shift are the values for the last subtest we'll tell the MCBIST about.
    // We need to set that subtest's done-bit so the MCBIST knows it's the end of the line
    l_memory_register_buffers[l_bin] |= l_done_bit >> l_register_shift;
    FAPI_DBG("setting MCBMR%dQ subtest %llu as the last subtest 0x%016llx",
             l_bin, l_register_shift, l_memory_register_buffers[l_bin]);

    // ... and slam the values in to the registers.
    // Could just decrement l_bin, but that scoms the subtests in backwards and is confusing
    for (auto l_index = 0; l_index <= l_bin; ++l_index)
    {
        FAPI_TRY( fapi2::putScom(i_target, l_memory_registers[l_index], l_memory_register_buffers[l_index]) );
    }

fapi_try_exit:
    return fapi2::current_err;
}

template< mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = mcbistTraits<MC, T> >
inline fapi2::ReturnCode start_stop( const fapi2::Target<T>& i_target, const bool i_start_stop )
{
    // This is the same as the CCS start_stop ... perhaps we need one template for all
    // 'engine' control functions? BRS
    fapi2::buffer<uint64_t> l_buf;
    FAPI_TRY(fapi2::getScom(i_target, TT::CNTLQ_REG, l_buf));

    FAPI_TRY( fapi2::putScom(i_target, TT::CNTLQ_REG,
                             i_start_stop ? l_buf.setBit<TT::MCBIST_START>() : l_buf.setBit<TT::MCBIST_STOP>()) );

fapi_try_exit:
    return fapi2::current_err;
}

template<  mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = mcbistTraits<MC, T> >
inline fapi2::ReturnCode load_data_config( const fapi2::Target<T>& i_target, const mcbist::program<MC>& i_program )
{
    // First load the data pattern registers
    FAPI_INF("Loading the data pattern seeds for %s!", mss::c_str(i_target));
    FAPI_TRY( mss::mcbist::load_pattern(i_target, i_program.iv_pattern) );

    // Load the 24b random data pattern seeds registers
    FAPI_INF("Loading the 24b Random data pattern seeds for %s!", mss::c_str(i_target));
    FAPI_TRY( mss::mcbist::load_random24b_seeds(i_target, i_program.iv_random24_data_seed,
              i_program.iv_random24_seed_map) );

    // Load the maint data pattern into the Maint entry in the RMW buffer
    // TK Might want to only load the RMW buffer if maint commands are present in the program
    // The load takes 33 Putscoms to load 16 64B registers, might slow down mcbist programs that
    // don't need the RMW buffer maint entry loaded
    FAPI_INF("Loading the maint data pattern into the RMW buffer for %s!", mss::c_str(i_target));
    FAPI_TRY( mss::mcbist::load_maint_pattern(i_target, i_program.iv_pattern) );

    FAPI_INF("Loading the data rotate config and seeds for %s!", mss::c_str(i_target));
    FAPI_TRY( fapi2::putScom(i_target, TT::DATA_ROTATE_CNFG_REG, i_program.iv_data_rotate_cnfg) );
    FAPI_TRY( fapi2::putScom(i_target, TT::DATA_ROTATE_SEED_REG, i_program.iv_data_rotate_seed) );

fapi_try_exit:
    return fapi2::current_err;

}

template< mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = mcbistTraits<MC, T> >
inline fapi2::ReturnCode load_control( const fapi2::Target<T>& i_target, const mcbist::program<MC>& i_program )
{
    FAPI_INF("loading MCBIST Control 0x%016lx for %s", i_program.iv_control, mss::c_str(i_target));
    return fapi2::putScom(i_target, TT::CNTLQ_REG, i_program.iv_control);
}

template< mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = mcbistTraits<MC, T>, typename ET = mcbistMCTraits<MC> >
fapi2::ReturnCode execute( const fapi2::Target<T>& i_target, const program<MC>& i_program )
{
    fapi2::buffer<uint64_t> l_status;
    bool l_poll_result = false;
    poll_parameters l_poll_parameters;

    // Init the fapi2 return code
    fapi2::current_err = fapi2::FAPI2_RC_SUCCESS;

    // Before we go off into the bushes, lets see if there are any instructions in the
    // program. If not, we can save everyone the hassle
    FAPI_ASSERT(0 != i_program.iv_subtests.size(),
                fapi2::MSS_MEMDIAGS_NO_MCBIST_SUBTESTS().set_MC_TARGET(i_target),
                "Attempt to run an MCBIST program with no subtests on %s", mss::c_str(i_target));
    FAPI_TRY( clear_error_helper<MC>(i_target, const_cast<program<MC>&>(i_program)) );
    // Configures the write/read FIFO bit
    FAPI_TRY( load_fifo_mode<MC>( i_target, i_program) );
    // Slam the address generator config
    FAPI_TRY( load_addr_gen<MC>(i_target, i_program) );
    // Slam the parameters in to the mcbist parameter register
    FAPI_TRY( load_mcbparm<MC>(i_target, i_program) );
    // Slam the configured address maps down
    FAPI_TRY( load_mcbamr( i_target, i_program) );
    // Slam the config register down
    FAPI_TRY( load_config<MC>( i_target, i_program) );
    // Slam the control register down
    FAPI_TRY( load_control<MC>( i_target, i_program) );
    // Load the patterns and any associated bits for random, etc
    FAPI_TRY( load_data_config<MC>( i_target, i_program) );
    // Load the thresholds
    FAPI_TRY( load_thresholds<MC>( i_target, i_program) );
    // Slam the subtests in to the mcbist registers
    // Always do this last so the action file triggers see the other bits set
    FAPI_TRY( load_mcbmr<MC>(i_target, i_program) );
    // Start the engine, and then poll for completion
    FAPI_TRY(start_stop<MC>(i_target, mss::START));
    // Verify that the in-progress bit has been set, so we know we started
    // Don't use the program's poll as it could be a very long time. Use the default poll.
    l_poll_result = mss::poll(i_target, TT::STATQ_REG, l_poll_parameters,
                              [&l_status](const size_t poll_remaining, const fapi2::buffer<uint64_t>& stat_reg) -> bool
    {
        l_status = stat_reg;
        // We're done polling when either we see we're in progress or we see we're done.
        return (l_status.getBit<TT::MCBIST_IN_PROGRESS>() == true) || (l_status.getBit<TT::MCBIST_DONE>() == true);
    });
    // If the user asked for async mode, we can leave. Otherwise, poll and check for errors
    if (!i_program.iv_async)
    {
        return mcbist::poll(i_target, i_program);
    }

fapi_try_exit:
    return fapi2::current_err;
}

inline fapi2::ReturnCode execute()
{
    return mss::mcbist::execute(iv_target, iv_program);
}

template< mss::mc_type MC, fapi2::TargetType T = mss::mcbistMCTraits<MC>::MC_TARGET_TYPE , typename TT = mcbistTraits<MC, T> >
fapi2::ReturnCode sf_read( const fapi2::Target<T>& i_target,
                           const stop_conditions<MC>& i_stop,
                           const mss::mcbist::address& i_address = mss::mcbist::address(),
                           const end_boundary i_end = end_boundary::STOP_AFTER_SLAVE_RANK,
                           const mss::mcbist::address& i_end_address = mss::mcbist::address(TT::LARGEST_ADDRESS) )
{
    using ET = mss::mcbistMCTraits<MC>;

    pre_maint_read_settings<MC>(i_target);

    fapi2::ReturnCode l_rc;
    constraints<MC> l_const(i_stop, speed::LUDICROUS, i_end, i_address, i_end_address);
    sf_read_operation<MC> l_read_op(i_target, l_const, l_rc);

    // calls inline fapi2::ReturnCode execute()
    return l_read_op.execute();

fapi_try_exit:
    return fapi2::current_err;
}

errlHndl_t StateMachine::doMaintCommand(WorkFlowProperties & i_wfp)
{
    errlHndl_t err = nullptr;
    uint64_t workItem;

    TargetHandle_t target;
    // starting a maint cmd ...  register a timeout monitor
    uint64_t maintCmdTO = getTimeoutValue();
    mutex_lock(&iv_mutex);
    uint64_t monitorId = CommandMonitor::INVALID_MONITOR_ID;
    i_wfp.timeoutCnt = 0; // reset for new work item
    workItem = *i_wfp.workItem;
    target = getTarget(i_wfp);
    mutex_unlock(&iv_mutex);

    TYPE trgtType = target->getAttr<ATTR_TYPE>();

    do
    {
        // new command...use the full range
        //target type is MBA
        if (TYPE_MBA == trgtType)
        {
            uint32_t stopCondition =
                mss_MaintCmd::STOP_END_OF_RANK                  |
                mss_MaintCmd::STOP_ON_MPE                       |
                mss_MaintCmd::STOP_ON_UE                        |
                mss_MaintCmd::STOP_ON_END_ADDRESS               |
                mss_MaintCmd::ENABLE_CMD_COMPLETE_ATTENTION;

            if( TARGETING::MNFG_FLAG_IPL_MEMORY_CE_CHECKING
                & iv_globals.mfgPolicy )
            {
                // For MNFG mode, check CE also
                stopCondition |= mss_MaintCmd::STOP_ON_HARD_NCE_ETE;
            }

            fapi2::buffer<uint64_t> startAddr, endAddr;
            mss_MaintCmd * cmd = nullptr;
            cmd = static_cast<mss_MaintCmd *>(i_wfp.data);
            fapi2::Target<fapi2::TARGET_TYPE_MBA> fapiMba(target);

            // We will always do ce setup though CE calculation
            // is only done during MNFG. This will give use better ffdc.
            err = ceErrorSetup<TYPE_MBA>( target );

            FAPI_INVOKE_HWP( err, mss_get_address_range, fapiMba, MSS_ALL_RANKS,
                             startAddr, endAddr );

            // new command...use the full range

            switch(workItem)
            {
                case START_RANDOM_PATTERN:

                    cmd = new mss_SuperFastRandomInit(
                            fapiMba,
                            startAddr,
                            endAddr,
                            mss_MaintCmd::PATTERN_RANDOM,
                            stopCondition,
                            false);

                    MDIA_FAST("sm: random init %p on: %x", cmd,
                            get_huid(target));
                    break;

                case START_SCRUB:

                    cmd = new mss_SuperFastRead(
                            fapiMba,
                            startAddr,
                            endAddr,
                            stopCondition,
                            false);

                    MDIA_FAST("sm: scrub %p on: %x", cmd,
                            get_huid(target));
                    break;

                case START_PATTERN_0:
                case START_PATTERN_1:
                case START_PATTERN_2:
                case START_PATTERN_3:
                case START_PATTERN_4:
                case START_PATTERN_5:
                case START_PATTERN_6:
                case START_PATTERN_7:

                    cmd = new mss_SuperFastInit(
                            fapiMba,
                            startAddr,
                            endAddr,
                            static_cast<mss_MaintCmd::PatternIndex>(workItem),
                            stopCondition,
                            false);

                    MDIA_FAST("sm: init %p on: %x", cmd,
                            get_huid(target));
                    break;

                default:
                    break;
            }

            if(!cmd)
            {
                MDIA_ERR("unrecognized maint command type %d on: %x",
                        workItem,
                        get_huid(target));
                break;
            }

            mutex_lock(&iv_mutex);

            i_wfp.data = cmd;

            mutex_unlock(&iv_mutex);

            // Command and address configured.
            // Invoke the command.
            FAPI_INVOKE_HWP( err, cmd->setupAndExecuteCmd );
            if( nullptr != err )
            {
                MDIA_FAST("sm: setupAndExecuteCmd %p failed", target);
                i_wfp.data = nullptr;
                if (cmd)
                {
                    delete cmd;
                }
            }

        }
        //target type is MCBIST
        else if (TYPE_MCBIST == trgtType)
        {
            #ifndef CONFIG_AXONE
            fapi2::Target<fapi2::TARGET_TYPE_MCBIST> fapiMcbist(target);
            mss::mcbist::stop_conditions<mss::mc_type::NIMBUS> stopCond;

            switch(workItem)
            {
                case START_RANDOM_PATTERN:

                    FAPI_INVOKE_HWP( err, nim_sf_init, fapiMcbist,
                                     mss::mcbist::PATTERN_RANDOM );
                    MDIA_FAST("sm: random init %p on: %x", fapiMcbist,
                            get_huid(target));
                    break;

                case START_SCRUB:

                    //set stop conditions
                    stopCond.set_pause_on_mpe(mss::ON);
                    stopCond.set_pause_on_ue( mss::ON);
                    stopCond.set_pause_on_aue(mss::ON);
                    stopCond.set_nce_inter_symbol_count_enable(mss::ON);
                    stopCond.set_nce_soft_symbol_count_enable( mss::ON);
                    stopCond.set_nce_hard_symbol_count_enable( mss::ON);
                    if (TARGETING::MNFG_FLAG_IPL_MEMORY_CE_CHECKING
                            & iv_globals.mfgPolicy)
                    {
                        stopCond.set_pause_on_nce_hard(mss::ON);
                    }

                    FAPI_INVOKE_HWP( err, nim_sf_read, fapiMcbist,
                                     stopCond );
                    break;

                case START_PATTERN_0:
                case START_PATTERN_1:
                case START_PATTERN_2:
                case START_PATTERN_3:
                case START_PATTERN_4:
                case START_PATTERN_5:
                case START_PATTERN_6:
                case START_PATTERN_7:

                    FAPI_INVOKE_HWP( err, nim_sf_init, fapiMcbist,
                                     workItem );
                    break;

            }
            if( nullptr != err )
            {
                i_wfp.data = nullptr;
            }
            #endif
        }
    } while(0);
    return err;
}

bool StateMachine::executeWorkItem(WorkFlowProperties * i_wfp)
{
    bool dispatched = false;

    // thread pool work item entry point

    mutex_lock(&iv_mutex);

    // ensure this thread sees the most recent state

    if(!iv_shutdown)
    {
        bool async = workItemIsAsync(*i_wfp);

        uint64_t workItem = *i_wfp->workItem;

        mutex_unlock(&iv_mutex);

        errlHndl_t err = 0;
        int32_t rc = 0;

        switch(workItem)
        {
            // do the appropriate thing based on the phase for this target

            case RESTORE_DRAM_REPAIRS:
            {
                TargetHandle_t target = getTarget( *i_wfp );
                TYPE trgtType = target->getAttr<ATTR_TYPE>();

                // MBA target
                if ( TYPE_MBA == trgtType )
                {
                    rc = PRDF::restoreDramRepairs<TYPE_MBA>( target );
                }
                // MCBIST target
                else if ( TYPE_MCBIST == trgtType )
                {
                    // Get the connected MCAs.
                    TargetHandleList mcaList;
                    getChildAffinityTargets( mcaList, target, CLASS_UNIT,
                                             TYPE_MCA );
                    for ( auto & mca : mcaList )
                    {
                        MDIA_SLOW( "sm: restoreDramRepairs(0x%08x)",
                                   get_huid(mca) );
                        rc |= PRDF::restoreDramRepairs<TYPE_MCA>( mca );
                    }
                }
                // OCMB target
                else if ( TYPE_OCMB_CHIP == trgtType )
                {
                    rc = PRDF::restoreDramRepairs<TYPE_OCMB_CHIP>( target );
                }
                break;
            }
            case START_PATTERN_0:
            case START_PATTERN_1:
            case START_PATTERN_2:
            case START_PATTERN_3:
            case START_PATTERN_4:
            case START_PATTERN_5:
            case START_PATTERN_6:
            case START_PATTERN_7:
            case START_RANDOM_PATTERN:
            case START_SCRUB:

                err = doMaintCommand(*i_wfp);

                break;

            case CLEAR_HW_CHANGED_STATE:

                mutex_lock(&iv_mutex);

                clearHWStateChanged(getTarget(*i_wfp));

                mutex_unlock(&iv_mutex);

                break;

            case ANALYZE_IPL_MNFG_CE_STATS:
            {
                bool calloutMade = false;
                TargetHandle_t target = getTarget( *i_wfp );
                rc = PRDF::analyzeIplCEStats( target,
                                              calloutMade );
            }
                break;

            default:
                break;
        }

        mutex_lock(&iv_mutex);

        if(err || rc)
        {
            // stop the workFlow for this target

            i_wfp->status = FAILED;
            i_wfp->log = err;
        }

        else if(!async)
        {
            // sync work item -
            // move the workFlow pointer to the next phase
            ++i_wfp->workItem;
        }

        if(err || !async)
        {
            // check to see if this was the last workFlow
            // in progress (if there was an error), or for sync
            // work items, schedule the next work item
            dispatched = scheduleWorkItem(*i_wfp);
        }
    }
    mutex_unlock(&iv_mutex);

    return dispatched;
}

void StateMachine::start()
{
    mutex_lock(&iv_mutex);
    iv_shutdown = false;

    // schedule the first work items for all target / workFlow associations

    for(WorkFlowPropertiesIterator wit = iv_workFlowProperties.begin();
        wit != iv_workFlowProperties.end();
        ++wit)
    {
        // bool StateMachine::executeWorkItem(WorkFlowProperties * i_wfp)
        // this is probably later called on it
        scheduleWorkItem(**wit);
    }

    mutex_unlock(&iv_mutex);
}

errlHndl_t StateMachine::run(const WorkFlowAssocMap & i_list)
{
    // load the workflow properties
    setup(i_list);
    // start work items
    start();
    // wait for all work items to finish
    wait();
    return 0;
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
        // list[0] = 12 (START_SCRUB)
        // list[1] = 0 (START_PATTERN_0)
        err = getWorkFlow(mode, list[*tit], globals);
    }

    if(nullptr == err)
    {
        // set global data
        Singleton<StateMachine>::instance().setGlobals(globals);
        // calling errlHndl_t StateMachine::run(const WorkFlowAssocMap & i_list)
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
