# 8.3 host_cbs_start

### src/usr/isteps/istep08/call_host_cbs_start.C

```python
for l_cpu_target in l_cpuTargetList:
    if l_cpu_target is not l_pMasterProcTarget:
        sendFifoReset(l_cpu_target)
        p9_start_cbs(l_cpu_target, True) ## described bellow
```

### src/import/chips/p9/procedures/hwp/perv/p9_start_cbs.C

```python
def p9_start_cbs(i_target_chip, i_sbe_start):
    temp8_RESET_SKIP = fapi2::FAPI_SYSTEM.ATTR_START_CBS_FIFO_RESET_SKIP
    i_target_chip.PERV_SB_MSG = 0
    # PERV_CBS_CS.setBit<3>()
    # PERV_CBS_CS[28] = 1
    i_target_chip.PERV_CBS_CS |= 1<<28

    temp32_PERV_SB_CS = i_target_chip.PERV_SB_CS

    # temp32_PERV_SB_CS.clearBit<12>()
    # temp32_PERV_SB_CS.clearBit<13>()

    # temp32_PERV_SB_CS[20] = 0
    temp32_PERV_SB_CS &= ~(1<<20)
    # temp32_PERV_SB_CS[19] = 0
    temp32_PERV_SB_CS &= ~(1<<19)

    i_target_chip.PERV_SB_CS = temp32_PERV_SB_CS

    # i_target_chip.PERV_CBS_ENVSTAT.getBit<2>()
    #l_read_vdn_pgood_status = i_target_chip.PERV_CBS_ENVSTAT[29]
    l_read_vdn_pgood_status = (i_target_chip.PERV_CBS_ENVSTAT & (1<<29)) == (1<<29)

    temp32_PERV_CBS_CS = i_target_chip.PERV_CBS_CS
    # temp32_PERV_CBS_CS.clearBit<0>()
    # temp32_PERV_CBS_CS.clearBit<2>()

    # temp32_PERV_CBS_CS[31] = 0
    temp32_PERV_CBS_CS &= ~(1<<31)
    # temp32_PERV_CBS_CS[29] = 0
    temp32_PERV_CBS_CS &= ~(1<<29)

    i_target_chip.PERV_CBS_CS = temp32_PERV_CBS_CS

    # temp32_PERV_CBS_CS[31] = 1
    temp32_PERV_CBS_CS |= 1<<31
    i_target_chip.PERV_CBS_CS = temp32_PERV_CBS_CS

    l_timeout = 20
    #UNTIL CBS_CS.CBS_CS_INTERNAL_STATE_VECTOR == CBS_IDLE_VALUE
    while l_poll_data != 0x002:
        temp32_PERV_CBS_CS = i_target_chip.PERV_CBS_CS
        # l_poll_data[15-0] = temp32_PERV_CBS_CS[15-0]
        l_poll_data = temp32_PERV_CBS_CS & 0xffff
        #fapi2::delay(P9_CBS_IDLE_HW_NS_DELAY, P9_CBS_IDLE_SIM_CYCLE_DELAY)
        sleep(640000ns)

    if temp8_RESET_SKIP:
        i_target_chip.PERV_FSB_FSB_DOWNFIFO_RESET = 0x80000000
    if i_sbe_start:
        #Setting up hreset
        # i_target_chip.PERV_SB_CS[19] = 0
        i_target_chip.PERV_SB_CS &= ~(1<<19)
        # i_target_chip.PERV_SB_CS[19] = 1
        i_target_chip.PERV_SB_CS |= 1<<19
        # i_target_chip.PERV_SB_CS[19] = 0
        i_target_chip.PERV_SB_CS &= ~(1<<19)
    #l_fsi2pib_status = i_target_chip.PERV_FSI2PIB_STATUS[15]
    l_fsi2pib_status = (i_target_chip.PERV_FSI2PIB_STATUS & (1 << 15)) == (1 << 15)
```
