# Step 8 Hostboot - Nest Chiplets

* [8.1 host_slave_sbe_config](isteps/8_nest_chiplets/1_host_slave_sbe_config.md)
* [8.2 host_setup_sbe](isteps/8_nest_chiplets/2_host_setup_sbe.md)
* [8.3 host_cbs_start](isteps/8_nest_chiplets/3_host_cbs_start.md)
* [8.4 proc_check_slave_sbe_seeprom_complete: Check Slave SBE Complete](isteps/8_nest_chiplets/4_proc_check_slave_sbe_seeprom_compl.md)
* [8.5 host_attnlisten_proc: Start attention poll for P9(s)](isteps/8_nest_chiplets/5_fabric_post_trainadv.md)
* [8.6 host_p9_fbc_eff_config: Determine Powerbus config](isteps/8_nest_chiplets/6_host_p9_fbc_eff_config.md)
* [8.7 host_p9_eff_config_links: Powerbuslinkconfig](isteps/8_nest_chiplets/7_host_p9_eff_config_links.md)
* [8.8 proc_attr_update: Proc ATTR Update](isteps/8_nest_chiplets/8_proc_attr_update.md)
* [8.9 proc_chiplet_scominit: Scom inits to all chiplets (sans Quad)](isteps/8_nest_chiplets/9_proc_chiplet_scominit.md)
* [8.10 proc_xbus_scominit: Apply scom inits to Xbus](isteps/8_nest_chiplets/10_proc_xbus_scominit.md)
* [8.11 proc_chiplet_enable_ridi: Enable RI/DI for xbus](isteps/8_nest_chiplets/11_proc_chiplet_enable_ridi.md)
* [8.12 host_set_voltages: Enable RI/DI for xbus](isteps/8_nest_chiplets/12_host_set_voltages.md)

# Step 9 Hostboot - EDI+ and Electrical O-Bus Initialization
* [9.1 fabric_erepair: Restore Fabric Bus eRepair data](isteps/9_EDI_and_obus_init/1_fabric_erepair.md)
* [9.2 fabric_io_dccal: Calibrate Fabric interfaces](isteps/9_EDI_and_obus_init/2_fabric_io_dccal.md)
* [9.3 fabric_pre_trainadv: Advanced pre training](isteps/9_EDI_and_obus_init/3_fabric_pre_trainadv.md)
* [9.4 fabric_io_run_training: Run training on internal buses](isteps/9_EDI_and_obus_init/4_fabric_io_run_training.md)
* [9.5 fabric_post_trainadv: Advanced post EI/EDI training](isteps/9_EDI_and_obus_init/5_fabric_post_trainadv.md)
* [9.6 proc_smp_link_layer: Start SMP link layer](isteps/9_EDI_and_obus_init/6_proc_smp_link_layer.md)
* [9.7 proc_fab_iovalid: Lower functional fences on local SMP](isteps/9_EDI_and_obus_init/7_proc_fab_iovalid.md)
* [9.8 host_fbc_eff_config_aggregate: Pick link(s) for coherency](isteps/9_EDI_and_obus_init/8_host_fbc_eff_config_aggregate.md)

# Step 11 Hostboot - Centaur Init
* [11.1 host_prd_hwreconfig](isteps/11_centaur_init/1_host_prd_hwreconfig.md)

# Step 13 Hostboot - DRAM training
* [13.1 host_disable_memvolt.md: Disable VDDR on Warm Reboots](isteps/13_dram_training/01_host_disable_memvolt.md)
* [13.2 mem_pll_reset.md: Reset PLL for MCAs in async](isteps/13_dram_training/02_mem_pll_reset.md)
* [13.3 mem_pll_initf.md: PLL Initfile for MBAs](isteps/13_dram_training/03_mem_pll_initf.md)
* [13.4 mem_pll_setup.md: Setup PLL for MBAs](isteps/13_dram_training/04_mem_pll_setup.md)
* [13.5 proc_mcs_skewadjust.md: Update clock mesh deskew](isteps/13_dram_training/05_proc_mcs_skewadjust.md)
* [13.6 mem_startclocks.md: Start clocks on MBA/MCAs](isteps/13_dram_training/06_mem_startclocks.md)
* [13.7 host_enable_memvolt.md: Enable the VDDR3 Voltage Rail](isteps/13_dram_training/07_host_enable_memvolt.md)
* [13.8 mss_scominit.md: Perform scom inits to MC and PHY](isteps/13_dram_training/08_mss_scominit.md)
* [13.9 mss_ddr_phy_reset.md: Soft reset of DDR PHY macros](isteps/13_dram_training/09_mss_ddr_phy_reset.md)
* [13.10 mss_draminit.md: Dram initialize](isteps/13_dram_training/10_mss_draminit.md)
* [13.11 mss_draminit_training.md: Dram training](isteps/13_dram_training/11_mss_draminit_training.md)
* [13.12 mss_draminit_trainadv.md: Advanced dram training](isteps/13_dram_training/12_mss_draminit_trainadv.md)
* [13.13 mss_draminit_mc.md: Hand off control to MC](isteps/13_dram_training/13_mss_draminit_mc.md)
