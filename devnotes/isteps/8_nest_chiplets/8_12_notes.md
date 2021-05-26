iv_frequency_step_khz = 16666;

iv_bias[POWERSAVE].frequency_hp = iv_attrs.attr_freq_bias_powersave;
iv_bias[POWERSAVE].vdd_ext_hp = iv_attrs.attr_voltage_ext_vdd_bias_powersave;
iv_bias[NOMINAL].frequency_hp = iv_attrs.attr_freq_bias_nominal;
iv_bias[NOMINAL].vdd_ext_hp = iv_attrs.attr_voltage_ext_vdd_bias_nominal;
iv_bias[TURBO].frequency_hp = iv_attrs.attr_freq_bias_turbo;
iv_bias[TURBO].vdd_ext_hp = iv_attrs.attr_voltage_ext_vdd_bias_turbo;
iv_bias[ULTRA].frequency_hp = iv_attrs.attr_freq_bias_ultraturbo;
iv_bias[ULTRA].vdd_ext_hp = iv_attrs.attr_voltage_ext_vdd_bias_ultraturbo;


// iv_attr_mvpd_poundV_raw[p].frequency_mhz is derived using a pointer
// look at src/import/chips/p9/procedures/hwp/pm/p9_pstate_parameter_block.C:3243

iv_operating_points[VPD_PT_SET_BIASED_SYSP][p].pstate =
  (bias_adjust_mhz(iv_attr_mvpd_poundV_raw[p].frequency_mhz, iv_bias[p].frequency_hp)
 - bias_adjust_mhz(iv_attr_mvpd_poundV_raw[p].frequency_mhz, iv_bias[p].frequency_hp))
  * 1000 / 16666;
iv_operating_points[VPD_PT_SET_BIASED_SYSP][p].vdd_mv = sysparm_uplift(
  bias_adjust_mv(l_vdd_mv, iv_bias[p].vdd_ext_hp),
  iv_biased_vpd_pts[p].idd_100ma * 100,
  254, 0, 0);

compute_slope_4_12(
  iv_operating_points[VPD_PT_SET_BIASED_SYSP][region_end].vdd_mv,
  iv_operating_points[VPD_PT_SET_BIASED_SYSP][region_start].vdd_mv,
  iv_operating_points[VPD_PT_SET_BIASED_SYSP][region_start].pstate,
  iv_operating_points[VPD_PT_SET_BIASED_SYSP][region_end].pstate)
