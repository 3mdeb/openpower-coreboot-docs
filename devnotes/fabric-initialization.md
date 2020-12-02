# Step 9 Hostboot –EDI+ and Electrical O-Bus Initialization

## 9.1 fabric_erepair: Restore Fabric Bus eRepair data
### p9_io_restore_erepair.C(O, X bus target pairs)

* Restore/preset bad lanes on electrical O and X buses from VPD
(in drawer)
* Applies powerbus repair data from module vpd (#ER keyword in VRML VWML)
* Runtime detected fails that were written to VPD are restored here
* NOOP for Cronus

```
For each lane_to_restore:
    if is_rx(lane_to_restore):
        # mark lines as disabled
        # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_restore_erepair.C:143
        if(lane_index < IO_GCR_REG_WIDTH):
            EDIP_RX_LANE_BAD_VEC_0_15 |= 0x8000 >> lane_to_restore
        else:
            EDIP_RX_LANE_BAD_VEC_16_23 |= 0x8000 >> lane_to_restore

        # power down digital and analog recieve lines
        # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_pdwn_lanes.C:139
        EDIP_RX_LANE_DIG_PDWN[lane_to_restore] = 1
        EDIP_RX_LANE_ANA_PDWN[lane_to_restore] = 1
    else:
        # power down transmit lines
        # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_pdwn_lanes.C:209
        EDIP_TX_LANE_PDWN[lane_to_restore] = 1
```

## 9.2 fabric_io_dccal: Calibrate Fabric interfaces
### io_dccal.C(O, X bus target pairs passed in)
* Will be called per bus target pair
* Calibration of TX impedance, RX offset for O and X busses
* Needs to be quiet on the bus –drivers are quiesced and driving 0s –O, X
buses
* Must be complete on ALL chips before starting O, X bus training
* Expect to use a calculation (floating point)
* At end of offset calibration there may be a lane that is bad
- FW must record bad lane and write to VPD for future eRepair
(handled when PRD starts)
- Must generate error log, procedure will mark lane bad in HW
(which future procedure take advantage of)

```
For each bus:
    if typeof(bus) == TYPE_XBUS:
        # configureXbusConnectionsRunBusMode()
        # src/usr/isteps/istep09/call_fabric_io_dccal.C:194
        for each connection in bus:
            for group in [0, 1]:
                # tx_zcal_run_bus(target)
                # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_dccal.C:269
                # start Tx Impedance Calibration
                EDIP_TX_ZCAL_REQ[bus] = 1
                sleep(20ms)
                while(!EDIP_TX_ZCAL_DONE || EDIP_TX_ZCAL_ERROR):
                    sleep(10us)
                if(EDIP_TX_ZCAL_DONE):
                    # success!!!
                    pass

                # fapi2::ReturnCode p9_io_xbus_dccal
                # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_dccal.C:108
                tx_zcal_set_grp(target, i_grp)
                # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_dccal.C:597

                # if impedance calibration is done
                if EDIP_TX_ZCAL_DONE == 1:
                    # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_dccal.C:607
                    # current value of enabled segments
                    # need to convert the 8R value to a 4R equivalent
                    l_pval = EDIP_TX_ZCAL_P / 2
                    # dont know what's the diffrence between _P and _N register
                    l_nval = EDIP_TX_ZCAL_N / 2
                else:
                    # default values
                    l_pval = 100
                    l_nval = 100

                # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_dccal.C:325
                # attributes, not a registers!
                l_margin_ratio = fapi2::ATTR_IO_XBUS_TX_MARGIN_RATIO
                l_ffe_pre_coef = fapi2::ATTR_IO_XBUS_TX_FFE_PRECURSOR

                p_en_margin_pu = 32
                p_en_margin_pd = 32
                l_4r_pval = i_pval - 18
                p_en_main = 0
                if l_4r_pval < 64
                    if l_4r_pval % 4 != 0
                        p_en_main = 2
                        l_4r_pval -= p_en_main
                    p_en_margin_pd = l_4r_pval / 2
                    p_en_margin_pu = l_4r_pval - p_en_margin_pd
                p_en_main += l_4r_pval - p_en_margin_pu - p_en_margin_pd
                p_en_main = min(p_en_main, 50)
                p_sel_pre = (i_pval * l_ffe_pre_coef) / 128
                p_sel_pre = min(p_sel_pre, 18)

                n_en_pre       = 18
                n_en_margin_pu = 32
                n_en_margin_pd = 32
                l_4r_nval = i_nval - n_en_pre
                n_en_main = 0
                if l_4r_nval < 64:
                    if l_4r_nval % 4 != 0:
                        n_en_main = 2
                        l_4r_nval -= n_en_main
                    n_en_margin_pd = l_4r_nval / 2
                    n_en_margin_pu = l_4r_nval - n_en_margin_pd
                n_en_main += l_4r_nval - n_en_margin_pu - n_en_margin_pd
                n_en_main = min(n_en_main, 50)
                n_sel_pre = (i_nval * l_ffe_pre_coef) / 128
                n_sel_pre = min(n_sel_pre, n_en_pre)

                sel_margin_pu = (i_pval * l_margin_ratio) / 256
                sel_margin_pu = min(sel_margin_pu, p_en_margin_pu, n_en_margin_pu)
                sel_margin_pd = (i_nval * l_margin_ratio) / 256
                sel_margin_pd = min(sel_margin_pd, p_en_margin_pd, n_en_margin_pd, sel_margin_pu)

                # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_dccal.C:539
                # pre bank pseg enable
                EDIP_TX_PSEG_PRE_EN       = convert_4r_with_2r(18, 5)
                # pre bank pseg mode selection
                EDIP_TX_PSEG_PRE_SEL      = convert_4r_with_2r(p_sel_pre, 5)
                # pre bank nseg enable
                EDIP_TX_NSEG_PRE_EN       = convert_4r_with_2r(18, 5)
                # pre bank nseg mode selection
                EDIP_TX_NSEG_PRE_SEL      = convert_4r_with_2r(n_sel_pre, 5)
                # margin pull-down bank pseg enable
                EDIP_TX_PSEG_MARGINPD_EN  = convert_4r(p_en_margin_pd)
                # margin pull-up bank pseg enable
                EDIP_TX_PSEG_MARGINPU_EN  = convert_4r(p_en_margin_pu)
                # margin pull-down bank nseg enable
                EDIP_TX_NSEG_MARGINPD_EN  = convert_4r(n_en_margin_pd)
                # margin pull-up bank nseg enable
                EDIP_TX_NSEG_MARGINPU_EN  = convert_4r(n_en_margin_pu)
                # margin pull-down bank margin selection
                EDIP_TX_MARGINPD_SEL      = convert_4r(sel_margin_pd)
                # margin pull-up bank margin selection
                EDIP_TX_MARGINPU_SEL      = convert_4r(sel_margin_pu)
                # main bank pseg enable
                EDIP_TX_PSEG_MAIN_EN      = convert_4r_with_2r(p_en_main, 13)
                # main bank nseg enable
                EDIP_TX_NSEG_MAIN_EN      = convert_4r_with_2r(n_en_main, 13)
        # src/usr/isteps/istep09/call_fabric_io_dccal.C:300
        # configureXbusConnectionsMode(l_stepError, l_pbusConnections, XbusDccalMode::RxDccalStartGrp);
        For each connection, group, target:
            # p9_io_xbus_dccal(RxDccalStartGrp)
            # mark lane as invalid
            # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_dccal.C:651
            EDIP_RX_LANE_INVALID[lane] = 0

            # p9_io_xbus_dccal(RxDccalStartGrp)
            # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_dccal.C:665
            # 1: selects bist refclock
            EDIP_RX_CTL_CNTL4_E_PG[EDIP_RX_WT_PLL_REFCLKSEL][target, group, lane0] = 1
            # 1: uses pll control to select refclk
            EDIP_RX_CTL_CNTL4_E_PG[EDIP_RX_PLL_REFCLKSEL_SCOM_EN][target, group, lane0] = 1
            sleep(150000ns)
            # 1: (pgood) sets pgood on rx pll for locking
            EDIP_RX_WT_CU_PLL_PGOOD[target, group, lane0] = 1
            # Clear the rx dccal done bit in case rx dccal was previously run.
            EDIP_RX_DC_CALIBRATE_DONE = 0
            # Start DC Calibrate, this iniates the rx dccal state machine
            EDIP_RX_START_DC_CALIBRATE = 1

            # p9_io_xbus_dccal(RxDccalCheckGrp)
            # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_dccal.C:842
            sleep(100ms)
            # wait for callibration to finish
            while(EDIP_RX_DC_CALIBRATE_DONE == 0)
                sleep(10ms)
            # Stop DC calibration
            EDIP_RX_START_DC_CALIBRATE[target, group, lane0] = 0
            # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_dccal.C:710
            # stop_cleanup_pll()
            EDIP_RX_CTL_CNTL4_E_PG[EDIP_RX_WT_PLL_REFCLKSEL][target, i_grp, lane0] = 0
            EDIP_RX_CTL_CNTL4_E_PG[EDIP_RX_PLL_REFCLKSEL_SCOM_EN][target, i_grp, lane0] = 0
            EDIP_RX_CTL_CNTL4_E_PG[EDIP_RX_WT_CU_PLL_PGOOD][target, i_grp, lane0] = 0
            sleep(110500ns)
            # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_dccal.C:639
            # set_lanes_invalid()
            For each lane:
                EDIP_RX_LANE_INVALID[target, group, lane] = 1
            # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_dccal.C:740
            # p9x_cm_workaround()
            # attribute, not a register!!!
            if fapi2::ATTR_CHIP_EC_FEATURE_XBUS_COMPRESSION_WORKAROUND:
                EDIP_RX_PG_SPARE_MODE_0[target, i_grp, lane0] = 1
                EDIP_RX_PG_SPARE_MODE_1[target, i_grp, lane0] = 0
                EDIP_RX_PG_SPARE_MODE_2[target, i_grp, lane0] = 1
                EDIP_RX_RC_ENABLE_CM_FINE_CAL[target, i_grp, lane0] = 0
                EDIP_RX_EO_ENABLE_DAC_H1_TO_A_CAL[target, i_grp, lane0] = 1
                EDIP_RX_EO_ENABLE_DAC_H1_CAL[target, i_grp, lane0] = 1
                for each lane:
                    EDIP_RX_A_INTEG_COARSE_GAIN[target, i_grp, lane] /= 2

    else if typeof(bus) == TYPE_OBUS:
        for each connection:
            # src/usr/isteps/istep09/istep09HelperFuncs.C:333
            if(typeof(connection) == TYPE_OBUS)
                # trainObus()
                For each target:
                    # :246
                    # FAPI_INVOKE_HWP(l_err,
                    #     p9_io_obus_dccal,
                    #     l_fapi2Target,
                    #     l_laneVector);
                    # src/import/chips/p9/procedures/hwp/io/p9_io_obus_dccal.C:762
                    fapi2::ATTR_IO_OBUS_LANE_PDWN_Type l_lane_pdwn;
                    fapi2::ATTR_IO_OBUS_PAT_A_CAPTURE_Type l_pat_a_capture;

                    if fapi2::ATTR_PROC_FABRIC_LINK_ACTIVE:
                        fapi2::ATTR_IO_OBUS_DCCAL_FLAGS = 0;
                        ##########################
                        # src/usr/fapi2/plat_hw_access.C:171
                        # Halt Obus PPE if HW446279_USE_PPE is enabled
                        if fapi2::ATTR_CHIP_EC_FEATURE_HW446279_USE_PPE:
                            # Perform SCOM write
                            deviceWrite(target, &i_data, 64, DEVICE_SCOM_ADDRESS(i_address, opMode));
                        ##########################
                        # src/import/chips/p9/procedures/hwp/io/p9_io_obus_dccal.C:563
                        # power up the unit
                        #
                        # disable RX clock group and put them in clow power mode
                        OPT_RX_CLKDIST_PDWN[target, group0] = 0
                        # disable TX clock group and put them in clow power mode
                        OPT_TX_CLKDIST_PDWN[target, group0] = 0
                        OPT_RX_IREF_PDWN_B[target, group0] = 1
                        OPT_RX_CTL_DATASM_CLKDIST_PDWN[target, group0] = 0
                        for each lane:
                            if (1<<lane) & (lanes_vetor) != 0:
                                OPT_RX_LANE_ANA_PDWN[target, group0, lane] = 0
                                OPT_RX_LANE_DIG_PDWN[target, group0, lane] = 0
                                OPT_TX_LANE_PDWN[target, group0, lane] = 0

                        ##########################
                        # src/import/chips/p9/procedures/hwp/io/p9_io_obus_dccal.C:660
                        # select ac coupled mode in receiver
                        OPT_RX_AC_COUPLED[lane0, group0, target] = 1
                        ##########################
                        # src/import/chips/p9/procedures/hwp/io/p9_io_obus_dccal.C:672
                        # Enable Disabled Lanes
                        p9_obus_lane_enable(target, 0x00FFFFFF);
                        for each lane:
                            if (0x01 << lane) & 0x00FFFFFF:
                                OPT_RX_LANE_DISABLED[group0, lane, target] = 0
                        ##########################
                        # src/import/chips/p9/procedures/hwp/io/p9_io_obus_dccal.C:149
                        # Impedance Calibration
                        OPT_TX_ZCAL_REQ[target, group0, lane0] = 1
                        sleep(20ms)
                        while OPT_TX_ZCAL_DONE == 0 && EDIP_TX_ZCAL_ERROR == 0
                            FAPI_TRY( fapi2::delay( 10us, DLY_1MIL_CYCLES ) );

                        if OPT_TX_ZCAL_DONE == 1 && EDIP_TX_ZCAL_ERROR == 0
                            # success
                            pass
                        ##########################
                        fapi2::ATTR_IO_OBUS_DCCAL_FLAGS = fapi2::ENUM_ATTR_IO_OBUS_DCCAL_FLAGS_TX;
                        ##########################
                        # src/import/chips/p9/procedures/hwp/io/p9_io_obus_dccal.C:392
                        # tx_set_zcal_ffe(target);
                        pvalx4 = 100
                        nvalx4 = 100
                        if OPT_TX_ZCAL_DONE == 1:
                            pvalx4 = OPT_TX_ZCAL_P[target, group0, lane0]
                            nvalx4 = OPT_TX_ZCAL_N[target, group0, lane0]

                            # src/import/chips/p9/procedures/hwp/io/p9_io_obus_dccal.C:231
                            pvalx2 = i_pvalx4 / 2;
                            nvalx2 = i_nvalx4 / 2;

                            pvalx2_int = pvalx2 - 67;
                            p_main_en_x2 = 13
                            if pvalx2_int < 0
                                p_main_en_x2 -= pvalx2_int

                            nvalx2_int = nvalx2 - 67;
                            n_main_en_x2 = 13
                            if nvalx2_int < 0:
                                n_main_en_x2 -= nvalx2_int

                            p_pre_sel_x2 = (pvalx2 * fapi2::ATTR_IO_OBUS_TX_FFE_PRECURSOR) / 128;
                            n_pre_sel_x2 = (nvalx2 * fapi2::ATTR_IO_OBUS_TX_FFE_PRECURSOR) / 128;

                            p_post_sel_x2 = (pvalx2 * fapi2::ATTR_IO_OBUS_TX_FFE_POSTCURSOR) / 128;
                            n_post_sel_x2 = (nvalx2 * fapi2::ATTR_IO_OBUS_TX_FFE_POSTCURSOR) / 128;

                            margin_pu_sel_x2 = (pvalx2 * fapi2::ATTR_IO_OBUS_TX_MARGIN_RATIO) / 256;
                            margin_pd_sel_x2 = (nvalx2 * fapi2::ATTR_IO_OBUS_TX_MARGIN_RATIO) / 256;

                            margin_sel =
                                toTherm((margin_pu_sel_x2 + 1) / 2)
                                & toTherm((margin_pd_sel_x2 + 1) / 2)
                                & 0xFF;

                            # Setting Pre Cursor Values
                            #
                            # toThermWithHalf(arg_1, arg_2) calculates the following
                            #     return (arg_1 & 0x1) << (arg_2 - 1) | ((0x1 << (arg_1 >> 1)) - 1 );
                            OPT_TX_PSEG_PRE_EN[target, group0, lane0]   = 0x1F;
                            OPT_TX_PSEG_PRE_SEL[target, group0, lane0]  = toThermWithHalf(p_pre_sel_x2, 5);
                            OPT_TX_NSEG_PRE_EN[target, group0, lane0]   = 0x1F;
                            OPT_TX_NSEG_PRE_SEL[target, group0, lane0]  = toThermWithHalf(n_pre_sel_x2, 5);
                            OPT_TX_PSEG_POST_EN[target, group0, lane0]  = 0x7F;
                            OPT_TX_PSEG_POST_SEL[target, group0, lane0] = toThermWithHalf(p_post_sel_x2, 7);
                            OPT_TX_NSEG_POST_EN[target, group0, lane0]  = 0x7F;
                            OPT_TX_NSEG_POST_SEL[target, group0, lane0] = toThermWithHalf(n_post_sel_x2, 7);

                            # Setting Post Cursor Values
                            OPT_TX_PSEG_MARGINPD_EN[target, group0, lane0] = 0xFF;
                            OPT_TX_PSEG_MARGINPU_EN[target, group0, lane0] = 0xFF;
                            OPT_TX_NSEG_MARGINPD_EN[target, group0, lane0] = 0xFF;
                            OPT_TX_NSEG_MARGINPU_EN[target, group0, lane0] = 0xFF;
                            OPT_TX_MARGINPD_SEL[target, group0, lane0] = margin_sel;
                            OPT_TX_MARGINPU_SEL[target, group0, lane0] = margin_sel;

                            # Setting Main Values
                            OPT_TX_PSEG_MAIN_EN[target, group0, lane0] = toThermWithHalf(p_main_en_x2, 7);
                            OPT_TX_NSEG_MAIN_EN[target, group0, lane0] = toThermWithHalf(n_main_en_x2, 7);

                        ##########################
                        # src/import/chips/p9/procedures/hwp/io/p9_io_obus_dccal.C:599
                        for each lane:
                            if( ((0x1 << lane) & 0x00FFFFFF ) != 0 )
                                OPT_RX_PR_FW_OFF[target, group0, lane] = 1
                        ##########################
                        # src/import/chips/p9/procedures/hwp/io/p9_io_obus_dccal.C:437
                        # Start DC Calibrate, this iniates the rx dccal state machine
                        for each lane:
                            if((0x1 << lane) & 0x00FFFFFF ) != 0:
                                OPT_RX_RUN_DCCAL[target, group0, lane] = 1
                        ##########################
                        rx_poll_dccal_done(target, 0x00FFFFFF);
                        ##########################
                        # src/import/chips/p9/procedures/hwp/io/p9_io_obus_dccal.C:437
                        # Stop DC Calibrate, this stops the rx dccal state machine
                        for each lane:
                            if((0x1 << lane) & 0x00FFFFFF ) != 0:
                                OPT_RX_RUN_DCCAL[target, group0, lane] = 0
                        ##########################
                        set_rx_b_bank_controls(target, 0x00FFFFFF, 0);
                        ##########################
                        fapi2::ATTR_IO_OBUS_DCCAL_FLAGS = fapi2::ENUM_ATTR_IO_OBUS_DCCAL_FLAGS_TX | fapi2::ENUM_ATTR_IO_OBUS_DCCAL_FLAGS_RX;
                        ##########################
                        # # src/import/chips/p9/procedures/hwp/io/p9_io_obus_dccal.C:599
                        # turn phase rotator wly wheel on
                        for each lane:
                            if( ((0x1 << lane) & 0x00FFFFFF ) != 0 )
                                OPT_RX_PR_FW_OFF[target, group0, lane] = 0
                        ##########################
                        # turn phase rotator wly wheel on continuation
                        for each lane:
                            if ((0x1 << lane) & 0x00FFFFFF) != 0:
                                OPT_RX_PR_EDGE_TRACK_CNTL[target, group0, lane] = 0
                        ##########################

                        fapi2::ATTR_IO_OBUS_PAT_A_DETECT_RUN = fapi2::ENUM_ATTR_IO_OBUS_PAT_A_DETECT_RUN_FALSE

                        l_lane_pdwn = fapi2::ATTR_IO_OBUS_LANE_PDWN;
                        l_pat_a_capture = fapi2::ATTR_IO_OBUS_PAT_A_CAPTURE;
                        for each lane
                            if((0x1 << lane) & 0x00FFFFFF) != 0:
                                l_lane_pdwn = l_lane_pdwn & (~(0x80000000 >> lane));
                                l_pat_a_capture[lane] = 0;
                        fapi2::ATTR_IO_OBUS_LANE_PDWN = l_lane_pdwn;
                        fapi2::ATTR_IO_OBUS_PAT_A_CAPTURE = l_pat_a_capture;


```

## 9.3 fabric_pre_trainadv: Advanced pre training
### p9_io_xbus_linktrain.C (called on each OO and X bus target pair)
* Debug routine for IO Characterization
* Nothing in it
```
# only debug in here
pass
```

## 9.4 fabric_io_run_training: Run training on internal buses
### p9_io_xbus_linktrain.C (called on each OO and X bus target pair)
* Hostboot will run training on all intra node buses.  For Nimbus this is
all X buses.  For Cumulus this is run by the SP in a later step
* Wiretest, Deskew, Eye Optimization, and repair
- Option to run extend bit patterns in optimization phase (replaces RDT)
- Repairable fails are left for PRD to analyze and move data into VPD
- PRD will use io_eRepair_read.Cto perform this
* Fatal bus training errors are handled by procedure, must return error
and FFDC (written to VPD)
* Expected that fatal error passes returncode backto HWPF, FW then
looks up
returncode and determines what to do based off of FFDC

```
for each bus, connection, group:
    if typeof(bus) == TYPE_XBUS:
        target_master = connection.master
        target_slave = connection.slave
        for target in [connection.master, connection.slave]:
            # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_linktrain.C
            # sets an attribute
            fapi2::ATTR_IO_XBUS_GRP0_PRE_BAD_LANE_DATA =
                ((EDIP_RX_LANE_BAD_VEC_0_15[bus, connection, group, target] << 8) & 0x00FFFF00)
               | (EDIP_RX_LANE_BAD_VEC_16_23[bus, connection, group, target]      & 0x000000FF)
            # src/import/chips/p9/procedures/hwp/io/p9_io_xbus_linktrain.C:723
            # tx_serializer_sync_power_on
            tx_serializer_sync_power_on( l_mtgt, l_stgt, i_grp )
            EDIP_TX_CLK_UNLOAD_CLK_DISABLE[target, lane0] = 0
            EDIP_TX_CLK_RUN_COUNT[target, lane0] = 0
            EDIP_TX_CLK_RUN_COUNT[target, lane0] = 1
            EDIP_TX_CLK_UNLOAD_CLK_DISABLE[target, lane0] = 1
            for each lane:
                EDIP_TX_UNLOAD_CLK_DISABLE[target, group, lane] = 0
            linktrain_start( target, i_grp, State::WDERF )
            EDIP_RX_START_WDERF_ALIAS[target, group, lane0] = 0x0000001F
        # src/import/chips/p9/procedures/hwp/io/p9_io_dmi_linktrain.C:590
        while True:
            if(EDIP_RX_WDERF_DONE_ALIAS[connection.master, group3, lane0] == 0x0000001F):
                # done
                break
            if(EDIP_RX_WDERF_FAILED_ALIAS[connection.master, group3, lane0] != 0):
                # failed
                break
            sleep(1ms)
    elif typeof(bus) == TYPE_OBUS:
        # src/usr/isteps/istep09/istep09HelperFuncs.C:200
        # determine link train capabilities (half/full)
            l_even = (fapi2::ATTR_LINK_TRAIN == fapi2::ENUM_ATTR_LINK_TRAIN_BOTH) ||
                    (fapi2::ATTR_LINK_TRAIN == fapi2::ENUM_ATTR_LINK_TRAIN_EVEN_ONLY);

            l_odd = (fapi2::ATTR_LINK_TRAIN == fapi2::ENUM_ATTR_LINK_TRAIN_BOTH) ||
                    (fapi2::ATTR_LINK_TRAIN == fapi2::ENUM_ATTR_LINK_TRAIN_ODD_ONLY);
        # run PHY init sequence + pattern A detection sequence
        # on both connected endpoints if not yet run
        if fapi2::ATTR_IO_OBUS_PAT_A_DETECT_RUN == fapi2::ENUM_ATTR_IO_OBUS_PAT_A_DETECT_RUN_FALSE:
            for each lane:
                remote_target = getOtherEnd(target)
                if (i_even && (l_lane  < 12))
                or (i_odd  && (l_lane >= 12)):
                    # src/import/chips/p9/procedures/hwp/io/p9_io_obus_linktrain.C:84
                    # init_tx_fifo
                    OPT_TX_MODE2_PL[target, group0, lane] = 0
                    OPT_TX_CNTL1G_PL[target, group0, lane] = 1
                    OPT_TX_MODE2_PL[target, group0, lane] = 1

                    # src/import/chips/p9/procedures/hwp/io/p9_io_obus_linktrain.C:84
                    OPT_TX_MODE2_PL[remote_target, group0, lane] = 0
                    OPT_TX_CNTL1G_PL[remote_target, group0, lane] = 1
                    OPT_TX_MODE2_PL[remote_target, group0, lane] = 1

            ##################################
            # send  TS1 for Cable CDR lock
            # set TX lane control to force send of TS1 pattern
            if i_even:
                OBUS_LL0_IOOL_LINK0_TX_LANE_CONTROL[target]        = 0x1111111111100000;
                OBUS_LL0_IOOL_LINK0_TX_LANE_CONTROL[remote_target] = 0x1111111111100000;
            if i_odd:
                OBUS_LL0_IOOL_LINK1_TX_LANE_CONTROL[target]        = 0x1111111111100000;
                OBUS_LL0_IOOL_LINK1_TX_LANE_CONTROL[remote_target] = 0x1111111111100000;
            # Delay to compensate for active links
            sleep(100000000ns)
            # resset TX lane control
            if i_even:
                OBUS_LL0_IOOL_LINK0_TX_LANE_CONTROL[target]        = 0;
                OBUS_LL0_IOOL_LINK0_TX_LANE_CONTROL[remote_target] = 0;
            if i_odd:
                OBUS_LL0_IOOL_LINK1_TX_LANE_CONTROL[target]        = 0;
                OBUS_LL0_IOOL_LINK1_TX_LANE_CONTROL[remote_target] = 0;

            # execute sequence to send pattern A and check receipt of pattern
            # in PHY RX data pipe logic on connected endpoint -- lanes which
            # do not see expected pattern will be powered down (sparing for
            # single lane faults, or training failure for multi lane faults
            # will be handled in p9_smp_link_layer)
            if i_even:
                OBUS_LL0_IOOL_LINK0_TX_LANE_CONTROL[target]        = 0x4444444444400000;
                OBUS_LL0_IOOL_LINK0_TX_LANE_CONTROL[remote_target] = 0x4444444444400000;
            if i_odd:
                OBUS_LL0_IOOL_LINK1_TX_LANE_CONTROL[target]        = 0x4444444444400000;
                OBUS_LL0_IOOL_LINK1_TX_LANE_CONTROL[remote_target] = 0x4444444444400000;

            # at IPL time, power down any unneeded lanes (not physically connected or
            # unused half-link)
            for each lane:
                if((lane_idx in [11, 12]) # unused lanes, always pdwn
                || (!fapi2::ATTR_IO_OBUS_TRAIN_FOR_RECOVERY && !l_even && (lane_idx <  11))   # even link is unused
                || (!fapi2::ATTR_IO_OBUS_TRAIN_FOR_RECOVERY && !l_odd  && (lane_idx >= 13))): # odd link is unused
                    pdwn_bad_lane(target,        group0, lane_idx)
                    pdwn_bad_lane(remote_target, group0, lane_idx)

            # Delay to compensate for pattern
            sleep(100000000ns)

            for target in [remote_target, target]:
                pattern_A_capture = fapi2::ATTR_IO_OBUS_PAT_A_CAPTURE

                for each lane:
                    # capture only if half link containing this lane is active, and should
                    # be physically connected
                    if ((i_even && (l_lane  < 11)) ||
                        (i_odd  && (l_lane >= 13))):

                        # install pre-set values for pattern detection/sampling
                        OPT_RX_PIPE_SEL[         target, group0, lane] = 2
                        OPT_RX_BANK_SEL_A[       target, group0, lane] = 1
                        OPT_RX_A_CONTROLS[       target, group0, lane] = 4
                        OPT_RX_AMP_VAL[          target, group0, lane] = fapi2::ATTR_IO_OBUS_PAT_A_DETECT_RX_AMP_VALUE
                        OPT_RX_CAL_LANE_SEL[     target, group0, lane] = 1
                        OPT_RX_DATA_PIPE_CAPTURE[target, group0, lane] = 1

                        # capture sample
                        # concatenate bits 0:10 of rx_data_pipe_0_15 and rx_data_pipe_16_31
                        # to get a total of 22 bits, left aligned
                        sleep(10000ns)
                        pattern_A_capture[l_lane] =
                            OPT_RX_DATA_PIPE_0_15[ target, group0, lane][48:58]
                            | (OPT_RX_DATA_PIPE_16_31[target, group0, lane][48:58] << 10);

                        # determine if pattern was received

                        # Maximum and minimum run length of bits.  This correponds to the number of Xs
                        # e.g.  Pattern A is 1111111100000000.... (8 up 8 down)
                        #       and can be received as 1111000000000000....  (4 up 12 down)
                        #       or 1111111111110000.... (12 up 4 down)
                        # algoritm from
                        # src/import/chips/p9/procedures/hwp/io/p9_io_obus_linktrain.C:307
                        pattern_A_detected = True
                        switches = []
                        for bit_index in range(1, 22):
                        if i_data_pipe[bit_index-1] != i_data_pipe[bit_index]:
                            switches.append(bit_index)

                        # ensure that we switched 2 or 3 times
                        if len(switches) not in [2, 3]:
                            pattern_A_detected = False;
                        # determine if switching history is viable given runs in transmitted pattern
                        elif switches[0] > 12
                            pattern_A_detected = False
                        else:
                            for index in range(1, len(switches)):
                                if   switches[index] - switches[index-1] < 4
                                or switches[index] - switches[index-1] > 12
                                or (index == len(switches)-1 && 22-switches[index] > 12):
                                pattern_A_detected = False

                            # if not, power down this lane
                        if not pattern_A_detected:
                            fapi2::ATTR_IO_OBUS_LANE_PDWN |= (0x80000000 >> i_lane)

                            OPT_RX_RECAL_ABORT[target, group, lane] = 1
                            while(OPT_RX_LANE_BUSY != 0):
                                pass

                            OPT_RX_AMP_VAL     = 0x7F
                            OPT_RX_A_CONTROLS |= 0x04
                            OPT_RX_BANK_SEL_A  = 1
                            # power down lane
                            OPT_RX_LANE_ANA_PDWN   = 1
                            OPT_RX_LANE_DIG_PDWN   = 1
                            OPT_RX_LANE_DISABLED   = 1
                            OPT_RX_FORCE_INIT_DONE = 1
                        else:
                            # reset lane parameters to default
                            OPT_RX_PIPE_SEL[target, group0, lane]   = 1
                            OPT_RX_A_CONTROLS[target, group0, lane] = 32
                            OPT_RX_AMP_VAL[target, group0, lane]    = 0

                        # clear lane select for next iteration
                        OPT_RX_CAL_LANE_SEL[target, group0, lane] = 0
                fapi2::ATTR_IO_OBUS_PAT_A_CAPTURE[target] = pattern_A_capture

            if(i_even):
                OBUS_LL0_IOOL_LINK0_TX_LANE_CONTROL[target]        = 0
                OBUS_LL0_IOOL_LINK0_TX_LANE_CONTROL[remote_target] = 0
            if(i_odd):
                OBUS_LL0_IOOL_LINK1_TX_LANE_CONTROL[target]        = 0
                OBUS_LL0_IOOL_LINK1_TX_LANE_CONTROL[remote_target] = 0

            # mark both endpoints as run
            fapi2::ATTR_IO_OBUS_PAT_A_DETECT_RUN[target]        = fapi2::ENUM_ATTR_IO_OBUS_PAT_A_DETECT_RUN_TRUE
            fapi2::ATTR_IO_OBUS_PAT_A_DETECT_RUN[remote_target] = fapi2::ENUM_ATTR_IO_OBUS_PAT_A_DETECT_RUN_TRUE

        # resend TS1 for Cable CDR lock and start phy training
        # send  TS1 for Cable CDR lock
        # set TX lane control to force send of TS1 pattern
        if i_even:
            OBUS_LL0_IOOL_LINK0_TX_LANE_CONTROL[target] = 0x1111111111100000;
        if i_odd:
            OBUS_LL0_IOOL_LINK1_TX_LANE_CONTROL[target] = 0x1111111111100000;
        # Delay to compensate for active links
        sleep(100000000ns)
        # resset TX lane control
        if i_even:
            OBUS_LL0_IOOL_LINK0_TX_LANE_CONTROL[target] = 0;
        if i_odd:
            OBUS_LL0_IOOL_LINK1_TX_LANE_CONTROL[target] = 0;

        # DD1.1+ HW Start training sequence
        if not fapi2::ATTR_CHIP_EC_FEATURE_HW419022:
            mask = 0
            data = 0
            if l_even:
                data[OBUS_LL0_IOOL_CONTROL_LINK0_PHY_TRAINING] = 1
                mask[OBUS_LL0_IOOL_CONTROL_LINK0_PHY_TRAINING] = 1
            if l_odd:
                data[OBUS_LL0_IOOL_CONTROL_LINK1_PHY_TRAINING] = 1
                mask[OBUS_LL0_IOOL_CONTROL_LINK1_PHY_TRAINING] = 1
            OBUS_LL0_IOOL_CONTROL[target] |= data & mask

            mask = 0
            data = 0
            if l_even and fapi2::ATTR_IO_OBUS_TRAIN_FOR_RECOVERY:
                data[OBUS_LL0_IOOL_CONTROL_LINK0_STARTUP] = 1
                mask[OBUS_LL0_IOOL_CONTROL_LINK0_STARTUP] = 1
            if l_odd and fapi2::ATTR_IO_OBUS_TRAIN_FOR_RECOVERY:
                data[OBUS_LL0_IOOL_CONTROL_LINK1_STARTUP] = 1
                mask[OBUS_LL0_IOOL_CONTROL_LINK1_STARTUP] = 1

            OBUS_LL0_IOOL_CONTROL[target] |= data & mask

```

## 9.5 fabric_post_trainadv: Advanced post EI/EDI training
### p9_io_post_trainadv.C(called on each O and X bus target pair)
* Debug routine for IO Characterization
* Nothing in it

```
for each bus:
  if typeof(bus) == TYPE_OBUS:
    if fapi2::ATTR_MNFG_FLAGS & fapi2::ENUM_ATTR_MNFG_FLAGS_MNFG_THRESHOLDS != 0:
        # src/import/chips/p9/procedures/hwp/io/p9_io_obus_post_trainadv.C:63
        # Setup ECC & CRC Masks
        OBUS_LL0_LL0_LL0_PB_IOOL_FIR_MASK_REG_OR =
          OBUS_LL0_LL0_LL0_PB_IOOL_FIR_MASK_REG_LINK0_SL_ECC_CORRECTABLE
        | OBUS_LL0_LL0_LL0_PB_IOOL_FIR_MASK_REG_LINK1_SL_ECC_CORRECTABLE
        | OBUS_LL0_LL0_LL0_PB_IOOL_FIR_MASK_REG_LINK0_TOO_MANY_CRC_ERRORS
        | OBUS_LL0_LL0_LL0_PB_IOOL_FIR_MASK_REG_LINK1_TOO_MANY_CRC_ERRORS

        # src/import/chips/p9/procedures/hwp/io/p9_io_obus_post_trainadv.C
        # Setup Performance Counters
        OBUS_LL0_IOOL_PERF_SEL_CONFIG =
          0x1B | (0x14 << 8) | (0x1B << 16) | (0x14 << 24)

        # Setup Performance Counters
        OBUS_LL0_IOOL_PERF_TRACE_CONFIG =
             0x02
          | (0x02 << 2)
          | (0x01 << 8)
          | (0x01 << 10)
          | (0x01 << 16)
          | (0x01 << 18)
          | (0x01 << 24)
          | (0x01 << 26)

        # Setup Performance Counters
        # Make it so there are only 10 or 5 ECC errors allowed per lane or 10 or 5 CRC errors allowed
        # per link (reliability test) --- Have a flag to go between 3.5/3.7 settings
        # Ex. putscom pu 0901080F 0F000F00000000000 -bor -pall
        # Ex. putscom pu 0901080F FF8AFF8AFFFFFFFF -band -pall
        # src/import/chips/p9/procedures/hwp/io/p9_io_obus_post_trainadv.C:116
        OBUS_LL0_IOOL_OPTICAL_CONFIG |= (0xF << 4) | (0xF << 20)

        OBUS_LL0_IOOL_OPTICAL_CONFIG |= (0x7F << 9) | (0x7F << 25)
        if fapi2::ATTR_IO_O_MNFG_ERROR_THRESHOLD == fapi2::ENUM_ATTR_IO_O_MNFG_ERROR_THRESHOLD_CORNER_MODE:
          OBUS_LL0_IOOL_OPTICAL_CONFIG &= (5 << 9) | (5 << 25)
        elif fapi2::ATTR_IO_O_MNFG_ERROR_THRESHOLD == ENUM_ATTR_IO_O_MNFG_ERROR_THRESHOLD_RELIABILITY_MODE:
          OBUS_LL0_IOOL_OPTICAL_CONFIG &= (10 << 9) | (10 << 25)
```

## 9.6 proc_smp_link_layer: Start SMP link layer
### p9_smp_link_layer.C(called on processor chip)
* Reads logical A/X link configuration attributes, trains the
DL/TL layers of selected links
* Set scom on both sides of the bus to trigger Data link layer training
* DLL sends training packets, sets link up FIR bit when done
* FIR done bit launches the Transaction Layer (TL)
* FIR bit in nest domain to indicate training done
* After this point the mailbox register are available to communicate
- Xstop would prevent mailbox communication
* Bus is NOT part of the SMP coherency
* Only performed on trained, valid buses

```
# logical link (X/A) configuration parameters
# enable on local end
uint8_t l_x_en[7]
uint8_t l_a_en[4]

# process set of enabled links
l_x_en = fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[target]
l_a_en = fapi2::ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG[target]

for link_index in range(0, 7):
  link = l_x_en[link_index]
  if link != 0:
    # defined in src/import/chips/p9/procedures/hwp/nest/p9_fbc_smp_utils.H:98
    # first 3 are electrical, any else is optical
    if P9_FBC_XBUS_LINK_CTL_ARR[link_index].endpoint_type == ELECTRICAL:
      if (link == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE)
      or (link == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_EVEN_ONLY):
        # REGISTER!!!!!!
        # update control register
        # register in a structure src/import/chips/p9/procedures/hwp/nest/p9_fbc_smp_utils.H:81
        P9_FBC_XBUS_LINK_CTL_ARR[link_index].dl_control_addr |= 1 << 1

      if (link == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE)
      or (link == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_ODD_ONLY):
        # REGISTER!!!!!!
        # update control register
        # register in a structure src/import/chips/p9/procedures/hwp/nest/p9_fbc_smp_utils.H:81
        P9_FBC_XBUS_LINK_CTL_ARR[link_index].dl_control_addr |= 1 << 33
    elif link.endpoint_type == OPTICAL:
      # src/import/chips/p9/procedures/hwp/nest/p9_smp_link_layer.C:328
      # p9_smp_link_layer_train_link_optical()
      even = (i_en == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_TRUE)
          or (i_en == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_EVEN_ONLY)
      odd  = (i_en == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_TRUE)
          or (i_en == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_ODD_ONLY)

      # find local endpoint target associated with this link
      for each target.getChildrens(fapi2::TARGET_TYPE_OBUS):
        if (fapi2::ATTR_CHIP_UNIT_POS[target] == P9_FBC_XBUS_LINK_CTL_ARR[link_index].endp_unit_id):
          local_target = target
          break
      remote_target = local_target.getOtherEnd()

      if not fapi2::ATTR_CHIP_EC_FEATURE_HW419022[target]:
        if even:
          P9_FBC_XBUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |=
            (1 << 0) | (1 << 1)
        if odd:
          P9_FBC_XBUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |=
            (1 << 32) | (1 << 33)
      else:
        if even:
          # force assertion of run_lane
          P9_FBC_XBUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |= 1 << 5
          # ensure that DL RX sees lane lock

          # loop a maximum of two times to determine lane lock status
          # 1st check is prior to application of any PHY TX inversions
          # if any lanes do not lock, apply inversions and check again
          # assert if any lane is unlocked at this point
          for phase in range(0, 2):
            rx_control_stable = 0
            stable_reads = 1
            # poll for stable pattern
            for _ in range(0, 10):
              sleep(100000000ns)
              # read from REGISTER!!!!
              dl_rx_control = *(P9_FBC_XBUS_LINK_CTL_ARR[link_index].dl_control_addr+7)[target]
              if (dl_rx_control & 0xFFF0000) == rx_control_stable:
                stable_reads++
                if stable_reads == 3:
                  break
              else:
                rx_control_stable = (dl_rx_control & 0xFFF0000)
                stable_reads = 1

            # apply PHY TX inversions only if needed
            if phase == 0 and ((dl_rx_control & 0xFFF0000) != 0xFFE0000):
              for lane_index in range(0, 11):
                # set PHY TX lane address, start at:
                # - PHY lane 0 for even (work up)
                # - PHY lane 23 for odd (work down) DD1.0

                # REGISTER address
                phy_tx_mode1_pl_addr = 0x8004040009010c3f
                phy_tx_mode1_pl_addr |= lane_index << 32

                # read DL RX per-lane lock indicator bit
                # if locked: do nothing
                # if not locked: apply lane-invert to associated PHY TX side
                if not dl_rx_control & (1 << (36+lane_index)):
                  # REGISTER!!!
                  *phy_tx_mode1_pl_addr[remote_target] |= 1 << 49
            if (dl_rx_control & 0xFFF0000) == 0xFFE0000:
              break
          # enable link startup
          P9_FBC_XBUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |= 1 << 1
          # disable run lane override
          P9_FBC_XBUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |= 1 << 5
          # clear TX lane control override, set to ENABLED
          P9_FBC_XBUS_LINK_CTL_ARR[link_index].dl_control_addr[target]+5 = 0
        if odd:
          # force assertion of run_lane
          P9_FBC_XBUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |= 1 << 37
          # ensure that DL RX sees lane lock

          # loop a maximum of two times to determine lane lock status
          # 1st check is prior to application of any PHY TX inversions
          # if any lanes do not lock, apply inversions and check again
          # assert if any lane is unlocked at this point
          for phase in range(0, 2):
            rx_control_stable = 0
            stable_reads = 1
            # poll for stable pattern
            for _ in range(0, 10):
              sleep(100000000ns)
              # read from REGISTER!!!!
              dl_rx_control = *(control.dl_control_addr+8)[target]
              if (dl_rx_control & 0xFFF0000) == rx_control_stable:
                stable_reads++
                if stable_reads == 3:
                  break
              else:
                rx_control_stable = (dl_rx_control & 0xFFF0000)
                stable_reads = 1

            # apply PHY TX inversions only if needed
            if phase == 0 and ((dl_rx_control & 0xFFF0000) != 0xFFE0000):
              for lane_index in range(0, 11):
                # set PHY TX lane address, start at:
                # - PHY lane 0 for even (work up)
                # - PHY lane 23 for odd (work down) DD1.0

                # REGISTER address
                phy_tx_mode1_pl_addr = 0x8004040009010c3f
                phy_tx_mode1_pl_addr |= (23-lane_index) << 32

                # read DL RX per-lane lock indicator bit
                # if locked: do nothing
                # if not locked: apply lane-invert to associated PHY TX side
                if not dl_rx_control & (1 << (36+lane_index)):
                  # REGISTER!!!
                  *phy_tx_mode1_pl_addr[remote_target] |= 1 << 49
            if (dl_rx_control & 0xFFF0000) == 0xFFE0000:
              break
          # enable link startup
          P9_FBC_XBUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |= 1 << 33
          # disable run lane override
          P9_FBC_XBUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |= 1 << 37
          # clear TX lane control override, set to ENABLED
          control.dl_control_addr[target]+6 = 0
      # CQ: HW453889 :: MFG Abus Stress >>>
      # Use rx_pr_data_a_offset to shift the offset by +1/2 or +2/4
      if even and (fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_EVEN[target] != 0):
        for lane_index in range(0, 11):
          # set PHY TX lane address, start at:
          # - PHY lane 0 for even (work up)
          # - PHY lane 23 for odd (work down)

          # REGISTER address!!!
          # OBUS_RX0_RXPACKS0_SLICE0_RX_BIT_CNTL3_EO_PL
          # src/import/chips/p9/procedures/hwp/nest/p9_smp_link_layer.C:278
          l_phy_rx_bit_cntl3_eo_pl_addr = 0x8002500009010c3f
          l_phy_rx_bit_cntl3_eo_pl_addr |= (lane_index << 32)

          fapi2::buffer<uint64_t> l_phy_rx_bit_cntl3_eo_pl
          # REGISTER read
          l_phy_rx_bit_cntl3_eo_pl = l_phy_rx_bit_cntl3_eo_pl_addr[target]
          l_phy_rx_bit_cntl3_eo_pl &= ~((0x3F << 48) | (0x3F << 54))
          l_phy_rx_bit_cntl3_eo_pl |= ((0x3F & fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_EVEN[target]) << 48)
                                    | ((0x3F & fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_EVEN[target]) << 54)
          # REGISTER write
          l_phy_rx_bit_cntl3_eo_pl_addr[target] = l_phy_rx_bit_cntl3_eo_pl
      if odd and (fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_ODD[target] != 0):
        for lane_index in range(0, 11):
          # set PHY TX lane address, start at:
          # - PHY lane 0 for even (work up)
          # - PHY lane 23 for odd (work down)

          # REGISTER address!!!
          l_phy_rx_bit_cntl3_eo_pl_addr = 0x8002500009010c3f | ((23-lane_index) << 32)

          fapi2::buffer<uint64_t> l_phy_rx_bit_cntl3_eo_pl
          # REGISTER read
          l_phy_rx_bit_cntl3_eo_pl = l_phy_rx_bit_cntl3_eo_pl_addr[target]
          l_phy_rx_bit_cntl3_eo_pl &= ~((0x3F << 48) | (0x3F << 54))
          l_phy_rx_bit_cntl3_eo_pl |= ((0x3F & fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_ODD[target]) << 48)
                                    | ((0x3F & fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_ODD[target]) << 54)
          # REGISTER write
          l_phy_rx_bit_cntl3_eo_pl_addr[target] = l_phy_rx_bit_cntl3_eo_pl
for link_index in range(0, 4):
  # all are optical
  link = l_a_en[link_index]
  # src/import/chips/p9/procedures/hwp/nest/p9_smp_link_layer.C:328
  # p9_smp_link_layer_train_link_optical()
  even = (i_en == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_TRUE)
      or (i_en == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_EVEN_ONLY)
  odd  = (i_en == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_TRUE)
      or (i_en == fapi2::ENUM_ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG_ODD_ONLY)

  # find local endpoint target associated with this link
  for each target.getChildrens(fapi2::TARGET_TYPE_OBUS):
    if (fapi2::ATTR_CHIP_UNIT_POS[target] == P9_FBC_ABUS_LINK_CTL_ARR[link_index].endp_unit_id):
      local_target = target
      break
  remote_target = local_target.getOtherEnd()

  if not fapi2::ATTR_CHIP_EC_FEATURE_HW419022[target]:
    if even:
      P9_FBC_ABUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |=
        (1 << 0) | (1 << 1)
    if odd:
        P9_FBC_ABUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |=
          (1 << 32) | (1 << 1)
  else:
    if even:
      # force assertion of run_lane
      P9_FBC_ABUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |= (1 << 5)
      # ensure that DL RX sees lane lock

      # loop a maximum of two times to determine lane lock status
      # 1st check is prior to application of any PHY TX inversions
      # if any lanes do not lock, apply inversions and check again
      # assert if any lane is unlocked at this point
      for phase in range(0, 2):
        rx_control_stable = 0
        stable_reads = 1
        # poll for stable pattern
        for _ in range(0, 10):
          sleep(100000000ns)
          # read from REGISTER!!!!
          dl_rx_control = *(P9_FBC_ABUS_LINK_CTL_ARR[link_index].dl_control_addr+7)[target]
          if (dl_rx_control & 0xFFF0000) == rx_control_stable:
            stable_reads++
            if stable_reads == 3:
              break
          else:
            rx_control_stable = (dl_rx_control & 0xFFF0000)
            stable_reads = 1

        # apply PHY TX inversions only if needed
        if phase == 0 and ((dl_rx_control & 0xFFF0000) != 0xFFE0000):
          for lane_index in range(0, 11):
            # set PHY TX lane address, start at:
            # - PHY lane 0 for even (work up)
            # - PHY lane 23 for odd (work down) DD1.0

            # REGISTER address
            phy_tx_mode1_pl_addr = 0x8004040009010c3f
            phy_tx_mode1_pl_addr |= lane_index << 32

            # read DL RX per-lane lock indicator bit
            # if locked: do nothing
            # if not locked: apply lane-invert to associated PHY TX side
            if not dl_rx_control & (1 << (36+lane_index)):
              # REGISTER!!!
              *phy_tx_mode1_pl_addr[remote_target] |= 1 << 49
        if (dl_rx_control & 0xFFF0000) == 0xFFE0000:
          break
      # enable link startup
      P9_FBC_ABUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |= 1 << 0
      # disable run lane override
      P9_FBC_ABUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |= 1 << 5
      # clear TX lane control override, set to ENABLED
      P9_FBC_ABUS_LINK_CTL_ARR[link_index].dl_control_addr[target]+5 = 0
    if odd:
      # force assertion of run_lane
      P9_FBC_ABUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |= 1 << 37
      # ensure that DL RX sees lane lock

      # loop a maximum of two times to determine lane lock status
      # 1st check is prior to application of any PHY TX inversions
      # if any lanes do not lock, apply inversions and check again
      # assert if any lane is unlocked at this point
      for phase in range(0, 2):
        rx_control_stable = 0
        stable_reads = 1
        # poll for stable pattern
        for _ in range(0, 10):
          sleep(100000000ns)
          # read from REGISTER!!!!
          dl_rx_control = *(control.dl_control_addr+8)[target]
          if (dl_rx_control & 0xFFF0000) == rx_control_stable:
            stable_reads++
            if stable_reads == 3:
              break
          else:
            rx_control_stable = (dl_rx_control & 0xFFF0000)
            stable_reads = 1

        # apply PHY TX inversions only if needed
        if phase == 0 and ((dl_rx_control & 0xFFF0000) != 0xFFE0000):
          for lane_index in range(0, 11):
            # set PHY TX lane address, start at:
            # - PHY lane 0 for even (work up)
            # - PHY lane 23 for odd (work down) DD1.0

            # REGISTER address
            phy_tx_mode1_pl_addr = 0x8004040009010c3f
            phy_tx_mode1_pl_addr |= (23-lane_index) << 32

            # read DL RX per-lane lock indicator bit
            # if locked: do nothing
            # if not locked: apply lane-invert to associated PHY TX side
            if not dl_rx_control & (1 << (36+lane_index)):
              # REGISTER!!!
              *phy_tx_mode1_pl_addr[remote_target] |= 1 << 49
        if (dl_rx_control & 0xFFF0000) == 0xFFE0000:
          break
      # enable link startup
      P9_FBC_ABUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |= 33
      # disable run lane override
      P9_FBC_ABUS_LINK_CTL_ARR[link_index].dl_control_addr[target] |= 37
      # clear TX lane control override, set to ENABLED
      control.dl_control_addr[target]+6 = 0
  # CQ: HW453889 :: MFG Abus Stress >>>
  # Use rx_pr_data_a_offset to shift the offset by +1/2 or +2/4
  if even and (fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_EVEN[target] != 0)
    for lane_index in range(0, 11):
      # set PHY TX lane address, start at:
      # - PHY lane 0 for even (work up)
      # - PHY lane 23 for odd (work down)

      # REGISTER address!!!
      # OBUS_RX0_RXPACKS0_SLICE0_RX_BIT_CNTL3_EO_PL
      # src/import/chips/p9/procedures/hwp/nest/p9_smp_link_layer.C:278
      l_phy_rx_bit_cntl3_eo_pl_addr = 0x8002500009010c3f | (lane_index << 32)

      # REGISTER read!!!
      l_phy_rx_bit_cntl3_eo_pl = l_phy_rx_bit_cntl3_eo_pl_addr[target]
      l_phy_rx_bit_cntl3_eo_pl &= ~((0x3F << 48) | (0x3F << 54))
      l_phy_rx_bit_cntl3_eo_pl |= ((0x3F & fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_EVEN[target]) << 48)
                                | ((0x3F & fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_EVEN[target]) << 54)
      # REGISTER write!!!
      l_phy_rx_bit_cntl3_eo_pl_addr[target] = l_phy_rx_bit_cntl3_eo_pl
  if odd and (fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_ODD[target] != 0):
    for lane_index in range(0, 11):
      # set PHY TX lane address, start at:
      # - PHY lane 0 for even (work up)
      # - PHY lane 23 for odd (work down)

      # REGISTER address!!!
      # OBUS_TX0_TXPACKS0_SLICE0_TX_MODE1_PL
      # src/import/chips/p9/procedures/hwp/nest/p9_smp_link_layer.C:278
      l_phy_rx_bit_cntl3_eo_pl_addr = 0x8002500009010c3f | ((23-lane_index) << 32)

      # REGISTER read
      l_phy_rx_bit_cntl3_eo_pl = l_phy_rx_bit_cntl3_eo_pl_addr[target]
      l_phy_rx_bit_cntl3_eo_pl &= ~((0x3F << 48) | (0x3F << 54))
      l_phy_rx_bit_cntl3_eo_pl |= ((0x3F & fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_ODD[target]) << 48)
                                | ((0x3F & fapi2::ATTR_IO_O_MFG_STRESS_PR_OFFSET_ODD[target]) << 54)
      # REGISTER write
      l_phy_rx_bit_cntl3_eo_pl_addr[target] = l_phy_rx_bit_cntl3_eo_pl
```

## 9.7 proc_fab_iovalid: Lower functional fences on local SMP
### p9_fab_iovalid.C(chip target)
* Reads logical A/X link config, sets iovalid for selected links
* Only performed on trained, valid buses
* After this point a checkstop on a slave will checkstop master
* Reads the A/X link delays for later HWP to pick best link for
coherent traffic
```
fapi2::ReturnCode
p9_fab_iovalid(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
               const bool True,
               const bool True,
               const bool i_manage_optical,
               std::vector<fapi2::ReturnCode>& o_obus_dl_fail_rcs)
{
  # logical link (X/A) configuration parameters
  # arrays indexed by link ID on local end
  # enable on local end
  uint8_t l_x_en[7]
  uint8_t l_a_en[4]
  # link ID on remote end
  uint8_t l_x_rem_link_id[7]
  uint8_t l_a_rem_link_id[4]
  # aggregate (local+remote) delays
  uint32_t l_x_agg_link_delay[7]
  uint32_t l_a_agg_link_delay[4]

  # seed arrays with attribute values
  l_x_en              = fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[target]
  l_x_rem_link_id     = fapi2::ATTR_PROC_FABRIC_X_ATTACHED_LINK_ID[target]
  l_x_agg_link_delay  = fapi2::ATTR_PROC_FABRIC_X_LINK_DELAY[target]
  l_a_en              = fapi2::ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG[target]
  l_a_rem_link_id     = fapi2::ATTR_PROC_FABRIC_A_ATTACHED_LINK_ID[target]
  l_a_agg_link_delay  = fapi2::ATTR_PROC_FABRIC_A_LINK_DELAY[target]

  # Add delay for dd1.1+ procedure to compensate for lack of lane lock polls
  sleep(100000000ns)

  for l_link_id in range(0, 7):
    if l_x_en[l_link_id]:
      if P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].endp_type == ELECTRICAL
      or (i_manage_optical and (P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].endp_type == OPTICAL)):

        if P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].endp_type == ELECTRICAL:
          l_rc = p9_fab_iovalid_link_validate<fapi2::TARGET_TYPE_XBUS>(
            i_target,
            P9_FBC_XBUS_LINK_CTL_ARR[l_link_id],
            P9_FBC_XBUS_LINK_CTL_ARR[l_x_rem_link_id[l_link_id]])
        else:
          l_rc = p9_fab_iovalid_link_validate<fapi2::TARGET_TYPE_OBUS>(
            i_target,
            P9_FBC_XBUS_LINK_CTL_ARR[l_link_id],
            P9_FBC_XBUS_LINK_CTL_ARR[l_x_rem_link_id[l_link_id]])

          if fapi2::ATTR_CHIP_EC_FEATURE_HW446279_USE_PPE[i_target]:
            # At this point, both halves of the SMP ABUS have completed training and
            # we will kick off the ppe if we need HW446279_USE_PPE

            # p9_fab_iovalid_enable_ppe()
            # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:889
            # obtain link endpoints for FFDC
            for child_target in i_target.getChildren(TYPE_OBUS):
              if  P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].endp_type == TYPE_OBUS
              and P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].endp_unit_id == fapi2::ATTR_CHIP_UNIT_POS[child_target]:
                # REGISTER write
                # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:913
                # OBUS_PPE_XCR_ADDR = 0x9011050
                OBUS_PPE_XCR_ADDR[child_target] = 0x6000000000000000
                OBUS_PPE_XCR_ADDR[child_target] = 0x2000000000000000
                break

        # form data buffers for iovalid/RAS FIR mask updates
        l_iovalid_mask = 0
        if (l_x_en[l_link_id] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE)
        or (l_x_en[l_link_id] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_EVEN_ONLY):
            l_iovalid_mask |= 1 << P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].iovalid_field_start_bit

        if (l_x_en[l_link_id] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE)
        or (l_x_en[l_link_id] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_ODD_ONLY):
            l_iovalid_mask |= 1 << (P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].iovalid_field_start_bit + 1)

        l_fbc_cent_fir_data = PU_PB_CENT_SM0_PB_CENT_FIR_REG[i_target]
        # clear RAS FIR mask for optical link, or electrical link if not already setup by SBE
        if (P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].endp_type == OPTICAL
        and not l_fbc_cent_fir_data & PU_PB_CENT_SM0_PB_CENT_FIR_MASK_REG_SPARE_14
        or (P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].endp_type == ELECTRICAL
        and not l_fbc_cent_fir_data & PU_PB_CENT_SM0_PB_CENT_FIR_MASK_REG_SPARE_13:
            # get the value of the action registers, clear the bit and write it
            # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:1190
            PU_PB_CENT_SM1_EXTFIR_ACTION0_REG[i_target] &= ~P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].ras_fir_field_bit
            PU_PB_CENT_SM1_EXTFIR_ACTION1_REG[i_target] &= ~i_P9_FBC_XBUS_LINK_CTL_ARR[l_link_id]ctl.ras_fir_field_bit

            # clear associated mask bit
            # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:1204
            PU_PB_CENT_SM1_EXTFIR_MASK_REG_AND[i_target] = 0xFFFFFFFFFFFFFFFF & ~P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].ras_fir_field_bit

        # use AND/OR mask registers to atomically update link specific fields
        # in iovalid control register
        # REGISTER write
        P9_FBC_XBUS_LINK_CTL_ARR[l_link_id].iovalid_or_addr[i_target] = l_iovalid_mask

        # This value is probably result of a bug
        # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:1021
        # called at
        # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:1110
        # o_link_delay should be an output parameter,
        # but it is not a pointer nor a reference
        l_x_agg_link_delay[l_link_id] = 0x1FFE

  for l_link_id in range(0, 4):
    if l_a_en[l_link_id]:
      if  i_manage_optical
      and P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].endp_type == OPTICAL:
        l_rc = p9_fab_iovalid_link_validate<T>(
          i_target,
          P9_FBC_ABUS_LINK_CTL_ARR[l_link_id],
          P9_FBC_ABUS_LINK_CTL_ARR[l_a_rem_link_id[l_link_id]])

        if T == fapi2::TARGET_TYPE_OBUS
        and fapi2::ATTR_CHIP_EC_FEATURE_HW446279_USE_PPE[i_target]:
          # At this point, both halves of the SMP ABUS have completed training and
          # we will kick off the ppe if we need HW446279_USE_PPE

          # p9_fab_iovalid_enable_ppe()
          # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:889
          # obtain link endpoints for FFDC
          for child_target in i_target.getChildren(TYPE_OBUS):
            if  P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].endp_type == TYPE_OBUS
            and P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].endp_unit_id == fapi2::ATTR_CHIP_UNIT_POS[child_target]:
              # REGISTER write
              # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:913
              # OBUS_PPE_XCR_ADDR = 0x9011050
              OBUS_PPE_XCR_ADDR[child_target] = 0x6000000000000000
              OBUS_PPE_XCR_ADDR[child_target] = 0x2000000000000000
              break

        l_iovalid_mask = 0
        if (l_a_en[l_link_id] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE)
        or (l_a_en[l_link_id] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_EVEN_ONLY):
            l_iovalid_mask |= 1 << P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].iovalid_field_start_bit

        if (l_a_en[l_link_id] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_TRUE)
        or (l_a_en[l_link_id] == fapi2::ENUM_ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG_ODD_ONLY):
            l_iovalid_mask |= 1 << (P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].iovalid_field_start_bit + 1)

        l_fbc_cent_fir_data = PU_PB_CENT_SM0_PB_CENT_FIR_REG[i_target]

        # clear RAS FIR mask for optical link, or electrical link if not already setup by SBE
        if (P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].endp_type == OPTICAL
        and not l_fbc_cent_fir_data & PU_PB_CENT_SM0_PB_CENT_FIR_MASK_REG_SPARE_14
        or (P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].endp_type == ELECTRICAL
        and not l_fbc_cent_fir_data & PU_PB_CENT_SM0_PB_CENT_FIR_MASK_REG_SPARE_13:
            # get the value of the action registers, clear the bit and write it
            # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:1190
            PU_PB_CENT_SM1_EXTFIR_ACTION0_REG[i_target] &= ~P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].ras_fir_field_bit
            PU_PB_CENT_SM1_EXTFIR_ACTION1_REG[i_target] &= ~P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].ras_fir_field_bit

            # clear associated mask bit
            # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:1204
            PU_PB_CENT_SM1_EXTFIR_MASK_REG_AND[i_target] = 0xFFFFFFFFFFFFFFFF & ~P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].ras_fir_field_bit

        # use AND/OR mask registers to atomically update link specific fields
        # in iovalid control register
        # REGISTER write
        P9_FBC_ABUS_LINK_CTL_ARR[l_link_id].iovalid_or_addr[i_target] = l_iovalid_mask

        # This value is probably result of a bug
        # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:1021
        # called at
        # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:1110
        # o_link_delay should be an output parameter,
        # but it is not a pointer nor a reference
        l_x_agg_link_delay[l_link_id] = 0x1FFE

  # update link delay attributes
  fapi2::ATTR_PROC_FABRIC_X_LINK_DELAY[i_target] = l_x_agg_link_delay
  fapi2::ATTR_PROC_FABRIC_A_LINK_DELAY[i_target] = l_a_agg_link_delay
}

#/ @brief Validate DL/TL link layers are trained
#/
#/ @param[in]  i_target          Processor chip target
#/ @param[in]  i_loc_link_ctl    X/A link control structure for link local end
#/ @param[in]  i_rem_link_ctl    X/A link control structure for link remote end
#/
#/ @return fapi2::ReturnCode. FAPI2_RC_SUCCESS if success, else error code.
template<fapi2::TargetType T>
fapi2::ReturnCode p9_fab_iovalid_link_validate(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_target,
    const p9_fbc_link_ctl_t& i_loc_link_ctl,
    const p9_fbc_link_ctl_t& i_rem_link_ctl)
{
  l_dl_status_reg = None
  l_dl_trained = False
  l_dl_status_even = 0
  l_dl_prior_status_even = 0
  l_dl_fail_even = False
  l_dl_status_odd = 0
  l_dl_prior_status_odd = 0
  l_dl_fail_odd = False

  for children_target in i_target.getChildren<T>():
    if (static_cast<fapi2::TargetType>(i_loc_link_ctl.endp_type) == T)
    and (i_loc_link_ctl.endp_unit_id == fapi2::ATTR_CHIP_UNIT_POS[children_target]):
      l_loc_endp_target = children_target
      break;

  l_loc_link_train = fapi2::ATTR_LINK_TRAIN[l_loc_endp_target]

  # poll for DL trained indications
  while not l_dl_trained:
    # validate DL training state
    l_dl_fir_reg = i_loc_link_ctl.dl_fir_addr[i_target]
    l_dl_status_reg = i_loc_link_ctl.dl_status_addr[i_target]
    # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:636
    l_dl_status_even        = (l_dl_status_reg & (0x0F <<  4)) >> 4
    l_dl_prior_status_even  = (l_dl_status_reg & (0x0F << 12)) >> 12
    l_dl_status_odd         = (l_dl_status_reg & (0x0F << 28)) >> 28
    l_dl_prior_status_odd   = (l_dl_status_reg & (0x0F << 36)) >> 36

    if l_loc_link_train == fapi2::ENUM_ATTR_LINK_TRAIN_BOTH:
      l_dl_trained = (l_dl_fir_reg & DL_FIR_LINK0_TRAINED_BIT)
                 and (l_dl_fir_reg & DL_FIR_LINK1_TRAINED_BIT)
      if not l_dl_trained:
        l_dl_fail_even =
          not ((((l_dl_status_even == 0x8) or (l_dl_prior_status_even == 0x8) or (l_dl_status_even == 0x9) or (l_dl_prior_status_even == 0x9))
          and ((l_dl_status_odd  >= 0xB) and (l_dl_status_odd  <= 0xE))) or ((l_dl_status_even == 0x2) and ((l_dl_status_odd  >= 0x8) and (l_dl_status_odd  <= 0xC))))
        l_dl_fail_odd =
          not ((((l_dl_status_odd == 0x8) or (l_dl_prior_status_odd == 0x8) or (l_dl_status_odd == 0x9) or (l_dl_prior_status_odd == 0x9))
          and ((l_dl_status_even >= 0xB) and (l_dl_status_even <= 0xE))) or ((l_dl_status_odd == 0x2) and ((l_dl_status_even >= 0x8) and (l_dl_status_even <= 0xC))))
    elif l_loc_link_train == fapi2::ENUM_ATTR_LINK_TRAIN_EVEN_ONLY:
      l_dl_trained   = (l_dl_fir_reg & (1 << DL_FIR_LINK0_TRAINED_BIT)) == 0
      l_dl_fail_even = not l_dl_trained
      l_dl_fail_odd  = True
    else:
      l_dl_trained   = (l_dl_fir_reg & (1 << DL_FIR_LINK1_TRAINED_BIT)) == 0
      l_dl_fail_even = True
      l_dl_fail_odd  = not l_dl_trained

    if not l_dl_trained:
      sleep(1000000ns)

  # OBUS DL reported trained, need to validate that no lane sparing occurred
  # in some cases, a spare may occur but not report in the FIR
  #
  # as we are not persisting bad lane information, we dont want to fail the
  # IPL directly if a single spare occurs, but can raise a FIR to indicate that the
  # spare has been consumed (MFG may choose to fail based on this criteria)
  #
  # if more than one spare is detected, mark the link as failed
  if l_dl_trained and (T == fapi2::TARGET_TYPE_OBUS):
    l_dl_fail_by_lane_status = False
    if not l_dl_fail_even and not l_dl_fail_odd:
      if not l_dl_fail_even:
        # REGISTER address
        l_dl_rx_control_addr = i_loc_link_ctl.dl_control_addr+7
        l_lane_failed     = (l_dl_rx_control_addr[i_target] & (0x7FF << 48)) >> 48
        l_lane_not_locked = (l_dl_rx_control_addr[i_target] & (0x7FF << 36)) >> 36
      # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:537
      l_lane_failed_count = count_bits_set(l_lane_failed)
      l_lane_not_locked_count = count_bits_set(l_lane_not_locked)
      if not ((l_lane_failed_count == 0) and (l_lane_not_locked_count == 0)):
        if (l_lane_failed_count <= 1) and (l_lane_not_locked_count <= 1):
          if i_even_not_odd:
            l_dl_fir |= 1 << 44
          else:
            l_dl_fir |= 1 << 45
          # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:560
          # REGISTER write
          i_loc_link_ctl.dl_fir_addr |= l_dl_fir
        else:
          l_dl_fail_by_lane_status = True
      if l_dl_fail_odd and l_dl_fail_by_lane_status:
        l_dl_trained = False
        l_dl_fail_even = True

      if not l_dl_fail_odd:
        # REGISTER address
        l_dl_rx_control_addr = i_loc_link_ctl.dl_control_addr+8)
        l_lane_failed     = (l_dl_rx_control_addr[i_target] & (0x7FF << 48)) >> 48
        l_lane_not_locked = (l_dl_rx_control_addr[i_target] & (0x7FF << 36)) >> 36
      # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:537
      l_lane_failed_count = count_bits_set(l_lane_failed)
      l_lane_not_locked_count = count_bits_set(l_lane_not_locked)
      if not ((l_lane_failed_count == 0) and (l_lane_not_locked_count == 0)):
        if (l_lane_failed_count <= 1) and (l_lane_not_locked_count <= 1):
          if i_even_not_odd:
            l_dl_fir |= 1 << 44
          else:
            l_dl_fir |= 1 << 45
          # src/import/chips/p9/procedures/hwp/nest/p9_fab_iovalid.C:560
          # REGISTER write
          i_loc_link_ctl.dl_fir_addr |= l_dl_fir
        else:
          l_dl_fail_by_lane_status = True
      if l_dl_fail_by_lane_status:
        l_dl_trained = False
        l_dl_fail_odd = True

  # control reconfig loop behavior
  if not l_dl_trained:
    if l_loc_link_train == fapi2::ENUM_ATTR_LINK_TRAIN_BOTH
    and T == fapi2::TARGET_TYPE_OBUS
    and ((l_dl_fail_even and not l_dl_fail_odd) or (not l_dl_fail_even and l_dl_fail_odd)):
      if l_dl_fail_even:
        fapi2::ATTR_LINK_TRAIN[l_loc_endp_target] = fapi2::ENUM_ATTR_LINK_TRAIN_ODD_ONLY
      else
        fapi2::ATTR_LINK_TRAIN[l_loc_endp_target] = fapi2::ENUM_ATTR_LINK_TRAIN_EVEN_ONLY
    else
      fapi2::ATTR_LINK_TRAIN[l_loc_endp_target] = fapi2::ENUM_ATTR_LINK_TRAIN_NONE
    return
}
```

## 9.8 host_fbc_eff_config_aggregate: Pick link(s) for coherency
### p9_fbc_eff_config_aggregate.C(chip target)
* Reads attributes from previous HWP and determines per-link address/data
capabilities
* Sets up attributes for build SMP

```
foreach target in processorChips:
  # read attributes for this chip
  l_x_en                = fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_CNFG[target]
  l_x_rem_link_id       = fapi2::ATTR_PROC_FABRIC_X_ATTACHED_LINK_ID[target]
  l_x_rem_fbc_chip_id   = fapi2::ATTR_PROC_FABRIC_X_ATTACHED_CHIP_ID[target]
  l_x_agg_link_delay    = fapi2::ATTR_PROC_FABRIC_X_LINK_DELAY[target]
  l_x_addr_dis          = fapi2::ATTR_PROC_FABRIC_X_ADDR_DIS[target]
  l_x_aggregate         = fapi2::ATTR_PROC_FABRIC_X_AGGREGATE[target]
  l_a_en                = fapi2::ATTR_PROC_FABRIC_A_ATTACHED_CHIP_CNFG[target]
  l_a_rem_link_id       = fapi2::ATTR_PROC_FABRIC_A_ATTACHED_LINK_ID[target]
  l_a_rem_fbc_group_id  = fapi2::ATTR_PROC_FABRIC_A_ATTACHED_CHIP_ID[target]
  l_a_agg_link_delay    = fapi2::ATTR_PROC_FABRIC_A_LINK_DELAY[target]
  l_a_addr_dis          = fapi2::ATTR_PROC_FABRIC_A_ADDR_DIS[target]
  l_a_aggregate         = fapi2::ATTR_PROC_FABRIC_A_AGGREGATE[target]

  l_loc_fbc_chip_id   = fapi2::ATTR_PROC_FABRIC_CHIP_ID[target]
  l_loc_fbc_group_id  = fapi2::ATTR_PROC_FABRIC_GROUP_ID[target]
  l_pump_mode         = fapi2::ATTR_PROC_FABRIC_PUMP_MODE[FAPI_SYSTEM]

  # calculate aggregate configuration
  # p9_fbc_eff_config_aggregate_link_setup()
  # src/import/chips/p9/procedures/hwp/nest/p9_fbc_eff_config_aggregate.C:61
  #
  # mark number of links targeting each fabric ID
  # set output defaults to disable aggregate mode (all links carry coherent traffic)
  l_fbc_id_active_count[8] = []

  for l_loc_id in range(0, 7):
    # if link is valid, bump fabric ID usage count
    if l_x_en[l_loc_link_id]:
      l_fbc_id_active_count[l_x_rem_fbc_chip_id[l_loc_link_id]]++
    l_x_addr_dis[l_loc_link_id] = 0

  l_x_aggregate = 0

  # set aggregate mode if more than one link is pointed at the same remote
  # fabric ID
  for l_rem_fbc_id in range(0, 8)
    if l_fbc_id_active_count[l_rem_fbc_id] > 1:
      l_x_aggregate = 1

      # flip default value for link address disable
      for l_loc_link_id in range(0, 7)
      if(l_x_en[l_loc_link_id])
        l_x_addr_dis[l_loc_link_id] = 1
      else:
        l_x_addr_dis[l_loc_link_id] = 0

      # scan link delays for smallest value
      # looks for minimal value
      uint32_t l_loc_coherent_link_delay = 0xFFFFFFFF
      for l_loc_link_id in range(0, 7):
        if l_x_en[l_loc_link_id]
        and l_x_agg_link_delay[l_loc_link_id] < l_loc_coherent_link_delay:
          l_loc_coherent_link_delay = l_x_agg_link_delay[l_loc_link_id]

      # determine if more than one link matches the minimum delay
      l_matches = 0
      l_loc_coherent_link_id = 0xFF
      for l_loc_link_id in range(0, 7):
        if l_x_en[l_loc_link_id]
        and (l_x_agg_link_delay[l_loc_link_id] == l_loc_coherent_link_delay):
          l_matches++
          l_loc_coherent_link_id = l_loc_link_id

      # ties must be broken consistenty on both connected chips (i.e., we
      # need to pick both ends of the same link to carry coherency
      # select link with lowest link ID number on chip with smaller fabric ID
      if l_matches != 1:
        if (fapi2::ATTR_PROC_FABRIC_PUMP_MODE[FAPI_SYSTEM] == fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_NODE and l_loc_fbc_chip_id  < l_rem_fbc_id)
        or (fapi2::ATTR_PROC_FABRIC_PUMP_MODE[FAPI_SYSTEM] != fapi2::ENUM_ATTR_PROC_FABRIC_PUMP_MODE_CHIP_IS_NODE and l_loc_fbc_group_id < l_rem_fbc_id)
          # local fabric ID is smaller than remote
          # looks for minimal value
          for l_loc_link_id in range(0, 7):
            if l_x_en[l_loc_link_id]
            and l_x_agg_link_delay[l_loc_link_id] == l_loc_coherent_link_delay:
              l_loc_coherent_link_id = l_loc_link_id
              break
        else
          # remote fabric ID is smaller than local
          # looks for minimal value
          l_rem_coherent_link_id = 0xFF
          for l_loc_link_id in range(0, 7)
            if l_x_en[l_loc_link_id]
            and l_x_agg_link_delay[l_loc_link_id] == l_loc_coherent_link_delay
            and l_x_rem_link_id[l_loc_link_id] < l_rem_coherent_link_id:
              l_rem_coherent_link_id = l_x_rem_link_id[l_loc_link_id]
              l_loc_coherent_link_id = l_loc_link_id
      l_x_addr_dis[l_loc_coherent_link_id] = 0

  # calculate aggregate configuration
  # p9_fbc_eff_config_aggregate_link_setup()
  # src/import/chips/p9/procedures/hwp/nest/p9_fbc_eff_config_aggregate.C:61
  #
  # mark number of links targeting each fabric ID
  # set output defaults to disable aggregate mode (all links carry coherent traffic)
  l_fbc_id_active_count[8] = []

  for l_loc_id in range(0, 4):
    # if link is valid, bump fabric ID usage count
    if l_a_en[l_loc_link_id]:
      l_fbc_id_active_count[l_a_rem_fbc_group_id[l_loc_link_id]]++
    l_a_addr_dis[l_loc_link_id] = 0

  l_a_aggregate = 0

  # set aggregate mode if more than one link is pointed at the same remote
  # fabric ID
  for l_rem_fbc_id in range(0, 8)
    if l_fbc_id_active_count[l_rem_fbc_id] > 1:
      l_a_aggregate = 1

      # flip default value for link address disable
      for l_loc_link_id in range(0, 4)
        if l_a_en[l_loc_link_id]:
          l_a_addr_dis[l_loc_link_id] = 1
        else:
          l_a_addr_dis[l_loc_link_id] = 0

      # scan link delays for smallest value
      # looks for minimal value
      uint32_t l_loc_coherent_link_delay = 0xFFFFFFFF
      for l_loc_link_id in range(0, 4):
        if l_a_en[l_loc_link_id]
        and l_a_agg_link_delay[l_loc_link_id] < l_loc_coherent_link_delay:
          l_loc_coherent_link_delay = l_a_agg_link_delay[l_loc_link_id]

      # determine if more than one link matches the minimum delay
      l_matches = 0
      l_loc_coherent_link_id = 0xFF
      for l_loc_link_id in range(0, 4):
        if l_a_en[l_loc_link_id]
        and (l_a_agg_link_delay[l_loc_link_id] == l_loc_coherent_link_delay):
          l_matches++
          l_loc_coherent_link_id = l_loc_link_id

      # ties must be broken consistenty on both connected chips (i.e., we
      # need to pick both ends of the same link to carry coherency
      # select link with lowest link ID number on chip with smaller fabric ID
      if l_matches != 1:
        if l_loc_fbc_group_id < l_rem_fbc_id:
          # local fabric ID is smaller than remote
          # looks for minimal value
          for l_loc_link_id in range(0, 4):
            if l_a_en[l_loc_link_id]
            and (l_a_agg_link_delay[l_loc_link_id] == l_loc_coherent_link_delay):
              l_loc_coherent_link_id = l_loc_link_id
              break
        else
          # remote fabric ID is smaller than local
          # looks for minimal value
          l_rem_coherent_link_id = 0xFF
          for l_loc_link_id in range(0, 4)
            if l_a_en[l_loc_link_id]
            and (l_a_agg_link_delay[l_loc_link_id] == l_loc_coherent_link_delay)
            and (l_a_rem_link_id[l_loc_link_id] < l_rem_coherent_link_id):
              l_rem_coherent_link_id = l_a_rem_link_id[l_loc_link_id]
              l_loc_coherent_link_id = l_loc_link_id
      l_a_addr_dis[l_loc_coherent_link_id] = 0

  # set attributes
  fapi2::ATTR_PROC_FABRIC_X_ADDR_DIS[target]  = l_x_addr_dis
  fapi2::ATTR_PROC_FABRIC_X_AGGREGATE[target] = l_x_aggregate
  fapi2::ATTR_PROC_FABRIC_A_ADDR_DIS[target]  = l_a_addr_dis
  fapi2::ATTR_PROC_FABRIC_A_AGGREGATE[target] = l_a_aggregate
```
