# 8.4 proc_check_slave_sbe_seeprom_complete: Check Slave SBE Complete

Requires:
    * SBE FIFO implementation
    * CFAM/FSI (for SBE FIFO)

### src/usr/isteps/istep08/call_proc_check_slave_sbe_seeprom_complete.C

```python
for l_cpu_target in l_cpuTargetList:
    if l_cpu_target is not l_pMasterProcTarget:
        #Note no PLID passed in
        SBEIO::SbeRetryHandler l_SBEobj = SBEIO::SbeRetryHandler(
                SBEIO::SbeRetryHandler::SBE_MODE_OF_OPERATION::ATTEMPT_REBOOT)

        l_SBEobj.setSbeRestartMethod(
            SBEIO::SbeRetryHandler::SBE_RESTART_METHOD::START_CBS)

        # We want to tell the retry handler that we have just powered
        # on the sbe, to distinguish this case from other cases where
        # we have determine there is something wrong w/ the sbe and
        # want to diagnose the problem
        l_SBEobj.setInitialPowerOn(True)
        l_SBEobj.main_sbe_handler(l_cpu_target)

        ## mostly status checking and error handling below (like restarting SBE
        ## after a failed boot or figuring out what went wrong during
        ## initialization)

        # We will judge whether or not the SBE had a successful
        # boot or not depending on if it made it to runtime or not
        if l_SBEobj.isSbeAtRuntime():
            # Set attribute indicating that SBE is started
            l_cpu_target.setAttr<ATTR_SBE_IS_STARTED>(1)
            # Make the FIFO call to get and apply the SBE Capabilities
            # The function queries version information from SBE and stores it in
            # target's attributes, look closer at it if any of these are needed:
            #  - ATTR_SBE_VERSION_INFO
            #  - ATTR_SBE_COMMIT_ID
            #  - ATTR_SBE_RELEASE_TAG
            SBEIO::getFifoSbeCapabilities(l_cpu_target)
            # Switch to using SBE SCOM
            ScomSwitches l_switches =
                l_cpu_target.getAttr<ATTR_SCOM_SWITCHES>()
            # Turn on SBE SCOM and turn off FSI SCOM.
            l_switches.useFsiScom = 0
            l_switches.useSbeScom = 1
            # proc_check_slave_sbe_seeprom_complete: changing SCOM
            l_cpu_target.setAttr<ATTR_SCOM_SWITCHES>(l_switches)
            # SUCCESS : proc_check_slave_sbe_seeprom_complete
        else:
            # FAILURE : proc_check_slave_sbe_seeprom_complete
            pass

    for l_cpu_target in l_cpuTargetList:
        const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_fapi2ProcTarget(
                const_cast<TARGETING::Target*> (l_cpu_target))
        # Hostboot has an output variable of 112 bits here which is just ignored
        p9_getecid(l_fapi2ProcTarget) # described below
```

```python
def p9_getecid(i_target_chip):
    uint64_t attr_data[2]

    temp64_ECID_PART0 = PU_OTPROM0_ECID_PART0_REGISTER
    temp64_ECID_PART1 = PU_OTPROM0_ECID_PART1_REGISTER
    temp64_ECID_PART2 = PU_OTPROM0_ECID_PART2_REGISTER
    temp64_ECID_PART0.reverse() # function originally used to reverse bits is pasted below
    temp64_ECID_PART1.reverse()
    temp64_ECID_PART2.reverse()

    attr_data[0] = temp64_ECID_PART0()
    attr_data[1] = temp64_ECID_PART1()
    #push fuse string into attribute
    FAPI_ATTR_SET(fapi2::ATTR_ECID, i_target_chip, attr_data)

    # Set some attributes memory can used to make work-around decisions.
    setup_memory_work_around_attributes(i_target_chip, temp64_ECID_PART2) # no-op for DD2
    setup_pcie_work_around_attributes(i_target_chip, temp64_ECID_PART2)   # no-op for DD2
```

```python
def reverse(io_buffer):
    l_result = io_buffer
    l_s = sizeof(T) * 8 - 1
    while io_buffer != 0:
        io_buffer >>= 1
        l_result <<= 1
        l_result |= io_buffer & 1
        l_s -= 1
    l_result <<= l_s
    io_buffer = l_result
```
