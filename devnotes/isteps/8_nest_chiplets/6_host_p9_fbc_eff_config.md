# 8.6 host_p9_fbc_eff_config: Determine Powerbus config

```python
#
# src/usr/isteps/istep08/call_host_p9_fbc_eff_config.C:39
#
fapi2::ReturnCode
p9_fbc_eff_config()
{
    fapi2::Target<fapi2::TARGET_TYPE_SYSTEM> FAPI_SYSTEM
    fapi2::ATTR_PROC_FABRIC_CORE_FLOOR_RATIO_Type l_core_floor_ratio
    fapi2::ATTR_PROC_FABRIC_CORE_CEILING_RATIO_Type l_core_ceiling_ratio
    fapi2::ATTR_FREQ_PB_MHZ_Type l_freq_fbc
    fapi2::ATTR_FREQ_CORE_CEILING_MHZ_Type l_freq_core_ceiling

    #############p9_fbc_eff_config_process_freq_attributes()#############

    # get core floor/nominal/ceiling frequency attributes
    l_freq_core_floor = fapi2::ATTR_FREQ_CORE_FLOOR_MHZ[FAPI_SYSTEM]
    l_freq_core_ceiling = fapi2::ATTR_FREQ_CORE_CEILING_MHZ[FAPI_SYSTEM]
    # calculate fabric/core frequency ratio attributes
    l_freq_fbc = fapi2::ATTR_FREQ_PB_MHZ[FAPI_SYSTEM]

    # determine table index based on fabric/core floor frequency ratio
    # breakpoint ratio: core floor 4.0, pb 2.0 (cache floor :: pb = 8/8)
    if (l_freq_core_floor) >= (2 * l_freq_fbc):
        l_core_floor_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_FLOOR_RATIO_RATIO_8_8
    # breakpoint ratio: core floor 3.5, pb 2.0 (cache floor :: pb = 7/8)
    elif (4 * l_freq_core_floor) >= (7 * l_freq_fbc):
        l_core_floor_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_FLOOR_RATIO_RATIO_7_8
    # breakpoint ratio: core floor 3.0, pb 2.0 (cache floor :: pb = 6/8)
    elif (2 * l_freq_core_floor) >= (3 * l_freq_fbc):
        l_core_floor_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_FLOOR_RATIO_RATIO_6_8
    # breakpoint ratio: core floor 2.5, pb 2.0 (cache floor :: pb = 5/8)
    elif (4 * l_freq_core_floor) >= (5 * l_freq_fbc):
        l_core_floor_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_FLOOR_RATIO_RATIO_5_8
    # breakpoint ratio: core floor 2.0, pb 2.0 (cache floor :: pb = 4/8)
    elif l_freq_core_floor >= l_freq_fbc:
        l_core_floor_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_FLOOR_RATIO_RATIO_4_8
    # breakpoint ratio: core floor 1.0, pb 2.0 (cache floor :: pb = 2/8)
    elif (2 * l_freq_core_floor) >= l_freq_fbc:
        l_core_floor_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_FLOOR_RATIO_RATIO_2_8

    # determine table index based on fabric/core ceiling frequency ratio
    # breakpoint ratio: core ceiling 4.0, pb 2.0 (cache ceiling :: pb = 8/8)
    if (l_freq_core_ceiling) >= (2 * l_freq_fbc):
        l_core_ceiling_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_CEILING_RATIO_RATIO_8_8
    # breakpoint ratio: core ceiling 3.5, pb 2.0 (cache ceiling :: pb = 7/8)
    elif (4 * l_freq_core_ceiling) >= (7 * l_freq_fbc):
        l_core_ceiling_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_CEILING_RATIO_RATIO_7_8
    # breakpoint ratio: core ceiling 3.0, pb 2.0 (cache ceiling :: pb = 6/8)
    elif (2 * l_freq_core_ceiling) >= (3 * l_freq_fbc):
        l_core_ceiling_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_CEILING_RATIO_RATIO_6_8
    # breakpoint ratio: core ceiling 2.5, pb 2.0 (cache ceiling :: pb = 5/8)
    elif (4 * l_freq_core_ceiling) >= (5 * l_freq_fbc):
        l_core_ceiling_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_CEILING_RATIO_RATIO_5_8
    # breakpoint ratio: core ceiling 2.0, pb 2.0 (cache ceiling :: pb = 4/8)
    elif l_freq_core_ceiling >= l_freq_fbc:
        l_core_ceiling_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_CEILING_RATIO_RATIO_4_8
    # breakpoint ratio: core ceiling 1.0, pb 2.0 (cache ceiling :: pb = 2/8)
    elif (2 * l_freq_core_ceiling) >= l_freq_fbc:
        l_core_ceiling_ratio = fapi2::ENUM_ATTR_PROC_FABRIC_CORE_CEILING_RATIO_RATIO_2_8

    # write attributes
    fapi2::ATTR_PROC_FABRIC_CORE_FLOOR_RATIO[FAPI_SYSTEM] = l_core_floor_ratio
    fapi2::ATTR_PROC_FABRIC_CORE_CEILING_RATIO[FAPI_SYSTEM] = l_core_ceiling_ratio
    fapi2::ATTR_PROC_FABRIC_ASYNC_SAFE_MODE[FAPI_SYSTEM] = fapi2::ENUM_ATTR_PROC_FABRIC_ASYNC_SAFE_MODE_PERFORMANCE_MODE

    ##################p9_fbc_eff_config_calc_epsilons()##################

    # epsilon output attributes
    # uint32_t l_eps_r[NUM_EPSILON_READ_TIERS]
    uint32_t l_eps_r[3]
    # uint32_t l_eps_w[NUM_EPSILON_WRITE_TIERS]
    uint32_t l_eps_w[2]
    fapi2::ATTR_PROC_EPS_GB_PERCENTAGE_Type l_eps_gb

    # fetch epsilon table type/pump mode attributes
    fapi2::ATTR_PROC_EPS_TABLE_TYPE_Type l_eps_table_type
    fapi2::ATTR_PROC_FABRIC_PUMP_MODE_Type l_pump_mode
    l_eps_table_type = fapi2::ATTR_PROC_EPS_TABLE_TYPE[FAPI_SYSTEM]
    l_pump_mode = fapi2::ATTR_PROC_FABRIC_PUMP_MODE[FAPI_SYSTEM]

    if l_eps_table_type == fapi2::ENUM_ATTR_PROC_EPS_TABLE_TYPE_EPS_TYPE_HE:
        if l_pump_mode == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_NODE:
            l_eps_r[0] = EPSILON_R_T0_HE[l_core_floor_ratio]
            l_eps_r[1] = EPSILON_R_T1_HE[l_core_floor_ratio]
            l_eps_r[2] = EPSILON_R_T2_HE[l_core_floor_ratio]

            l_eps_w[0] = EPSILON_W_T0_HE[l_core_floor_ratio]
            l_eps_w[1] = EPSILON_W_T1_HE[l_core_floor_ratio]
        else:
            l_eps_r[0] = EPSILON_R_T0_F4[l_core_floor_ratio]
            l_eps_r[1] = EPSILON_R_T1_F4[l_core_floor_ratio]
            l_eps_r[2] = EPSILON_R_T2_F4[l_core_floor_ratio]

            l_eps_w[0] = EPSILON_W_T0_F4[l_core_floor_ratio]
            l_eps_w[1] = EPSILON_W_T1_F4[l_core_floor_ratio]

    if l_eps_table_type == fapi2::ENUM_ATTR_PROC_EPS_TABLE_TYPE_EPS_TYPE_HE_F8:
        l_eps_r[0] = EPSILON_R_T0_F8[l_core_floor_ratio]
        if l_pump_mode == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_NODE:
            l_eps_r[1] = EPSILON_R_T1_F8[l_core_floor_ratio]
        else:
            l_eps_r[1] = EPSILON_R_T0_F8[l_core_floor_ratio]
        l_eps_r[2] = EPSILON_R_T2_F8[l_core_floor_ratio]

        l_eps_w[0] = EPSILON_W_T0_F8[l_core_floor_ratio]
        l_eps_w[1] = EPSILON_W_T1_F8[l_core_floor_ratio]

    if l_eps_table_type == fapi2::ENUM_ATTR_PROC_EPS_TABLE_TYPE_EPS_TYPE_LE:
        l_eps_r[0] = EPSILON_R_T0_LE[l_core_floor_ratio]
        if l_pump_mode == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_NODE:
            l_eps_r[1] = EPSILON_R_T1_LE[l_core_floor_ratio]
        else:
            l_eps_r[1] = EPSILON_R_T0_LE[l_core_floor_ratio]
        l_eps_r[2] = EPSILON_R_T2_LE[l_core_floor_ratio]

        l_eps_w[0] = EPSILON_W_T0_LE[l_core_floor_ratio]
        l_eps_w[1] = EPSILON_W_T1_LE[l_core_floor_ratio]

    # set guardband default value to +20%
    fapi2::ATTR_PROC_EPS_GB_PERCENTAGE[FAPI_SYSTEM] = 20

    # get gardband attribute
    # Note: if a user makes an attribute override with CONST, it would
    # override the above default value settings. This mechanism is to
    # allow users to change the default settings for testing.
    l_eps_gb = fapi2::ATTR_PROC_EPS_GB_PERCENTAGE[FAPI_SYSTEM]

    # scale base epsilon values if core is running 2x nest frequency
    if l_core_ceiling_ratio == fapi2::ENUM_ATTR_PROC_FABRIC_CORE_CEILING_RATIO_RATIO_8_8:
        uint8_t l_scale_percentage =
            ((100*l_freq_core_ceiling) / (2*l_freq_fbc)) - 100
        # scale/apply guardband read epsilons
        for ii in range(0, NUM_EPSILON_READ_TIERS):
            p9_fbc_eff_config_guardband_epsilon(l_scale_percentage, l_eps_r[ii])
        # Scale write epsilons
        for ii in range(0, NUM_EPSILON_WRITE_TIERS):
            p9_fbc_eff_config_guardband_epsilon(l_scale_percentage, l_eps_w[ii])
    for ii in range(0, NUM_EPSILON_READ_TIERS):
        p9_fbc_eff_config_guardband_epsilon(l_eps_gb, l_eps_r[ii])
    for ii in range(0, NUM_EPSILON_WRITE_TIERS):
        p9_fbc_eff_config_guardband_epsilon(l_eps_gb, l_eps_w[ii])

    # write attributes
    fapi2::ATTR_PROC_EPS_READ_CYCLES_T0[FAPI_SYSTEM] = l_eps_r[0]
    fapi2::ATTR_PROC_EPS_READ_CYCLES_T1[FAPI_SYSTEM] = l_eps_r[1]
    fapi2::ATTR_PROC_EPS_READ_CYCLES_T2[FAPI_SYSTEM] = l_eps_r[2]
    fapi2::ATTR_PROC_EPS_WRITE_CYCLES_T1[FAPI_SYSTEM] = l_eps_w[0]
    fapi2::ATTR_PROC_EPS_WRITE_CYCLES_T2[FAPI_SYSTEM] = l_eps_w[1]

    #############p9_fbc_eff_config_reset_attrs()#############

    # src/import/chips/p9/procedures/hwp/nest/p9_fbc_eff_config.C:516
    fapi2::ATTR_PROC_FABRIC_LINK_ACTIVE_Type l_link_active = fapi2::ENUM_ATTR_PROC_FABRIC_LINK_ACTIVE_FALSE
    for l_proc_target in FAPI_SYSTEM.getChildren<fapi2::TARGET_TYPE_PROC_CHIP>():
        for l_obus_chiplet_target in l_proc_target.getChildren<fapi2::TARGET_TYPE_OBUS>():
            fapi2::ATTR_PROC_FABRIC_LINK_ACTIVE[l_obus_chiplet_target] = l_link_active
        for l_xbus_chiplet_target in l_proc_target.getChildren<fapi2::TARGET_TYPE_XBUS>():
            fapi2::ATTR_PROC_FABRIC_LINK_ACTIVE[l_xbus_chiplet_target] = l_link_active
}

#/
#/ @brief Utility function to apply positive/negative scaing factor
#/        to base epsilon value
#/
#/ @param[in] i_gb_percentage       Scaling factor (e.g. 20% = 20)
#/ @param[in/out] io_target_value   Target epsilon value, after application of
#/                                  scaling factor
#/                                  NOTE: scaling will be clamped to
#/                                  minimum/maximum value
#/
#/ @return void.
#/
void p9_fbc_eff_config_guardband_epsilon(
    const uint8_t i_gb_percentage,
    uint32_t& io_target_value)
{
    uint32_t l_delta =  ((io_target_value * i_gb_percentage) / 100)
    if (io_target_value * i_gb_percentage) % 100 == 0:
        l_delta += 1

    # Apply guardband
    if i_gb_percentage >= 0:
        # Clamp to maximum value if necessary
        #
        # 0xFFFFFFFF = EPSILON_MAX_VALUE
        io_target_value = MAX(io_target_value+l_delta, 0xFFFFFFFF)
    else:
        # Clamp to minimum value if necessary
        #
        # 1 = EPSILON_MIN_VALUE
        io_target_value = MIN(io_target_value-l_delta, 1)
    return
}
```
