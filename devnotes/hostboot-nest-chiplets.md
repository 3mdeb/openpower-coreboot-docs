# Step 8 Hostboot - Nest Chiplets
## 8.5 host_attnlisten_proc: Start attention poll for P9(s)
* Enable hostboot to start including all processorattentions in its post istep analysis
* Enable OCC to collect FIR data on all processors if master processor checkstops
* From this point on ATTN/PRD will listen (“poll”) for powerbus attentions after each named istep

```
# src/include/usr/initservice/initserviceif.H:166
# spBaseServicesEnabled()
#
# It is not clear where TARGETING::SpFunctions originates from
sys = getTopLevelTarget()
TARGETING::SpFunctions spfuncs
# sys['spfuncs'] originates from
# sys.tryGetAttr<TARGETING::ATTR_SP_FUNCTIONS>(spfuncs)
if  sys
and sys['spfuncs']
and spfuncs.baseServices:
    # send_analyzable_procs()
    l_chipHuids = []

    # get all functional Proc targets
    l_procsList = getAllChips(TYPE_PROC)

    # now fill in the list with proc huids
    for l_cpu_target in l_procList:
        l_chipHuids.push_back(TARGETING::get_huid(l_cpu_target))

    #
    # send the message to alert ATTN to start monitoring these chips
    #

    msg_t myMsg
    # INITSERVICE::ATTN_MONITOR_CHIPID_LIST = 0x40000030
    myMsg.type = INITSERVICE::ATTN_MONITOR_CHIPID_LIST
    myMsg.data[0] = 0
    # Contains the full size of the extra_data field of myMsg
    # extra_data includes attn_chipid_msg + list of HUIDs.
    # attn_chipid_msg.data is the start of the huid list so
    # need to remove that variable's size from the total
    myMsg.data[1] = (sizeof(INITSERVICE::attn_chipid_msg) - sizeof(l_data_ptr.data))
                + (sizeof(TARGETING::ATTR_HUID_type) * len(l_chipHuids))
    # MBOX::alocate()
    #
    # MSGQ_RESOLVE_ROOT - defined in enum, probably compiled as 8
    # MSGQ_ROOT_VFS - defined in enum, probably compiled as 0
    msg_q_t vfsQ = syscall(MSGQ_RESOLVE_ROOT, MSGQ_ROOT_VFS)
    #####################################
    msg_t msq_resolve_message
    msq_resolve_message.type = VFS_MSG_RESOLVE_MSGQ
    msq_resolve_message.extra_data = name
    # MSG_SENDRECV - defined in enum, compiles to 10 probably
    syscall(MSG_SENDRECV, vfsQ, msq_resolve_message, NULL)
    #####################################
    msg_q_t mboxQ = msq_resolve_message.data[0]
    msg_t message_allocate
    # MSG_MBOX_ALLOCATE - defined in enum, compiles to 9 probably
    message_allocate.type = MSG_MBOX_ALLOCATE
    message_allocate.data[0] = i_size
    # MSG_SENDRECV - defined in enum, compiles to 10 probably
    syscall(MSG_SENDRECV, mboxQ, message_allocate, NULL)
    myMsg.extra_data = message_allocate.data[1]
    #####################################
    l_data_ptr = myMsg.extra_data
    # total chip huid's in list
    l_data_ptr.chipIdCount = len(l_chipHuids)
    # data length in bytes of the list (sizeof(huid) * Number of huids)
    l_data_ptr.size = sizeof(TARGETING::ATTR_HUID_type) * len(l_chipHuids)
    # now fill in the list with huids
    # copy the memory
    l_data_ptr.data = copy(l_chipHuids)
    # send message to alert ATTN to start monitoring these chips
    # MBOX::sendrecv()
    # src/usr/mbox/mailboxsp.C:2327
    myMsg.__reserved__async = 1
    # MSGQ_RESOLVE_ROOT - defined in enum, compiles to 8 probably
    # MSGQ_ROOT_VFS - defined in enum, compiles to 0 probably
    msg_q_t vfsQ = syscall(MSGQ_RESOLVE_ROOT, MSGQ_ROOT_VFS)
    #####################################
    msg_t msg_q_resolve
    # VFS_MSG_RESOLVE_MSGQ - Message to VFS_ROOT to find a message queue
    # defined in enum
    msg_q_resolve.type = VFS_MSG_RESOLVE_MSGQ
    msg_q_resolve.extra_data = "/msg/mbox"
    # MSG_SENDRECV - defined in enum
    syscall(MSG_SENDRECV, vfsQ, msg_q_resolve, NULL)
    #####################################
    msg_t msg
    msg.type = MBOX::MSG_SEND
    # HWSVRQ = 0x80000008
    msg.data[0] = HWSVRQ
    msg.extra_data = myMsg # Payload message
    syscall(MBOX::MSG_SEND, msg_q_resolve.data[0], msg)
    #####################################
```
