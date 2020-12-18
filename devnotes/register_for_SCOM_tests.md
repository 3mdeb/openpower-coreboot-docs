# About this document
This document presents a proposition of a register used for testing `SCOM`
writing and reading.

# Proposed registers

## TP.TPCHIP.PIB.PCBMS.DEVICE_ID_REG
* SCOM address: 00000000000F000F
* ID readout.
**0:19 RO**: CFAM_CHIPID: This field is also known as cfam_chipid.
**0 RO**: constant = 0b0
**21:31 RO**: VENDOR_ID: The IBM ID is 0x49
**32:35 RO**: constant = 0b0000
**36:38 RW**: SOCKET_ID: Socket position \
Socket 0x0 is connected to the primary service element. \
Socket 0x1 is connected to the secondary service element (high end only).
**39 RW**: CHIPPOS_ID: DCM position \
0 = SCMs or the first chip on the DCM \
1 = The second chip on the DCM (always a slave)
**40 RO**: IO_TP_VSB_CHIP_POS
**41:47 RO**: constant = 0b0000000
**48 ROX**: FUSE_NX_ALLOW_CRYPTO: 1 = NX crypto-functionality is enabled (export regulation).
**49 ROX**: FUSE_VMX_CRYPTO_DIS: 1 = VMX crypto-functionality is disabled (export regulation).
**50 ROX**: FUSE_FP_THROTTLE_EN: 1 = Floating point is throttled (export regulation).
**51 RO**: TP_NP_NVLINK_DISABLE: Indicates that NVLink is disabled.
**52 RO**: FUSE_TOPOLOGY_2CHIP
**53:54 RO**: FUSE_TOPOLOGY_GROUP
**55:63 RO**: constant = 0b000000000

## NPU.STCK0.CS.CTL.MISC.CONFIG1
* SCOM address: 0000000005011081
* Future Configuration 1 Register. \
  Currently a reserved register. \
  IDIAL_CONFIG1: Future configuration register.

## VA.VA_NORTH.VA_RG.SCF.VAS_PMCNTL
* SCOM address: 0000000003011830
* Performance Monitor Control Register
* This register is used to enable performance counter bits. \
**0:31 RW** PU_BIT_ENABLES: Each bit of this dial is used to enable
the associated performance counter bit and send it to the PMU. \
**32:35 RW** PU_CNTL_UNUSED: Unused bits. \
**36:63 RO** constant = 0b0000000000000000000000000000

## TP.TPCHIP.OCC.OCI.OCB.OCB_OCI_OCCHBR
* SCOM address: 000000000006C08F
* OCB_OCI OCB OCC Heartbeat Register
* **0:15 RW** OCB_OCI_OCCHBR_OCC_HEARTBEAT_COUNT: When written, this
field defines the starting value for a counter that increments at about
1us if OCC_HEARTBEAT_EN = 1 and the counter value is non-zero. \
If OCC_HEARTBEAT_EN = 0 or the counter value is 0, the counter does
not increment. \
If OCC_HEARTBEAT_EN = 1 and this counter becomes 0 (either from a written
value or from the counter wrapping) constitutes the loss of the OCC
heartbeat and surfaces an attention through TBD LFIR(TBD).
The pulses used for this field come from a free running pervasive hang
timer pulse (PM_Hang_Pulse) programmed to be ~ 32 ns that has a
5-bit precounter whose carryout forms a resultant ~ 1us decrement pulse. \
Upon writing this register with OCC_HEARTBEAT_EN = 1, the precounter is cleared
and will begin counting upon the next PM_Hang_Pulse. This PM_Hang_Pulse might
arrive immediately or a full duration later. With a (215)-1 range and an ~1 us
incrementation time yields a heartbeat range of 1us (+0-32ns) (value 0xFFFF)
to 65.535 ms (+0-32ns) (value 0x0001). The value chosen to be written for
debug purposes only, writing 0x0000 causes an immediate heartbeat_lost
if OCC_HEARTBEAT_EN = 1. Reads return the current value of the counter value.\
**16 RW** OCB_OCI_OCCHBR_OCC_HEARTBEAT_EN: OCC Heartbeat Timer Enable.\
**17:63 RO** constant = 0b00000000000000000000000000000000000000000000000
* used in `src/import/chips/p9/procedures/hwp/pm/p9_pm_ocb_init.C:691`

# Summary
`TP.TPCHIP.PIB.PCBMS.DEVICE_ID_REG` Has non 0, partially known value after boot, so it is great for testing SCOM reads. \
`NPU.STCK0.CS.CTL.MISC.CONFIG1` According to the documentation it is currently unused,
and can be written to and read from freely. \
`VA.VA_NORTH.VA_RG.SCF.VAS_PMCNTL` can be used for testing too.
Part of the register can be read and written to freely.
It is used to enable performance counters,
so unpredicted side effects could occur. \
`TP.TPCHIP.OCC.OCI.OCB.OCB_OCI_OCCHBR` register is also a good fit for testing,
but the value can change at any time. It has to be keept in mind while
testing SCOM access.
