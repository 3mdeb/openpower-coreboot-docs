# 9.4 fabric_io_run_training: Run training on internal buses
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

```python
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
