From "POWER9 IPL flow" document:

> 10.1 proc_build_smp : Integrate P9 Islands into SMP
> a p9_build_smp.C (vector of all chips to include in SMP)
>  * Look for checkstops
>  * Use the fabric concurrent maintenance operation to merge P9 PB islands into the SMP
>  * Fabric config between IO/CAPI are set here – only can set once, must be known by this point in time
>  * After this point the SMP is built for normal mode
>  * Runs initfiles to set current/next values for full config in slaves, setup master next value
>   - p9.fbc.ab_hp.scom.initfile
>   - p9.fbc.cd_hp.scom.initfile
>  * Trigger fabric quiesce/switch/init on the master

# Hostboot sources

* `src/usr/isteps/istep10/call_proc_build_smp.C`
* `src/import/chips/p9/procedures/hwp/nest/p9_build_smp.C`
* `src/import/chips/p9/procedures/hwp/nest/p9_build_smp.H`
* and many others (lots of functions are in individual files)

## istep itself

```cpp
const uint8_t P9_BUILD_SMP_MAX_SIZE = 16;

PU_ALTD_ADDR_REG = 0x00090000;

PU_SND_MODE_REG = 0x00090021;
PU_SND_MODE_REG_PB_STOP = 22;
PU_SND_MODE_REG_ENABLE_PB_SWITCH_AB = 30;
PU_SND_MODE_REG_ENABLE_PB_SWITCH_CD = 31;

PU_PB_CENT_SM0_PB_CENT_MODE = 0x05011C0A;
PU_PB_CENT_SM0_PB_CENT_MODE_PB_CENT_PBIXXX_INIT = 0;

PU_ALTD_CMD_REG = 0x00090001;
PU_ALTD_CMD_REG_FBC_START_OP = 2;
PU_ALTD_CMD_REG_FBC_CLEAR_STATUS = 3;
PU_ALTD_CMD_REG_FBC_RESET_FSM = 4;
PU_ALTD_CMD_REG_FBC_AXTYPE = 6;
PU_ALTD_CMD_REG_FBC_LOCKED = 11;
PU_ALTD_CMD_REG_FBC_SCOPE = 16;
PU_ALTD_CMD_REG_FBC_SCOPE_LEN = 3;
PU_ALTD_CMD_REG_FBC_DROP_PRIORITY = 20;
PU_ALTD_CMD_REG_FBC_OVERWRITE_PBINIT = 22;
PU_ALTD_CMD_REG_FBC_WITH_TM_QUIESCE = 24;
PU_ALTD_CMD_REG_FBC_TTYPE = 25;
PU_ALTD_CMD_REG_FBC_TTYPE_LEN = 7;
PU_ALTD_CMD_REG_FBC_TSIZE = 32;
PU_ALTD_CMD_REG_FBC_TSIZE_LEN = 8;

ALTD_CMD_TTYPE_PB_OPER = 0x3F;
ALTD_CMD_TTYPE_PMISC_OPER = 0x31;
ALTD_CMD_PMISC_TSIZE_1 = 2; // PMISC SWITCH
ALTD_CMD_SCOPE_SYSTEM = 5;
ALTD_CMD_PB_DIS_OPERATION_TSIZE = 8;

PU_ALTD_STATUS_REG = 0x00090003;
PU_ALTD_STATUS_REG_FBC_ALTD_BUSY = 0;
PU_ALTD_STATUS_REG_FBC_WAIT_CMD_ARBIT = 1;
PU_ALTD_STATUS_REG_FBC_ADDR_DONE = 2;
PU_ALTD_STATUS_REG_FBC_DATA_DONE = 3;
PU_ALTD_STATUS_REG_FBC_WAIT_RESP = 4;
PU_ALTD_STATUS_REG_FBC_OVERRUN_ERROR = 5;
PU_ALTD_STATUS_REG_FBC_AUTOINC_ERROR = 6;
PU_ALTD_STATUS_REG_FBC_COMMAND_ERROR = 7;
PU_ALTD_STATUS_REG_FBC_ADDRESS_ERROR = 8;
PU_ALTD_STATUS_REG_FBC_PBINIT_MISSING = 18;
PU_ALTD_STATUS_REG_FBC_ECC_CE = 48;
PU_ALTD_STATUS_REG_FBC_ECC_UE = 49;
PU_ALTD_STATUS_REG_FBC_ECC_SUE = 50;

PU_ALTD_OPTION_REG = 0x00090002;
PU_ALTD_OPTION_REG_FBC_WITH_PRE_QUIESCE = 23;
PU_ALTD_OPTION_REG_FBC_AFTER_QUIESCE_WAIT_COUNT = 28;
PU_ALTD_OPTION_REG_FBC_AFTER_QUIESCE_WAIT_COUNT_LEN = 20;
PU_ALTD_OPTION_REG_FBC_WITH_POST_INIT = 51;
PU_ALTD_OPTION_REG_FBC_ALTD_HW397129 = 52;
PU_ALTD_OPTION_REG_FBC_BEFORE_INIT_WAIT_COUNT = 54;
PU_ALTD_OPTION_REG_FBC_BEFORE_INIT_WAIT_COUNT_LEN = 10;

PU_ALTD_DATA_REG = 0x00090004;

// HWP argument, define supported execution modes
enum p9_build_smp_operation
{
    // used to initialize scope of HBI drawer
    // call from HB (switch C/D + A/B),
    SMP_ACTIVATE_PHASE1 = 1,
    // used to stitch drawers
    // call from FSP (only switch A/B)
    SMP_ACTIVATE_PHASE2 = 2
};

// Structure to represent fabric connectivty & properites for a single chip
// in the SMP topology
struct p9_build_smp_chip
{
    // associated target handle from HWP input vector
    fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>* target;

    // fabric chip/node ID
    uint8_t chip_id;             /// = 0 for both chips
    uint8_t group_id;            /// = first CPU: 0, second CPU: 1

    // node/system master designation (curr)
    bool master_chip_group_curr; /// = false for both chips
    bool master_chip_sys_curr;   /// = true for both chips

    // node/system master designation (next)
    bool master_chip_group_next; /// = true for both chips
    bool master_chip_sys_next;   /// = true for the first CPU
    bool issue_quiesce_next;     /// = true for the second CPU
    bool quiesced_next;          /// = true for the second CPU
};

// Structure to represent properties for a single group in the SMP topology
struct p9_build_smp_group
{
    // chips which reside in this node
    std::map<uint8_t, p9_build_smp_chip> chips;

    // node properties/attributes:
    // fabric node ID
    uint8_t group_id;
};

// Structure to represent collection of groups in SMP topology
struct p9_build_smp_system
{
    // groups which reside in this SMP
    std::map<uint8_t, p9_build_smp_group> groups;
};

enum sbeMemoryAccessFlags
{
    SBE_MEM_ACCESS_FLAGS_TARGET_PROC          = 0x00000001,
    SBE_MEM_ACCESS_FLAGS_TARGET_PBA           = 0x00000002,
    SBE_MEM_ACCESS_FLAGS_AUTO_INCR_ON         = 0x00000004,
    SBE_MEM_ACCESS_FLAGS_ECC_OVERRIDE         = 0x00000008,
    SBE_MEM_ACCESS_FLAGS_TAG                  = 0x00000010,
    SBE_MEM_ACCESS_FLAGS_FAST_MODE_ON         = 0x00000020,
    SBE_MEM_ACCESS_FLAGS_LCO_MODE             = 0x00000040,
    SBE_MEM_ACCESS_FLAGS_CACHE_INHIBITED_MODE = 0x00000080,
    SBE_MEM_ACCESS_FLAGS_HOST_PASS_THROUGH    = 0x00000100,
    SBE_MEM_ACCESS_FLAGS_CACHE_INJECT_MODE    = 0x00000200,
    SBE_MEM_ACCESS_FLAGS_PB_DIS_MODE          = 0x00000400,
    SBE_MEM_ACCESS_FLAGS_SWITCH_MODE          = 0x00000800,
    SBE_MEM_ACCESS_FLAGS_PB_INIT_MODE         = 0x00001000,
    SBE_MEM_ACCESS_FLAGS_PRE_SWITCH_CD_MODE   = 0x00002000,
    SBE_MEM_ACCESS_FLAGS_PRE_SWITCH_AB_MODE   = 0x00004000,
    SBE_MEM_ACCESS_FLAGS_POST_SWITCH_MODE     = 0x00008000,
};

void* call_proc_build_smp (void *io_pArgs)
{
    IStepError l_StepError;

    do {

        errlHndl_t  l_errl = nullptr;
        TARGETING::TargetHandleList l_cpuTargetList;
        getAllChips(l_cpuTargetList, TYPE_PROC);

        // Identify the master processor to know which chips are slaves
        TARGETING::Target * l_masterProc = nullptr;
        TARGETING::Target * l_masterNode = nullptr;
        bool l_onlyFunctional = true; // Make sure masterproc is functional
        l_errl = TARGETING::targetService().queryMasterProcChipTargetHandle(
                                                 l_masterProc,
                                                 l_masterNode,
                                                 l_onlyFunctional);
        if (l_errl) break;

        std::vector<fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>> l_procList;
        // Loop through all proc chips and convert them to FAPI targets
        for (const auto & curproc: l_cpuTargetList) {
            const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_fapi2_proc_target(curproc);
            l_procList.push_back(l_fapi2_proc_target);
        }

        const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_fapi2_master_proc(l_masterProc);

        l_errl = p9_build_smp(l_procList, l_fapi2_master_proc, SMP_ACTIVATE_PHASE1);
        if (l_errl) break;

        // At the point where we can now change the proc chips to use
        // XSCOM rather than SBESCOM which is the default at the moment.

        TARGETING::TargetHandleList procChips;
        getAllChips(procChips, TYPE_PROC);

        // Loop through all proc chips
        for (TARGETING::Target* l_proc_target: l_cpuTargetList) {
            // If the proc chip supports XSCOM.
            if (l_proc_target->getAttr<ATTR_PRIMARY_CAPABILITIES>().supportsXscom) {
                ScomSwitches l_switches = l_proc_target->getAttr<ATTR_SCOM_SWITCHES>();

                // If Xscom is not already enabled.
                if (!l_switches.useXscom || l_switches.useSbeScom) {
                    // Turn off SBE scom and turn on Xscom.
                    l_switches.useSbeScom = false;
                    l_switches.useXscom = true;
                    l_proc_target->setAttr<ATTR_SCOM_SWITCHES>(l_switches);

                    // Reset the FSI2OPB logic on the new chips
                    l_errl = FSI::resetPib2Opb(l_proc_target);
                    if (l_errl) {
                        // Commit error
                        break;
                    }
                }
            }

            if (l_proc_target != l_masterProc) {
                // Enable PSIHB Interrupts for slave proc -- moved from above
                l_errl = INTR::enablePsiIntr(l_proc_target);
                if (l_errl) break;

                // Now that the SMP is connected, it's possible to establish
                // untrusted memory windows for non-master processor SBEs.  Open
                // up the Hostboot read-only memory range for each one to allow
                // Hostboot dumps / attention handling via any processor chip.
                const auto hbHrmor = cpu_spr_value(CPU_SPR_HRMOR);
                l_errl = SBEIO::openUnsecureMemRegion(
                    hbHrmor,
                    VMM_MEMORY_SIZE,
                    false, // False = read-only
                    l_proc_target);
                if (l_errl) {
                    /*
                     * Failed attempting to open Hostboot's VMM region in SBE of
                     * non-master processor chip
                     */
                    break;
                }
            }

            // Clear XBUS FIR bits for bad lanes that existed prior to link training
            TARGETING::TargetHandleList xbusTargets;
            getChildChiplets(xbusTargets, l_proc_target, TYPE_XBUS);
            for (auto pXbusTarget : xbusTargets) {
                const fapi2::Target<fapi2::TARGET_TYPE_XBUS> fapi2_xbus(pXbusTarget);

                l_errl = p9_io_xbus_erepair_cleanup(fapi2_xbus);
                if (l_errl) { /* log it and continue */ }
            }
        }

        // Set a flag so that the ATTN code will check ALL processors
        // the next time it gets called versus just the master proc.
        uint8_t    l_useAllProcs = 1;
        TARGETING::Target  *l_sys = NULL;
        TARGETING::targetService().getTopLevelTarget( l_sys );
        l_sys->setAttr<ATTR_ATTN_CHK_ALL_PROCS>(l_useAllProcs);

    } while (0);

    // end task, returning any errorlogs to IStepDisp
    return l_StepError.getErrorHandle();
}

/// @brief Perform fabric SMP reconfiguration operation
///
/// @param[in] i_chips                  Vector of processor chip targets to be included in SMP
/// @param[in] i_master_chip_sys_next   Target designating chip which should be designated fabric
///                                     system master post-reconfiguration
///                                     NOTE: this chip must currently be designated a
///                                       master in its enclosing fabric
///                                       PHASE1/HB: any chip
///                                       PHASE2/FSP: any current drawer master
/// @param[in] i_op                     Enumerated type representing SMP build phase (HB or FSP)
///
/// @return fapi2:ReturnCode. FAPI2_RC_SUCCESS if success, else error code.
fapi2::ReturnCode p9_build_smp(std::vector<fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>>& i_chips,
                               const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_master_chip_sys_next,
                               const p9_build_smp_operation i_op)
{
    // process HWP input vector of chips
    p9_build_smp_system l_smp;
    p9_build_smp_insert_chips(i_chips, i_master_chip_sys_next, i_op, l_smp);
    /// return on error ///

    // check topology before continuing
    p9_build_smp_check_topology(i_op, l_smp);
    /// return on error ///

    // activate new SMP configuration
    if (i_op == SMP_ACTIVATE_PHASE1) {
        // set fabric configuration registers (hotplug, switch CD set)
        p9_build_smp_set_fbc_cd(l_smp);
        /// return on error ///
    }

    // set fabric configuration registers (hotplug, switch AB set)
    return p9_build_smp_set_fbc_ab(l_smp, i_op);
}

/// @brief Insert HWP inputs and build SMP data structure
///
/// @param[in] i_chips                 Vector of processor chip targets to be included in SMP
/// @param[in] i_master_chip_sys_next  Target designating chip which should be designated fabric
///                                    system master post-reconfiguration
///                                    NOTE: this chip must currently be designated a
///                                          master in its enclosing fabric
///                                          PHASE1/HB: any chip
///                                          PHASE2/FSP: any current drawer master
/// @param[in] i_op                    Enumerated type representing SMP build phase (HB or FSP)
/// @param[in] io_smp                  Fully specified structure encapsulating SMP
///
/// @return FAPI2_RC_SUCCESS if success, else error code.
fapi2::ReturnCode p9_build_smp_insert_chips(
    std::vector<fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>>& i_chips,
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_master_chip_sys_next,
    const p9_build_smp_operation i_op,
    p9_build_smp_system& io_smp)
{
    // loop over input processor chips
    bool l_master_chip_sys_next_found = false;
    uint8_t l_master_chip_next_group_id = 0;

    for (auto l_iter = i_chips.begin(); l_iter != i_chips.end(); l_iter++)
    {
        bool l_master_chip_sys_next = (*l_iter == i_master_chip_sys_next);
        uint8_t l_group_id;

        if (l_master_chip_sys_next) {
            // ensure that we haven't already designated one
            assert(!l_master_chip_sys_next_found);
            l_master_chip_sys_next_found = true;
        }

        p9_build_smp_insert_chip(*l_iter, l_master_chip_sys_next,
                                 i_op, io_smp, l_group_id);
        /// return on error ///

        if (l_master_chip_sys_next)
            l_master_chip_next_group_id = l_group_id;
    }

    // ensure that new system master was designated
    assert(l_master_chip_sys_next_found);
    // check that SMP size does not exceed maximum number of chips supported
    assert(i_chips.size() <= P9_BUILD_SMP_MAX_SIZE);

    // based on master designation, and operation phase,
    // determine whether each chip will be quiesced as a result
    // of hotplug switch activity
    for (auto g_iter = io_smp.groups.begin(); g_iter != io_smp.groups.end(); g_iter++) {
        for (auto p_iter = g_iter->second.chips.begin(); p_iter != g_iter->second.chips.end(); p_iter++) {
            if ((i_op == SMP_ACTIVATE_PHASE1 && p_iter->second.issue_quiesce_next) ||
                (i_op == SMP_ACTIVATE_PHASE2 && g_iter->first != l_master_chip_next_group_id))
            {
                p_iter->second.quiesced_next = true;
            }
            else
            {
                p_iter->second.quiesced_next = false;
            }
        }
    }

    return fapi2::current_err;
}

/// @brief Insert chip structure into proper position within SMP strucure based
///        on its fabric group/chip IDs
///
/// @param[in] i_target                 Processor chip target
/// @param[in] i_master_chip_sys_next   Flag designating this chip should be designated fabric
///                                     system master post-reconfiguration
///                                     NOTE: this chip must currently be designated a
///                                           master in its enclosing fabric
///                                           PHASE1/HB: any chip
///                                           PHASE2/FSP: any current drawer master
/// @param[in] i_op                     Enumerated type representing SMP build phase (HB or FSP)
/// @param[in/out] io_smp               Fully specified structure encapsulating SMP
/// @param[out] o_group_id              Group which chip belongs to
///
/// @return FAPI2_RC_SUCCESS if success, else error code.
fapi2::ReturnCode
p9_build_smp_insert_chip(fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
                         const bool i_master_chip_sys_next, /// true for first CPU ///
                         const p9_build_smp_operation i_op,
                         p9_build_smp_system& io_smp,
                         uint8_t& o_group_id)
{
    uint8_t l_group_id;
    uint8_t l_chip_id;
    p9_build_smp_chip l_smp_chip;
    bool l_first_chip_found_in_group = false;
    std::map<uint8_t, p9_build_smp_group>::iterator g_iter;
    std::map<uint8_t, p9_build_smp_chip>::iterator p_iter;

    // get chip/group ID attributes
    p9_fbc_utils_get_group_id_attr(i_target, l_group_id); // does ATTR_GET
    /// return on error ///
    p9_fbc_utils_get_chip_id_attr(i_target, l_chip_id); // does ATTR_GET
    /// return on error ///

    // search to see if group structure already exists for the group this chip resides in
    g_iter = io_smp.groups.find(l_group_id);

    // if no matching groups found, create one
    if (g_iter == io_smp.groups.end()) {
        // No matching group found, inserting new group into structure
        p9_build_smp_group l_smp_group;
        l_smp_group.group_id = l_group_id;
        auto l_ret = io_smp.groups.insert(std::pair<uint8_t, p9_build_smp_group>(l_group_id, l_smp_group));
        g_iter = l_ret.first;
        assert(l_ret.second);
        // mark as first chip found in its group
        l_first_chip_found_in_group = true;
    }

    // ensure that no chip has already been inserted into this group
    // with the same chip ID as this chip
    p_iter = io_smp.groups[l_group_id].chips.find(l_chip_id);
    // matching chip ID & group ID already found, flag an error
    assert(p_iter == io_smp.groups[l_group_id].chips.end());

    // process/fill chip data structure
    p9_build_smp_process_chip(i_target, l_group_id, l_chip_id,
                              i_master_chip_sys_next,
                              l_first_chip_found_in_group, i_op, l_smp_chip);
    /// return on error ///

    // insert chip into SMP structure
    io_smp.groups[l_group_id].chips[l_chip_id] = l_smp_chip;

    // return group ID
    o_group_id = l_group_id;

    return fapi2::current_err;
}

/// @brief Process single chip target into SMP chip data structure
///
/// @param[in] i_target                     Processor chip target
/// @param[in] i_group_id                   Fabric group ID for this chip target
/// @param[in] i_chip_id                    Fabric chip ID for this chip target
/// @param[in] i_master_chip_sys_next       True if this chip should be designated
///                                         fabric system master post-reconfiguration
/// @param[in] i_first_chip_found_in_group  True if this chip is the first discovered
///                                         in its group (when processing HWP inputs)
/// @param[in] i_op                         Enumerated type representing SMP build phase
/// @param[in/out] io_smp_chip              Structure encapsulating single chip in SMP topology
///
/// @return FAPI2_RC_SUCCESS if success, else error code.
fapi2::ReturnCode
p9_build_smp_process_chip(fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
                          const uint8_t i_group_id,               /// = first CPU: 0, second CPU: 1
                          const uint8_t i_chip_id,                /// = 0
                          const bool i_master_chip_sys_next,      /// = true for first CPU
                          const bool i_first_chip_found_in_group, /// = true
                          const p9_build_smp_operation i_op,      /// = SMP_ACTIVATE_PHASE1
                          p9_build_smp_chip& io_smp_chip)
{
    fapi2::buffer<uint64_t> l_hp_mode_curr;
    bool l_err = false;
    fapi2::ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP_Type l_sys_master_chip_attr;
    fapi2::ATTR_PROC_FABRIC_GROUP_MASTER_CHIP_Type l_group_master_chip_attr;

    // set target handle pointer
    io_smp_chip.target = &i_target;

    // set group/chip IDs
    io_smp_chip.group_id = i_group_id;
    io_smp_chip.chip_id = i_chip_id;

    // set group/system master CURR data structure fields from HW
    fapi2::getScom(i_target, PU_PB_CENT_SM0_PB_CENT_HP_MODE_CURR, l_hp_mode_curr);
    io_smp_chip.master_chip_group_curr =
        l_hp_mode_curr.getBit<PU_PB_CENT_SM0_PB_CENT_HP_MODE_CURR_CFG_CHG_RATE_GP_MASTER>();
    io_smp_chip.master_chip_sys_curr =
        l_hp_mode_curr.getBit<PU_PB_CENT_SM0_PB_CENT_HP_MODE_CURR_CFG_MASTER_CHIP>();

    // set system master NEXT designation from HWP platform input
    io_smp_chip.master_chip_sys_next = i_master_chip_sys_next;

    // set group master NEXT designation based on phase
    if (i_op == SMP_ACTIVATE_PHASE1) {
        // each chip should match the flush state of the fabric logic
        if (!io_smp_chip.master_chip_sys_curr || io_smp_chip.master_chip_group_curr) {
            // Error: chip does not match flash state of fabric
            l_err = true;
        } else {
            // designate first chip found in each group as group master after reconfiguration
            io_smp_chip.master_chip_group_next = i_first_chip_found_in_group;
        }
    } else {
        // maintain current group master status after reconfiguration
        io_smp_chip.master_chip_group_next = io_smp_chip.master_chip_group_curr;
    }

    // set issue quiesce NEXT flag
    if (io_smp_chip.master_chip_sys_next) {
        // this chip will not be quiesced, to enable switch AB
        io_smp_chip.issue_quiesce_next = false;

        // in both activation scenarios, we expect that
        // the newly designated master is currently configured
        // as a master within the scope of its current enclosing fabric
        if (!io_smp_chip.master_chip_sys_curr) {
            // Error: newly designated master is not currently a master
            l_err = true;
        }
    } else {
        if (io_smp_chip.master_chip_sys_curr) {
            // this chip will not be the new master, but is one now
            // use it to quiesce all chips in its fabric
            io_smp_chip.issue_quiesce_next = true;
        } else {
            io_smp_chip.issue_quiesce_next = false;
        }
    }

    // default remaining NEXT state data structure fields
    io_smp_chip.quiesced_next = false;

    // assert if local error is set
    assert(l_err == false);

    // write attributes for initfile consumption
    l_sys_master_chip_attr = io_smp_chip.master_chip_sys_next ?
                             fapi2::ENUM_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP_TRUE :
                             fapi2::ENUM_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP_FALSE;
    FAPI_ATTR_SET(fapi2::ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP, i_target, l_sys_master_chip_attr);

    l_group_master_chip_attr = io_smp_chip.master_chip_group_next ?
                               fapi2::ENUM_ATTR_PROC_FABRIC_GROUP_MASTER_CHIP_TRUE :
                               fapi2::ENUM_ATTR_PROC_FABRIC_GROUP_MASTER_CHIP_FALSE;
    FAPI_ATTR_SET(fapi2::ATTR_PROC_FABRIC_GROUP_MASTER_CHIP, i_target, l_group_master_chip_attr);

    return fapi2::current_err;
}

/// @brief Check validity of SMP topology
///
/// @param[in] i_op    Enumerated type representing SMP build phase (HB or FSP)
/// @param[in] i_smp   Fully specified structure encapsulating SMP
///
/// @return FAPI2_RC_SUCCESS if success, else error code.
fapi2::ReturnCode
p9_build_smp_check_topology(const p9_build_smp_operation i_op,
                            p9_build_smp_system& i_smp);
  // check that fabric topology is logically valid
  // 1) in a given group, all chips are connected to every other
  //    chip in the group, by an X bus (if pump mode = chip_is_node)
  // 2) each chip is connected to its partner chip (with same chip id)
  //    in every other group, by an A bus or X bus (if pump mode = chip_is_group)

  /// our topology is too simple to check ///

/// @brief Program fabric configuration register (hotplug, C/D set)
///
/// @param[in] i_smp     Structure encapsulating SMP topology
///
/// @return fapi2:ReturnCode. FAPI2_RC_SUCCESS if success, else error code.
fapi2::ReturnCode p9_build_smp_set_fbc_cd(p9_build_smp_system& i_smp)
{
    fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM;

    // iterate through application of three CD hotplug sequences, each with their
    // own unique initfile
    for (uint8_t ii = 1; ii <= 3; ii++) {
        // apply initfile on all chips
        for (auto g_iter = i_smp.groups.begin(); g_iter != i_smp.groups.end(); ++g_iter) {
            for (auto p_iter = g_iter->second.chips.begin(); p_iter != g_iter->second.chips.end(); ++p_iter) {
                fapi2::ReturnCode l_rc;

                // initialize serial SCOM chains
                switch (ii) {
                    case 1:
                        l_rc = p9_fbc_cd_hp1_scom(*(p_iter->second.target), FAPI_SYSTEM);
                        break;

                    case 2:
                        l_rc = p9_fbc_cd_hp2_scom(*(p_iter->second.target), FAPI_SYSTEM);
                        break;

                    case 3:
                        l_rc = p9_fbc_cd_hp3_scom(*(p_iter->second.target), FAPI_SYSTEM);
                        break;
                }

                if (l_rc) {
                    fapi2::current_err = l_rc;
                    goto fapi_try_exit;
                }
            }
        }

        // issue switch CD on all chips to force updates to occur
        p9_build_smp_sequence_adu(i_smp, SMP_ACTIVATE_PHASE1, SWITCH_CD);
        /// return on error ///
    }

fapi_try_exit:
    return fapi2::current_err;
}

fapi2::ReturnCode p9_fbc_cd_hp1_scom(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT0,
                                     const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1)
{
    /// l_chip_id == 0x05 (Nimbus) ///
    /// l_TGT1_ATTR_PROC_EPS_TABLE_TYPE == fapi2::ENUM_ATTR_PROC_EPS_TABLE_TYPE_EPS_TYPE_LE ///
    /// l_TGT0_ATTR_PROC_FABRIC_X_LINKS_CNFG == 1 ///
    /// l_TGT1_ATTR_PROC_FABRIC_SMP_OPTICS_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_SMP_OPTICS_MODE_OPTICS_IS_X_BUS ///
    /// l_TGT1_ATTR_PROC_FABRIC_ASYNC_SAFE_MODE == ENUM_ATTR_PROC_FABRIC_ASYNC_SAFE_MODE_PERFORMANCE_MODE ///
    /// l_TGT0_ATTR_CHIP_EC_FEATURE_HW409019 == 1 ///
    /// l_TGT0_ATTR_CHIP_EC_FEATURE_AXONE_FBC_SETTINGS == 0 ///

    fapi2::ATTR_FREQ_PB_MHZ_Type l_TGT1_ATTR_FREQ_PB_MHZ;
    FAPI_ATTR_GET(fapi2::ATTR_FREQ_PB_MHZ, TGT1, l_TGT1_ATTR_FREQ_PB_MHZ);

    /// Frequency of XBus, 2000 MHz for Nimbus DD2 ///
    uint32_t l_TGT1_ATTR_FREQ_X_MHZ = 2000;

    uint64_t l_def_X_RATIO_120_100 = ((100 * l_TGT1_ATTR_FREQ_X_MHZ) >= (120 * l_TGT1_ATTR_FREQ_PB_MHZ));

    uint64_t l_def_X_RATIO_115_100 = ((100 * l_TGT1_ATTR_FREQ_X_MHZ) >= (115 * l_TGT1_ATTR_FREQ_PB_MHZ));
    uint64_t l_def_X_RATIO_110_100 = ((100 * l_TGT1_ATTR_FREQ_X_MHZ) >= (110 * l_TGT1_ATTR_FREQ_PB_MHZ));
    uint64_t l_def_X_RATIO_105_100 = ((100 * l_TGT1_ATTR_FREQ_X_MHZ) >= (105 * l_TGT1_ATTR_FREQ_PB_MHZ));
    uint64_t l_def_X_RATIO_100_100 = ((100 * l_TGT1_ATTR_FREQ_X_MHZ) >= (100 * l_TGT1_ATTR_FREQ_PB_MHZ));
    uint64_t l_def_X_RATIO_100_105 = ((105 * l_TGT1_ATTR_FREQ_X_MHZ) >= (100 * l_TGT1_ATTR_FREQ_PB_MHZ));
    uint64_t l_def_X_RATIO_100_110 = ((110 * l_TGT1_ATTR_FREQ_X_MHZ) >= (100 * l_TGT1_ATTR_FREQ_PB_MHZ));
    uint64_t l_def_X_RATIO_100_115 = ((115 * l_TGT1_ATTR_FREQ_X_MHZ) >= (100 * l_TGT1_ATTR_FREQ_PB_MHZ));
    uint64_t l_def_X_RATIO_100_120 = ((120 * l_TGT1_ATTR_FREQ_X_MHZ) >= (100 * l_TGT1_ATTR_FREQ_PB_MHZ));
    uint64_t l_def_X_RATIO_100_125 = ((125 * l_TGT1_ATTR_FREQ_X_MHZ) >= (100 * l_TGT1_ATTR_FREQ_PB_MHZ));

    fapi2::ATTR_PROC_FABRIC_CORE_FLOOR_RATIO_Type l_TGT1_ATTR_PROC_FABRIC_CORE_FLOOR_RATIO;
    FAPI_ATTR_GET(fapi2::ATTR_PROC_FABRIC_CORE_FLOOR_RATIO, TGT1, l_TGT1_ATTR_PROC_FABRIC_CORE_FLOOR_RATIO);
    uint64_t l_def_CORE_FLOOR_RATIO_2_8 = (l_TGT1_ATTR_PROC_FABRIC_CORE_FLOOR_RATIO == ENUM_ATTR_PROC_FABRIC_CORE_FLOOR_RATIO_RATIO_2_8);
    uint64_t l_def_CORE_FLOOR_RATIO_4_8 = (l_TGT1_ATTR_PROC_FABRIC_CORE_FLOOR_RATIO == ENUM_ATTR_PROC_FABRIC_CORE_FLOOR_RATIO_RATIO_4_8);

    fapi2::ATTR_PROC_FABRIC_CORE_CEILING_RATIO_Type l_TGT1_ATTR_PROC_FABRIC_CORE_CEILING_RATIO;
    FAPI_ATTR_GET(fapi2::ATTR_PROC_FABRIC_CORE_CEILING_RATIO, TGT1, l_TGT1_ATTR_PROC_FABRIC_CORE_CEILING_RATIO);
    uint64_t l_def_CORE_CEILING_RATIO_8_8 = (l_TGT1_ATTR_PROC_FABRIC_CORE_CEILING_RATIO == ENUM_ATTR_PROC_FABRIC_CORE_CEILING_RATIO_RATIO_8_8);

    fapi2::buffer<uint64_t> l_scom_buffer;

    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<54, 5, 59, uint64_t>(0x08);
        l_scom_buffer.insert<59, 5, 59, uint64_t>(0x03);
        fapi2::putScom(TGT0, 0x90000cb205012011ull, l_scom_buffer);
    }
    {
        l_scom_buffer.flush<0> ();

        l_scom_buffer.insert<53, 1, 63, uint64_t>(0);

        if (l_def_X_RATIO_120_100)
            l_scom_buffer.insert<54, 5, 59, uint64_t>(0x09);
        else if (l_def_X_RATIO_115_100)
            l_scom_buffer.insert<54, 5, 59, uint64_t>(0x0A);
        else if (l_def_X_RATIO_110_100)
            l_scom_buffer.insert<54, 5, 59, uint64_t>(0x0A);
        else if (l_def_X_RATIO_105_100)
            l_scom_buffer.insert<54, 5, 59, uint64_t>(0x0A);
        else if (l_def_X_RATIO_100_100)
            l_scom_buffer.insert<54, 5, 59, uint64_t>(0x0A);
        else if (l_def_X_RATIO_100_105)
            l_scom_buffer.insert<54, 5, 59, uint64_t>(0x0B);
        else if (l_def_X_RATIO_100_110)
            l_scom_buffer.insert<54, 5, 59, uint64_t>(0x0B);
        else if (l_def_X_RATIO_100_115)
            l_scom_buffer.insert<54, 5, 59, uint64_t>(0x0B);
        else if (l_def_X_RATIO_100_120)
            l_scom_buffer.insert<54, 5, 59, uint64_t>(0x0B);
        else if (l_def_X_RATIO_100_125)
            l_scom_buffer.insert<54, 5, 59, uint64_t>(0x0C);

        l_scom_buffer.insert<59, 5, 59, uint64_t>(0x03);

        fapi2::putScom(TGT0, 0x90000cb305012011ull, l_scom_buffer);
    }
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<51, 5, 59, uint64_t>(0x10);
        l_scom_buffer.insert<56, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<58, 2, 62, uint64_t>(2);
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0xF);
        fapi2::putScom(TGT0, 0x90000cdb05011c11ull, l_scom_buffer);
    }
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<49, 3, 61, uint64_t>(7);
        l_scom_buffer.insert<52, 6, 58, uint64_t>(0x4);
        l_scom_buffer.insert<58, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<60, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<63, 1, 63, uint64_t>(0);
        fapi2::putScom(TGT0, 0x90000cf405011c11ull, l_scom_buffer);
    }
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<45, 4, 60, uint64_t>(0xC);
        l_scom_buffer.insert<49, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<52, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<54, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<57, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<59, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<61, 3, 61, uint64_t>(0);
        fapi2::putScom(TGT0, 0x90000d3f05011c11ull, l_scom_buffer);
    }
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<41, 2, 62, uint64_t>(3);
        l_scom_buffer.insert<43, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<45, 4, 60, uint64_t>(3);
        l_scom_buffer.insert<49, 8, 56, uint64_t>(0xC0);
        l_scom_buffer.insert<57, 6, 58, uint64_t>(0x00);
        l_scom_buffer.insert<63, 1, 63, uint64_t>(0);
        fapi2::putScom(TGT0, 0x90000d7805011c11ull, l_scom_buffer);
    }
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<38, 4, 60, uint64_t>(8);
        l_scom_buffer.insert<42, 4, 60, uint64_t>(4);
        l_scom_buffer.insert<46, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<49, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<50, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<51, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<54, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<57, 3, 61, uint64_t>(1);
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0xF);
        fapi2::putScom(TGT0, 0x90000daa05011c11ull, l_scom_buffer);
    }
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<36, 3, 61, uint64_t>(4);
        l_scom_buffer.insert<39, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<40, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<41, 8, 56, uint64_t>(0x20);
        l_scom_buffer.insert<49, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<50, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<51, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<52, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<55, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<56, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<57, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<58, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<59, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0xF);
        fapi2::putScom(TGT0, 0x90000dcc05011c11ull, l_scom_buffer);
    }
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<32, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<35, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<38, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<41, 3, 61, uint64_t>(1);
        l_scom_buffer.insert<44, 3, 61, uint64_t>(1);
        l_scom_buffer.insert<47, 3, 61, uint64_t>(2);
        l_scom_buffer.insert<50, 3, 61, uint64_t>(3);
        l_scom_buffer.insert<53, 3, 61, uint64_t>(5);
        l_scom_buffer.insert<56, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<57, 3, 61, uint64_t>(5);
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0x0);
        fapi2::putScom(TGT0, 0x90000e0605011c11ull, l_scom_buffer);
    }
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<28, 5, 59, uint64_t>(0x00);
        l_scom_buffer.insert<33, 5, 59, uint64_t>(0x06);
        l_scom_buffer.insert<38, 5, 59, uint64_t>(0x0D);
        l_scom_buffer.insert<43, 5, 59, uint64_t>(0x00);
        l_scom_buffer.insert<48, 5, 59, uint64_t>(0x1E);
        l_scom_buffer.insert<53, 5, 59, uint64_t>(0x19);
        l_scom_buffer.insert<58, 5, 59, uint64_t>(0x00);
        l_scom_buffer.insert<63, 1, 63, uint64_t>(1);
        fapi2::putScom(TGT0, 0x90000e4305011c11ull, l_scom_buffer);
    }
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<22, 12, 52, uint64_t>(0x400);
        l_scom_buffer.insert<34, 12, 52, uint64_t>(0x400);
        l_scom_buffer.insert<46, 3, 61, uint64_t>(2);
        l_scom_buffer.insert<49, 3, 61, uint64_t>(2);
        l_scom_buffer.insert<52, 3, 61, uint64_t>(2);
        l_scom_buffer.insert<55, 3, 61, uint64_t>(2);
        l_scom_buffer.insert<58, 3, 61, uint64_t>(2);
        l_scom_buffer.insert<61, 3, 61, uint64_t>(2);
        fapi2::putScom(TGT0, 0x90000ea205011c11ull, l_scom_buffer);
    }
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<20, 8, 56, uint64_t>(0x0C);
        l_scom_buffer.insert<28, 8, 56, uint64_t>(0x00);
        l_scom_buffer.insert<36, 8, 56, uint64_t>(0x00);

        /// l_TGT0_ATTR_CHIP_EC_FEATURE_HW409019 == 1 ///
        l_scom_buffer.insert<44, 1, 63, uint64_t>(1);

        l_scom_buffer.insert<45, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<46, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<47, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<48, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<49, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<52, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<53, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<56, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<57, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<59, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<60, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<61, 3, 61, uint64_t>(0);
        fapi2::putScom(TGT0, 0x90000ec705011c11ull, l_scom_buffer);
    }
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<18, 10, 54, uint64_t>(0x4);
        l_scom_buffer.insert<28, 12, 52, uint64_t>(0x141);
        l_scom_buffer.insert<40, 12, 52, uint64_t>(0x21B);
        l_scom_buffer.insert<52, 12, 52, uint64_t>(0x30D);
        fapi2::putScom(TGT0, 0x90000ee105011c11ull, l_scom_buffer);
    }
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<16, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<19, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<22, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<25, 3, 61, uint64_t>(1);
        l_scom_buffer.insert<28, 3, 61, uint64_t>(1);
        l_scom_buffer.insert<31, 3, 61, uint64_t>(2);
        l_scom_buffer.insert<34, 3, 61, uint64_t>(3);
        l_scom_buffer.insert<37, 3, 61, uint64_t>(5);
        l_scom_buffer.insert<40, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<43, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<46, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<49, 3, 61, uint64_t>(1);
        l_scom_buffer.insert<52, 3, 61, uint64_t>(1);
        l_scom_buffer.insert<55, 3, 61, uint64_t>(2);
        l_scom_buffer.insert<58, 3, 61, uint64_t>(3);
        l_scom_buffer.insert<61, 3, 61, uint64_t>(5);
        fapi2::putScom(TGT0, 0x90000f0505011c11ull, l_scom_buffer);
    }
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<14, 10, 54, uint64_t>(0x7);
        l_scom_buffer.insert<24, 10, 54, uint64_t>(0x5);
        l_scom_buffer.insert<34, 10, 54, uint64_t>(0x5);
        l_scom_buffer.insert<44, 10, 54, uint64_t>(0x4);
        l_scom_buffer.insert<54, 10, 54, uint64_t>(0x5);
        fapi2::putScom(TGT0, 0x90000f2005011c11ull, l_scom_buffer);
    }
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<12, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<15, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<16, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<18, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<19, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<20, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<21, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<23, 1, 63, uint64_t>(0);

        if (l_def_CORE_CEILING_RATIO_8_8 == 1)
            l_scom_buffer.insert<24, 2, 62, uint64_t>(0);
        else
            l_scom_buffer.insert<24, 2, 62, uint64_t>(3);

        l_scom_buffer.insert<26, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<27, 1, 63, uint64_t>(0);

        if ((l_def_CORE_CEILING_RATIO_8_8 == 1))
            l_scom_buffer.insert<28, 2, 62, uint64_t>(3);
        else
            l_scom_buffer.insert<28, 2, 62, uint64_t>(2);

        l_scom_buffer.insert<30, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<31, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<32, 2, 62, uint64_t>(3);
        l_scom_buffer.insert<34, 3, 61, uint64_t>(7);
        l_scom_buffer.insert<37, 2, 62, uint64_t>(3);
        l_scom_buffer.insert<39, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<40, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<41, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<42, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<43, 1, 63, uint64_t>(0);

        if (l_def_CORE_CEILING_RATIO_8_8 == 1)
            l_scom_buffer.insert<44, 2, 62, uint64_t>(0);
        else
            l_scom_buffer.insert<44, 2, 62, uint64_t>(3);

        l_scom_buffer.insert<46, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<47, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<48, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<50, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<51, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<52, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<54, 10, 54, uint64_t>(0x000);

        fapi2::putScom(TGT0, 0x90000f4005011811ull, l_scom_buffer);
        fapi2::putScom(TGT0, 0x90000f4005012011ull, l_scom_buffer);
    }
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<12, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<13, 4, 60, uint64_t>(4);
        l_scom_buffer.insert<17, 4, 60, uint64_t>(4);
        l_scom_buffer.insert<21, 4, 60, uint64_t>(4);
        l_scom_buffer.insert<25, 3, 61, uint64_t>(1);
        l_scom_buffer.insert<28, 3, 61, uint64_t>(1);
        l_scom_buffer.insert<31, 3, 61, uint64_t>(1);
        l_scom_buffer.insert<34, 8, 56, uint64_t>(0xFE);
        l_scom_buffer.insert<42, 8, 56, uint64_t>(0xFE);
        l_scom_buffer.insert<50, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<52, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<54, 3, 61, uint64_t>(2);
        l_scom_buffer.insert<57, 2, 62, uint64_t>(2);
        l_scom_buffer.insert<59, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<60, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<61, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<62, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<63, 1, 63, uint64_t>(1);
        fapi2::putScom(TGT0, 0x90000f4d05011c11ull, l_scom_buffer);
    }
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<26, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<28, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<29, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<31, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<33, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<34, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<35, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<36, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<38, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<39, 2, 62, uint64_t>(2);

        if (l_def_CORE_FLOOR_RATIO_2_8 == 1)
            l_scom_buffer.insert<41, 2, 62, uint64_t>(3);
        else if ((l_def_CORE_FLOOR_RATIO_4_8 == 1))
            l_scom_buffer.insert<41, 2, 62, uint64_t>(2);
        else
            l_scom_buffer.insert<41, 2, 62, uint64_t>(1);

        l_scom_buffer.insert<43, 1, 63, uint64_t>(0);

        if (l_def_CORE_FLOOR_RATIO_2_8 == 1)
            l_scom_buffer.insert<44, 2, 62, uint64_t>(0);
        else if (l_def_CORE_FLOOR_RATIO_4_8 == 1)
            l_scom_buffer.insert<44, 2, 62, uint64_t>(3);
        else
            l_scom_buffer.insert<44, 2, 62, uint64_t>(2);

        l_scom_buffer.insert<46, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<48, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<49, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<50, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<51, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<53, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<54, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<55, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<56, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<57, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<59, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<60, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<62, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<63, 1, 63, uint64_t>(0);

        fapi2::putScom(TGT0, 0x90000e6105011811ull, l_scom_buffer);
        fapi2::putScom(TGT0, 0x90000e6105012011ull, l_scom_buffer);
    }

    return fapi2::current_err;
}

fapi2::ReturnCode p9_fbc_cd_hp2_scom(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT0,
                                     const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1)
{
    fapi2::buffer<uint64_t> l_scom_buffer;
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<38, 4, 60, uint64_t>(8);
        l_scom_buffer.insert<42, 4, 60, uint64_t>(4);
        l_scom_buffer.insert<46, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<49, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<50, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<51, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<54, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<57, 3, 61, uint64_t>(1);
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0xF);

        fapi2::putScom(TGT0, 0x90000daa05011c11ull, l_scom_buffer);
    }
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<12, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<13, 4, 60, uint64_t>(4);
        l_scom_buffer.insert<17, 4, 60, uint64_t>(4);
        l_scom_buffer.insert<21, 4, 60, uint64_t>(4);
        l_scom_buffer.insert<25, 3, 61, uint64_t>(1);
        l_scom_buffer.insert<28, 3, 61, uint64_t>(1);
        l_scom_buffer.insert<31, 3, 61, uint64_t>(1);
        l_scom_buffer.insert<34, 8, 56, uint64_t>(0xFE);
        l_scom_buffer.insert<42, 8, 56, uint64_t>(0xFE);
        l_scom_buffer.insert<50, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<52, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<54, 3, 61, uint64_t>(2);
        l_scom_buffer.insert<57, 2, 62, uint64_t>(2);
        l_scom_buffer.insert<59, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<60, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<61, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<62, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<63, 1, 63, uint64_t>(1);

        fapi2::putScom(TGT0, 0x90000f4d05011c11ull, l_scom_buffer);
    }

    return fapi2::current_err;
}

fapi2::ReturnCode p9_fbc_cd_hp3_scom(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT0,
                                     const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1)
{
    fapi2::buffer<uint64_t> l_scom_buffer;
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<38, 4, 60, uint64_t>(8);
        l_scom_buffer.insert<42, 4, 60, uint64_t>(4);
        l_scom_buffer.insert<46, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<49, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<50, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<51, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<54, 3, 61, uint64_t>(0);
        l_scom_buffer.insert<57, 3, 61, uint64_t>(1);
        l_scom_buffer.insert<60, 4, 60, uint64_t>(0xF);

        fapi2::putScom(TGT0, 0x90000daa05011c11ull, l_scom_buffer);
    }
    {
        l_scom_buffer.flush<0> ();
        l_scom_buffer.insert<12, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<13, 4, 60, uint64_t>(4);
        l_scom_buffer.insert<17, 4, 60, uint64_t>(4);
        l_scom_buffer.insert<21, 4, 60, uint64_t>(4);
        l_scom_buffer.insert<25, 3, 61, uint64_t>(1);
        l_scom_buffer.insert<28, 3, 61, uint64_t>(1);
        l_scom_buffer.insert<31, 3, 61, uint64_t>(1);
        l_scom_buffer.insert<34, 8, 56, uint64_t>(0xFE);
        l_scom_buffer.insert<42, 8, 56, uint64_t>(0xFE);
        l_scom_buffer.insert<50, 2, 62, uint64_t>(1);
        l_scom_buffer.insert<52, 2, 62, uint64_t>(0);
        l_scom_buffer.insert<54, 3, 61, uint64_t>(2);
        l_scom_buffer.insert<57, 2, 62, uint64_t>(2);
        l_scom_buffer.insert<59, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<60, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<61, 1, 63, uint64_t>(1);
        l_scom_buffer.insert<62, 1, 63, uint64_t>(0);
        l_scom_buffer.insert<63, 1, 63, uint64_t>(1);

        fapi2::putScom(TGT0, 0x90000f4d05011c11ull, l_scom_buffer);
    }

    return fapi2::current_err;
}

/// @brief Program fabric configuration register (hotplug, A/B set)
///
/// @param[in] i_smp     Structure encapsulating SMP topology
/// @param[in] i_op      Enumerated type representing SMP build phase
///
/// @return fapi2::ReturnCode. FAPI2_RC_SUCCESS if success, else error code.
fapi2::ReturnCode p9_build_smp_set_fbc_ab(p9_build_smp_system& i_smp,
        const p9_build_smp_operation i_op)
{
    fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM;

    // quiesce 'slave' fabrics in preparation for joining
    //   PHASE1 -> quiesce all chips except the chip which is the new fabric master
    //   PHASE2 -> quiesce all drawers except the drawer containing the new fabric master
    p9_build_smp_sequence_adu(i_smp, i_op, QUIESCE);
    /// return on error ///

    // program NEXT register set for all chips via initfile
    // program CURR register set only for chips which were just quiesced
    for (auto g_iter = i_smp.groups.begin(); g_iter != i_smp.groups.end(); ++g_iter) {
        for (auto p_iter = g_iter->second.chips.begin(); p_iter != g_iter->second.chips.end(); ++p_iter) {
            // run initfile HWP (sets NEXT)
            fapi2::ReturnCode l_rc = p9_fbc_ab_hp_scom(*(p_iter->second.target), FAPI_SYSTEM);
            if (l_rc) return l_rc;

            // for chips just quiesced, copy NEXT->CURR
            if (p_iter->second.quiesced_next) {
                p9_build_smp_copy_hp_ab_next_curr(*(p_iter->second.target));
                /// return on error ///
            }
        }
    }

    // issue switch AB reconfiguration from chip designated as new master
    // (which is guaranteed to be a master now)
    p9_build_smp_sequence_adu(i_smp, i_op, SWITCH_AB);
    /// return on error ///

    // reset NEXT register set (copy CURR->NEXT) for all chips
    for (auto g_iter = i_smp.groups.begin(); g_iter != i_smp.groups.end(); ++g_iter) {
        for (auto p_iter = g_iter->second.chips.begin(); p_iter != g_iter->second.chips.end(); ++p_iter) {
            p9_build_smp_copy_hp_ab_curr_next(*(p_iter->second.target));
            /// return on error ///
        }
    }

    return fapi2::current_err;
}

fapi2::ReturnCode p9_fbc_ab_hp_scom(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& TGT0,
                                    const fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>& TGT1)
{
    fapi2::ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP_Type l_TGT0_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP;
    FAPI_ATTR_GET(fapi2::ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP, TGT0, l_TGT0_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP);

    /// l_TGT0_ATTR_CHIP_EC_FEATURE_HW423589_OPTION1 == 1 ///
    /// l_TGT0_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE[*] == fapi2::ENUM_ATTR_PROC_FABRIC_OPTICS_CONFIG_MODE_SMP ///
    /// l_TGT0_ATTR_CHIP_EC_FEATURE_HW386013 == 0 ///
    /// l_TGT1_ATTR_PROC_FABRIC_A_BUS_WIDTH == 0 ///
    /// l_TGT0_ATTR_PROC_FABRIC_X_AGGREGATE == ENUM_ATTR_PROC_FABRIC_X_AGGREGATE_OFF ///
    /// l_TGT1_ATTR_PROC_FABRIC_X_BUS_WIDTH == fapi2::ENUM_ATTR_PROC_FABRIC_X_BUS_WIDTH_4_BYTE ///
    /// l_TGT1_ATTR_PROC_FABRIC_CAPI_MODE == fapi2::ENUM_ATTR_PROC_FABRIC_CAPI_MODE_OFF ///

    fapi2::ATTR_PROC_FABRIC_GROUP_MASTER_CHIP_Type l_TGT0_ATTR_PROC_FABRIC_GROUP_MASTER_CHIP;
    if (l_TGT0_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP)
        l_TGT0_ATTR_PROC_FABRIC_GROUP_MASTER_CHIP = true;
    else
        l_TGT0_ATTR_PROC_FABRIC_GROUP_MASTER_CHIP = false;

    fapi2::ATTR_FREQ_PB_MHZ_Type l_TGT1_ATTR_FREQ_PB_MHZ;
    FAPI_ATTR_GET(fapi2::ATTR_FREQ_PB_MHZ, TGT1, l_TGT1_ATTR_FREQ_PB_MHZ);

    fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_Type l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG;
    FAPI_ATTR_GET(fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG, TGT0,
                  l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG);

    uint8_t l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_ID;
    if (l_TGT0_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP) 
        l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_ID = 1;
    else
        l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_ID = 0;

    /// l_TGT0_ATTR_CHIP_EC_FEATURE_HW407123 == DD <= 0x20 ///
    fapi2::ATTR_CHIP_EC_FEATURE_HW407123_Type l_TGT0_ATTR_CHIP_EC_FEATURE_HW407123;
    FAPI_ATTR_GET(fapi2::ATTR_CHIP_EC_FEATURE_HW407123, TGT0, l_TGT0_ATTR_CHIP_EC_FEATURE_HW407123);

    /// Frequency of XBus, 2000 MHz for Nimbus DD2 ///
    uint32_t l_TGT1_ATTR_FREQ_X_MHZ = 2000;

    uint64_t l_def_X_CMD_RATE_4B_R = ((6 * l_TGT1_ATTR_FREQ_PB_MHZ) % l_TGT1_ATTR_FREQ_X_MHZ);

    uint64_t l_def_X_CMD_RATE_D = l_TGT1_ATTR_FREQ_X_MHZ;
    uint64_t l_def_X_CMD_RATE_4B_N = (6 * l_TGT1_ATTR_FREQ_PB_MHZ);
    fapi2::buffer<uint64_t> l_scom_buffer;
    {
        fapi2::getScom( TGT0, 0x501180bull, l_scom_buffer );

        if (l_TGT0_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP == fapi2::ENUM_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP_FALSE) {
            constexpr auto l_PB_COM_PB_CFG_MASTER_CHIP_NEXT_OFF = 0x0;
            l_scom_buffer.insert<0, 1, 61, uint64_t>(l_PB_COM_PB_CFG_MASTER_CHIP_NEXT_OFF );

            constexpr auto l_PB_COM_PB_CFG_TM_MASTER_NEXT_OFF = 0x0;
            l_scom_buffer.insert<1, 1, 61, uint64_t>(l_PB_COM_PB_CFG_TM_MASTER_NEXT_OFF );
        }

        constexpr auto l_PB_COM_PB_CFG_CHG_RATE_GP_MASTER_NEXT_OFF = 0x0;
        l_scom_buffer.insert<2, 1, 61, uint64_t>(l_PB_COM_PB_CFG_CHG_RATE_GP_MASTER_NEXT_OFF );

        if (l_TGT0_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP == fapi2::ENUM_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP_TRUE) {
            constexpr auto l_PB_COM_PB_CFG_CHG_RATE_SP_MASTER_NEXT_ON = 0x7;
            l_scom_buffer.insert<3, 1, 61, uint64_t>(l_PB_COM_PB_CFG_CHG_RATE_SP_MASTER_NEXT_ON );
        } else {
            constexpr auto l_PB_COM_PB_CFG_CHG_RATE_SP_MASTER_NEXT_OFF = 0x0;
            l_scom_buffer.insert<3, 1, 61, uint64_t>(l_PB_COM_PB_CFG_CHG_RATE_SP_MASTER_NEXT_OFF );
        }

        constexpr auto l_PB_COM_PB_CFG_HOP_MODE_NEXT_OFF = 0x0;
        l_scom_buffer.insert<29, 1, 61, uint64_t>(l_PB_COM_PB_CFG_HOP_MODE_NEXT_OFF );

        fapi2::putScom(TGT0, 0x501180bull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x501180full, l_scom_buffer );

        if (l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[1] != fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_FALSE) {
            constexpr auto l_PB_COM_PB_CFG_LINK_X1_EN_NEXT_ON = 0x7;
            l_scom_buffer.insert<1, 1, 61, uint64_t>(l_PB_COM_PB_CFG_LINK_X1_EN_NEXT_ON );
        }

        if (l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_ID[1] == 0) {
            constexpr auto l_PB_COM_PB_CFG_LINK_X1_CHIPID_NEXT_ID_0 = 0x0;
            l_scom_buffer.insert<19, 3, 55, uint64_t>(l_PB_COM_PB_CFG_LINK_X1_CHIPID_NEXT_ID_0 );
        } else if (l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_ID[1] == 1) {
            constexpr auto l_PB_COM_PB_CFG_LINK_X1_CHIPID_NEXT_ID_1 = 0x49;
            l_scom_buffer.insert<19, 3, 55, uint64_t>(l_PB_COM_PB_CFG_LINK_X1_CHIPID_NEXT_ID_1 );
        }

        constexpr auto l_PB_COM_PB_CFG_X_INDIRECT_EN_NEXT_ON = 0x7;
        l_scom_buffer.insert<49, 1, 61, uint64_t>(l_PB_COM_PB_CFG_X_INDIRECT_EN_NEXT_ON );
        constexpr auto l_PB_COM_PB_CFG_X_GATHER_ENABLE_NEXT_ON = 0x7;
        l_scom_buffer.insert<50, 1, 61, uint64_t>(l_PB_COM_PB_CFG_X_GATHER_ENABLE_NEXT_ON );

        if (l_def_X_CMD_RATE_4B_R != 0 && l_TGT0_ATTR_CHIP_EC_FEATURE_HW407123 != 0)
            l_scom_buffer.insert<56, 8, 56, uint64_t>(((l_def_X_CMD_RATE_4B_N / l_def_X_CMD_RATE_D) + 3));
        else if (l_def_X_CMD_RATE_4B_R == 0 && (l_TGT0_ATTR_CHIP_EC_FEATURE_HW407123 != 0))
            l_scom_buffer.insert<56, 8, 56, uint64_t>(((l_def_X_CMD_RATE_4B_N / l_def_X_CMD_RATE_D) + 2));
        else if (l_def_X_CMD_RATE_4B_R != 0)
            l_scom_buffer.insert<56, 8, 56, uint64_t>((l_def_X_CMD_RATE_4B_N / l_def_X_CMD_RATE_D));
        else if (l_def_X_CMD_RATE_4B_R == 0)
            l_scom_buffer.insert<56, 8, 56, uint64_t>(((l_def_X_CMD_RATE_4B_N / l_def_X_CMD_RATE_D) - 1));

        fapi2::putScom(TGT0, 0x501180full, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5011c0bull, l_scom_buffer );

        if (l_TGT0_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP == fapi2::ENUM_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP_FALSE) {
            constexpr auto l_PB_COM_PB_CFG_MASTER_CHIP_NEXT_OFF = 0x0;
            l_scom_buffer.insert<0, 1, 62, uint64_t>(l_PB_COM_PB_CFG_MASTER_CHIP_NEXT_OFF );

            constexpr auto l_PB_COM_PB_CFG_TM_MASTER_NEXT_OFF = 0x0;
            l_scom_buffer.insert<1, 1, 62, uint64_t>(l_PB_COM_PB_CFG_TM_MASTER_NEXT_OFF );
        }

        constexpr auto l_PB_COM_PB_CFG_CHG_RATE_GP_MASTER_NEXT_OFF = 0x0;
        l_scom_buffer.insert<2, 1, 62, uint64_t>(l_PB_COM_PB_CFG_CHG_RATE_GP_MASTER_NEXT_OFF );

        if (l_TGT0_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP == fapi2::ENUM_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP_TRUE) {
            constexpr auto l_PB_COM_PB_CFG_CHG_RATE_SP_MASTER_NEXT_ON = 0x7;
            l_scom_buffer.insert<3, 1, 62, uint64_t>(l_PB_COM_PB_CFG_CHG_RATE_SP_MASTER_NEXT_ON );
        } else {
            constexpr auto l_PB_COM_PB_CFG_CHG_RATE_SP_MASTER_NEXT_OFF = 0x0;
            l_scom_buffer.insert<3, 1, 62, uint64_t>(l_PB_COM_PB_CFG_CHG_RATE_SP_MASTER_NEXT_OFF );
        }

        constexpr auto l_PB_COM_PB_CFG_HOP_MODE_NEXT_OFF = 0x0;
        l_scom_buffer.insert<29, 1, 62, uint64_t>(l_PB_COM_PB_CFG_HOP_MODE_NEXT_OFF );

        fapi2::putScom(TGT0, 0x5011c0bull, l_scom_buffer);
    }
    {
        fapi2::getScom( TGT0, 0x5011c0full, l_scom_buffer );

        if (l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[1] != fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_FALSE) {
            constexpr auto l_PB_COM_PB_CFG_LINK_X1_EN_NEXT_ON = 0x7;
            l_scom_buffer.insert<1, 1, 62, uint64_t>(l_PB_COM_PB_CFG_LINK_X1_EN_NEXT_ON );
        }

        if (l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_ID[1] == 0) {
            constexpr auto l_PB_COM_PB_CFG_LINK_X1_CHIPID_NEXT_ID_0 = 0x0;
            l_scom_buffer.insert<19, 3, 58, uint64_t>(l_PB_COM_PB_CFG_LINK_X1_CHIPID_NEXT_ID_0 );
        } else if (l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_ID[1] == 1) {
            constexpr auto l_PB_COM_PB_CFG_LINK_X1_CHIPID_NEXT_ID_1 = 0x49;
            l_scom_buffer.insert<19, 3, 58, uint64_t>(l_PB_COM_PB_CFG_LINK_X1_CHIPID_NEXT_ID_1 );
        }

        constexpr auto l_PB_COM_PB_CFG_X_INDIRECT_EN_NEXT_ON = 0x7;
        l_scom_buffer.insert<49, 1, 62, uint64_t>(l_PB_COM_PB_CFG_X_INDIRECT_EN_NEXT_ON );
        constexpr auto l_PB_COM_PB_CFG_X_GATHER_ENABLE_NEXT_ON = 0x7;
        l_scom_buffer.insert<50, 1, 62, uint64_t>(l_PB_COM_PB_CFG_X_GATHER_ENABLE_NEXT_ON );

        if (l_def_X_CMD_RATE_4B_R != 0 && l_TGT0_ATTR_CHIP_EC_FEATURE_HW407123 != 0)
            l_scom_buffer.insert<56, 8, 56, uint64_t>(((l_def_X_CMD_RATE_4B_N / l_def_X_CMD_RATE_D) + 3) );
        else if (l_def_X_CMD_RATE_4B_R == 0 && l_TGT0_ATTR_CHIP_EC_FEATURE_HW407123 != 0)
            l_scom_buffer.insert<56, 8, 56, uint64_t>(((l_def_X_CMD_RATE_4B_N / l_def_X_CMD_RATE_D) + 2) );
        else if (l_def_X_CMD_RATE_4B_R != 0)
            l_scom_buffer.insert<56, 8, 56, uint64_t>((l_def_X_CMD_RATE_4B_N / l_def_X_CMD_RATE_D) );
        else if (l_def_X_CMD_RATE_4B_R == 0)
            l_scom_buffer.insert<56, 8, 56, uint64_t>(((l_def_X_CMD_RATE_4B_N / l_def_X_CMD_RATE_D) - 1) );

        fapi2::putScom(TGT0, 0x5011c0full, l_scom_buffer);
    }
    {
        fapi2::getScom(TGT0, 0x501200bull, l_scom_buffer );

        if (l_TGT0_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP == fapi2::ENUM_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP_FALSE) {
            constexpr auto l_PB_COM_PB_CFG_MASTER_CHIP_NEXT_OFF = 0x0;
            l_scom_buffer.insert<0, 1, 63, uint64_t>(l_PB_COM_PB_CFG_MASTER_CHIP_NEXT_OFF );
        }

        if (l_TGT0_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP == fapi2::ENUM_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP_FALSE) {
            constexpr auto l_PB_COM_PB_CFG_TM_MASTER_NEXT_OFF = 0x0;
            l_scom_buffer.insert<1, 1, 63, uint64_t>(l_PB_COM_PB_CFG_TM_MASTER_NEXT_OFF );
        }

        constexpr auto l_PB_COM_PB_CFG_CHG_RATE_GP_MASTER_NEXT_OFF = 0x0;
        l_scom_buffer.insert<2, 1, 63, uint64_t>(l_PB_COM_PB_CFG_CHG_RATE_GP_MASTER_NEXT_OFF );

        if (l_TGT0_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP == fapi2::ENUM_ATTR_PROC_FABRIC_SYSTEM_MASTER_CHIP_TRUE) {
            constexpr auto l_PB_COM_PB_CFG_CHG_RATE_SP_MASTER_NEXT_ON = 0x7;
            l_scom_buffer.insert<3, 1, 63, uint64_t>(l_PB_COM_PB_CFG_CHG_RATE_SP_MASTER_NEXT_ON );
        } else {
            constexpr auto l_PB_COM_PB_CFG_CHG_RATE_SP_MASTER_NEXT_OFF = 0x0;
            l_scom_buffer.insert<3, 1, 63, uint64_t>(l_PB_COM_PB_CFG_CHG_RATE_SP_MASTER_NEXT_OFF );
        }

        constexpr auto l_PB_COM_PB_CFG_HOP_MODE_NEXT_OFF = 0x0;
        l_scom_buffer.insert<29, 1, 63, uint64_t>(l_PB_COM_PB_CFG_HOP_MODE_NEXT_OFF );

        fapi2::putScom(TGT0, 0x501200bull, l_scom_buffer);
    }
    {
        fapi2::getScom(TGT0, 0x501200full, l_scom_buffer );

        if (l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[1] != fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_FALSE) {
            constexpr auto l_PB_COM_PB_CFG_LINK_X1_EN_NEXT_ON = 0x7;
            l_scom_buffer.insert<1, 1, 63, uint64_t>(l_PB_COM_PB_CFG_LINK_X1_EN_NEXT_ON );
        }

        if (l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_ID[1] == 0) {
            constexpr auto l_PB_COM_PB_CFG_LINK_X1_CHIPID_NEXT_ID_0 = 0x0;
            l_scom_buffer.insert<19, 3, 61, uint64_t>(l_PB_COM_PB_CFG_LINK_X1_CHIPID_NEXT_ID_0 );
        } else if (l_TGT0_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_ID[1] == 1) {
            constexpr auto l_PB_COM_PB_CFG_LINK_X1_CHIPID_NEXT_ID_1 = 0x49;
            l_scom_buffer.insert<19, 3, 61, uint64_t>(l_PB_COM_PB_CFG_LINK_X1_CHIPID_NEXT_ID_1 );
        }

        constexpr auto l_PB_COM_PB_CFG_X_INDIRECT_EN_NEXT_ON = 0x7;
        l_scom_buffer.insert<49, 1, 63, uint64_t>(l_PB_COM_PB_CFG_X_INDIRECT_EN_NEXT_ON );
        constexpr auto l_PB_COM_PB_CFG_X_GATHER_ENABLE_NEXT_ON = 0x7;
        l_scom_buffer.insert<50, 1, 63, uint64_t>(l_PB_COM_PB_CFG_X_GATHER_ENABLE_NEXT_ON );

        if (l_def_X_CMD_RATE_4B_R != 0 && l_TGT0_ATTR_CHIP_EC_FEATURE_HW407123 != 0)
            l_scom_buffer.insert<56, 8, 56, uint64_t>(((l_def_X_CMD_RATE_4B_N / l_def_X_CMD_RATE_D) + 3) );
        else if (l_def_X_CMD_RATE_4B_R == 0 && l_TGT0_ATTR_CHIP_EC_FEATURE_HW407123 != 0)
            l_scom_buffer.insert<56, 8, 56, uint64_t>(((l_def_X_CMD_RATE_4B_N / l_def_X_CMD_RATE_D) + 2) );
        else if (l_def_X_CMD_RATE_4B_R != 0)
            l_scom_buffer.insert<56, 8, 56, uint64_t>((l_def_X_CMD_RATE_4B_N / l_def_X_CMD_RATE_D) );
        else if (l_def_X_CMD_RATE_4B_R == 0)
            l_scom_buffer.insert<56, 8, 56, uint64_t>(((l_def_X_CMD_RATE_4B_N / l_def_X_CMD_RATE_D) - 1) );

        fapi2::putScom(TGT0, 0x501200full, l_scom_buffer);
    }

    return fapi2::current_err;
}

enum {
    PU_PB_WEST_SM0_PB_WEST_HP_MODE_CURR  = 0x0501180C,
    PU_PB_CENT_SM0_PB_CENT_HP_MODE_CURR  = 0x05011C0C,
    PU_PB_EAST_HP_MODE_CURR              = 0x0501200C,

    PU_PB_WEST_SM0_PB_WEST_HP_MODE_NEXT  = 0x0501180B,
    PU_PB_CENT_SM0_PB_CENT_HP_MODE_NEXT  = 0x05011C0B,
    PU_PB_EAST_HP_MODE_NEXT              = 0x0501200B,

    PU_PB_WEST_SM0_PB_WEST_HPX_MODE_CURR = 0x05011810,
    PU_PB_CENT_SM0_PB_CENT_HPX_MODE_CURR = 0x05011C10,
    PU_PB_EAST_HPX_MODE_CURR             = 0x05012010,

    PU_PB_WEST_SM0_PB_WEST_HPX_MODE_NEXT = 0x0501180F,
    PU_PB_CENT_SM0_PB_CENT_HPX_MODE_NEXT = 0x05011C0F,
    PU_PB_EAST_HPX_MODE_NEXT             = 0x0501200F,

    PU_PB_WEST_SM0_PB_WEST_HPA_MODE_CURR = 0x0501180E,
    PU_PB_CENT_SM0_PB_CENT_HPA_MODE_CURR = 0x05011C0E,
    PU_PB_EAST_HPA_MODE_CURR             = 0x0501200E,

    PU_PB_WEST_SM0_PB_WEST_HPA_MODE_NEXT = 0x0501180D,
    PU_PB_CENT_SM0_PB_CENT_HPA_MODE_NEXT = 0x05011C0D,
    PU_PB_EAST_HPA_MODE_NEXT             = 0x0501200D,
};

// PB shadow register constant definition
const uint8_t P9_BUILD_SMP_NUM_SHADOWS = 3;

// HP (HotPlug Mode Register)
const uint64_t PB_HP_MODE_CURR_SHADOWS[P9_BUILD_SMP_NUM_SHADOWS] =
{
    PU_PB_WEST_SM0_PB_WEST_HP_MODE_CURR,
    PU_PB_CENT_SM0_PB_CENT_HP_MODE_CURR,
    PU_PB_EAST_HP_MODE_CURR
};

const uint64_t PB_HP_MODE_NEXT_SHADOWS[P9_BUILD_SMP_NUM_SHADOWS] =
{
    PU_PB_WEST_SM0_PB_WEST_HP_MODE_NEXT,
    PU_PB_CENT_SM0_PB_CENT_HP_MODE_NEXT,
    PU_PB_EAST_HP_MODE_NEXT
};

// HPX (Hotplug Mode Register Extension)
const uint64_t PB_HPX_MODE_CURR_SHADOWS[P9_BUILD_SMP_NUM_SHADOWS] =
{
    PU_PB_WEST_SM0_PB_WEST_HPX_MODE_CURR,
    PU_PB_CENT_SM0_PB_CENT_HPX_MODE_CURR,
    PU_PB_EAST_HPX_MODE_CURR
};

const uint64_t PB_HPX_MODE_NEXT_SHADOWS[P9_BUILD_SMP_NUM_SHADOWS] =
{
    PU_PB_WEST_SM0_PB_WEST_HPX_MODE_NEXT,
    PU_PB_CENT_SM0_PB_CENT_HPX_MODE_NEXT,
    PU_PB_EAST_HPX_MODE_NEXT
};

// HPA
const uint64_t PB_HPA_MODE_CURR_SHADOWS[P9_BUILD_SMP_NUM_SHADOWS] =
{
    PU_PB_WEST_SM0_PB_WEST_HPA_MODE_CURR,
    PU_PB_CENT_SM0_PB_CENT_HPA_MODE_CURR,
    PU_PB_EAST_HPA_MODE_CURR
};

const uint64_t PB_HPA_MODE_NEXT_SHADOWS[P9_BUILD_SMP_NUM_SHADOWS] =
{
    PU_PB_WEST_SM0_PB_WEST_HPA_MODE_NEXT,
    PU_PB_CENT_SM0_PB_CENT_HPA_MODE_NEXT,
    PU_PB_EAST_HPA_MODE_NEXT
};

/// @brief Copy all hotplug content from NEXT->CURR
///
/// @param[in] i_target        Processor chip target
///
/// @return fapi2:ReturnCode. FAPI2_RC_SUCCESS if success, else error code.
fapi2::ReturnCode
p9_build_smp_copy_hp_ab_next_curr(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> i_target)
{
    fapi2::buffer<uint64_t> l_hp_mode_data;
    fapi2::buffer<uint64_t> l_hpx_mode_data;
    fapi2::buffer<uint64_t> l_hpa_mode_data;

    // read NEXT
    p9_build_smp_get_hp_ab_shadow(i_target, PB_HP_MODE_NEXT_SHADOWS, l_hp_mode_data);   /// return on error ///
    p9_build_smp_get_hp_ab_shadow(i_target, PB_HPX_MODE_NEXT_SHADOWS, l_hpx_mode_data); /// return on error ///
    p9_build_smp_get_hp_ab_shadow(i_target, PB_HPA_MODE_NEXT_SHADOWS, l_hpa_mode_data); /// return on error ///

    // write CURR
    p9_build_smp_set_hp_ab_shadow(i_target, PB_HP_MODE_CURR_SHADOWS, l_hp_mode_data);   /// return on error ///
    p9_build_smp_set_hp_ab_shadow(i_target, PB_HPX_MODE_CURR_SHADOWS, l_hpx_mode_data); /// return on error ///
    p9_build_smp_set_hp_ab_shadow(i_target, PB_HPA_MODE_CURR_SHADOWS, l_hpa_mode_data); /// return on error ///

    return fapi2::current_err;
}

/// @brief Copy all hotplug content from CURR->NEXT
///
/// @param[in] i_target        Processor chip target
///
/// @return fapi2:ReturnCode. FAPI2_RC_SUCCESS if success, else error code.
fapi2::ReturnCode
p9_build_smp_copy_hp_ab_curr_next(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> i_target)
{
    fapi2::buffer<uint64_t> l_hp_mode_data;
    fapi2::buffer<uint64_t> l_hpx_mode_data;
    fapi2::buffer<uint64_t> l_hpa_mode_data;

    // read CURR
    p9_build_smp_get_hp_ab_shadow(i_target, PB_HP_MODE_CURR_SHADOWS, l_hp_mode_data);   /// return on error ///
    p9_build_smp_get_hp_ab_shadow(i_target, PB_HPX_MODE_CURR_SHADOWS, l_hpx_mode_data); /// return on error ///
    p9_build_smp_get_hp_ab_shadow(i_target, PB_HPA_MODE_CURR_SHADOWS, l_hpa_mode_data); /// return on error ///

    // write NEXT
    p9_build_smp_set_hp_ab_shadow(i_target, PB_HP_MODE_NEXT_SHADOWS, l_hp_mode_data);   /// return on error ///
    p9_build_smp_set_hp_ab_shadow(i_target, PB_HPX_MODE_NEXT_SHADOWS, l_hpx_mode_data); /// return on error ///
    p9_build_smp_set_hp_ab_shadow(i_target, PB_HPA_MODE_NEXT_SHADOWS, l_hpa_mode_data); /// return on error ///

    return fapi2::current_err;
}

/// @brief Read and consistency check hotplug shadow register set
///
/// @param[in] i_target        Processor chip target
/// @param[in] i_shadow_regs   Array of hotplug shadow register addresses
/// @param[out] o_data         Hotplug register data
///
/// @return fapi2:ReturnCode. FAPI2_RC_SUCCESS if success, else error code.
fapi2::ReturnCode p9_build_smp_get_hp_ab_shadow(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> i_target,
    const uint64_t i_shadow_regs[],
    fapi2::buffer<uint64_t>& o_data)
{
    // check consistency of west/center/east register copies
    for (uint8_t rr = 0; rr < P9_BUILD_SMP_NUM_SHADOWS; rr++) {
        fapi2::buffer<uint64_t> l_scom_data;
        fapi2::getScom(i_target, i_shadow_regs[rr], l_scom_data));
        /// return on error ///
        // raise error if shadow copies aren't equal
        assert(rr == 0 || l_scom_data == o_data);
        // set output (will be used to compare with next HW read)
        o_data = l_scom_data;
    }

    return fapi2::current_err;
}

/// @brief Write hotplug shadow register set
///
/// @param[in] i_target        Processor chip target
/// @param[in] i_shadow_regs   Array of hotplug shadow register addresses
/// @param[in] i_data          Hotplug register data to write
///
/// @return fapi2:ReturnCode. FAPI2_RC_SUCCESS if success, else error code.
fapi2::ReturnCode p9_build_smp_set_hp_ab_shadow(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> i_target,
    const uint64_t i_shadow_regs[],
    fapi2::buffer<uint64_t>& i_data)
{
    // write data to all shadows
    for (uint8_t rr = 0; rr < P9_BUILD_SMP_NUM_SHADOWS; rr++) {
        fapi2::putScom(i_target, i_shadow_regs[rr], i_data);
        /// return on error ///
    }

    return fapi2::current_err;
}

/**
 * @brief Cleanup the FSI PIB2OPB logic on the procs
 *
 * @param[in] i_target  Proc Chip Target to reset
 *
 * @return errlHndl_t  NULL on success
 */
errlHndl_t FsiDD::resetPib2Opb( TARGETING::Target* i_target )
{
    errlHndl_t errhdl = NULL;

    do {
        uint64_t opb_offset = FSI2OPB_OFFSET_0;
        if (i_target != iv_master
            && i_target->getAttr<TARGETING::ATTR_FSI_OPTION_FLAGS>().flipPort
            && !iv_useAlt)
        {
            // Flipping
            opb_offset = FSI2OPB_OFFSET_1;
        }
        else if (i_target != iv_master && iv_useAlt)
        {
            // Using alt path
            opb_offset = FSI2OPB_OFFSET_1;
        }

        // Clear out OPB error
        uint64_t scom_data = 0;
        size_t scom_size = sizeof(scom_data);

        uint64_t opbaddr = opb_offset | OPB_REG_RES;
        scom_data = 0x8000000000000000; //0=Unit Reset
        errhdl = deviceOp( DeviceFW::WRITE,
                           i_target,
                           &scom_data,
                           scom_size,
                           DEVICE_XSCOM_ADDRESS(opbaddr) );
        if( errhdl ) break;

        opbaddr = opb_offset | OPB_REG_STAT;
        errhdl = deviceOp( DeviceFW::WRITE,
                           i_target,
                           &scom_data,
                           scom_size,
                           DEVICE_XSCOM_ADDRESS(opbaddr) );
        if( errhdl ) break;

        // Check if we have any errors left
        opbaddr = opb_offset | OPB_REG_STAT;
        scom_data = 0;
        errhdl = deviceOp( DeviceFW::READ,
                           i_target,
                           &scom_data,
                           scom_size,
                           DEVICE_XSCOM_ADDRESS(opbaddr) );
        if( errhdl ) break;
    } while(0);

    return errhdl;
}

// Maybe do in the future:
INTR::enablePsiIntr(l_proc_target);
cpu_spr_value(CPU_SPR_HRMOR);
SBEIO::openUnsecureMemRegion(...);
p9_io_xbus_erepair_cleanup(fapi2_xbus);
```

