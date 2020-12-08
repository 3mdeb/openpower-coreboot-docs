# 8.2 host_setup_sbe

### src/usr/isteps/istep08/call_host_setup_sbe.C

```python
for l_cpu_target in l_cpuTargetList:
    if l_cpu_target is not l_pMasterProcTarget:
        p9_set_fsi_gp_shadow(l_cpu_target) ## described bellow
```
### src/import/chips/p9/procedures/hwp/perv/p9_set_fsi_gp_shadow.C

```python
# PERV_ROOT_CTRLx_COPY is TP.TPVSB.FSI.W.FSI_MAILBOX.FSXCOMP.FSXLOG.ROOT_CTRLx_COPY
# PERV_PERV_CTRLx_COPY is TP.TPVSB.FSI.W.FSI_MAILBOX.FSXCOMP.FSXLOG.PERV_CTRLx_COPY
def p9_set_fsi_gp_shadow(i_target_chip):

    if fapi2::i_target_chip.ATTR_CHIP_EC_FEATURE_FSI_GP_SHADOWS_OVERWRITE:
        # Setting flush values for root_ctrl_copy and perv_ctrl_copy registers
        PERV_ROOT_CTRL0_COPY = 0x80FE4003
        PERV_ROOT_CTRL1_COPY = 0x00180000
        PERV_ROOT_CTRL2_COPY = 0x0400E000
        PERV_ROOT_CTRL3_COPY = 0x0080C000
        PERV_ROOT_CTRL4_COPY = 0x00000000
        PERV_ROOT_CTRL5_COPY &= 0xFFFF0000
        PERV_ROOT_CTRL6_COPY = (PERV_ROOT_CTRL6_COPY & 0xF0000000) | 0x00800000
        PERV_ROOT_CTRL7_COPY = 0x00000000
        PERV_ROOT_CTRL8_COPY = (PERV_ROOT_CTRL6_COPY & 0x0000008F) | 0xEEECF300
        PERV_PERV_CTRL0_COPY = 0x7C0E2000
        PERV_PERV_CTRL1_COPY = 0x63C00000

    # Write the value of FUSED_CORE_MODE into PERV_CTRL0(23) regardless of chip EC the bit is nonfunctional on Nimbus DD1
    if fapi2::fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>().ATTR_FUSED_CORE_MODE
        # PERV_PERV_CTRL0_COPY.setBit<23()
        # PERV_PERV_CTRL0_COPY[8] = 1
        PERV_PERV_CTRL0_COPY |= (1ULL << (sizeof(PERV_PERV_CTRL0_COPY) * 8 - 1 - 23))
    else
        # PERV_PERV_CTRL0_COPY.clearBit<23>()
        # PERV_PERV_CTRL0_COPY[8] = 0
        PERV_PERV_CTRL0_COPY &= ~(1ULL << (sizeof(PERV_PERV_CTRL0_COPY) * 8 - 1 - 23))
```
