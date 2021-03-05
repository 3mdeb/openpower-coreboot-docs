NOTE: `mss_SuperFastRandomInit`, `mss_SuperFastRead` and `mss_SuperFastInit`
related analysis is located in [mss_SuperFast.md](mss_SuperFast.md)
```cpp
fapi2::ReturnCode broadcast_out_of_sync(const fapi2::Target<fapi2::TARGET_TYPE_MCBIST>& i_target,
        const mss::states i_value)
{
    fapi2::buffer<uint64_t> l_mcbist_action_buffer;
    // Change Broadcast of out sync to checkstop post workaround
    // Current FIR register API resets FIR mask registers when setting up FIR
    // This can result in FIRs being incorrectly unmasked after being handled in memdiags
    // The scoms below set the mask to checkstop while preserving the current mask state
    mss::getScom(i_target, MCBIST_MCBISTFIRACT1, l_mcbist_action_buffer);
    l_mcbist_action_buffer.clearBit<MCBIST_MCBISTFIRQ_MCBIST_BRODCAST_OUT_OF_SYNC>();
    mss::putScom(i_target, MCBIST_MCBISTFIRACT1, l_mcbist_action_buffer);

    for (const auto& p : mss::find_targets<fapi2::TARGET_TYPE_MCA>(i_target))
    {
        fapi2::buffer<uint64_t> l_recr_buffer;
        // Set UE noise window for workaround
        // ----
        // mss::read_recr_register(p, l_recr_buffer);
        mss::getScom(p, TT::ECC_REG, l_recr_buffer);
        // ----
        // mss::set_enable_ue_noise_window(l_recr_buffer, i_value);
        l_recr_buffer.template writeBit<portTraits<mss::mc_type::NIMBUS>::RECR_ENABLE_UE_NOISE_WINDOW>(i_value);
        // ----
        // mss::write_recr_register(p, l_recr_buffer);
        mss::putScom(p, TT::ECC_REG, l_recr_buffer);
        // -----
    }
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
        fapi2::buffer<uint64_t> l_data;

        // Read out the wr_done and rd_tag delays and find min
        // and set the RCD Protect Time to this value
        // ----
        // mss::read_dsm0q_register(p, dsm0_buffer);
        mss::getScom(p, TT::DSM0Q_REG, dsm0_buffer);
        // ----
        // mss::get_wrdone_delay(dsm0_buffer, wr_done_delay);
        dsm0_buffer.template extractToRight<24, 6>(wr_done_delay);
        // mss::get_rdtag_delay(dsm0_buffer, rd_tag_delay);
        rcd_protect_time = min(wr_done_delay, rd_tag_delay);

        // ----
        // mss::change_rcd_protect_time(p, rcd_protect_time);
        // get farb0q register
        mss::getScom(p, 0x7010913, l_data);
        // set rcd protect time
        l_data.template insertFromRight<48, 6>(rcd_protect_time);
        // set farb0q register
        mss::putScom(p, 0x7010913, l_data);
        // ----

        l_ecc64_fir_reg.checkstop<MCA_FIR_MAINLINE_AUE>()
          .recoverable_error<MCA_FIR_MAINLINE_UE>()
          .checkstop<MCA_FIR_MAINLINE_IAUE>()
          .recoverable_error<MCA_FIR_MAINLINE_IUE>();

        l_cal_fir_reg.recoverable_error<MCA_MBACALFIRQ_PORT_FAIL>();

        // If ATTR_CHIP_EC_FEATURE_HW414700 is enabled set checkstops
        // True for Nimbus DD2.0 chip_ec == 0x20
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
        // ----
        // mss::change_port_fail_disable(p, mss::LOW);
        mss::getScom(p, TT::FARB0Q_REG, l_data);
        l_data.writeBit<TT::PORT_FAIL_DISABLE>(mss::LOW);
        mss::putScom(p, TT::FARB0Q_REG, l_data);
        // ----
        // mss::change_rcd_recovery_disable(p, mss::LOW);
        mss::getScom(p, TT::FARB0Q_REG, l_data);
        l_data.writeBit<TT::RCD_RECOVERY_DISABLE>(mss::LOW);
        mss::putScom(p, TT::FARB0Q_REG, l_data);
        // ----
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
        ATTR_RECONFIGURE_LOOP_type reconfigAttr = top->getAttr<TARGETING::ATTR_RECONFIGURE_LOOP>();
        reconfigAttr &= ~RECONFIGURE_LOOP_BAD_DQ_BIT_SET;
        top->setAttr<TARGETING::ATTR_RECONFIGURE_LOOP>(reconfigAttr);

        // all workFlows are finished
        // release the init service dispatcher
        // thread waiting for completion
        iv_done = true;
        // synconizes threads
        // void sync_cond_broadcast(sync_cond_t * i_cond)
    } else if(i_wfp.status == IN_PROGRESS)
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
            iv_tp = new Util::ThreadPool<WorkItem>();
            iv_tp->start();
        }
        TargetHandle_t target = getTarget(i_wfp);
        iv_tp->insert(new WorkItem(*this, &i_wfp, priority, i_wfp.chipUnit));

        return true;
    }
    return false;
}

template<mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = mcbistTraits<MC, T> >
fapi2::ReturnCode load_mcbmr( const fapi2::Target<T>& i_target, const mcbist::program<MC>& i_program )
{
    // List of the 8 MCBIST registers - each holds 4 subtests.
    const std::vector<uint64_t> l_memory_registers =
    {
        TT::MCBMR0_REG, TT::MCBMR1_REG, TT::MCBMR2_REG, TT::MCBMR3_REG,
        TT::MCBMR4_REG, TT::MCBMR5_REG, TT::MCBMR6_REG, TT::MCBMR7_REG,
    };
    std::vector<uint64_t> l_memory_register_buffers = {0, 0, 0, 0, 0, 0, 0, 0};
    ssize_t l_bin = -1;
    size_t l_register_shift = 0;

    // We'll shift this in to position to indicate which subtest is the last
    // TT::DONE = MCBIST_MCBMR0Q_MCBIST_CFG_TEST00_DONE
    const uint64_t l_done_bit(0x8000000000000000 >> TT::DONE);
    // For now limit MCBIST programs to 32 subtests.
    const auto l_program_size = i_program.iv_subtests.size();
    // Distribute the program over the 8 MCBIST subtest registers
    // We need the index, so increment thru i_program.iv_subtests.size()
    for (size_t l_index = 0; l_index < l_program_size; ++l_index)
    {
        l_bin = (l_index % TT::SUBTEST_PER_REG) == 0 ? l_bin + 1 : l_bin;
        l_register_shift = (l_index % TT::SUBTEST_PER_REG) * TT::BITS_IN_SUBTEST;
        l_memory_register_buffers[l_bin] |=
            (uint64_t(i_program.iv_subtests[l_index].iv_mcbmr) << TT::LEFT_SHIFT) >> l_register_shift;
    }

    // l_bin and l_register_shift are the values for the last subtest we'll tell the MCBIST about.
    // We need to set that subtest's done-bit so the MCBIST knows it's the end of the line
    l_memory_register_buffers[l_bin] |= l_done_bit >> l_register_shift;

    // ... and slam the values in to the registers.
    // Could just decrement l_bin, but that scoms the subtests in backwards and is confusing
    for (auto l_index = 0; l_index <= l_bin; ++l_index)
    {
        fapi2::putScom(i_target, l_memory_registers[l_index], l_memory_register_buffers[l_index]);
    }
}

template<  mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = mcbistTraits<MC, T> >
inline fapi2::ReturnCode load_data_config( const fapi2::Target<T>& i_target, const mcbist::program<MC>& i_program )
{
    // First load the data pattern registers
    mss::mcbist::load_pattern(i_target, i_program.iv_pattern);

    // Load the 24b random data pattern seeds registers
    mss::mcbist::load_random24b_seeds(i_target, i_program.iv_random24_data_seed,
              i_program.iv_random24_seed_map);

    // Load the maint data pattern into the Maint entry in the RMW buffer
    // TK Might want to only load the RMW buffer if maint commands are present in the program
    // The load takes 33 Putscoms to load 16 64B registers, might slow down mcbist programs that
    // don't need the RMW buffer maint entry loaded
    mss::mcbist::load_maint_pattern(i_target, i_program.iv_pattern);

    fapi2::putScom(i_target, TT::DATA_ROTATE_CNFG_REG, i_program.iv_data_rotate_cnfg);
    fapi2::putScom(i_target, TT::DATA_ROTATE_SEED_REG, i_program.iv_data_rotate_seed);
}

// using cache_line = std::pair<uint64_t, uint64_t>;
// using pattern = std::vector<cache_line>;
template<  mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = mcbistTraits<MC, T> >
fapi2::ReturnCode load_maint_pattern( const fapi2::Target<T>& i_target, const pattern& i_pattern, const bool i_invert )
{
    // The scom registers are in the port target. PT: port traits
    using PT = mcbistTraits<MC, TT::PORT_TYPE>;
    // Init the fapi2 return code
    fapi2::current_err = fapi2::FAPI2_RC_SUCCESS;
    // Array access control
    fapi2::buffer<uint64_t> l_aacr;
    // Array access data
    fapi2::buffer<uint64_t> l_aadr;

    // first we must setup the access control register
    // Setup the array address
    // enable the auto increment bit
    // set ecc mode bit on
    l_aacr
    .template writeBit<PT::RMW_WRT_BUFFER_SEL>(mss::states::OFF)
    .template insertFromRight<PT::RMW_WRT_ADDRESS, PT::RMW_WRT_ADDRESS_LEN>(PT::MAINT_DATA_INDEX_START)
    .template writeBit<PT::RMW_WRT_AUTOINC>(mss::states::ON)
    .template writeBit<PT::RMW_WRT_ECCGEN>(mss::states::ON);

    // This loop will be run twice to write the pattern twice.  Once per 64B write.
    // When MCBIST maint mode is in 64B mode it will only use the first 64B when in 128B mode
    // MCBIST maint will use all 128B (it will perform two consecutive writes)
    const auto l_ports =  mss::find_targets<TT::PORT_TYPE>(i_target);
    // Init the port map
    for (const auto& p : l_ports)
    {
        l_aacr.template insertFromRight<PT::RMW_WRT_ADDRESS, PT::RMW_WRT_ADDRESS_LEN>(PT::MAINT_DATA_INDEX_START);
        for (auto l_num_writes = 0; l_num_writes < 2; ++l_num_writes)
        {
            fapi2::putScom(p, PT::RMW_WRT_BUF_CTL_REG, l_aacr);
            for (const auto& l_cache_line : i_pattern)
            {
                fapi2::buffer<uint64_t> l_value_first  = i_invert ? ~l_cache_line.first : l_cache_line.first;
                fapi2::buffer<uint64_t> l_value_second = i_invert ? ~l_cache_line.second : l_cache_line.second;
                fapi2::putScom(p, PT::RMW_WRT_BUF_DATA_REG, l_value_first);
                // In order for the data to actually be written into the RMW buffer, we must issue a putscom to the MCA_AAER register
                // This register is used for the ECC, we will just write all zero to this register.  The ECC will be auto generated
                // when the aacr MCA_WREITE_AACR_ECCGEN bit is set
                fapi2::putScom(p, PT::RMW_WRT_BUF_ECC_REG, 0);
                // No need to increment the address because the logic does it automatically when MCA_WREITE_AACR_AUTOINC is set
                fapi2::putScom(p, PT::RMW_WRT_BUF_DATA_REG, l_value_second);
                // In order for the data to actually be written into the RMW buffer, we must issue a putscom to the MCA_AAER register
                // This register is used for the ECC, we will just write all zero to this register.  The ECC will be auto generated
                // when the aacr MCA_WREITE_AACR_ECCGEN bit is set
                fapi2::putScom(p, PT::RMW_WRT_BUF_ECC_REG, 0);
            }
            l_aacr.template insertFromRight<PT::RMW_WRT_ADDRESS, PT::RMW_WRT_ADDRESS_LEN>(PT::MAINT_DATA_INDEX_END);
        }
    }
}

template< mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = mcbistTraits<MC, T> >
inline fapi2::ReturnCode load_config( const fapi2::Target<T>& i_target, const mcbist::program<MC>& i_program )
{
    // Copy the program's config settings - we want to modify them if we're in sim.
    fapi2::buffer<uint64_t> l_config = i_program.iv_config;
    fapi2::putScom(i_target, TT::CFGQ_REG, l_config);
}

inline bool fifo_mode_required() const
{
    // Gets the op type for this subtest
    uint64_t l_value_to_find = 0;
    iv_mcbmr.extractToRight<TT::OP_TYPE, TT::OP_TYPE_LEN>(l_value_to_find);

    // Finds if this op type is in the vector that stores the OP types that require FIFO mode to be run
    const auto l_op_type_it = std::find(TT::FIFO_MODE_REQUIRED_OP_TYPES.begin(), TT::FIFO_MODE_REQUIRED_OP_TYPES.end(), l_value_to_find);
    // If the op type is required (aka was found), it will be less than end
    // std::find returns the ending iterator if it was not found, so this will return false in that case
    return l_op_type_it != TT::FIFO_MODE_REQUIRED_OP_TYPES.end();
}

template<  mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = mcbistTraits<MC, T> >
inline fapi2::ReturnCode load_fifo_mode( const fapi2::Target<T>& i_target, const mcbist::program<MC>& i_program )
{
    // Checks if FIFO mode is required by checking all subtests
    const auto l_subtest_it = std::find_if(i_program.iv_subtests.begin(),
                                           i_program.iv_subtests.end(),
                                           []( const mss::mcbist::subtest_t<MC, T, TT>& i_rhs) -> bool
                                           {
                                               return i_rhs.fifo_mode_required();
                                           });

    // if the FIFO load is not needed (no subtest requiring it was found), just exit out
    if(l_subtest_it == i_program.iv_subtests.end())
    {
        return;
    }

    // ----
    // configure_wrq(i_target, FIFO_ON);
    for(const auto& l_port : mss::find_targets<TT::PORT_TYPE>(i_target) )
    {
        fapi2::buffer<uint64_t> l_data;
        mss::getScom(l_port, TT::WRQ_REG, l_data);
        l_data.writeBit<TT::WRQ_FIFO_MODE>(1);
        mss::putScom(l_port, TT::WRQ_REG, l_data);
    }
    // ----
    // configure_rrq(i_target, FIFO_ON);
    for(const auto& l_port : mss::find_targets<TT::PORT_TYPE>(i_target))
    {
        fapi2::buffer<uint64_t> l_data;
        mss::getScom(l_port, TT::RRQ_REG, l_data);
        l_data.writeBit<TT::RRQ_FIFO_MODE>(1);
        mss::putScom(l_port, TT::RRQ_REG, l_data);
    }
    // ----
}

template< mss::mc_type MC = DEFAULT_MC_TYPE, fapi2::TargetType T, typename TT = mcbistTraits<MC, T>, typename ET = mcbistMCTraits<MC> >
fapi2::ReturnCode execute(const fapi2::Target<T>& i_target, const program<MC>& i_program)
{
    fapi2::buffer<uint64_t> l_status;
    poll_parameters l_poll_parameters;

    // Init the fapi2 return code
    fapi2::current_err = fapi2::FAPI2_RC_SUCCESS;

    // ----
    // clear_errors(i_target);
    fapi2::putScom(i_target, TT::MCBSTATQ_REG, 0);
    fapi2::putScom(i_target, TT::SRERR0_REG, 0);
    fapi2::putScom(i_target, TT::SRERR1_REG, 0);
    fapi2::putScom(i_target, TT::FIRQ_REG, 0);
    // ----
    load_fifo_mode<MC>(i_target, i_program);
    load_addr_gen<MC>(i_target, i_program);
    // ----
    // load_mcbparm<MC>(i_target, i_program);
    fapi2::putScom(i_target, TT::MCBPARMQ_REG, i_program.iv_parameters);
    // ----
    // load_mcbamr(i_target, i_program);
    fapi2::putScom(i_target, TT::MCBAMR0A0Q_REG, i_program.iv_addr_map0);
    fapi2::putScom(i_target, TT::MCBAMR1A0Q_REG, i_program.iv_addr_map1);
    fapi2::putScom(i_target, TT::MCBAMR2A0Q_REG, i_program.iv_addr_map2);
    fapi2::putScom(i_target, TT::MCBAMR3A0Q_REG, i_program.iv_addr_map3);
    // ----
    load_config<MC>( i_target, i_program);
    // ----
    // load_control<MC>(i_target, i_program);
    fapi2::putScom(i_target, TT::CNTLQ_REG, i_program.iv_control);
    // ----
    load_data_config<MC>( i_target, i_program);
    // ----
    // load_thresholds<MC>(i_target, i_program);
    fapi2::putScom(i_target, TT::THRESHOLD_REG, i_program);
    // ----
    load_mcbmr<MC>(i_target, i_program);
    // ----
    // start_stop<MC>(i_target, mss::START);
    fapi2::buffer<uint64_t> l_buf;
    fapi2::getScom(i_target, TT::CNTLQ_REG, l_buf);
    fapi2::putScom(i_target, TT::CNTLQ_REG, l_buf.setBit<TT::MCBIST_START>());
    // ----
    // Verify that the in-progress bit has been set, so we know we started
    // Don't use the program's poll as it could be a very long time. Use the default poll.
    mss::poll(i_target, TT::STATQ_REG, l_poll_parameters,
                              [&l_status](const size_t poll_remaining, const fapi2::buffer<uint64_t>& stat_reg) -> bool
    {
        l_status = stat_reg;
        return (l_status.getBit<TT::MCBIST_IN_PROGRESS>() == true) || (l_status.getBit<TT::MCBIST_DONE>() == true);
    });
    return mcbist::poll(i_target, i_program);
}

inline fapi2::ReturnCode execute()
{
    return mss::mcbist::execute(iv_target, iv_program);
}

errlHndl_t StateMachine::doMaintCommand(WorkFlowProperties & i_wfp)
{
    errlHndl_t err = nullptr;
    uint64_t workItem;

    TargetHandle_t target;

    // starting a maint cmd ...  register a timeout monitor
    uint64_t maintCmdTO = getTimeoutValue();

    uint64_t monitorId = CommandMonitor::INVALID_MONITOR_ID;
    i_wfp.timeoutCnt = 0; // reset for new work item
    workItem = *i_wfp.workItem;

    target = getTarget(i_wfp);

    TYPE trgtType = target->getAttr<ATTR_TYPE>();
    // new command...use the full range
    // target type is MBA
    if (TYPE_MBA == trgtType)
    {
        uint32_t stopCondition =
            mss_MaintCmd::STOP_END_OF_RANK                  |
            mss_MaintCmd::STOP_ON_MPE                       |
            mss_MaintCmd::STOP_ON_UE                        |
            mss_MaintCmd::STOP_ON_END_ADDRESS               |
            mss_MaintCmd::ENABLE_CMD_COMPLETE_ATTENTION;

        if(TARGETING::MNFG_FLAG_IPL_MEMORY_CE_CHECKING & iv_globals.mfgPolicy)
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
        err = ceErrorSetup<TYPE_MBA>(target);

        FAPI_INVOKE_HWP(err, mss_get_address_range, fapiMba, MSS_ALL_RANKS, startAddr, endAddr);
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
                break;
            case START_SCRUB:

                cmd = new mss_SuperFastRead(
                        fapiMba,
                        startAddr,
                        endAddr,
                        stopCondition,
                        false);
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
                break;
            default:
                break;
        }
        i_wfp.data = cmd;
        // Command and address configured.
        // Invoke the command.
        FAPI_INVOKE_HWP(err, cmd->setupAndExecuteCmd );
    }
    //target type is MCBIST
    else if(TYPE_MCBIST == trgtType)
    {
        fapi2::Target<fapi2::TARGET_TYPE_MCBIST> fapiMcbist(target);
        mss::mcbist::stop_conditions<mss::mc_type::NIMBUS> stopCond;
        switch(workItem)
        {
            case START_RANDOM_PATTERN:
                FAPI_INVOKE_HWP(err, sf_init, fapiMcbist, mss::mcbist::PATTERN_RANDOM);
                break;
            case START_SCRUB:
                //set stop conditions
                stopCond.set_pause_on_mpe(mss::ON);
                stopCond.set_pause_on_ue(mss::ON);
                stopCond.set_pause_on_aue(mss::ON);
                stopCond.set_nce_inter_symbol_count_enable(mss::ON);
                stopCond.set_nce_soft_symbol_count_enable(mss::ON);
                stopCond.set_nce_hard_symbol_count_enable(mss::ON);
                if(TARGETING::MNFG_FLAG_IPL_MEMORY_CE_CHECKING & iv_globals.mfgPolicy)
                {
                    stopCond.set_pause_on_nce_hard(mss::ON);
                }
                FAPI_INVOKE_HWP(err, nim_sf_read, fapiMcbist, stopCond);
                break;
            case START_PATTERN_0:
            case START_PATTERN_1:
            case START_PATTERN_2:
            case START_PATTERN_3:
            case START_PATTERN_4:
            case START_PATTERN_5:
            case START_PATTERN_6:
            case START_PATTERN_7:
                FAPI_INVOKE_HWP(err, sf_init, fapiMcbist, workItem);
                break;
            default:
                break;
        }
    }
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
}

fapi2::ReturnCode custom_read_ctr::execute( const fapi2::Target<fapi2::TARGET_TYPE_MCA>& i_target,
        const uint64_t i_rp,
        const uint8_t i_abort_on_error ) const
{
    reset_data_pattern();
    return step::execute(i_target, i_rp, i_abort_on_error);
}

bool StateMachine::executeWorkItem(WorkFlowProperties * i_wfp)
{
    bool dispatched = false;
    uint64_t workItem = *i_wfp->workItem;
    errlHndl_t err = 0;
    int32_t rc = 0;

    switch(workItem)
    {
        case RESTORE_DRAM_REPAIRS:
        {
            TargetHandle_t target = getTarget(*i_wfp);
            // Get the connected MCAs.
            TargetHandleList mcaList;
            getChildAffinityTargets(mcaList, target, CLASS_UNIT, TYPE_MCA);
            for (auto & mca : mcaList)
            {
                PRDF::restoreDramRepairs<TYPE_MCA>(mca);
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
            clearHWStateChanged(getTarget(*i_wfp));
            break;
        case ANALYZE_IPL_MNFG_CE_STATS:
            TargetHandle_t target = getTarget(*i_wfp);
            rc = PRDF::analyzeIplCEStats(target, false);
            break;

        default:
            break;
    }
    ++i_wfp->workItem;
    dispatched = scheduleWorkItem(*i_wfp);
    return dispatched;
}

void StateMachine::start()
{
    // schedule the first work items for all target / workFlow associations
    for(WorkFlowPropertiesIterator wit = iv_workFlowProperties.begin();
        wit != iv_workFlowProperties.end(); ++wit)
    {
        // bool StateMachine::executeWorkItem(WorkFlowProperties * i_wfp)
        // this is probably later called on it
        scheduleWorkItem(**wit);
    }
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
    Globals globals;
    TargetHandle_t top = nullptr;
    targetService().getTopLevelTarget(top);

    if(top)
    {
        globals.mfgPolicy = top->getAttr<ATTR_MNFG_FLAGS>();
        // by default 0
        // see hostboot/src/usr/targeting/common/xmltohb/attribute_types.xml:7292
        uint8_t maxMemPatterns = top->getAttr<ATTR_RUN_MAX_MEM_PATTERNS>();
        // This registry / attr is the same as the
        // exhaustive mnfg one
        if(maxMemPatterns)
        {
            globals.mfgPolicy |= MNFG_FLAG_ENABLE_EXHAUSTIVE_PATTERN_TEST;
        }
        globals.simicsRunning = false;
    }

    // get the workflow for each target mba passed in.
    // associate each workflow with the target handle.
    WorkFlowAssocMap list;
    TargetHandleList::const_iterator tit;
    DiagMode mode;
    for(tit = i_targetList.begin(); tit != i_targetList.end(); ++tit)
    {
        // mode = 0 (ONE_PATTERN) is the default output
        getDiagnosticMode(globals, *tit, mode);
        // create a list with patterns
        // for ONE_PATTERN the list is as follows
        // list[0] = 12 (START_SCRUB)
        // list[1] = 0 (START_PATTERN_0)
        getWorkFlow(mode, list[*tit], globals);
    }

    // set global data
    Singleton<StateMachine>::instance().setGlobals(globals);
    // calling errlHndl_t StateMachine::run(const WorkFlowAssocMap & i_list)
    Singleton<StateMachine>::instance().run(list);

    // If this step completes without the need for a reconfig due to an RCD
    // parity error, clear all RCD parity error counters.
    ATTR_RECONFIGURE_LOOP_type attr = top->getAttr<ATTR_RECONFIGURE_LOOP>();
    if (0 == (attr & RECONFIGURE_LOOP_RCD_PARITY_ERROR))
    {
        TargetHandleList trgtList; getAllChiplets(trgtList, TYPE_MCA);
        for (auto & trgt : trgtList)
        {
            if (0 != trgt->getAttr<ATTR_RCD_PARITY_RECONFIG_LOOP_COUNT>())
                trgt->setAttr<ATTR_RCD_PARITY_RECONFIG_LOOP_COUNT>(0);
        }
    }
}

errlHndl_t __runMemDiags(TargetHandleList i_trgtList)
{
    // The service is started to handle all the interrupts
    // ATTN::startService();
    MDIA::runStep(i_trgtList);
    // ATTN::stopService();
}

void* call_mss_memdiag(void* io_pArgs)
{
    TARGETING::Target* masterproc = nullptr;
    TARGETING::targetService().masterProcChipTargetHandle(masterproc);

    TargetHandleList trgtList;
    getAllChiplets(trgtList, TYPE_MCBIST);
    // Start Memory Diagnostics.
    errl = __runMemDiags(trgtList);

    for(auto & tt : trgtList)
    {
        fapi2::Target<fapi2::TARGET_TYPE_MCBIST> ft (tt);
        // Unmask mainline FIRs.
        mss::unmask::after_memdiags(ft);
        // Turn off FIFO mode to improve performance.

        // ****
        // mss::reset_reorder_queue_settings(ft);
        uint8_t l_reorder_queue = 0;
        FAPI_ATTR_GET(fapi2::ATTR_MSS_REORDER_QUEUE_SETTING, i_target, l_reorder_queue)

        // Changes the reorder queue settings
        // Two settings are FIFO and REORDER.  FIFO is a 1 in the registers, while reorder is a 0 state
        const mss::states l_state =  ((l_reorder_queue == fapi2::ENUM_ATTR_MEM_REORDER_QUEUE_SETTING_FIFO) ?
                                        mss::states::ON : mss::states::OFF);
        // ----
        // configure_rrq(i_target, l_state);
        for( const auto& l_port : mss::find_targets<TT::PORT_TYPE>(i_target))
        {
            fapi2::buffer<uint64_t> l_data;
            mss::getScom(l_port, TT::RRQ_REG, l_data);
            l_data.writeBit<TT::RRQ_FIFO_MODE>(l_state == mss::states::ON);
            mss::putScom(l_port, TT::RRQ_REG, l_data);
        }
        // -----
        // configure_wrq(i_target, l_state);
        for(const auto& l_port : mss::find_targets<TT::PORT_TYPE>(i_target))
        {
            fapi2::buffer<uint64_t> l_data;
            mss::getScom(l_port, TT::WRQ_REG, l_data);
            l_data.writeBit<TT::WRQ_FIFO_MODE>(l_state == mss::states::ON);
            mss::putScom(l_port, TT::WRQ_REG, l_data);
        }
        // ----
        // ****
    }
}
```
