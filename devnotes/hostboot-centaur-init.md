# Step 11Hostboot Centaur Init
## 11.1host_prd_hwreconfig: Hook to handle HW reconfig

* This step is always called
* Move all Centaur's inband scom back to FSI scom
* Call PRD to allow them to rebuild model to remove non-functional Centaurs
* Protect Centaur from SP operations during initialization
  ** Set the CFP Security bit. This will prevent the SP from performing FSI operations to the Centaur while it is being initialized
* Used for HW reconfig path. FW's strategy is to perform the reconfig on ALL functional Centaurs/MCS's in the system.
* The following procedures must be called:

### bp9_switch_cfsim.C(proc target)
* Call on all present processors
* Move all Centaur’s inband scom back to FSI scom

### p9_enable_reconfig.C (MCS, DMI, MCA/MBuf)

### Call on all presentMCStargets
* Enables HW for reconfig loop
* Cumulus/Centaur:
  ** Attribute (ATTR_CEN_MSS_INIT_STATE) to each Centaur to track where the Reconfig loop got to
  ** Clocks on (can do fir masking) –set after step 11
  ** DMI bus up (inject special bit) –set after
    *** Turn's on special bit that allows the MCS DMI to get errors and not get into a hang condition
    *** Mask a bunch of FIRson processor
    *** Mask a bunch FIRs on centaur (HWP will check clock state)
    *** Injects a fail on the DMI bus (only if DMI bus is alive)
    *** Clears IO/MCS FIRs•Turns off special bit
* Nimbus
  ** Raise the MCU chiplet fences
  ** Stop clocks
  ** Scan 0 flush the MCU chiplet each and everytime through this loop
  ** How do we cleanup the nest portion of the MCS?

```
if defined(CONFIG_SECUREBOOT) and not defined(CONFIG_AXONE):
    // Send message to secure provider to load the section
    msg_q_t spnorQ = msg_q_resolve(SPNORRP_MSG_Q);
    msg = malloc(sizeof(msg_t));
    memset(msg, 0, sizeof(msg_t));
    // Load the MEMD section here as the first part of step11, it
    //  will stay loaded until the end of step14
    //
    // PNOR::MSG_LOAD_SECTION = 0x02
    msg->type = PNOR::MSG_LOAD_SECTION;
    msg->data[0] = static_cast<uint64_t>(PNOR::MEMD);
    // MSG_SENDRECV = 10
    syscall(MSG_SENDRECV, spnorQ, msg, NULL)
    free(msg);
```
