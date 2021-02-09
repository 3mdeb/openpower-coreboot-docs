# 8.7 host_p9_eff_config_links: Powerbuslinkconfig

```py
void fapiHWPCallWrapper(HWP_CALL_TYPE    P9_FBC_EFF_CONFIG_LINKS_T_F,
                        IStepError      &o_stepError,
                        compId_t         HWPF_COMP_ID,
                        TARGETING::TYPE  TYPE_PROC)
{
    # Get a list of all the processors in the system
    l_targetList = getAllChips(TYPE_PROC)
    # Loop through all processors including master
    for l_target in l_targetList:
        # Get a FAPI2 target of type PROC
        const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>l_fapi2Target(l_target)

        ####################p9_fbc_eff_config_links()####################

        # logical link (X/A) configuration parameters, init arrays to default values
        # enable on local end

        # link/fabric ID on remote end
        # indexed by link ID on local end
        uint8_t l_x_rem_link_id[7] = { 0 }
        uint8_t l_x_rem_fbc_chip_id[7] = { 0 }
        uint8_t l_a_rem_link_id[4] = { 0 }
        uint8_t l_a_rem_fbc_group_id[4] = { 0 }
        # FAPI_SYSTEM = 0x0000000000000001
        fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM
        # process XBUS (electrical) endp targets
        auto l_electrical_targets = l_fapi2Target.getChildren<fapi2::TARGET_TYPE_XBUS>()

        for l_iter in range(0, len(l_electrical_targets)):
            ###################p9_fbc_eff_config_links_query_endp()###################
            # A/X link ID for local end
            uint8_t l_loc_link_id = 0
            # remote end target
            fapi2::Target<fapi2::TARGET_TYPE_XBUS> l_rem_target
            # determine link ID/enable state for local end
            p9_fbc_eff_config_links_map_endp(
                l_iter,
                P9_FBC_XBUS_LINK_CTL_ARR,
                # P9_FBC_UTILS_MAX_X_LINKS = 7
                7,
                l_loc_link_id)
            p9_fbc_eff_config_links_query_link_en(l_iter, l_x_en[l_loc_link_id])
            # local end link target is enabled, query remote end
            if l_x_en[l_loc_link_id]:
                # obtain endpoint target associated with remote end of link
                l_rem_target = getOtherEnd(l_iter)
                if l_rc:
                    # endpoint target for remote end of link is not configured
                    l_x_en[l_loc_link_id] = 0
                else:
                    # endpoint target is configured, qualify local link enable with remote endpoint state
                    p9_fbc_eff_config_links_query_link_en(l_rem_target, l_x_en[l_loc_link_id])

            # link is enabled, gather remaining remote end parameters
            if l_x_en[l_loc_link_id]:
                p9_fbc_eff_config_links_map_endp<fapi2::TARGET_TYPE_XBUS>(
                    l_rem_target,
                    P9_FBC_XBUS_LINK_CTL_ARR,
                    # P9_FBC_UTILS_MAX_X_LINKS = 7
                    7,
                    l_x_rem_link_id[l_loc_link_id])
                if fapi2::ATTR_PROC_FABRIC_PUMP_MODE[FAPI_SYSTEM] == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_NODE:
                    # return either chip or group ID of remote chip
                    o_rem_fbc_id[l_loc_link_id] = fapi2::ATTR_PROC_FABRIC_CHIP_ID[l_rem_target]
                else:
                    # p9_fbc_utils_get_group_id_attr(l_rem_target, l_rem_fbc_group_id)
                    o_rem_fbc_id[l_loc_link_id] = fapi2::ATTR_PROC_FABRIC_GROUP_ID[l_rem_target]
            ###########p9_fbc_eff_config_links_set_link_active_attr()###########
            if l_x_en[l_loc_link_id]:
                fapi2::ATTR_PROC_FABRIC_LINK_ACTIVE[l_iter] = fapi2::ENUM_ATTR_PROC_FABRIC_LINK_ACTIVE_TRUE
            else:
                fapi2::ATTR_PROC_FABRIC_LINK_ACTIVE[l_iter] = fapi2::ENUM_ATTR_PROC_FABRIC_LINK_ACTIVE_FALSE
            ###########end of p9_fbc_eff_config_links_set_link_active_attr()###########
        # const uint32_t P9_FBC_UTILS_MAX_X_LINKS = 7
        # const uint32_t P9_FBC_UTILS_MAX_A_LINKS = 4
        l_x_num = 0
        l_x_en[7] = { 0 }
        l_a_num = 0
        l_a_en[4] = { 0 }

        for l_link_id in range(0, 7): # 7 = P9_FBC_UTILS_MAX_X_LINKS
            if l_x_en[l_link_id]:
                l_x_num += 1
        for l_link_id in range(0, 4): # 4 = P9_FBC_UTILS_MAX_A_LINKS
            if l_a_en[l_link_id]:
                l_a_num += 1

        fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[l_fapi2Target] = l_x_en
        fapi2::ATTR_PROC_FABRIC_X_LINKS_CNFG[l_fapi2Target]         = l_x_num
        fapi2::ATTR_PROC_FABRIC_X_ATTACHED_LINK_ID[l_fapi2Target]   = l_x_rem_link_id
        fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_ID[l_fapi2Target]   = l_x_rem_fbc_chip_id
        fapi2::ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG[l_fapi2Target] = l_a_en
        fapi2::ATTR_PROC_FABRIC_A_LINKS_CNFG[l_fapi2Target]         = l_a_num
        fapi2::ATTR_PROC_FABRIC_A_ATTACHED_LINK_ID[l_fapi2Target]   = l_a_rem_link_id
        fapi2::ATTR_PROC_FABRIC_A_ATTACHED_CHIP_ID[l_fapi2Target]   = l_a_rem_fbc_group_id

        # aggregate (local+remote) delays
        # const uint32_t P9_FBC_UTILS_MAX_X_LINKS = 7
        # const uint32_t P9_FBC_UTILS_MAX_A_LINKS = 4
        uint32_t l_x_agg_link_delay[7]
        uint32_t l_a_agg_link_delay[4]
        std::fill_n(l_x_agg_link_delay, 7, 0xFFFFFFFF)
        std::fill_n(l_a_agg_link_delay, 4, 0xFFFFFFFF)

        # aggregate model/address disable on local end
        uint8_t l_x_addr_dis[7] = { 0 }
        uint8_t l_a_addr_dis[4] = { 0 }

        fapi2::ATTR_PROC_FABRIC_X_LINK_DELAY[l_fapi2Target] = l_x_agg_link_delay
        fapi2::ATTR_PROC_FABRIC_X_ADDR_DIS[l_fapi2Target]   = l_x_addr_dis
        fapi2::ATTR_PROC_FABRIC_X_AGGREGATE[l_fapi2Target]  = 0
        fapi2::ATTR_PROC_FABRIC_A_LINK_DELAY[l_fapi2Target] = l_a_agg_link_delay
        fapi2::ATTR_PROC_FABRIC_A_ADDR_DIS[l_fapi2Target]   = l_a_addr_dis
        fapi2::ATTR_PROC_FABRIC_A_AGGREGATE[l_fapi2Target]  = 0
        ####################end p9_fbc_eff_config_links()####################
}

#
# @brief Map endpoint target to X/A link ID
#
# @tparam T template parameter, passed in target.
# @param[in]  i_loc_target          Endpoint target (of type T) of local end of link
# @param[in]  i_link_ctl_arr        Array of X/A link control structures
# @param[in]  i_link_ctl_arr_size   Number of entries in i_link_ctl_arr
# @param[in]  o_link_id             X/A logical link ID
#
# @return fapi2::ReturnCode. FAPI2_RC_SUCCESS if success, else error code.
#
fapi2::ReturnCode p9_fbc_eff_config_links_map_endp(
    const fapi2::Target<T>& i_target,
    const p9_fbc_link_ctl_t i_link_ctl_arr[],
    const uint8_t i_link_ctl_arr_size,
    uint8_t& o_link_id)
{
    for l_link_id in range(0, i_link_ctl_arr_size):
        if  (static_cast<fapi2::TargetType>(i_link_ctl_arr[l_link_id].endp_type) == T)
        and (i_link_ctl_arr[l_link_id].endp_unit_id == fapi2::ATTR_CHIP_UNIT_POS[i_target]):
            o_link_id = l_link_id
            break
}

fapi2::ReturnCode p9_fbc_eff_config_links_query_link_en(
    const fapi2::Target<fapi2::TARGET_TYPE_XBUS>& i_target,
    uint8_t& o_link_is_enabled)
{
    l_link_train = fapi2::ATTR_LINK_TRAIN[i_target]
    if l_link_train == fapi2::ENUM_ATTR_LINK_TRAIN_BOTH:
        o_link_is_enabled = fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE
    elif l_link_train == fapi2::ENUM_ATTR_LINK_TRAIN_EVEN_ONLY:
        o_link_is_enabled = fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_EVEN_ONLY
    elif l_link_train == fapi2::ENUM_ATTR_LINK_TRAIN_ODD_ONLY:
        o_link_is_enabled = fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_ODD_ONLY
    else:
        o_link_is_enabled = fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_FALSE
}
```
