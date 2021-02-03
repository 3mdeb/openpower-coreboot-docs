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
