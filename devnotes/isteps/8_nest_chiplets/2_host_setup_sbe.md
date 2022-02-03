# 8.2 host_setup_sbe

### src/usr/isteps/istep08/call_host_setup_sbe.C

```python
for l_cpu_target in l_cpuTargetList:
    if l_cpu_target is not l_pMasterProcTarget:
        p9_set_fsi_gp_shadow(l_cpu_target) ## described below
```
### src/import/chips/p9/procedures/hwp/perv/p9_set_fsi_gp_shadow.C

```python
# PERV_ROOT_CTRLx_COPY is TP.TPVSB.FSI.W.FSI_MAILBOX.FSXCOMP.FSXLOG.ROOT_CTRLx_COPY
# PERV_PERV_CTRLx_COPY is TP.TPVSB.FSI.W.FSI_MAILBOX.FSXCOMP.FSXLOG.PERV_CTRLx_COPY
def p9_set_fsi_gp_shadow(i_target_chip):
    perv_perv_ctrl0_copy = fapi2::getCfamRegister(i_target_chip, PERV_PERV_CTRL0_COPY))

    # Write the value of FUSED_CORE_MODE into PERV_CTRL0(23) regardless of chip EC the bit is nonfunctional on Nimbus DD1
    if fapi2::fapi2::Target<fapi2::TARGET_TYPE_SYSTEM>().ATTR_FUSED_CORE_MODE
        # perv_perv_ctrl0_copy .setBit<23>()
        # perv_perv_ctrl0_copy [8] = 1
        perv_perv_ctrl0_copy |= (1ULL << (sizeof(perv_perv_ctrl0_copy) * 8 - 1 - 23))
    else
        # perv_perv_ctrl0_copy .clearBit<23>()
        # perv_perv_ctrl0_copy [8] = 0
        perv_perv_ctrl0_copy &= ~(1ULL << (sizeof(perv_perv_ctrl0_copy) * 8 - 1 - 23))

    putCfamRegister(i_target_chip, PERV_PERV_CTRL0_COPY, perv_perv_ctrl0_copy))
```
