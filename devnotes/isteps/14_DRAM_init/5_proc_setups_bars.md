# 14.5 proc_setup_bars: Setup Memory BARs

> a) p9_mss_setup_bars.C (proc chip) -- Nimbus
> b) p9c_mss_setup_bars.C (proc chip) -- Cumulus
>    - Same HWP interface for both Nimbus and Cumulus, input target is
>      TARGET_TYPE_PROC_CHIP; HWP is to figure out if target is a Nimbus (MCS)
>      or Cumulus (MI) internally.
>    - Prior to setting the memory bars on each processor chip, this procedure
>      needs to set the centaur security protection bit
>      - TCM_CHIP_PROTECTION_EN_DC is SCOM Addr 0x03030000
>      - TCN_CHIP_PROTECTION_EN_DC is SCOM Addr 0x02030000
>      - Both must be set to protect Nest and Mem domains
>    - Based on system memory map
>      - Each MCS has its mirroring and non mirrored BARs
>      - Set the correct checkerboard configs. Note that chip flushes to
>        checkerboard
>      - need to disable memory bar on slave otherwise base flush values will
>        ack all memory accesses
> c) p9_setup_bars.C
>    - Sets up Powerbus/MCD, L3 BARs on running core
>      - Other cores are setup via winkle images
>    - Setup dSMP and PCIe Bars
>      - Setup PCIe outbound BARS (doing stores/loads from host core)
>        - Addresses that PCIE responds to on powerbus (PCI init 1-7)
>      - Informing PCIe of the memory map (inbound)
>        - PCI Init 8-15
>    - Set up Powerbus Epsilon settings
>      - Code is still running out of L3 cache
>      - Use this procedure to setup runtime epsilon values
>      - Must be done before memory is viable