## ADU interaction

This covers only single byte writes where written data is irrelevant.

```cpp
enum p9_build_smp_adu_action
{
    SWITCH_AB = 1,
    SWITCH_CD = 2,
    QUIESCE   = 4,
    RESET_SWITCH = 8
};

const uint32_t P9_ADU_ACCESS_ADU_OPER_HW_NS_DELAY = 10000;
const uint32_t PROC_ADU_UTILS_ADU_OPER_HW_NS_DELAY = 10000;
const uint32_t PROC_ADU_UTILS_ADU_STATUS_HW_NS_DELAY = 100;

/// @brief Perform fabric quiesce/switch operation via ADU
///
/// @param[in] i_smp       Fully specified structure encapsulating SMP
/// @param[in] i_op        Enumerated type representing SMP build phase (HB or FSP)
/// @param[in] i_action    Enumerated type representing fabric operation
///
/// @return FAPI2_RC_SUCCESS if success, else error code.
fapi2::ReturnCode p9_build_smp_sequence_adu(p9_build_smp_system& i_smp,
        const p9_build_smp_operation i_op,
        const p9_build_smp_adu_action i_action)
{
    // validate input action, set ADU operation parameters
    uint32_t l_flags = fapi2::SBE_MEM_ACCESS_FLAGS_TARGET_PROC;
    uint32_t l_bytes = 1;
    uint64_t l_addr = 0x0ULL;
    uint8_t l_data_unused[1];

    switch (i_action)
    {
        case SWITCH_AB:
        case SWITCH_CD:
            l_flags |= fapi2::SBE_MEM_ACCESS_FLAGS_SWITCH_MODE;
            break;
        case QUIESCE:
            l_flags |= fapi2::SBE_MEM_ACCESS_FLAGS_PB_DIS_MODE;
            break;
        default:
            assert(false && "Invalid ADU action specified");
    }

    // loop through all chips, set switch operation
    for (auto g_iter = i_smp.groups.begin(); g_iter != i_smp.groups.end(); ++g_iter) {
        for (auto p_iter = g_iter->second.chips.begin(); p_iter != g_iter->second.chips.end(); ++p_iter) {
            // Condition for hotplug switch operation
            // all chips which were not quiesced prior to switch AB will
            // need to observe the switch
            if (i_action != QUIESCE) {
                p9_build_smp_adu_set_switch_action(*(p_iter->second.target), i_action);
                /// return on error ///
            }
        }
    }

    // perform action on specified chips
    for (auto g_iter = i_smp.groups.begin(); g_iter != i_smp.groups.end(); ++g_iter) {
        for (auto p_iter = g_iter->second.chips.begin(); p_iter != g_iter->second.chips.end(); ++p_iter) {
            if ((i_action == QUIESCE && (p_iter->second.issue_quiesce_next)) ||
                (i_action == SWITCH_AB && p_iter->second.master_chip_sys_next) ||
                (i_action == SWITCH_CD))
            {
                // issue ADU operation for target
                fapi2::current_err = p9_putmemproc(
                    (*(p_iter->second.target)),
                    l_addr,
                    l_bytes,
                    l_data_unused,
                    l_flags);
                /// return on error ///
            }
        }
    }

    // operation complete, loop through all chips, reset switch controls
    if (i_action != QUIESCE) {
        for (auto g_iter = i_smp.groups.begin(); g_iter != i_smp.groups.end(); ++g_iter) {
            for (auto p_iter = g_iter->second.chips.begin(); p_iter != g_iter->second.chips.end(); ++p_iter) {
                // reset switch controls
                p9_build_smp_adu_set_switch_action(*(p_iter->second.target),
                                                    p9_build_smp_adu_action::RESET_SWITCH);
                /// return on error ///
            }
        }
    }

    return fapi2::current_err;
}

/// @brief Set action which will occur on fabric pmisc switch command
///
/// @param[in] i_target    Processor chip target
/// @param[in] i_action    Enumerated type representing fabric operation
///
/// @return fapi2:ReturnCode. FAPI2_RC_SUCCESS if success, else error code.
fapi2::ReturnCode p9_build_smp_adu_set_switch_action(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9_build_smp_adu_action i_action)
{
    uint64_t l_addr = 0x0ULL;
    uint32_t l_bytes = 1;
    uint32_t l_flags = fapi2::SBE_MEM_ACCESS_FLAGS_TARGET_PROC;
    uint8_t l_data_unused[1];

    if (i_action == SWITCH_AB)
        l_flags |= fapi2::SBE_MEM_ACCESS_FLAGS_PRE_SWITCH_AB_MODE;
    else if (i_action == SWITCH_CD)
        l_flags |= fapi2::SBE_MEM_ACCESS_FLAGS_PRE_SWITCH_CD_MODE;
    else
        l_flags |= fapi2::SBE_MEM_ACCESS_FLAGS_POST_SWITCH_MODE;

    // issue operation
    return p9_putmemproc(i_target, l_addr, l_bytes, l_data_unused, l_flags);
}

/// @brief Invoke ADU putmem chipop
///
/// @param[in] i_target Reference to processor chip target
/// @param[in] i_address Base address for write operation
/// @param[in] i_bytes Size of write data, in B
/// @param[in] i_data Pointer to write data
/// @param[in] i_mem_flags Flags to pass to chipop
///
/// @return FAPI_RC_SUCCESS if success, else error code
fapi2::ReturnCode p9_putmemproc(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const uint64_t i_address,
    const uint32_t i_bytes,
    uint8_t* i_data,
    const uint32_t i_mem_flags)
{
    p9_ADU_oper_flag l_flags;
    uint64_t l_target_address = i_address;
    uint64_t l_end_address = i_address + i_bytes;
    uint32_t l_granules_before_setup = 0;
    uint32_t l_granule = 0;
    uint8_t l_data[1];
    bool l_first_access = true;

    // Invalid flags specified for ADU access
    assert(((i_mem_flags & fapi2::SBE_MEM_ACCESS_FLAGS_TARGET_PROC) == fapi2::SBE_MEM_ACCESS_FLAGS_TARGET_PROC) &&
           ((i_mem_flags & fapi2::SBE_MEM_ACCESS_FLAGS_TARGET_PBA)  == 0) &&
           ((i_mem_flags & fapi2::SBE_MEM_ACCESS_FLAGS_LCO_MODE) == 0) &&
           ((i_mem_flags & fapi2::SBE_MEM_ACCESS_FLAGS_CACHE_INJECT_MODE) == 0))
    assert(((i_mem_flags & fapi2::SBE_MEM_ACCESS_FLAGS_ECC_OVERRIDE) == 0) &&
           ((i_mem_flags & fapi2::SBE_MEM_ACCESS_FLAGS_TAG) == 0) &&
           ((i_mem_flags & fapi2::SBE_MEM_ACCESS_FLAGS_HOST_PASS_THROUGH) == 0));

    // set auto-increment
    l_flags.setAutoIncrement((i_mem_flags & fapi2::SBE_MEM_ACCESS_FLAGS_AUTO_INCR_ON) ==
                             fapi2::SBE_MEM_ACCESS_FLAGS_AUTO_INCR_ON);

    // set fast mode
    l_flags.setFastMode((i_mem_flags & fapi2::SBE_MEM_ACCESS_FLAGS_FAST_MODE_ON) ==
                        fapi2::SBE_MEM_ACCESS_FLAGS_FAST_MODE_ON);

    // set operation type and transaction size
    if ((i_mem_flags & fapi2::SBE_MEM_ACCESS_FLAGS_CACHE_INHIBITED_MODE) == fapi2::SBE_MEM_ACCESS_FLAGS_CACHE_INHIBITED_MODE)
    {
        l_flags.setOperationType(p9_ADU_oper_flag::CACHE_INHIBIT);

        if (i_bytes == 4)
            l_flags.setTransactionSize(p9_ADU_oper_flag::TSIZE_4);
        else if (i_bytes == 2)
            l_flags.setTransactionSize(p9_ADU_oper_flag::TSIZE_2);
        else if (i_bytes == 1)
            l_flags.setTransactionSize(p9_ADU_oper_flag::TSIZE_1);
        else if (i_bytes == 8)
            l_flags.setTransactionSize(p9_ADU_oper_flag::TSIZE_8);
        else
            die("Invalid byte count specified for cache-inhibited access");
    }
    else if ((i_mem_flags & fapi2::SBE_MEM_ACCESS_FLAGS_PB_DIS_MODE) == fapi2::SBE_MEM_ACCESS_FLAGS_PB_DIS_MODE)
    {
        l_flags.setOperationType(p9_ADU_oper_flag::PB_DIS_OPER);
    }
    else if ((i_mem_flags & fapi2::SBE_MEM_ACCESS_FLAGS_SWITCH_MODE) == fapi2::SBE_MEM_ACCESS_FLAGS_SWITCH_MODE)
    {
        l_flags.setOperationType(p9_ADU_oper_flag::PMISC_OPER);
    }
    else if ((i_mem_flags & fapi2::SBE_MEM_ACCESS_FLAGS_PB_INIT_MODE) == fapi2::SBE_MEM_ACCESS_FLAGS_PB_INIT_MODE)
    {
        l_flags.setOperationType(p9_ADU_oper_flag::PB_INIT_OPER);
    }
    else if ((i_mem_flags & fapi2::SBE_MEM_ACCESS_FLAGS_PRE_SWITCH_CD_MODE) ==
             fapi2::SBE_MEM_ACCESS_FLAGS_PRE_SWITCH_CD_MODE)
    {
        l_flags.setOperationType(p9_ADU_oper_flag::PRE_SWITCH_CD);
    }
    else if ((i_mem_flags & fapi2::SBE_MEM_ACCESS_FLAGS_PRE_SWITCH_AB_MODE) ==
             fapi2::SBE_MEM_ACCESS_FLAGS_PRE_SWITCH_AB_MODE)
    {
        l_flags.setOperationType(p9_ADU_oper_flag::PRE_SWITCH_AB);
    }
    else if ((i_mem_flags & fapi2::SBE_MEM_ACCESS_FLAGS_POST_SWITCH_MODE) == fapi2::SBE_MEM_ACCESS_FLAGS_POST_SWITCH_MODE)
    {
        l_flags.setOperationType(p9_ADU_oper_flag::POST_SWITCH);
    }
    else
    {
        l_flags.setOperationType(p9_ADU_oper_flag::DMA_PARTIAL);

        if (i_bytes == 4)
            l_flags.setTransactionSize(p9_ADU_oper_flag::TSIZE_4);
        else if (i_bytes == 2)
            l_flags.setTransactionSize(p9_ADU_oper_flag::TSIZE_2);
        else if (i_bytes == 1)
            l_flags.setTransactionSize(p9_ADU_oper_flag::TSIZE_1);
        else if ((i_bytes % 8) == 0)
            l_flags.setTransactionSize(p9_ADU_oper_flag::TSIZE_8);
        else
            die("Invalid byte count specified for DMA partial write access");
    }

    while (l_target_address < l_end_address) {
        // invoke ADU setup HWP to prepare current stream of contiguous granules
        p9_adu_setup(i_target, l_target_address, false, l_flags.setFlag(), l_granules_before_setup));
        /// return on error ///

        l_first_access = true;

        while (l_granules_before_setup && (l_target_address < l_end_address)) {
            // invoke ADU access HWP to move one granule (8B)
            l_data[0] = i_data[l_granule];
            p9_adu_access(i_target, l_target_address, false, l_flags.setFlag(),
                          l_first_access,
                          (l_granules_before_setup == 1) || ((l_target_address + 8) >= l_end_address),
                          l_data);
            /// return on error ///

            l_first_access = false;
            l_granules_before_setup--;
            l_target_address += 8;
            l_granule++;
        }
    }

    return fapi2::current_err;
}

/// @brief setup for reads/writes from the ADU
/// @param[in] i_target       => P9 chip target
/// @param[in] i_address      => base real address for read/write operation (expected to be 8B aligned)
/// @param[in] i_rnw          => if the operation is read not write (1 for read, 0 for write)
/// @param[in] i_flags        => other information that is needed - see the p9_adu_constants adu_flags enums for bit definitions
///                              Note: To construct the flag you can use p9_ADU_oper_flag class
/// @param[out] o_numGranules  => number of 8B granules that can be read/written before setup needs to be called again
//
/// @return FAPI_RC_SUCCESS if the setup completes successfully,
fapi2::ReturnCode p9_adu_setup(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const uint64_t i_address,
    const bool i_rnw,
    const uint32_t i_flags,
    uint32_t& o_numGranules)
{
    fapi2::ReturnCode l_rc = fapi2::FAPI2_RC_SUCCESS;
    uint32_t num_attempts = 1;
    bool lock_pick = false;

    // Process input flag
    p9_ADU_oper_flag l_myAduFlag;
    l_myAduFlag.getFlag(i_flags);

    // permit/pass autoinc only if performing a DMA operation
    if (l_myAduFlag.getOperationType() != p9_ADU_oper_flag::DMA_PARTIAL)
        l_myAduFlag.setAutoIncrement(false);

    uint32_t l_flags = l_myAduFlag.setFlag();

    // don't generate fabric command, just pre-condition ADU for upcoming switch
    if (l_myAduFlag.getOperationType() == p9_ADU_oper_flag::PRE_SWITCH_AB ||
        l_myAduFlag.getOperationType() == p9_ADU_oper_flag::PRE_SWITCH_CD ||
        l_myAduFlag.getOperationType() == p9_ADU_oper_flag::POST_SWITCH)
    {
        p9_adu_coherent_utils_set_switch_action(
                     i_target,
                     (l_myAduFlag.getOperationType() == p9_ADU_oper_flag::PRE_SWITCH_AB),
                     (l_myAduFlag.getOperationType() == p9_ADU_oper_flag::PRE_SWITCH_CD));
        /// return on error ///
        o_numGranules = 1;
        goto fapi_try_exit;
    }

    // check arguments
    p9_adu_coherent_utils_check_args(i_target, i_address, l_flags);
    /// return on error ///

    // ensure fabric is running, unless we're trying to initialize it
    if (l_myAduFlag.getOperationType() != p9_ADU_oper_flag::PB_INIT_OPER) {
        p9_adu_coherent_utils_check_fbc_state(i_target);
        /// return on error ///
    }

    // acquire ADU lock to guarantee exclusive use of the ADU resources
    // ADU state machine will be reset/cleared by this routine
    lock_pick = l_flags & FLAG_LOCK_PICK;
    num_attempts = l_flags & FLAG_LOCK_TRIES;
    p9_adu_coherent_manage_lock(i_target, lock_pick, true, num_attempts);
    /// return on error ///

    if (l_myAduFlag.getAutoIncrement() == true) {
        //figure out how many granules can be requested before setup needs to be run again
        p9_adu_coherent_utils_get_num_granules(i_address, o_numGranules);
        /// return on error ///
    } else {
        o_numGranules = 1;
    }

    //setup the ADU registers for the read/write
    p9_adu_coherent_setup_adu(i_target, i_address, i_rnw, l_flags);

    return l_rc;
}

/// @brief Set action which will occur on fabric pmisc switch command
///
/// @param[in] i_target        Processor chip target
/// @param[in] i_switch_ab     Perform switch AB operation?
/// @param[in] i_switch_cd     Perform switch CD operation?
///
/// @return fapi2:ReturnCode. FAPI2_RC_SUCCESS if success, else error code.
fapi2::ReturnCode p9_adu_coherent_utils_set_switch_action(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const bool i_switch_ab,
    const bool i_switch_cd)
{
    fapi2::buffer<uint64_t> pmisc_data;
    fapi2::buffer<uint64_t> pmisc_mask;

    // Build ADU pMisc Mode register content

    // Switch AB bit
    pmisc_data.writeBit<PU_SND_MODE_REG_ENABLE_PB_SWITCH_AB>(i_switch_ab);
    pmisc_mask.setBit<PU_SND_MODE_REG_ENABLE_PB_SWITCH_AB>();

    // Switch CD bit
    pmisc_data.writeBit<PU_SND_MODE_REG_ENABLE_PB_SWITCH_CD>(i_switch_cd);
    pmisc_mask.setBit<PU_SND_MODE_REG_ENABLE_PB_SWITCH_CD>();

    return fapi2::putScomUnderMask(i_target, PU_SND_MODE_REG, pmisc_data, pmisc_mask);
}

/// @brief check that the address is cacheline aligned and within the fabric real address range
/// @param[in] i_target  => P9 chip target
/// @param[in] i_address => starting address for ADU operation
/// @return FAPI_RC_SUCCESS if arguments are valid
fapi2::ReturnCode p9_adu_coherent_utils_check_args(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const uint64_t i_address,
    const uint32_t i_flags)
{
    p9_ADU_oper_flag l_myAduFlag;
    p9_ADU_oper_flag::Transaction_size_t l_transSize;
    uint32_t l_actualTransSize;

    // Get the transaction size
    l_transSize = l_myAduFlag.getTransactionSize();

    // Translate the transaction size to the actual size (1, 2, 4, or 8 bytes)
    if (l_transSize == p9_ADU_oper_flag::TSIZE_1)
        l_actualTransSize = 1;
    else if (l_transSize == p9_ADU_oper_flag::TSIZE_2)
        l_actualTransSize = 2;
    else if (l_transSize == p9_ADU_oper_flag::TSIZE_4)
        l_actualTransSize = 4;
    else
        l_actualTransSize = 8;

    //Check the address alignment (must be cachline aligned)
    assert(!(i_address & (l_actualTransSize - 1)));

    // Make sure the address is within the ADU bounds so it doesn't exceeds
    // supported fabric real address range
    assert(i_address <= P9_FBC_UTILS_FBC_MAX_ADDRESS);

    return fapi2::current_err;
}

/// @brief ensure that fabric is initialized and stop control is not set
///           (by checkstop/mode switch), which if set would prohibit fabric
///           commands from being broadcasted
/// @param[in] i_target => P9 chip target
/// @return FAPI_RC_SUCCESS if fabric is not stopped
fapi2::ReturnCode p9_adu_coherent_utils_check_fbc_state(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    bool fbc_initialized = false;
    bool fbc_running = false;

    //Get the state of the fabric
    p9_fbc_utils_get_fbc_state(i_target, fbc_initialized, fbc_running);
    /// return on error ///

    //Make sure the fabric is initialized and running othewise set an error
    assert(fbc_initialized && fbc_running);

    return fapi2::current_err;
}

/// @brief Read FBC/ADU registers to determine state of fabric init and stop
/// control signals
///
/// @param[in] i_target Reference to processor chip target
/// @param[out] o_is_initialized State of fabric init signal
/// @param[out] o_is_running State of fabric pervasive stop control
/// @return fapi::ReturnCode, FAPI2_RC_SUCCESS if success, else error code.
fapi2::ReturnCode p9_fbc_utils_get_fbc_state(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    bool& o_is_initialized,
    bool& o_is_running)
{
    fapi2::buffer<uint64_t> l_fbc_mode_data;
    fapi2::buffer<uint64_t> l_pmisc_mode_data;

    // readFBC Mode Register
    fapi2::getScom(i_target, PU_PB_CENT_SM0_PB_CENT_MODE, l_fbc_mode_data);
    // fabric is initialized if PB_INITIALIZED bit is one/set
    o_is_initialized = l_fbc_mode_data.getBit<PU_PB_CENT_SM0_PB_CENT_MODE_PB_CENT_PBIXXX_INIT>();

    // read ADU PMisc Mode Register state
    fapi2::getScom(i_target, PU_SND_MODE_REG, l_pmisc_mode_data);

    // fabric is running if FBC_STOP bit is zero/clear
    o_is_running = !(l_pmisc_mode_data.getBit<PU_SND_MODE_REG_PB_STOP>());

    return fapi2::current_err;
}

/// @brief this will acquire and release a lock as well as deal with any lock picking
/// @param[in] i_target       => P9 chip target
/// @param[in] i_lock_pick    => If the lock does not go through should we set a lock pick
/// @param[in] i_lock         => true if this is to lock the ADU false if this is to unlock the ADU
/// @param[in] i_num_attempts => number of times to try locking the ADU (must be > 0)
fapi2::ReturnCode p9_adu_coherent_manage_lock(const
        fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
        const bool i_lock_pick,
        const bool i_lock,
        const uint32_t i_num_attempts)
{
    fapi2::ReturnCode rc;
    fapi2::buffer<uint64_t> lock_control(0x0);
    uint32_t attempt_count = 1;
    bool lock_pick_first_time = true;

    // set up data buffer to perform desired lock manipulation operation
    // If we are locking set the locked bit, reset_fsm bit, and clear_status bit
    if (i_lock) {
        // Configuring lock manipulation control data buffer to perform lock acquisition
        lock_control.setBit(PU_ALTD_CMD_REG_FBC_LOCKED);
        lock_control.setBit<PU_ALTD_CMD_REG_FBC_RESET_FSM>();
        lock_control.setBit<PU_ALTD_CMD_REG_FBC_CLEAR_STATUS>();
    } else {
        // Configuring lock manipulation control data buffer to perform lock release
    }

    // try to lock/unlock the lock the number of times specified with i_num_attempts
    while (1)
    {
        // write ADU command register to attempt lock manipulation
        rc = fapi2::putScom(i_target, PU_ALTD_CMD_REG, lock_control);

        // pass back return code to caller unless it specifically indicates
        // that the ADU lock manipulation was unsuccessful and we're going
        // to try again
        if (rc != fapi2::FAPI2_RC_PLAT_ERR_ADU_LOCKED || attempt_count == i_num_attempts) {
            // rc does not indicate success
            if (rc) {
                // rc does not indicate lock held, exit
                if (rc != fapi2::FAPI2_RC_PLAT_ERR_ADU_LOCKED) {
                    FAPI_ERR("fapiPutScom error (PU_ALTD_CMD_REG)");
                    break;
                }

                // rc indicates lock held, out of attempts
                if (attempt_count == i_num_attempts)
                {
                    //if out of attempts but lock pick is desired try to pick the
                    //lock once and see if it works
                    if (i_lock_pick && i_lock && lock_pick_first_time)
                    {
                        lock_control.setBit(PU_ALTD_CMD_REG_FBC_LOCK_PICK);
                        attempt_count--;
                        lock_pick_first_time = false;
                        // Trying to do a lock pick as desired
                    }
                    //If we are out of attempts and are not trying to pick the lock or
                    //if we already picked the lock, error out
                    else
                    {
                        die("Ran out of lock attempts or were unable to pick lock");
                        break;
                    }
                }
            }

            // Lock manipulation successful or going to try a lock pick
            break;
        }

        // delay to provide time for ADU lock to be released
        nsdelay(PROC_ADU_UTILS_ADU_STATUS_HW_NS_DELAY);

        // increment attempt count, loop again
        attempt_count++;
    }

    return fapi2::current_err;
}

/// @brief calculates the number of 8 byte granules that can be read/written before setup needs to be run again
/// @param[in] i_target  => P9 chip target
/// @param[in] i_address => starting address for ADU operation
/// @return number of 8 byte granules that can be read/written before setup needs to be run again
fapi2::ReturnCode p9_adu_coherent_utils_get_num_granules(
    const uint64_t i_address,
    uint32_t& o_numGranules)
{
    fapi2::ReturnCode rc;

    //From the address figure out when it is going to no longer be within the ADU bound by
    //doing the max fbc address minus the address and then divide by 8 to get number of bytes
    //and by 8 to get number of 8 byte granules that can be sent
    o_numGranules = ((P9_FBC_UTILS_FBC_MAX_ADDRESS - i_address) / 8) / 8;

    return rc;
}

/// @brief does the setup for the ADU to set up the initial registers for a read/write
/// @param[in] i_target  => P9 chip target
/// @param[in] i_address => starting address for ADU operation
/// @param[in] i_rnw     => whether the operation is a read or write
/// @param[in] i_flags   => flags that contain information that the ADU needs to know to set up registers
/// @return FAPI_RC_SUCCESS if setting up the adu registers is a success
fapi2::ReturnCode p9_adu_coherent_setup_adu(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const uint64_t i_address,
    const bool i_rnw,
    const uint32_t i_flags)
{
    fapi2::ReturnCode rc;
    fapi2::buffer<uint64_t> altd_cmd_reg_data(0x0);
    fapi2::buffer<uint64_t> altd_addr_reg_data(i_address);
    fapi2::buffer<uint64_t> altd_option_reg_data(0x0);
    p9_ADU_oper_flag l_myAduFlag;
    p9_ADU_oper_flag::OperationType_t l_operType;
    p9_ADU_oper_flag::Transaction_size_t l_transSize;
    uint32_t var_PU_ALTD_CMD_REG_FBC_TTYPE = 0;
    uint32_t var_PU_ALTD_CMD_REG_FBC_TSIZE = 0;

    //Write the address into altd_addr_reg
    fapi2::putScom(i_target, PU_ALTD_ADDR_REG, altd_addr_reg_data);

    // Process input flag
    l_myAduFlag.getFlag(i_flags);
    l_operType = l_myAduFlag.getOperationType();
    l_transSize = l_myAduFlag.getTransactionSize();

    //Now work on getting the altd cmd register set up - go through all the bits and set/clear as needed
    //this routine assumes the lock is held by the caller, preserve this locked state
    altd_cmd_reg_data.setBit<PU_ALTD_CMD_REG_FBC_LOCKED>();

    // ---------------------------------------------
    // Setting for DMA and CI operations
    // ---------------------------------------------
    if ( (l_operType == p9_ADU_oper_flag::CACHE_INHIBIT) ||
         (l_operType == p9_ADU_oper_flag::DMA_PARTIAL) )
    {

        // ---------------------------------------------
        // DMA & CI common settings
        // ---------------------------------------------
        // Set fbc_altd_rnw if it's a read
        if (i_rnw)
        {
            altd_cmd_reg_data.setBit<PU_ALTD_CMD_REG_FBC_RNW>();
        }
        // Clear fbc_altd_rnw if it's a write
        else
        {
            altd_cmd_reg_data.clearBit<PU_ALTD_CMD_REG_FBC_RNW>();
        }

        // If auto-inc set the auto-inc bit
        if (l_myAduFlag.getAutoIncrement() == true)
        {
            altd_cmd_reg_data.setBit<PU_ALTD_CMD_REG_FBC_AUTO_INC>();
        }

        // ---------------------------------------------------
        // Cache Inhibit specific: TTYPE & TSIZE
        // ---------------------------------------------------
        if (l_operType == p9_ADU_oper_flag::CACHE_INHIBIT)
        {
            // Set TTYPE
            if (i_rnw)
            {
                var_PU_ALTD_CMD_REG_FBC_TTYPE = ALTD_CMD_TTYPE_CI_PR_RD;
            }
            else
            {
                var_PU_ALTD_CMD_REG_FBC_TTYPE = ALTD_CMD_TTYPE_CI_PR_WR;
            }

            // Set TSIZE
            if ( l_transSize == p9_ADU_oper_flag::TSIZE_1 )
            {
                var_PU_ALTD_CMD_REG_FBC_TSIZE = ALTD_CMD_CI_TSIZE_1;
            }
            else if ( l_transSize == p9_ADU_oper_flag::TSIZE_2 )
            {
                var_PU_ALTD_CMD_REG_FBC_TSIZE = ALTD_CMD_CI_TSIZE_2;
            }
            else if ( l_transSize == p9_ADU_oper_flag::TSIZE_4 )
            {
                var_PU_ALTD_CMD_REG_FBC_TSIZE = ALTD_CMD_CI_TSIZE_4;
            }
            else
            {
                var_PU_ALTD_CMD_REG_FBC_TSIZE = ALTD_CMD_CI_TSIZE_8;
            }
        }

        // ---------------------------------------------------
        // DMA specific: TTYPE & TSIZE
        // ---------------------------------------------------
        else
        {
            // If a read, set ALTD_CMD_TTYPE_CL_DMA_RD
            // Set the tsize to ALTD_CMD_DMAR_TSIZE
            if (i_rnw)
            {
                var_PU_ALTD_CMD_REG_FBC_TTYPE = ALTD_CMD_TTYPE_CL_DMA_RD;
                var_PU_ALTD_CMD_REG_FBC_TSIZE = ALTD_CMD_DMAR_TSIZE;
            }
            // If a write set ALTD_CMD_TTYPE_DMA_PR_WR
            // Set the tsize according to flag setting
            else
            {
                var_PU_ALTD_CMD_REG_FBC_TTYPE = ALTD_CMD_TTYPE_DMA_PR_WR;

                //Set scope to system scope
                altd_cmd_reg_data.insertFromRight<PU_ALTD_CMD_REG_FBC_SCOPE, PU_ALTD_CMD_REG_FBC_SCOPE_LEN>(ALTD_CMD_SCOPE_SYSTEM);

                // Set TSIZE
                if ( l_transSize == p9_ADU_oper_flag::TSIZE_1 )
                {
                    var_PU_ALTD_CMD_REG_FBC_TSIZE = ALTD_CMD_DMAW_TSIZE_1;
                }
                else if ( l_transSize == p9_ADU_oper_flag::TSIZE_2 )
                {
                    var_PU_ALTD_CMD_REG_FBC_TSIZE = ALTD_CMD_DMAW_TSIZE_2;
                }
                else if ( l_transSize == p9_ADU_oper_flag::TSIZE_4 )
                {
                    var_PU_ALTD_CMD_REG_FBC_TSIZE = ALTD_CMD_DMAW_TSIZE_4;
                }
                else
                {
                    var_PU_ALTD_CMD_REG_FBC_TSIZE = ALTD_CMD_DMAW_TSIZE_8;
                }
            }
        }
    }

    // ---------------------------------------------
    // Setting for PB and PMISC operations
    // ---------------------------------------------
    if (l_operType == p9_ADU_oper_flag::PB_DIS_OPER ||
        l_operType == p9_ADU_oper_flag::PB_INIT_OPER ||
        l_operType == p9_ADU_oper_flag::PMISC_OPER)
    {

        // ---------------------------------------------
        // PB & PMISC common settings
        // ---------------------------------------------

        // Set the start op bit
        altd_cmd_reg_data.setBit<PU_ALTD_CMD_REG_FBC_START_OP>();

        // Set operation scope
        altd_cmd_reg_data.insertFromRight<PU_ALTD_CMD_REG_FBC_SCOPE,
                                          PU_ALTD_CMD_REG_FBC_SCOPE_LEN>(ALTD_CMD_SCOPE_SYSTEM);

        // Set DROP_PRIORITY = HIGH
        altd_cmd_reg_data.setBit<PU_ALTD_CMD_REG_FBC_DROP_PRIORITY>();

        // Set AXTYPE = Address only
        altd_cmd_reg_data.setBit<PU_ALTD_CMD_REG_FBC_AXTYPE>();

        // ---------------------------------------------------
        // PB specific: TTYPE & TSIZE
        // ---------------------------------------------------
        if (l_operType == p9_ADU_oper_flag::PB_DIS_OPER ||
            l_operType == p9_ADU_oper_flag::PB_INIT_OPER)
        {
            // Set TTYPE
            var_PU_ALTD_CMD_REG_FBC_TTYPE = ALTD_CMD_TTYPE_PB_OPER;
            // Set TM_QUIESCE
            altd_cmd_reg_data.setBit<PU_ALTD_CMD_REG_FBC_WITH_TM_QUIESCE>();

            if (l_operType == p9_ADU_oper_flag::PB_DIS_OPER)
            {
                // TSIZE for PB operation is fixed value: 0b00001000
                var_PU_ALTD_CMD_REG_FBC_TSIZE = ALTD_CMD_PB_DIS_OPERATION_TSIZE;
            }
            else
            {
                // Set OVERWRITE_PBINIT
                altd_cmd_reg_data.setBit<PU_ALTD_CMD_REG_FBC_OVERWRITE_PBINIT>();

                // Set up quiesce
                altd_option_reg_data.setBit<PU_ALTD_OPTION_REG_FBC_WITH_PRE_QUIESCE>();
                altd_option_reg_data.insertFromRight<PU_ALTD_OPTION_REG_FBC_AFTER_QUIESCE_WAIT_COUNT,
                                                     PU_ALTD_OPTION_REG_FBC_AFTER_QUIESCE_WAIT_COUNT_LEN>
                                                     (QUIESCE_SWITCH_WAIT_COUNT);
                fapi2::putScom(i_target, PU_ALTD_OPTION_REG, altd_option_reg_data);

                // TSIZE for PB operation is fixed value: 0b00001011
                var_PU_ALTD_CMD_REG_FBC_TSIZE = ALTD_CMD_PB_INIT_OPERATION_TSIZE;
            }
        }

        // ---------------------------------------------------
        // PMISC specific: TTYPE & TSIZE
        // ---------------------------------------------------
        else
        {
            // Set TTYPE
            var_PU_ALTD_CMD_REG_FBC_TTYPE = ALTD_CMD_TTYPE_PMISC_OPER;

            // Set TSIZE
            if ( l_transSize == p9_ADU_oper_flag::TSIZE_1 )
            {
                // Set TM_QUIESCE
                altd_cmd_reg_data.setBit<PU_ALTD_CMD_REG_FBC_WITH_TM_QUIESCE>();

                var_PU_ALTD_CMD_REG_FBC_TSIZE = ALTD_CMD_PMISC_TSIZE_1;
                // Set quiesce and init around a switch operation in option reg
                setQuiesceInit(i_target);
                /// return on error ///
            }
            else if ( l_transSize == p9_ADU_oper_flag::TSIZE_2 )
            {
                var_PU_ALTD_CMD_REG_FBC_TSIZE = ALTD_CMD_PMISC_TSIZE_2;
            }
        }
    }

    altd_cmd_reg_data.insertFromRight<PU_ALTD_CMD_REG_FBC_TTYPE, PU_ALTD_CMD_REG_FBC_TTYPE_LEN>
    (var_PU_ALTD_CMD_REG_FBC_TTYPE);
    altd_cmd_reg_data.insertFromRight<PU_ALTD_CMD_REG_FBC_TSIZE,
                                      PU_ALTD_CMD_REG_FBC_TSIZE_LEN>(var_PU_ALTD_CMD_REG_FBC_TSIZE);

    //Write altd cmd register with the settings that were set above
    fapi2::putScom(i_target, PU_ALTD_CMD_REG, altd_cmd_reg_data);

    return fapi2::current_err;
}

/// @brief Setup the value for ADU option register to enable
///        quiesce & init around a switch operation.
///
/// @param [in] i_target   Proc target
///
/// @return FAPI2_RC_SUCCESS if OK
fapi2::ReturnCode setQuiesceInit(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    fapi2::buffer<uint64_t> altd_option_reg_data(0);

    // Set up quiesce
    altd_option_reg_data.setBit<PU_ALTD_OPTION_REG_FBC_WITH_PRE_QUIESCE>();
    altd_option_reg_data.insertFromRight<PU_ALTD_OPTION_REG_FBC_AFTER_QUIESCE_WAIT_COUNT,
                                         PU_ALTD_OPTION_REG_FBC_AFTER_QUIESCE_WAIT_COUNT_LEN>
                                         (QUIESCE_SWITCH_WAIT_COUNT);

    // Setup Post-command init
    altd_option_reg_data.setBit<PU_ALTD_OPTION_REG_FBC_WITH_POST_INIT>();
    altd_option_reg_data.insertFromRight<PU_ALTD_OPTION_REG_FBC_BEFORE_INIT_WAIT_COUNT,
                                         PU_ALTD_OPTION_REG_FBC_BEFORE_INIT_WAIT_COUNT_LEN>
                                         (INIT_SWITCH_WAIT_COUNT);

    //If DD2 setup workaround for HW397129 to re-enable fastpath for DD2
    altd_option_reg_data.setBit<FBC_ALTD_HW397129>();

    // Write to ADU option reg
    return fapi2::putScom(i_target, PU_ALTD_OPTION_REG, altd_option_reg_data);
}

/// @brief do the actual read/write from the ADU
/// @param[in] i_target       => P9 chip target
/// @param[in] i_address      => base real address for read/write operation (expected to be 8B aligned)
/// @param[in] i_rnw          => if the operation is a read not write (1 for read, 0 for write)
/// @param[in] i_flags        => other information that is needed - see the p9_adu_constants adu_flags enums for bit definitions
//                               Note: To construct the flag you can use p9_ADU_oper_flag class
/// @param[in] i_lastGranule  => if this is the last 8B of data that we are collecting (true = last granule, false = not last granule)
/// @param[in] i_firstGranule => if this is the first 8B of data that we are collecting (true = first granule, false = not first granule)
/// @param[in, out] io_data   => The data is read/written
/// @return FAPI_RC_SUCCESS if the read/write completes successfully
fapi2::ReturnCode p9_adu_access(const
                                fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
                                const uint64_t i_address,
                                const bool i_rnw,
                                const uint32_t i_flags,
                                const bool i_firstGranule,
                                const bool i_lastGranule,
                                uint8_t io_data[])
{
    bool l_busyBitStatus = false;
    adu_status_busy_handler l_busyHandling;
    fapi2::ReturnCode l_rc = fapi2::FAPI2_RC_SUCCESS;

    // Process input flag
    p9_ADU_oper_flag l_myAduFlag;
    l_myAduFlag.getFlag(i_flags);

    //If autoinc is set and this is not a DMA operation unset autoinc before passing the flags through since autoinc is only allowed for DMA operations
    if (l_myAduFlag.getOperationType() != p9_ADU_oper_flag::DMA_PARTIAL)
        l_myAduFlag.setAutoIncrement(false);

    // don't generate fabric command
    if (l_myAduFlag.getOperationType() == p9_ADU_oper_flag::PRE_SWITCH_AB ||
        l_myAduFlag.getOperationType() == p9_ADU_oper_flag::PRE_SWITCH_CD ||
        l_myAduFlag.getOperationType() == p9_ADU_oper_flag::POST_SWITCH)
        goto fapi_try_exit;

    //If we were using autoinc and this is the last granule we need to clear autoinc before the last read/write
    if (i_lastGranule && l_myAduFlag.getAutoIncrement())
        p9_adu_coherent_clear_autoinc(i_target);
        /// return on error ///

    if (l_myAduFlag.isAddressOnly()) {
        nsdelay(P9_ADU_ACCESS_ADU_OPER_HW_NS_DELAY);
    } else {
        //If we are doing a read operation read the data
        if (i_rnw)
        {
            p9_adu_coherent_adu_read(i_target, i_firstGranule, i_address, l_myAduFlag, io_data);
            /// return on error ///
        }
        //Otherwise this is a write and write the data
        else
        {
            p9_adu_coherent_adu_write(i_target, i_firstGranule, i_address, l_myAduFlag, io_data);
            /// return on error ///
        }
    }

    //If we are not in fastmode or this is the last granule, we want to check the status
    if (i_lastGranule || !l_myAduFlag.getFastMode())
    {
        //If we are using autoincrement and this is not the last granule we expect the busy bit to still be set
        if ( (l_myAduFlag.getAutoIncrement()) && !i_lastGranule )
        {
            l_busyHandling = EXPECTED_BUSY_BIT_SET;
        }
        //Otherwise we expect the busy bit to be cleared
        else
        {
            l_busyHandling = EXPECTED_BUSY_BIT_CLEAR;
        }

        //We only want to do the status check if this is not a ci operation
        if (l_myAduFlag.getOperationType() != p9_ADU_oper_flag::CACHE_INHIBIT) {
            p9_adu_coherent_status_check(i_target, l_busyHandling,
                                         l_myAduFlag.isAddressOnly(), l_busyBitStatus);
            /// return on error ///
        }

        //If it's the last read/write cleanup the adu
        if (i_lastGranule)
            p9_adu_coherent_cleanup_adu(i_target);
    }

fapi_try_exit:
    return l_rc;
}

/// @brief  Manage ADU operation flag that is used to program the
//          ADU CMD register, PU_ALTD_CMD_REG (Addr: 0x00090001)
class p9_ADU_oper_flag
{
public:

    // Type of ADU operations
    enum OperationType_t
    {
        CACHE_INHIBIT = 0, // cache-inhibited 1, 2, 4, or 8 byte read/write
        DMA_PARTIAL   = 1, // partial cache line direct memory access - always 8 byte read/write
        PB_DIS_OPER   = 2, // pbop.disable_all
        PMISC_OPER    = 3, // pmisc switch
        PB_INIT_OPER  = 4, // pbop.enable_all
        PRE_SWITCH_CD = 5, // do not issue PB command, pre-set for switch CD operation
        PRE_SWITCH_AB = 6, // do not issue PB command, pre-set for switch AB operation
        POST_SWITCH   = 7  // do not issue PB command, clear switch CD/AB flags
    };

    // Transaction size -- only checked if not DMA
    enum Transaction_size_t
    {
        TSIZE_1 = 1,
        TSIZE_2 = 2,
        TSIZE_4 = 4,
        TSIZE_8 = 8
    };

    inline p9_ADU_oper_flag()
        : iv_operType(CACHE_INHIBIT), iv_autoInc(false), iv_lockPick(false),
          iv_numLockAttempts(1), iv_cleanUp(true), iv_fastMode(false),
          iv_itag(false), iv_ecc(false), iv_eccItagOverwrite(false),
          iv_transSize(TSIZE_1)
    { }

    /// Determine if ADU operation type is address only / or will require data transfer
    inline bool isAddressOnly(void)
    {
        return ((iv_operType == PB_DIS_OPER) ||
                (iv_operType == PMISC_OPER) ||
                (iv_operType == PB_INIT_OPER));
    }

    inline void setOperationType(const OperationType_t i_type)
    { iv_operType = i_type; }
    inline const OperationType_t getOperationType()
    { return iv_operType; }
    inline void setTransactionSize(Transaction_size_t i_value)
    { iv_transSize = i_value; }

    /// Set the Auto Increment option, for DMA operations only.
    inline void setAutoIncrement(bool i_value)
    { iv_autoInc = i_value; }

    /// @brief Set fast read/write mode.
    ///        For fast read/write mode, no status check.  Otherwise,
    ///        do status check after every read/write.
    ///
    /// @param[in] i_value     True: Enable fast read/write mode.
    ///                        False: Disable fast read/write mode.
    inline void setFastMode(bool i_value)
    { iv_fastMode = i_value; }

    /// @brief Assemble the 32-bit ADU flag based on current
    ///        info contained in this class.
    ///        This flag is to be used in ADU interface call
    ///        See flag bit definitions in p9_adu_constants.H
    /// @return uint32_t
    inline uint32_t setFlag()
    {
        uint32_t l_aduFlag = 0;

        // Operation type
        l_aduFlag |= (iv_operType << FLAG_ADU_TTYPE_SHIFT);

        if (iv_autoInc)
            l_aduFlag |= FLAG_AUTOINC;
        if (iv_fastMode)
            l_aduFlag |= FLAG_ADU_FASTMODE;

        // Lock attempts
        l_aduFlag |= (iv_numLockAttempts << FLAG_LOCK_TRIES_SHIFT);

        // Leave dirty
        if (!iv_cleanUp)
            l_aduFlag |= FLAG_LEAVE_DIRTY;

        if (iv_lockPick)
            l_aduFlag |= FLAG_LOCK_PICK;
        if (iv_itag)
            l_aduFlag |= FLAG_ITAG;
        if (iv_ecc)
            l_aduFlag |= FLAG_ECC;

        // Overwrite ECC
        if (iv_eccItagOverwrite)
            l_aduFlag |= FLAG_OVERWRITE_ECC;

        // Transaction size
        if (iv_transSize == TSIZE_1)
            l_aduFlag |= FLAG_SIZE_TSIZE_1;
        else if (iv_transSize == TSIZE_2)
            l_aduFlag |= FLAG_SIZE_TSIZE_2;
        else if (iv_transSize == TSIZE_4)
            l_aduFlag |= FLAG_SIZE_TSIZE_4;
        else if (iv_transSize == TSIZE_8)
            l_aduFlag |= FLAG_SIZE_TSIZE_8;
        else
            die("Invalid transaction size: iv_transSize %d", iv_transSize);

        return l_aduFlag;
    }

    /// Update the class instant variables with info embedded in the passed in flag value.
    inline void getFlag(uint32_t i_flag)
    {
        // Decode Operation type
        iv_operType = static_cast<OperationType_t>
                    ((i_flag & FLAG_ADU_TTYPE) >> FLAG_ADU_TTYPE_SHIFT);

        iv_autoInc = (i_flag & FLAG_AUTOINC);
        iv_lockPick = (i_flag & FLAG_LOCK_PICK);
        iv_numLockAttempts = ( (i_flag & FLAG_LOCK_TRIES) >> FLAG_LOCK_TRIES_SHIFT);
        iv_cleanUp = ~(i_flag & FLAG_LEAVE_DIRTY);
        iv_fastMode = (i_flag & FLAG_ADU_FASTMODE);
        iv_itag = (i_flag & FLAG_ITAG);
        iv_ecc = (i_flag & FLAG_ECC);
        iv_eccItagOverwrite = (i_flag & FLAG_OVERWRITE_ECC);

        // Transaction size
        if ((i_flag & FLAG_SIZE) == FLAG_SIZE_TSIZE_1)
            iv_transSize = TSIZE_1;
        else if ((i_flag & FLAG_SIZE) == FLAG_SIZE_TSIZE_2)
            iv_transSize = TSIZE_2;
        else if ((i_flag & FLAG_SIZE) == FLAG_SIZE_TSIZE_4)
            iv_transSize = TSIZE_4;
        else if ((i_flag & FLAG_SIZE) == FLAG_SIZE_TSIZE_8)
            iv_transSize = TSIZE_8;
        else
            die("Invalid transaction size: iv_transSize %d", iv_transSize);
    }

    // ...
};

/// @brief does the write for the ADU
/// @param[in] i_target       => P9 chip target
/// @param[in] i_firstGranule => the first 8B granule that we are writing
/// @param[in] i_address      => address for this write
/// @param[in] i_aduOper      => Contains information that the ADU needs to know to set up registers
/// @param[in] i_write_data   => the data that is to be written to the ADU
/// @return FAPI_RC_SUCCESS if writing the ADU is a success
fapi2::ReturnCode p9_adu_coherent_adu_write(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const bool i_firstGranule,
    const uint64_t i_address,
    p9_ADU_oper_flag& i_aduOper,
    const uint8_t i_write_data[])
{
    fapi2::buffer<uint64_t> altd_cmd_reg_data;
    fapi2::buffer<uint64_t> altd_status_reg_data;
    fapi2::buffer<uint64_t> force_ecc_reg_data;
    uint64_t write_data = 0x0ull;
    int eccIndex = 8;

    // Get ADU operation info from flag
    bool l_itagMode          = i_aduOper.getItagMode();
    bool l_eccMode           = i_aduOper.getEccMode();
    bool l_overrideEccMode   = i_aduOper.getEccItagOverrideMode();
    bool l_autoIncMode       = i_aduOper.getAutoIncrement();
    bool l_accessForceEccReg = (l_itagMode | l_eccMode | l_overrideEccMode);

    //Get the write data that was passed in as a uint8 into a uint64
    for (int i = 0; i < 8; i++)
        write_data |= ( static_cast<uint64_t>(i_write_data[i]) << (56 - (8 * i)) );

    //Put the uint64 write data into the buffer
    fapi2::buffer<uint64_t> altd_data_reg_data(write_data);

    //If we are doing something with ecc/itag data
    if (l_accessForceEccReg == true)
    {
        fapi2::getScom(i_target, PU_FORCE_ECC_REG, force_ecc_reg_data);

        //if we want to write the itag bit set that bit
        if (l_itagMode == true) {
            eccIndex++;
            force_ecc_reg_data.setBit<PU_FORCE_ECC_REG_ALTD_DATA_ITAG>();
        }

        //if we want to write the ecc data get the data
        if (l_eccMode == true)
            force_ecc_reg_data.insertFromRight < PU_FORCE_ECC_REG_ALTD_DATA_TX,
                                               PU_FORCE_ECC_REG_ALTD_DATA_TX_LEN >
                                               ((uint64_t)i_write_data[eccIndex]);

        //if we want to overwrite the ecc data set that bit
        if (l_overrideEccMode == true)
            force_ecc_reg_data.setBit<PU_FORCE_ECC_REG_ALTD_DATA_TX_OVERWRITE>();

        fapi2::putScom(i_target, PU_FORCE_ECC_REG, force_ecc_reg_data);
    }

    fapi2::putScom(i_target, PU_ALTD_DATA_REG, altd_data_reg_data);

    //Set the ALTD_CMD_START_OP bit to start the write(first granule for autoinc case or not autoinc)
    if (i_firstGranule || !l_autoIncMode) {
        fapi2::getScom(i_target, PU_ALTD_CMD_REG, altd_cmd_reg_data);
        altd_cmd_reg_data.setBit<PU_ALTD_CMD_REG_FBC_START_OP>();
        fapi2::putScom(i_target, PU_ALTD_CMD_REG, altd_cmd_reg_data);
    }

    // If this is a ci operation we want to poll the status register for completion
    if (i_aduOper.getOperationType() == p9_ADU_oper_flag::CACHE_INHIBIT) {
        bool l_busyBitStatus = true;

        for (uint32_t i = 0; i < 100000; i++) {
            //Check the busy bit if it's busy exit otherwise check the status
            p9_adu_coherent_status_check(i_target, EXIT_ON_BUSY, false, l_busyBitStatus);
            /// return on error ///

            //If the data done bit is set (the data transfer is done we are done
            if (!l_busyBitStatus)
                break;
        }
    }
    //If it's not a ci operation we just want to delay for a while and then this write is done
    else
    {
        //delay to allow time for the write to progress
        nsdelay(PROC_ADU_UTILS_ADU_OPER_HW_NS_DELAY);
    }

    return fapi2::current_err;
}

/// @brief this does any cleanup for the ADU after all reads/writes have been done
/// @param[in] i_target => P9 chip target
/// @return FAPI_RC_SUCCESS if cleaning up the ADU is a success
fapi2::ReturnCode p9_adu_coherent_cleanup_adu(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target)
{
    //write all 0s to altd_cmd_reg to cleanup everything
    fapi2::buffer<uint64_t> altd_cmd_reg_data(0x0);
    return fapi2::putScom(i_target, PU_ALTD_CMD_REG, altd_cmd_reg_data);
}

/// @brief This function checks the status of the adu.
///        If ADU is busy, it will handle
///
/// @param[in] i_target            P9 chip target
/// @param[in] i_busyBitHandler    Instruction on how to handle the ADU busy
/// @param[in] i_addressOnlyOper   Indicate the check is called after an Address
///                                only operation
/// @param[out] o_busyStatus       ADU status busy bit.
///
/// @return FAPI_RC_SUCCESS if the status check is a success
fapi2::ReturnCode p9_adu_coherent_status_check(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const adu_status_busy_handler i_busyBitHandler,
    const bool i_addressOnlyOper,
    bool& o_busyBitStatus)
{
    fapi2::buffer<uint64_t> l_statusReg(0x0);
    bool l_statusError = false;

    //Check for a successful status 10 times
    for (int i = 0; i < 10; i++) {
        //Delay to allow the write/read/other command to finish
        nsdelay(PROC_ADU_UTILS_ADU_STATUS_HW_NS_DELAY);

        l_statusError = false;

        // Read ALTD_STATUS_REG
        fapi2::getScom(i_target, PU_ALTD_STATUS_REG, l_statusReg);

        // ---- Handle busy options ----

        // Get busy bit output
        o_busyBitStatus = l_statusReg.getBit<PU_ALTD_STATUS_REG_FBC_ALTD_BUSY>();

        // Handle busy bit according to specified input
        if (o_busyBitStatus == true)
        {
            // Exit if busy
            if (i_busyBitHandler == EXIT_ON_BUSY)
                goto fapi_try_exit;
            // if the busy bit was set and it was expected to be clear there is a status error
            else if (i_busyBitHandler == EXPECTED_BUSY_BIT_CLEAR)
                l_statusError = true;
        }
        //If the busy bit was not set and it was expected to be set there is a status error
        else if (i_busyBitHandler == EXPECTED_BUSY_BIT_SET)
        {
            l_statusError = true;
        }

        // ---- Check for other errors ----
        // Check the WAIT_CMD_ARBIT bit and make sure it's 0
        // Check the ADDR_DONE bit and make sure it's set
        // Check the WAIT_RESP bit to make sure it's clear
        // Check the OVERRUN_ERR to make sure it's clear
        // Check the AUTOINC_ERR to make sure it's clear
        // Check the COMMAND_ERR to make sure it's clear
        // Check the ADDRESS_ERR to make sure it's clear
        // Check the COMMAND_HANG_ERR to make sure it's clear
        // Check the DATA_HANG_ERR to make sure it's clear
        // Check the PBINIT_MISSING to make sure it's clear
        // Check the ECC_CE to make sure it's clear
        // Check the ECC_UE to make sure it's clear
        // Check the ECC_SUE to make sure it's clear
        l_statusError =
            ( l_statusError ||
              l_statusReg.getBit<PU_ALTD_STATUS_REG_FBC_WAIT_CMD_ARBIT>()    ||
              !l_statusReg.getBit<PU_ALTD_STATUS_REG_FBC_ADDR_DONE>()        ||   //The address potion of the operation is complete
              l_statusReg.getBit<PU_ALTD_STATUS_REG_FBC_WAIT_RESP>()         ||   //Waiting for a clean combined response (CResp)
              l_statusReg.getBit<PU_ALTD_STATUS_REG_FBC_OVERRUN_ERROR>()
              ||   //New data was written before the previous data was used/read or a read was performed without new data arriving
              l_statusReg.getBit<PU_ALTD_STATUS_REG_FBC_AUTOINC_ERROR>()
              || //AutoInc Error indicates internal address counter rolled over the 0.5M boundary
              l_statusReg.getBit<PU_ALTD_STATUS_REG_FBC_COMMAND_ERROR>()
              || //New command was issued before previous one finished
              l_statusReg.getBit<PU_ALTD_STATUS_REG_FBC_ADDRESS_ERROR>()
              || //Invalid Address error : PB responded with Address Error CResp
              l_statusReg.getBit<PU_ALTD_STATUS_REG_FBC_PBINIT_MISSING>()    ||   //attempt to start a command without pb_init active
              l_statusReg.getBit<PU_ALTD_STATUS_REG_FBC_ECC_CE>()            ||   //ECC Correctable error
              l_statusReg.getBit<PU_ALTD_STATUS_REG_FBC_ECC_UE>()            ||   //ECC Uncorrectable error
              l_statusReg.getBit<PU_ALTD_STATUS_REG_FBC_ECC_SUE>()                //ECC Special Uncorrectable error
            );

        // If Address only operation, do not check for PU_ALTD_STATUS_REG_FBC_DATA_DONE otherwise it should be set
        if ( i_addressOnlyOper == false )
            l_statusError |= !l_statusReg.getBit<PU_ALTD_STATUS_REG_FBC_DATA_DONE>();

        //If there is not a status error, we can break out of checking status 10 times
        if (!l_statusError)
            break;
    }

    assert(!l_statusError);

    return fapi2::current_err;
}

// Maybe do in the future:
p9_adu_coherent_clear_autoinc(i_target);
p9_adu_coherent_utils_reset_adu(i_target);
p9_adu_coherent_adu_read(i_target, i_firstGranule, i_address, l_myAduFlag, io_data);
```
