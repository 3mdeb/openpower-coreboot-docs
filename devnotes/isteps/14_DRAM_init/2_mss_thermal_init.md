```cpp
void* call_mss_thermal_init (void *io_pArgs)
{
    IStepError  l_StepError;
    nimbus_call_mss_thermal_init(l_StepError);
    run_proc_throttle_sync(l_StepError);
}

void nimbus_call_mss_thermal_init(IStepError & io_istepError)
{
    errlHndl_t  l_errl  =   nullptr;
    // -- Nimbus only ---
    // Get all MCS targets
    TARGETING::TargetHandleList l_mcsTargetList;
    getAllChiplets(l_mcsTargetList, TYPE_MCS);
    //  --------------------------------------------------------------------
    //  run mss_thermal_init on all functional MCS chiplets
    //  --------------------------------------------------------------------
    for (const auto & l_pMcs : l_mcsTargetList)
    {
        fapi2::Target<fapi2::TARGET_TYPE_MCS> l_fapi_pMcs(l_pMcs);
        p9_mss_thermal_init(l_fapi_pMcs);
    }

}

void run_proc_throttle_sync(IStepError & io_istepError)
{
    // Run proc throttle sync
    // Get all functional proc chip targets
    // Use targeting code to get a list of all processors
    TARGETING::TargetHandleList l_procChips;
    getAllChips(l_procChips, TARGETING::TYPE_PROC);
    for (const auto & l_procChip: l_procChips)
    {
        //C onvert the TARGETING::Target into a fapi2::Target by passing
        // l_procChip into the fapi2::Target constructor
        fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>l_fapi2CpuTarget(l_procChip);
        p9_throttle_sync(l_fapi2CpuTarget);
    }
}

fapi2::ReturnCode p9_mss_thermal_init( const fapi2::Target<TARGET_TYPE_MCS>& i_target )
{
    // Prior to starting OCC, we go into "safemode" throttling
    // After OCC is started, they can change throttles however they want
    for (const auto& l_mca : mss::find_targets<TARGET_TYPE_MCA>(i_target))
    {
        // ----
        // mss::mc::set_runtime_throttles_to_safe(l_mca);
        typedef mss::mcTraits<fapi2::TARGET_TYPE_MCA> TT;
        fapi2::buffer<uint64_t> l_data;
        mss::getScom(l_mca, MCA_MBA_FARB3Q, l_data);
        //Same value for both throttles
        // fapi2::ATTR_MSS_MRW_SAFEMODE_MEM_THROTTLED_N_COMMANDS_PER_PORT
        l_data.insertFromRight<TT::RUNTIME_N_SLOT, TT::RUNTIME_N_SLOT_LEN>(32);
        l_data.insertFromRight<TT::RUNTIME_N_PORT, TT::RUNTIME_N_PORT_LEN>(32);
        // fapi2::ATTR_MSS_MRW_MEM_M_DRAM_CLOCKS
        l_data.insertFromRight<TT::RUNTIME_M, TT::RUNTIME_M_LEN>(0x200);
        mss::putScom(l_mca, MCA_MBA_FARB3Q, l_data);
        // ----
    }
    // ----
    // mss::mc::disable_emergency_throttle(i_target);
    fapi2::buffer<uint64_t> l_data;
    mss::getScom(i_target, MCS_MCMODE0, l_data);
    l_data.clearBit<MCS_MCMODE0_ENABLE_EMER_THROTTLE>();
    mss::putScom(i_target, MCS_MCMODE0, l_data);
    // ----
}

fapi2::ReturnCode p9_throttle_sync(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    auto l_mcsChiplets = i_target.getChildren<fapi2::TARGET_TYPE_MCS>();
    // Get the functional MCS on this proc
    if (l_mcsChiplets.size() > 0)
    {
        throttleSync(l_mcsChiplets, false);
    }
}

template< fapi2::TargetType T>
fapi2::ReturnCode throttleSync(
    const std::vector< fapi2::Target<T> >& i_mcTargets,
    fapi2::ATTR_CHIP_EC_FEATURE_HW397255_Type i_HW397255_enabled)
{
    // MAX_MC_SIDES_PER_PROC = 2
    mcSideInfo_t<T> l_mcSide[MAX_MC_SIDES_PER_PROC];
    uint8_t l_sideNum = 0;
    uint8_t l_pos = 0;
    uint8_t l_numMasterProgrammed = 0;

    // Initialization
    // MAX_MC_SIDES_PER_PROC = 2
    for (l_sideNum = 0; l_sideNum < MAX_MC_SIDES_PER_PROC; l_sideNum++)
    {
        l_mcSide[l_sideNum].masterMcFound = false;
    }

    // ---------------------------------------------------------------------
    // 1. Pick the first MCS/MI with DIMMS as potential master
    //    for both MC sides (MC01/MC23)
    // ---------------------------------------------------------------------
    for (auto l_mc : i_mcTargets)
    {
        uint8_t l_num_dimms = findNumDimms(l_mc);

        if (l_num_dimms > 0)
        {
            // This MCS or MI has DIMMs attached, find out which MC side it
            // belongs to:
            //    l_sideNum = 0 --> MC01
            //                1 --> MC23
            // fapi2::ATTR_CHIP_UNIT_POS by default 0
            FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, l_mc, l_pos);
            // MAX_MC_SIDES_PER_PROC = 2
            l_sideNum = l_pos / MAX_MC_SIDES_PER_PROC;


            // If there's no master MCS or MI marked for this side yet, mark
            // this MCS as master
            if (l_mcSide[l_sideNum].masterMcFound == false)
            {
                l_mcSide[l_sideNum].masterMcFound = true;
                l_mcSide[l_sideNum].masterMc = l_mc;
            }
        }
        progMCMODE0(l_mc, i_mcTargets);
    }

    // --------------------------------------------------------------
    // 2. Program the master MCS or MI
    // --------------------------------------------------------------
    // MAX_MC_SIDES_PER_PROC = 2
    for (l_sideNum = 0; l_sideNum < MAX_MC_SIDES_PER_PROC; l_sideNum++)
    {
        // If there is a potential master MCS or MI found for this side
        if (l_mcSide[l_sideNum].masterMcFound == true)
        {
            // No master MCS or MI programmed for either side yet,
            // go ahead and program this MCS or MI as master.
            if (l_numMasterProgrammed == 0)
            {
                progMaster(l_mcSide[l_sideNum].masterMc);
                l_numMasterProgrammed++;
            }
        }
    }
}

template< fapi2::TargetType T>
fapi2::ReturnCode progMaster(const fapi2::Target<T>& i_mcTarget)
{
    fapi2::ReturnCode l_rc;
    fapi2::buffer<uint64_t> l_scomData(0);
    fapi2::buffer<uint64_t> l_scomMask(0);

    // -------------------------------------------------------------------
    // 1. Reset sync command
    // -------------------------------------------------------------------

    // Note:
    // MCS_MCSYNC reg bit 16 is now used to setup SYNC_GO for both channels.
    // Bit 17 is now reserved (it was MCS_MCSYNC_SYNC_GO_CH1 before)
    //   REGISTER MCSYNC(mcsync(0:27), 0x000000000);
    //      MCSYNC.address(SCOM) += 0x00000015;
    //      MCSYNC.comment = "MC Sync Command Register (MCSYNC)";
    //      MCSYNC.attr(part_decl) = "0:7   = MCSYNC_Channel_Select"
    //                               "8:15  = MCSYNC_Sync_Type"
    //                               "16    = MCSYNC_Sync_Go"
    //                               "17:27 = MCSYNC_Reserved";
    //      MCSYNC.attr(access) = "**::SCOM = RW";
    //      MCSYNC.attr(parity) = "mcSYNC_pe";
    //      MCSYNC.attr(wpulse) = "act_sc15";

    // MCS_MCSYNC_SYNC_GO_CH0 = 16
    l_scomMask.flush<0>().setBit<MCS_MCSYNC_SYNC_GO_CH0>();
    l_scomData.flush<0>();
    // MCS_MCSYNC = 0x5010815
    fapi2::putScomUnderMask(i_mcTarget, MCS_MCSYNC, l_scomData, l_scomMask);

    // --------------------------------------------------------------
    // 2. Setup MC Sync Command Register data for master MCS or MI
    // --------------------------------------------------------------
    // Clear buffers
    l_scomData.flush<0>();
    l_scomMask.flush<0>();

    // Setup MCSYNC_CHANNEL_SELECT
    // Set ALL channels with or without DIMMs (bits 0:7)
    // MCS_MCSYNC_CHANNEL_SELECT = 0
    // MCS_MCSYNC_CHANNEL_SELECT_LEN = 8
    l_scomData.setBit<MCS_MCSYNC_CHANNEL_SELECT, MCS_MCSYNC_CHANNEL_SELECT_LEN>();
    l_scomMask.setBit<MCS_MCSYNC_CHANNEL_SELECT, MCS_MCSYNC_CHANNEL_SELECT_LEN>();

    // Setup MCSYNC_SYNC_TYPE
    // Set all sync types except Super Sync
    // SUPER_SYNC_BIT == bit 14, supersync for Nimbus, reserved for cumulus.
    // Clear it in both cases.
    // MCS_MCSYNC_SYNC_TYPE = 8
    // MCS_MCSYNC_SYNC_TYPE_LEN = 8
    l_scomData.setBit<MCS_MCSYNC_SYNC_TYPE, MCS_MCSYNC_SYNC_TYPE_LEN>().clearBit<SUPER_SYNC_BIT>();
    l_scomMask.setBit<MCS_MCSYNC_SYNC_TYPE, MCS_MCSYNC_SYNC_TYPE_LEN>();

    // Setup SYNC_GO (bit 16 is now used for both channels)
    // MCS_MCSYNC_SYNC_GO_CH0 = 16
    l_scomMask.setBit<MCS_MCSYNC_SYNC_GO_CH0>();
    l_scomData.setBit<MCS_MCSYNC_SYNC_GO_CH0>();

    // --------------------------------------------------------------
    // 3. Write to MC Sync Command Register of master MCS or MI
    // --------------------------------------------------------------
    // Write to MCSYNC reg
    // MCS_MCSYNC = 0x5010815
    fapi2::putScomUnderMask(i_mcTarget, MCS_MCSYNC, l_scomData, l_scomMask);

    // Note: No need to read Sync replay count and retry in P9.

    // --------------------------------------------------------------
    // 4. Clear refresh sync bit
    // --------------------------------------------------------------
    l_scomData.flush<0>();
    // MBA_REFRESH_SYNC_BIT = 8
    l_scomMask.flush<0>().setBit<MBA_REFRESH_SYNC_BIT>();
    // MCS_MCSYNC = 0x5010815
    fapi2::putScomUnderMask(i_mcTarget, MCS_MCSYNC, l_scomData, l_scomMask);
}

template< fapi2::TargetType T>
fapi2::ReturnCode progMCMODE0(
    fapi2::Target<T>& i_mcTarget,
    const std::vector< fapi2::Target<T> >& i_mcTargets)
{
    // --------------------------------------------------------------
    // Setup MCMODE0 for disabling MC SYNC to other-side and same-side
    // partner unit.
    // BIT27: set if other-side MC is non-functional, 0<->2, 1<->3
    // BIT28: set if same-side MC is non-functional, 0<->1, 2<->3
    // --------------------------------------------------------------
    fapi2::buffer<uint64_t> l_scomData(0);
    fapi2::buffer<uint64_t> l_scomMask(0);
    bool l_other_side_functional = false;
    bool l_same_side_functional = false;
    uint8_t l_current_pos = 0;
    uint8_t l_other_side_pos = 0;
    uint8_t l_same_side_pos = 0;

    // fapi2::ATTR_CHIP_UNIT_POS by default 0
    FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, i_mcTarget, l_current_pos);
    // Calculate the peer MC in the other side and in the same side.
    // MAX_MC_PER_PROC = 4
    // MAX_MC_PER_SIDE = 2
    l_other_side_pos = (l_current_pos + MAX_MC_PER_SIDE) % MAX_MC_PER_PROC;
    // MAX_MC_SIDES_PER_PROC = 2
    // MAX_MC_PER_SIDE = 2
    l_same_side_pos = ((l_current_pos / MAX_MC_SIDES_PER_PROC) * MAX_MC_PER_SIDE)
                     + ((l_current_pos % MAX_MC_PER_SIDE) + 1) % MAX_MC_PER_SIDE;
    // Determine side functionality
    for (auto l_mc : i_mcTargets)
    {
        uint8_t l_tmp_pos = 0;
        // fapi2::ATTR_CHIP_UNIT_POS by default 0
        FAPI_ATTR_GET(fapi2::ATTR_CHIP_UNIT_POS, l_mc, l_tmp_pos);
        if (l_tmp_pos == l_other_side_pos)
        {
            l_other_side_functional = true;
        }
        if (l_tmp_pos == l_same_side_pos)
        {
            l_same_side_functional = true;
        }
    }
    l_scomData.flush<0>();
    l_scomMask.flush<0>();
    if (!l_other_side_functional)
    {
        // MCS_MCMODE0_DISABLE_MC_SYNC = 27
        l_scomData.setBit<MCS_MCMODE0_DISABLE_MC_SYNC>();
        l_scomMask.setBit<MCS_MCMODE0_DISABLE_MC_SYNC>();
    }
    else
    {
        // MCS_MCMODE0_DISABLE_MC_SYNC = 27
        l_scomData.clearBit<MCS_MCMODE0_DISABLE_MC_SYNC>();
        l_scomMask.setBit<MCS_MCMODE0_DISABLE_MC_SYNC>();
    }
    if (!l_same_side_functional)
    {
        // MCS_MCMODE0_DISABLE_MC_PAIR_SYNC = 28
        l_scomData.setBit<MCS_MCMODE0_DISABLE_MC_PAIR_SYNC>();
        l_scomMask.setBit<MCS_MCMODE0_DISABLE_MC_PAIR_SYNC>();
    }
    else
    {
        // MCS_MCMODE0_DISABLE_MC_PAIR_SYNC = 28
        l_scomData.clearBit<MCS_MCMODE0_DISABLE_MC_PAIR_SYNC>();
        l_scomMask.setBit<MCS_MCMODE0_DISABLE_MC_PAIR_SYNC>();
    }
    // MCS_MCMODE0 = 0x5010811
    fapi2::putScomUnderMask(i_mcTarget, MCS_MCMODE0, l_scomData, l_scomMask);
}
```
