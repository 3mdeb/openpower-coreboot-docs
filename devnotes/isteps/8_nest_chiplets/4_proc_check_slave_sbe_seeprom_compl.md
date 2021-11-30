# 8.4 proc_check_slave_sbe_seeprom_complete: Check Slave SBE Complete

Requires:
    * SBE FIFO implementation
    * CFAM/FSI (for SBE FIFO)

Analysis assumptions:
    * `#define __HOSTBOOT_MODULE`
    * `#undef __HOSTBOOT_RUNTIME`
    * `Util::isSimicsRunning() == false`
    * `INITSERVICE::spBaseServicesEnabled() == false` (from the log).

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

### src/include/usr/sbeio/sbe_retry_handler.H
### src/usr/sbeio/common/sbe_retry_handler.C

This unit checks status of SBE, reports whether it managed to boot successfully
and handles its failures.

```cpp
SbeRetryHandler::SbeRetryHandler(SBE_MODE_OF_OPERATION i_sbeMode) : SbeRetryHandler(i_sbeMode, 0)
{
}

SbeRetryHandler::SbeRetryHandler(SBE_MODE_OF_OPERATION i_sbeMode, uint32_t i_plid)
: iv_useSDB(false)
, iv_secureModeDisabled(false) //Per HW team this should always be 0
, iv_masterErrorLogPLID(i_plid)
, iv_switchSidesCount(0)
, iv_currentAction(P9_EXTRACT_SBE_RC::ERROR_RECOVERED)
, iv_currentSBEState(SBE_REG_RETURN::SBE_NOT_AT_RUNTIME)
, iv_shutdownReturnCode(0)
, iv_currentSideBootAttempts(1) // It is safe to assume that the current side has attempted to boot
, iv_sbeMode(i_sbeMode)
, iv_sbeRestartMethod(SBE_RESTART_METHOD::HRESET)
, iv_initialPowerOn(false)
{
    // Initialize members that have no default initialization
    iv_sbeRegister.reg = 0;
}

inline void setSbeRestartMethod(SBE_RESTART_METHOD i_method)
{
    iv_sbeRestartMethod = i_method;
}

inline void setInitialPowerOn(bool i_isInitialPowerOn)
{
    iv_initialPowerOn = i_isInitialPowerOn;
}

inline bool isSbeAtRuntime()
{
    return (iv_currentSBEState == SbeRetryHandler::SBE_REG_RETURN::SBE_AT_RUNTIME);
}

void SbeRetryHandler::main_sbe_handler( TARGETING::Target * i_target )
{
    do
    {
        // Only set the secure debug bit (SDB) if we are not using xscom yet
        if(!i_target->getAttr<TARGETING::ATTR_SCOM_SWITCHES>().useXscom &&
           !i_target->getAttr<TARGETING::ATTR_PROC_SBE_MASTER_CHIP>())
        {
            this->iv_useSDB = true;
        }

        // Get the SBE status register, this will tell us what state
        // the SBE is in , if the asynFFDC bit is set on the sbe_reg
        // then FFDC will be collected at this point in time.
        // sbe_run_extract_msg_reg will return false if there was an error reading the status
        if(!this->sbe_run_extract_msg_reg(i_target))
        {
            // Failed to get sbe register something is seriously wrong, we should always be able to read that!!
            //Error log should have already committed in sbe_run_extract_msg_reg for this issue
            break;
        }

        // We will only trust the currState value if we know the SBE has just been booted.
        // In this case we have been told by the caller that the sbe just powered on
        // so it is safe to assume that the currState value is legit and we can trust that
        // the sbe has booted successfully to runtime.
        if( this->iv_initialPowerOn && (this->iv_sbeRegister.currState == SBE_STATE_RUNTIME))
        {
            //We have successfully powered on the SBE
            // Initial power on of the SBE was a success!!
            break;
        }

        //////******************************************************************
        // If we have made it this far we can assume that something is wrong w/ the SBE
        //////******************************************************************

        // if the sbe is not booted at all extract_rc will fail so we only
        // will run extract RC if we know the sbe has at least tried to boot
        if(this->iv_sbeRegister.sbeBooted)
        {
            // No async ffdc found and sbe says it has been booted, running run p9_sbe_extract_rc.

            // Call the function that runs extract_rc, this needs to run to determine
            // what broke and what our retry action should be
            this->sbe_run_extract_rc(i_target);
        }
        // If we have determined that the sbe never booted
        // then set the current action to be "restart sbe"
        // that way we will attempt to start the sbe again
        else
        {
            // SBE reports it was never booted, calling p9_sbe_extract_rc will fail. Setting action to be RESTART_SBE"
            this->iv_currentAction = P9_EXTRACT_SBE_RC::RESTART_SBE;
        }

        // If the mode was marked as informational that means the caller did not want
        // any actions to take place, the caller only wanted information collected
        if(this->iv_sbeMode == INFORMATIONAL_ONLY)
        {
            // Retry handler is being called in INFORMATIONAL mode so we are exiting without attempting any retry actions
            break;
        }

        // This do-while loop will continuously look at iv_currentAction, act
        // accordingly, then read status register and determine next action.
        // The ideal way to exit the loop is if the SBE makes it up to runtime after
        // attempting a retry which indicates we have recovered. If the currentAction
        // says NO_RECOVERY_ACTION then we break out of this loop.  Also if we fail
        // to read the sbe's status register or if we get write fails when trying to switch
        // seeprom sides. Both the fails mentioned last indicate there is a larger problem
        do
        {
            // We need to handle the following values that currentAction could be,
            // it is possible that iv_currentAction can be any of these values except there
            // is currently no path that will set it to be ERROR_RECOVERED
            //        ERROR_RECOVERED    = 0,
            //           - We should never hit this, if we have recovered then
            //             curreState should be RUNTIME
            //        RESTART_SBE        = 1,
            //        RESTART_CBS        = 2,
            //           - We will not listen to p9_extract_rc on HOW to restart the
            //             sbe. We will assume iv_sbeRestartMethod is correct and
            //             perform the restart method that iv_sbeRestartMethod says
            //             regardless if currentAction = RESTART_SBE or RESTART_CBS
            //        REIPL_BKP_SEEPROM  = 3,
            //        REIPL_UPD_SEEPROM  = 4,
            //            - We will switch the seeprom side (if we have not already)
            //            - then attempt to restart the sbe w/ iv_sbeRestartMethod
            //        NO_RECOVERY_ACTION = 5,
            //            - we deconfigure the processor we are retrying and fail out
            //
            // Important things to remember, we only want to attempt a single side
            // a maxiumum of 2 times, and also we only want to switch sides once

            if(this->iv_currentAction == P9_EXTRACT_SBE_RC::NO_RECOVERY_ACTION)
            {
                // We have concluded there are no further recovery actions to take, deconfiguring proc and exiting handler
                // There is no action possible. Gard and Callout the proc

                l_errl->addHwCallout( i_target,
                                        HWAS::SRCI_PRIORITY_HIGH,
                                        HWAS::DELAYED_DECONFIG,
                                        HWAS::GARD_NULL );

                this->iv_currentSBEState = SBE_REG_RETURN::PROC_DECONFIG;
                break;
            }

            // if the bkp_seeprom or upd_seeprom, attempt to switch sides.
            // This is also dependent on the iv_switchSideCount.
            // Note: we do this for upd_seeprom because we don't support
            //       updating the seeprom during IPL time
            if((this->iv_currentAction == P9_EXTRACT_SBE_RC::REIPL_BKP_SEEPROM ||
                this->iv_currentAction == P9_EXTRACT_SBE_RC::REIPL_UPD_SEEPROM))
            {
                // We cannot switch sides and perform an hreset if the seeprom's
                // versions do not match. If this happens, log an error and stop
                // trying to recover the SBE
                if(this->iv_sbeRestartMethod == HRESET)
                {
                    TARGETING::ATTR_HB_SBE_SEEPROM_VERSION_MISMATCH_type l_versionsMismatch =
                        i_target->getAttr<TARGETING::ATTR_HB_SBE_SEEPROM_VERSION_MISMATCH>();

                    if(l_versionsMismatch)
                    {
                        // We cannot switch SEEPROM sides if their versions do not match, exiting handler
                        l_errl->addHwCallout(i_target,
                                             HWAS::SRCI_PRIORITY_HIGH,
                                             HWAS::NO_DECONFIG,
                                             HWAS::GARD_NULL);

                        break; // break out of the retry loop
                    }
                }
                if(this->iv_switchSidesCount >= MAX_SWITCH_SIDE_COUNT)
                {
                     // We have already flipped seeprom sides once and we should
                     // not have attempted to flip again

                    // Break out of loop, something bad happened and we dont want end
                    // up in a endless loop
                    break;
                }

                if(this->switch_sbe_sides(i_target) != NULL)
                {
                    // If any error occurs while we are trying to switch sides
                    // this indicates big problems so we want to break out of the
                    // retry loop
                    break;
                }

                // Note that we do not want to `continue` here because we want to
                // attempt to restart using whatever sbeRestartMethod is set to after
                // switching seeprom sides
            }

            // Both of the retry methods require a FAPI2 version of the target because they
            // are fapi2 HWPs
            const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_fapi2_proc_target (i_target);
            if(this->iv_currentSideBootAttempts >= MAX_SIDE_BOOT_ATTEMPTS)
            {
                // We have already done the max attempts for the current
                // seeprom side. For some reason we are attempting to do another boot.

                // Break out of loop, something bad happened and we dont want end
                // up in a endless loop
                break;
            }
            // Look at the sbeRestartMethd instance variable to determine which method
            // we will use to attempt the restart. In general during IPL time we will
            // attempt CBS, during runtime we will want to use HRESET.
            else if(this->iv_sbeRestartMethod == SBE_RESTART_METHOD::START_CBS)
            {
                //Increment attempt count for this side
                this->iv_currentSideBootAttempts++;

                // Invoking p9_start_cbs HWP on processor

                // For now we only use p9_start_cbs if we fail to boot the slave SBE
                // on our initial attempt, the bool param is true we are telling the
                // HWP that we are starting up the SBE which is true in this case
                if (p9_start_cbs(l_fapi2_proc_target, true))
                {
                    // Deconfig the target when SBE Retry fails
                    l_errl->addHwCallout(i_target,
                                         HWAS::SRCI_PRIORITY_LOW,
                                         HWAS::DELAYED_DECONFIG,
                                         HWAS::GARD_NULL);

                    // If we got an errlog while attempting start_cbs
                    // we will assume that no future retry actions
                    // will work so we will break out of the retry loop
                    break;
                }
            }
            // The only other type of reset method is HRESET
            else
            {
                // Increment attempt count for this side
                this->iv_currentSideBootAttempts++;

                // Invoking p9_sbe_hreset HWP on processor %.8X"

                // For now we only use HRESET during runtime, the bool param
                // we are passing in is supposed to be FALSE if runtime, TRUE is ipl time
                if (p9_sbe_hreset(l_fapi2_proc_target, false)) {
                {
                    // Deconfig the target when SBE Retry fails
                    l_errl->addHwCallout(i_target,
                                         HWAS::SRCI_PRIORITY_LOW,
                                         HWAS::DELAYED_DECONFIG,
                                         HWAS::GARD_NULL);

                    // If we got an errlog while attempting p9_sbe_hreset
                    // we will assume that no future retry actions
                    // will work so we will exit
                    break;
                }
            }

            // Get the sbe register  (note that if asyncFFDC bit is set in status register then
            // we will read it in this call)
            if(!this->sbe_run_extract_msg_reg(i_target))
            {
                // Error log should have already committed in sbe_run_extract_msg_reg for this issue
                // we need to stop our recovery efforts and bail out of the retry handler
                break;
            }

            // If the currState of the SBE is not RUNTIME then we will assume
            // our attempt to boot the SBE has failed, so run extract rc again
            // to determine why we have failed
            if (this->iv_sbeRegister.currState != SBE_STATE_RUNTIME)
            {
                this->sbe_run_extract_rc(i_target);
            }

        } while((this->iv_sbeRegister).currState != SBE_STATE_RUNTIME);

        // If we ended up switching sides we want to mark it down as
        // as informational log
        if(this->iv_switchSidesCount)
        {
             // SBE booted from unexpected side.
        }

    } while(0);
}

bool SbeRetryHandler::sbe_run_extract_msg_reg(TARGETING::Target * i_target)
{
    //Assume that reading the status succeeded
    bool l_statusReadSuccess = true;

    // This function will poll the status register for 60 seconds
    // waiting for the SBE to reach runtime
    // we will exit the polling before 60 seconds if we either reach
    // runtime, or get an error reading the status reg, or if the asyncFFDC
    // bit is set
    errlHndl_t l_errl = this->sbe_poll_status_reg(i_target);

    // If there is no error getting the status register, and the SBE
    // did not make it to runtime AND the asyncFFDC bit is set, we will
    // use the FFDC to decide our actions rather than using p9_extract_sbe_rc
    if(!l_errl && this->iv_sbeRegister.currState != SBE_STATE_RUNTIME && this->iv_sbeRegister.asyncFFDC)
    {
        // WARNING: sbe_run_extract_msg_reg completed without error.
        // However, there was asyncFFDC found though so we will run the FFDC parser

        // The SBE has responded to an asyncronus request that hostboot
        // made with FFDC indicating an error has occurred.
        // This should be the path we hit when we are waiting to see
        // if the sbe boots
        this->sbe_get_ffdc_handler(i_target);
    }
    // If there was an error log that means that we failed to read the
    // cfam register to get the SBE status, something is seriously wrong
    // if we hit this
    else if (l_errl)
    {
        l_statusReadSuccess = false;
    }
    // No error,  able to read the sbe status register okay
    // No guarantees that the SBE made it to runtime
    else
    {
        // sbe_run_extract_msg_reg completed without error
    }

    return l_statusReadSuccess;
}

constexpr uint64_t SBE_RETRY_TIMEOUT_HW_SEC     = 60;  // 60 seconds
constexpr uint32_t SBE_RETRY_NUM_LOOPS          = 60;

errlHndl_t SbeRetryHandler::sbe_poll_status_reg(TARGETING::Target * i_target)
{
    this->iv_currentSBEState = SbeRetryHandler::SBE_REG_RETURN::SBE_NOT_AT_RUNTIME;

    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> l_fapi2_proc_target(i_target);

    // Each sbe gets 60s to respond with the fact that it's
    // booted and at runtime (stable state)
    uint64_t l_sbeTimeout = SBE_RETRY_TIMEOUT_HW_SEC;  // 60 seconds

    //Sleep time should be 1 second on HW
    const uint64_t SBE_WAIT_SLEEP_SEC = (l_sbeTimeout/SBE_RETRY_NUM_LOOPS);

    // Running p9_get_sbe_msg_register HWP on proc

    for( uint64_t l_loops = 0; l_loops < SBE_RETRY_NUM_LOOPS; l_loops++ )
    {
        if (p9_get_sbe_msg_register(l_fapi2_proc_target, this->iv_sbeRegister))
        {
            this->iv_currentSBEState = SbeRetryHandler::SBE_REG_RETURN::FAILED_COLLECTING_REG;
            break;
        }
        else if (this->iv_sbeRegister.currState == SBE_STATE_RUNTIME)
        {
            // SBE booted and at runtime
            this->iv_currentSBEState = SbeRetryHandler::SBE_REG_RETURN::SBE_AT_RUNTIME;
            break;
        }
        else if (this->iv_sbeRegister.asyncFFDC)
        {
            // SBE has async FFDC bit set

            // Async FFDC is indicator that SBE is failing to boot, and if
            // in DUMP state, that SBE is done dumping, so leave loop
            break;
        }
        else
        {
            if( !(l_loops % 10) )
            {
                // SBE NOT booted yet
            }
            l_loops++;
            // reset watchdog before performing the nanosleep
            INITSERVICE::sendProgressCode();
            nanosleep(SBE_WAIT_SLEEP_SEC,0);
        }
    }

    if (this->iv_sbeRegister.currState != SBE_STATE_RUNTIME)
    {
        // Switch to using FSI SCOM if we are not using xscom
        TARGETING::ScomSwitches l_switches =
            i_target->getAttr<TARGETING::ATTR_SCOM_SWITCHES>();
        TARGETING::ScomSwitches l_switches_before = l_switches;

        if(!l_switches.useXscom)
        {
            // Turn off SBE SCOM and turn on FSI SCOM.
            l_switches.useFsiScom = 1;
            l_switches.useSbeScom = 0;
            i_target->setAttr<TARGETING::ATTR_SCOM_SWITCHES>(l_switches);
        }
    }
}

// TODO the next level of calls if there are issues with SBE startup:
// - this->sbe_get_ffdc_handler(i_target)
// - this->sbe_run_extract_rc(i_target)

// Other calls we're unlikely to look at here:
// - this->switch_sbe_sides(i_target) unlikely to be of interest
// - p9_sbe_hreset(l_fapi2_proc_target, false) isn't for power on case
// - p9_start_cbs(l_fapi2_proc_target, true) is what istep 8.3 does
// - INITSERVICE::sendProgressCode() resets watchdog
```

### src/import/chips/p9/procedures/hwp/sbe/p9_get_sbe_msg_register.C

```cpp
fapi2::ReturnCode p9_get_sbe_msg_register(const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_chip,
        sbeMsgReg_t& o_sbeReg)
{
    fapi2::buffer<uint32_t> l_cfamReg;
    fapi2::buffer<uint64_t> l_scomReg;
    uint8_t l_is_master_chip = 0;
    FAPI_TRY(FAPI_ATTR_GET(fapi2::ATTR_PROC_SBE_MASTER_CHIP, i_chip, l_is_master_chip));

    if(l_is_master_chip)
    {
        FAPI_TRY(fapi2::getScom(i_chip, PERV_SB_MSG_SCOM, l_scomReg));
        l_scomReg.extract<0, 32>(o_sbeReg.reg);
    }
    else
    {
        FAPI_TRY(fapi2::getCfamRegister(i_chip, PERV_SB_MSG_FSI, l_cfamReg));
        o_sbeReg.reg = l_cfamReg;
    }
}
```
