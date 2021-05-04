# What this document is about?
This document is an attempt to gather information about undocumented registers
and registers that code analysis proofed to work diffrently than described.

**P9N2_MCS_PORT02_MCPERF0 0x05010823** \
| Bit range | Name      | Description |
| --------- | --------- | ----------- |
| 22-27     | AMO_LIMIT |             |

**P9N2_MCS_PORT02_MCPERF2 0x05010824** \
| Bit range | Name                      | Description                                                                                 |
| --------- | ------------------------- | ------------------------------------------------------------------------------------------- |
| 0-2       | PF_DROP_VALUE0            |                                                                                             |
| 3-5       | PF_DROP_VALUE1            |                                                                                             |
| 6-8       | PF_DROP_VALUE2            |                                                                                             |
| 9-11      | PF_DROP_VALUE3            |                                                                                             |
| 13-15     | REFRESH_BLOCK_CONFIG      | if has only one DIMM in MCA:                                                                |
|           |                           | 0b000 : if master ranks = 1                                                                 |
|           |                           | 0b001 : if master ranks = 2                                                                 |
|           |                           | 0b100 : if master ranks = 4                                                                 |
|           |                           | Per allowable DIMM mixing rules, we cannot mix different number of ranks on any single port |
|           |                           | if has both DIMMs in MCA:                                                                   |
|           |                           | 0b010 : if master ranks = 1                                                                 |
|           |                           | 0b011 : if master ranks = 2                                                                 |
|           |                           | 0b100 : if master ranks = 4 // 4 mranks is the same for one and two DIMMs in MCA            |
| 16        | ENABLE_REFRESH_BLOCK_SQ   | Always same value as [17]                                                                   |
| 17        | ENABLE_REFRESH_BLOCK_NSQ  | 1 : if (1 < (DIMM0 + DIMM1 logical ranks) <= 8 && not (one DIMM, 4 mranks, 2H 3DS)          |
|           |                           | 0 : otherwise                                                                               |
| 18        | ENABLE_REFRESH_BLOCK_DISP |                                                                                             |
| 28-31     | SQ_LFSR_CNTL              |                                                                                             |
| 50-54     | NUM_RMW_BUF               |                                                                                             |
| 61        | EN_ALT_ECR_ERR            |                                                                                             |

**P9N2_MCS_PORT02_MCAMOC 0x05010825** \
| Bit range | Name                              | Description |
| --------- | --------------------------------- | ----------- |
| 1         | FORCE_PF_DROP0                    |             |
| 4-28      | WRTO_AMO_COLLISION_RULES          |             |
| 29-31     | AMO_SIZE_SELECT, 128B_RW_64B_DATA |             |

**P9N2_MCS_PORT02_MCEPSQ 0x05010826** \
[0-7]   = 1                                 // JITTER_EPSILON \
// ATTR_PROC_EPS_READ_CYCLES_T* are calculated in istep 8.6 \
[8-15]  = (ATTR_PROC_EPS_READ_CYCLES_T0 + 6) / 4        // LOCAL_NODE_EPSILON \
[16-23] = (ATTR_PROC_EPS_READ_CYCLES_T1 + 6) / 4        // NEAR_NODAL_EPSILON \
[24-31] = (ATTR_PROC_EPS_READ_CYCLES_T1 + 6) / 4        // GROUP_EPSILON \
[32-39] = (ATTR_PROC_EPS_READ_CYCLES_T2 + 6) / 4        // REMOTE_NODAL_EPSILON \
[40-47] = (ATTR_PROC_EPS_READ_CYCLES_T2 + 6) / 4        // VECTOR_GROUP_EPSILON

**P9N2_MCS_PORT02_MCBUSYQ 0x05010827** \
[0]     = 1  // ENABLE_BUSY_COUNTERS \
[1-3]   = 1  // BUSY_COUNTER_WINDOW_SELECT, 1024 cycles \
[4-13]  = 38 // BUSY_COUNTER_THRESHOLD0 \
[14-23] = 51 // BUSY_COUNTER_THRESHOLD1 \
[24-33] = 64 // BUSY_COUNTER_THRESHOLD2

**P9N2_MCS_PORT02_MCPERF3 0x0501082B** \
[31] = 1                                    // ENABLE_CL0 \
[41] = 1                                    // ENABLE_AMO_MSI_RMW_ONLY \
[43] = !ATTR_ENABLE_MEM_EARLY_DATA_SCOM     // ENABLE_CP_M_MDI0_LOCAL_ONLY, !0 = 1? \
[44] = 1                                    // DISABLE_WRTO_IG \
[45] = 1                                    // AMO_LIMIT_SEL