```
// No loop for different targets yet, first function runs only on master PROC

// Start MCS reset
// Reset memory controller configuration written by SBE
// Close the MCS acker before enabling the real memory bars
p9_revert_sbe_mcs_setup():
  revert_mc_hb_dcbz_config():
    for each present MCS:       // not only functional, all MCSs
      if (TP.TCN1.N3.CPLT_CTRL1[10] == 0):  // that was for MCS0/1, use TP.TCN1.N1.CPLT_CTRL1[9] for MCS2/3 instead,
                                            // 0x05000001 and 0x03000001 respectively
        // MCFGP -- mark BAR invalid & reset grouping configuration fields
        MCS_n_MCFGP                         // undocumented, 0x0501080A, 0x0501088A, 0x0301080A, 0x0301088A for MCS{0-3}
            [0]     VALID =                                 0
            [1-4]   MC_CHANNELS_PER_GROUP =                 0
            [5-7]   CHANNEL_0_GROUP_MEMBER_IDENTIFICATION = 0   // CHANNEL_1_GROUP_MEMBER_IDENTIFICATION not cleared?
            [13-23] GROUP_SIZE =                            0

        // MCMODE1 -- enable speculation, cmd bypass, fp command bypass
        MCS_n_MCMODE1                       // undocumented, 0x05010812, 0x05010892, 0x03010812, 0x03010892
            [32]    DISABLE_ALL_SPEC_OPS =      0
            [33-51] DISABLE_SPEC_OP =           0x40    // bit 45 (called DCBF_BIT in code) set because of HW414958
            [54-60] DISABLE_COMMAND_BYPASS =    0
            [61]    DISABLE_FP_COMMAND_BYPASS = 0

        // MCS_MCPERF1 -- enable fast path
        MCS_n_MCPERF1                       // undocumented, 0x05010810, 0x05010890, 0x03010810, 0x03010890
            [0]     DISABLE_FASTPATH =  0

        // Re-mask MCFIR. We want to ensure all MCSs are masked until the BARs are opened later during IPL.
        MCS_n_MCFIRMASK_OR                  // undocumented, 0x05010805, 0x05010885, 0x03010805, 0x03010885
            [all]   1

for each functional PROC:
  p9_mss_setup_bars():
    // Skipping Axone/Cumulus
    validateGroupData():
      // This and following functions use ATTR_MSS_MCS_GROUP_32 generated in istep 7.4. This ATTR holds group data: port
      // sizes and IDs, base addresses, sizes and base addresses for Alt and SMF memory. The layout of this ATTR is
      // described in import/chips/p9/procedures/xml/attribute_info/nest_attributes.xml:1128.
      //
      // Perhaps it would make more sense to calculate all that data now, after DRAM is trained and possibly one or more
      // DIMMs is put out of commission. On the other hand, that data may be necessary for earlier isteps, in which case
      // it may not be possible to delay it until now. No other code uses ATTR_MSS_MCS_GROUP_32 directly, but a piece of
      // it or something based on it (e.g. base addresses and sizes) are saved in a different ATTR.
      // FIXME: check analysis of previous steps and see if the above is possible
      //
      // This function only asserts that:
      // - for each MCS, sum of memory under its MCAs is equal to the size specified by ATTR (non-mirrored part only?)
      //   - data was aggregated from DIMMs through MCA, MCS into groups, and now is being split back and iterated again
      //     to test for current MCS position; maybe different layout would be better?
      //   - both approaches start from SPD, the only reason why they could be different is an error in the code (unless
      //     cache suffers from a hardware issue, but then we have bigger problems)
      // - any of the port IDs appears at most in one group.
      //
      // The function only tests ATTRs set by earlier code, it does not touch any registers, so it is kind of no-op from
      // the hardware's point of view. Alas, no detailed description here.

    buildMCBarData():
      // Assuming mirroring is disabled. There is no code in Hostboot that enables it, according to the comment in
      // import/chips/p9/procedures/hwp/nest/p9_mss_setup_bars.C:1505 it is always disabled on Nimbus.
      for each functional MCS:
        l_MCA[] = getPortData():
          // l_MCA has 2 members, there are 2 MCAs per MCS. ATTR_MSS_MCS_GROUP_32 holds MCA numbers (absolute), to get
          // MCS position divide that number (as integer) by 2. MCA number relative to MCS is absolute MCA number % 2.
          //
          // Loop through non-mirrored part of ATTR_MSS_MCS_GROUP_32
          for i = 0..7:
            for j = 4..11:        // 4-11 are port IDs in group, iterate over all
            if ((ATTR_MSS_MCS_GROUP_32[i][j] / 2) == MCS pos)
              ATTR = ATTR_MSS_MCS_GROUP_32[i]
              port = ATTR_MSS_MCS_GROUP_32[i][j] % 2
              // Except for .myGroup, .channelId and a hole in [4-11] this is basically memcpy
              l_MCA[port].myGroup =         i
              l_MCA[port].numPortsInGroup = ATTR[1]
              l_MCA[port].groupSize =       ATTR[2]
              l_MCA[port].groupBaseAddr =   ATTR[3]
              l_MCA[port].channelId =       j - 4
              if (ATTR[12]):
                l_MCA[port].altMemValid[0] =  1             // maybe just copy it and don't bother with 'if'
                l_MCA[port].altMemSize[0] =   ATTR[14]
                l_MCA[port].altBaseAddr[0] =  ATTR[16]
              if (ATTR[13]):
                l_MCA[port].altMemValid[1] =  1
                l_MCA[port].altMemSize[1] =   ATTR[15]
                l_MCA[port].altBaseAddr[1] =  ATTR[17]
              if (ATTR[18]):
                l_MCA[port].smfMemValid[0] =  1
                l_MCA[port].smfMemSize[0] =   ATTR[19]
                l_MCA[port].smfBaseAddr[0] =  ATTR[20]

          // If odd port (port1) has memory and even port (port0) is empty,
          // and odd port is in a group of 2 (obviously with a cross-MCS port),
          // then program channel id for port0 (because HW looks for id at this
          // port), zero out port1's group id
          if (l_MCA[0].numPortsInGroup == 0 && l_MCA[1].numPortsInGroup == 2):
            l_MCA[0].channelId = l_MCA[1].channelId
            l_MCA[1].channelId = 0

        if (l_MCA[0].numPortsInGroup > 0 || l_MCA[1].numPortsInGroup > 0):
          // Build MCFGP/MCFGM data based on port group info
          //
          // Table for chan_per_group, reworked with 0->1 translation, sorted by encoded value:
          //  MCA0   MCA1  chan_per_group
          // {   0,     1,         0b0000  },   // 1 MC port/group for port1, port0 not populated
          // {   1,     0,         0b0000  },   // 1 MC port/group for port0, port1 not populated
          // {   1,     1,         0b0000  },   // 1 MC port/group for both port0 and port1
          // {   0,     3,         0b0001  },   // 3 MC port/group for port1, port0 not populated
          // {   3,     0,         0b0010  },   // 3 MC port/group for port0, port1 not populated
          // {   3,     1,         0b0010  },   // 3 MC port/group for port0, 1 MC port/group for port1
          // {   3,     3,         0b0011  },   // 3 MC port/group in the same MC port pairs
          // {   2,     0,         0b0100  },   // 2 MC port/group different MC port pairs, port1 not populated
          // {   0,     2,         0b0100  },   // 2 MC port/group different MC port pairs, port0 not populated
          // {   2,     2,         0b0100  },   // 2 MC port/group different MC port pairs
          // {   2,     2,         0b0101  },   // 2 MC port/group in the same MC port pairs
          // {   4,     4,         0b0110  },   // 4 MC ports/group, two ports in the same MC pairs
          // {   6,     6,         0b0111  },   // 6 MC ports/group, two ports in the same MC pairs
          // {   8,     8,         0b1000  },   // 8 MC ports/group, two ports in the same MC pairs
          //
          // Impossible values based on assumptions from 7.4:
          // {   1,     2,         0b0100  },   // If one port is in a group of 2, the other one must be
          // {   2,     1,         0b0100  },   // a) in the same group (2),
          // {   3,     2,         0b0100  },   // b) cross-MCS (2), or
          //                                    // c) empty (0).
          // {   1,     3,         0b0001  },   // Port1 can be in a group of 3 only if port0 is in the same group
          //                                    // or is empty.
          // {   2,     3,         0b0100  },   // Both of the above.
          // {   0,     0,         0b0000  },   // Should not enter "for each functional" loop.
          getNonMirrorBarData():
            // Separated different registers, but otherwise original field names preserved, including mixed underscores
            // and camel case.
            mcBarData
              MCFGP
                valid                     l_MCA[0].numPortsInGroup != 0
                chan_per_group            // encoded, depends on numPortsInGroup from both MCAs, see above
                chan0_group_member_id     l_MCA[0].channelId
                chan1_group_member_id     l_MCA[1].channelId
                group_size                (1 << (log2(l_MCA[0].groupSize) - 1)) - 1   // 4GB->0, 8GB->0b1, 16GB->0b11 ...
                groupBaseAddr             l_MCA[0].groupBaseAddr      (if valid)
              MCFGPM
                valid                     l_MCA[1].numPortsInGroup != 0 && chan_per_group < 0b0101
                group_size                (1 << (log2(l_MCA[1].groupSize) - 1)) - 1
                groupBaseAddr             l_MCA[1].groupBaseAddr      (if valid)
              MCFGPA
                HOLE_valid[2]             l_MCA[0].altMemValid[i]
                HOLE_LOWER_addr[2]        HOLE_valid[i] ? l_MCA[0].altBaseAddr[i] : 0
                HOLE_UPPER_addr[2]        HOLE_valid[i] ? (l_MCA[0].altBaseAddr[i] + lMCA[0].altMemSize[i]) : 0
                SMF_valid                 l_MCA[0].smfMemValid[i]
                SMF_LOWER_addr            SMF_valid[i] ? l_MCA[0].smfBaseAddr[i] : 0
                SMF_UPPER_addr            SMF_valid[i] ? (l_MCA[0].smfBaseAddr[i] + lMCA[0].smfMemSize[i]) : 0
              MCFGPMA
                HOLE_valid[2]             l_MCA[1].altMemValid[i]
                HOLE_LOWER_addr[2]        HOLE_valid[i] ? l_MCA[1].altBaseAddr[i] : 0
                HOLE_UPPER_addr[2]        HOLE_valid[i] ? (l_MCA[1].altBaseAddr[i] + lMCA[1].altMemSize[i]) : 0
                SMF_valid                 l_MCA[1].smfMemValid[i]
                SMF_LOWER_addr            SMF_valid[i] ? l_MCA[1].smfBaseAddr[i] : 0
                SMF_UPPER_addr            SMF_valid[i] ? (l_MCA[1].smfBaseAddr[i] + lMCA[1].smfMemSize[i]) : 0

            // mcBarData of each MCS is added to std::vector<std::pair<fapi2::Target<T>, mcBarData_t>> and returned from
            // buildMCBarData().

          // TODO: maybe we can call the following functions recursively here, i.e. inside "if (numPortsPerGroup > 0)"

    // One form of this function takes MCSs out of vector of pairs returned above and starts the second form in a loop.
    unmaskMCFIR():
      for each functional MCS present in any group:
        // Code doesn't set Action0
        MCS_MCFIRACT1                                     // undocumented, 0x05010807
            [all]   0
            [0]     MC_INTERNAL_RECOVERABLE_ERROR = 1
            [8]     COMMAND_LIST_TIMEOUT =          1

        MCS_MCFIRMASK (AND)                               // undocumented, 0x05010804
            [all]   1
            [0]     MC_INTERNAL_RECOVERABLE_ERROR =     0
            [1]     MC_INTERNAL_NONRECOVERABLE_ERROR =  0
            [2]     POWERBUS_PROTOCOL_ERROR =           0
            [4]     MULTIPLE_BAR =                      0
            [5]     INVALID_ADDRESS =                   0
            [8]     COMMAND_LIST_TIMEOUT =              0

    writeMCBarData():
      // Assuming interleave granularity is set to default (128B)
      for each pair in the vector:      // returned from buildMCBarData(), by extension this is "for each MCS in any group"
        // Set MCFGP register
        MCS_MCFGP                                       // undocumented, 0x0501080A
          [all]   0
          if (mcBarData.MCFGP.valid):
            [0]     VALID =               1
            [13-23] GROUP_SIZE =          mcBarData.MCFGP.group_size
            [24-47] GROUP_BASE_ADDRESS =  mcBarData.MCFGP.groupBaseAddr >> 2      // 4GB, 8GB and 16GB have the same base?
          [1-4]   MC_CHANNELS_PER_GROUP =                 mcBarData.MCFGP.chan_per_group
          [5-7]   CHANNEL_0_GROUP_MEMBER_IDENTIFICATION = mcBarData.MCFGP.chan0_group_member_id
          [8-10]  CHANNEL_1_GROUP_MEMBER_IDENTIFICATION = mcBarData.MCFGP.chan1_group_member_id

        // Set MCFGPM register
        MCS_MCFGPM                                      // undocumented, 0x0501080C
          [all]   0
          if (mcBarData.MCFGPM.valid):
            [0]     VALID =               1
            [13-23] GROUP_SIZE =          mcBarData.MCFGPM.group_size
            [24-47] GROUP_BASE_ADDRESS =  mcBarData.MCFGPM.groupBaseAddr >> 2     // 4GB, 8GB and 16GB have the same base?

        // Set MCFGPA register
        // HOLE1 and SMF cannot be both valid. Hostboot asserts in that case only now, after calculating both ranges and
        // passing both sets of variables. This could be checked all the way back in 7.4 (maybe it is?).
        MCS_MCFGPA                                      // undocumented, 0x0501080B
          [all]   0
          if (mcBarData.MCFGPA.HOLE_valid[0]):
            [0]     HOLE0_VALID =         1
            [2-11]  HOLE0_LOWER_ADDRESS = mcBarData.MCFGPA.HOLE_LOWER_addr[0] >> 2
            [12-21] HOLE0_UPPER_ADDRESS = mcBarData.MCFGPA.HOLE_UPPER_addr[0] >> 2
          if (mcBarData.MCFGPA.HOLE_valid[1]):          // these fields are not listed in p9n2_mc_scom_addresses_fld.H
            [24]    HOLE1_VALID =         1
            [26-35] HOLE1_LOWER_ADDRESS = mcBarData.MCFGPA.HOLE_LOWER_addr[1] >> 2
            [38-47] HOLE1_UPPER_ADDRESS = mcBarData.MCFGPA.HOLE_UPPER_addr[1] >> 2
          if (mcBarData.MCFGPA.SMF_valid):    // these are in p9n2_mc_scom_addresses_fld.H, not in p9_mc_scom_addresses_fld.H
            [28]    SMF_VALID =                         1
            [29]    SMF_UPPER_ADDRESS_AT_END_OF_RANGE = 1
            [30-43] SMF_LOWER_ADDRESS =                 mcBarData.MCFGPA.SMF_LOWER_addr
            [44-57] SMF_UPPER_ADDRESS =                 mcBarData.MCFGPA.SMF_UPPER_addr

        // Set MCFGPMA register
        // HOLE1 and SMF cannot be both valid. Hostboot asserts in that case only now, after calculating both ranges and
        // passing both sets of variables. This could be checked all the way back in 7.4 (maybe it is?).
        MCS_MCFGPMA                                     // undocumented, 0x0501080D
          [all]   0
          if (mcBarData.MCFGPMA.HOLE_valid[0]):
            [0]     HOLE0_VALID =         1
            [2-11]  HOLE0_LOWER_ADDRESS = mcBarData.MCFGPMA.HOLE_LOWER_addr[0] >> 2
            [12-21] HOLE0_UPPER_ADDRESS = mcBarData.MCFGPMA.HOLE_UPPER_addr[0] >> 2
          if (mcBarData.MCFGPMA.HOLE_valid[1]):          // these fields are not listed in p9n2_mc_scom_addresses_fld.H
            [24]    HOLE1_VALID =         1
            [26-35] HOLE1_LOWER_ADDRESS = mcBarData.MCFGPMA.HOLE_LOWER_addr[1] >> 2
            [38-47] HOLE1_UPPER_ADDRESS = mcBarData.MCFGPMA.HOLE_UPPER_addr[1] >> 2
          if (mcBarData.MCFGPMA.SMF_valid):    // these are in p9n2_mc_scom_addresses_fld.H, not in p9_mc_scom_addresses_fld.H
            [28]    SMF_VALID =                         1
            [29]    SMF_UPPER_ADDRESS_AT_END_OF_RANGE = 1
            [30-43] SMF_LOWER_ADDRESS =                 mcBarData.MCFGPMA.SMF_LOWER_addr
            [44-57] SMF_UPPER_ADDRESS =                 mcBarData.MCFGPMA.SMF_UPPER_addr

    // Hostboot does the following. It doesn't seem to be used anywhere else in the code except p9_query_mssinfo(), which
    // isn't called anywhere, but grep found it in these binary files:
    // - import/chips/p9/procedures/hwp/memory/p9_mss_bulk_pwr_throttles.o
    // - import/chips/p9/procedures/hwp/memory/p9_mss_utils_to_throttle.o
    FAPI_ATTR_SET(fapi2::ATTR_MSS_MEM_IPL_COMPLETE, PROC, 1)

// Hostboot uses separate loop, can this be done in one?
for each functional PROC:
  p9_setup_bars():

```
