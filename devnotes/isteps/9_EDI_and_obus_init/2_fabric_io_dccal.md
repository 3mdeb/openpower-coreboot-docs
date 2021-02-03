# 9.2 fabric_io_dccal: Calibrate Fabric interfaces
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

```python
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
                    # dont know whats the diffrence between _P and _N register
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
                target = connection.convertToTarget()
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
