```cpp
void* call_mss_power_cleanup (void *io_pArgs)
{
    IStepError  l_stepError;

    TARGETING::Target* l_sys = nullptr;
    TARGETING::targetService().getTopLevelTarget(l_sys);
    uint8_t l_mpipl = l_sys->getAttr<ATTR_IS_MPIPL_HB>();
    if (!l_mpipl)
    {
        TARGETING::TargetHandleList l_mcbistTargetList;
        getAllChiplets(l_mcbistTargetList, TYPE_MCBIST);

        for (const auto & l_target : l_mcbistTargetList)
        {
            fapi2::Target<fapi2::TARGET_TYPE_MCBIST>l_fapi_target(l_target);
            //  call the HWP with each fapi2::Target
            FAPI_INVOKE_HWP(l_err, p9_mss_power_cleanup, l_fapi_target);
        }
    }

#ifdef CONFIG_NVDIMMS
    TARGETING::TargetHandleList l_procList;
    getAllChips(l_procList, TARGETING::TYPE_PROC, false);
    TARGETING::ATTR_MODEL_type l_chipModel = l_procList[0]->getAttr<TARGETING::ATTR_MODEL>();

    // Check for any NVDIMMs after the mss_power_cleanup
    TARGETING::TargetHandleList l_dimmTargetList;
    TARGETING::TargetHandleList l_nvdimmTargetList;
    getAllLogicalCards(l_dimmTargetList, TYPE_DIMM);

    // Walk the dimm list and collect all the nvdimm targets
    for (auto const l_dimm : l_dimmTargetList)
    {
        if (TARGETING::isNVDIMM(l_dimm))
        {
            l_nvdimmTargetList.push_back(l_dimm);
        }
    }

    // Run the nvdimm management functions if the list is not empty
    if (!l_nvdimmTargetList.empty())
    {
        NVDIMM::nvdimm_restore(l_nvdimmTargetList);
        NVDIMM::nvdimm_encrypt_enable(l_nvdimmTargetList);
    }
#endif
}

void getAllLogicalCards(TARGETING::TargetHandleList & o_vector, TYPE i_cardType, bool i_functional)
{
    if (i_functional)
    {
        getClassResources(o_vector, CLASS_LOGICAL_CARD, i_cardType, UTIL_FILTER_FUNCTIONAL);
    }
    else
    {
        getClassResources(o_vector, CLASS_LOGICAL_CARD, i_cardType, UTIL_FILTER_ALL);
    }
}

void getClassResources( TARGETING::TargetHandleList & o_vector,
                     CLASS i_class, TYPE  i_type, ResourceState i_state )
{
    switch(i_state)
    {
        case UTIL_FILTER_ALL:
        {
            // Type predicate
            TARGETING::PredicateCTM l_CtmFilter(i_class, i_type);
            // Apply the filter through all targets
            TARGETING::TargetRangeFilter l_targetList(
                                    TARGETING::targetService().begin(),
                                    TARGETING::targetService().end(),
                                    &l_CtmFilter);
            o_vector.clear();
            for ( ; l_targetList; ++l_targetList)
            {
                o_vector.push_back(*l_targetList);
            }
            break;
        }
        case UTIL_FILTER_FUNCTIONAL:
        {
            // Get all functional chips or chiplets
            // Functional predicate
            TARGETING::PredicateIsFunctional l_isFunctional;
            // Type predicate
            TARGETING::PredicateCTM l_CtmFilter(i_class, i_type);
            // Set up compound predicate
            TARGETING::PredicatePostfixExpr l_functional;
            l_functional.push(&l_CtmFilter).push(&l_isFunctional).And();
            // Apply the filter through all targets
            TARGETING::TargetRangeFilter l_funcTargetList(
                                    TARGETING::targetService().begin(),
                                    TARGETING::targetService().end(),
                                    &l_functional);
            o_vector.clear();
            for ( ; l_funcTargetList; ++l_funcTargetList)
            {
                o_vector.push_back(*l_funcTargetList);
            }
            break;
        }
    }
    // If target vector contains more than one element, sorty by HUID
    if (o_vector.size() > 1)
    {
        std::sort(o_vector.begin(), o_vector.end(), compareTargetHuid);
    }
}

bool nvdimm_encrypt_enable(TargetHandleList &i_nvdimmList)
{
    // Get the sys pointer, attribute keys are system level
    Target* l_sys = nullptr;
    targetService().getTopLevelTarget(l_sys);

    // Get the FW key attributes
    auto l_attrKeysFw = l_sys->getAttrAsStdArr<ATTR_NVDIMM_ENCRYPTION_KEYS_FW>();
    // Cast to key data struct type for easy access to each key
    nvdimmKeyData_t* l_keysFw = reinterpret_cast<nvdimmKeyData_t*>(&l_attrKeysFw);
    // Handle encryption for all nvdimms
    for (const auto & l_nvdimm : i_nvdimmList)
    {
        // Check encryption state in the config/status reg
        encryption_config_status_t l_encStatus = {0};
        nvdimmReadReg(l_nvdimm, ENCRYPTION_CONFIG_STATUS, l_encStatus.whole);
        // Status = 0x01, enable encryption
        // Set the Random String (RS) reg
        nvdimm_setKeyReg(l_nvdimm, l_keysFw->rs, ENCRYPTION_RAMDOM_STRING_SET, ENCRYPTION_RANDOM_STRING_VERIFY, false);
        // Set the Erase Key (EK) Reg
        nvdimm_setKeyReg(l_nvdimm, l_keysFw->ek, ENCRYPTION_ERASE_KEY_SET, ENCRYPTION_ERASE_KEY_VERIFY, false);
        // Set the Access Key (AK) Reg
        nvdimm_setKeyReg(l_nvdimm, l_keysFw->ak, ENCRYPTION_ACCESS_KEY_SET, ENCRYPTION_ACCESS_KEY_VERIFY, false);
        // Verify encryption is enabled
        nvdimmReadReg(l_nvdimm, ENCRYPTION_CONFIG_STATUS, l_encStatus.whole);
        notifyNvdimmProtectionChange(l_nvdimm, ENCRYPTION_ENABLED);
    }
}

errlHndl_t nvdimm_setKeyReg(Target* i_nvdimm,
                            uint8_t* i_keyData,
                            uint32_t i_keyReg,
                            uint32_t i_verifyReg,
                            bool i_secondAttempt)
{
    uint32_t l_byte = 0;
    uint8_t l_verifyData = 0x0;

    // Before setting the key reg we need to
    // init the verif reg with a random value
    uint8_t l_genData[ENC_KEY_SIZE] = {0};
    nvdimm_getRandom(l_genData);

    // Write the verif reg one byte at a time
    for (l_byte = 0; l_byte < ENC_KEY_SIZE; l_byte++)
    {
        // Write the verification byte
        nvdimmWriteReg(i_nvdimm, i_verifyReg, l_genData[l_byte]);
    }

    // Delay to allow verif write to complete
    sleep(0, 100000000ns);

    // Write the reg, one byte at a time
    for (l_byte = 0; l_byte < ENC_KEY_SIZE; l_byte++)
    {
        // Write the key byte
        nvdimmWriteReg(i_nvdimm, i_keyReg, i_keyData[l_byte]);
    }

    // Delay to allow write to complete
    sleep(0, 100000000ns);
    return l_err;
}

bool nvdimm_keyifyRandomNumber(uint8_t* o_genData, uint8_t* i_xtraData)
{
    bool l_failed = false;
    uint32_t l_xtraByte = 0;

    // ENC_KEY_SIZE = 32
    for (uint32_t l_byte = 0; l_byte < ENC_KEY_SIZE; l_byte++)
    {
        // KEY_TERMINATE_BYTE = 0x00
        // KEY_ABORT_BYTE = 0xFF
        if ((o_genData[l_byte] != KEY_TERMINATE_BYTE) &&
            (o_genData[l_byte] != KEY_ABORT_BYTE))
        {
            // This byte is valid
            continue;
        }
        // This byte is not valid, replace it
        // Find a valid byte in the replacement data
        while ((i_xtraData[l_xtraByte] == KEY_TERMINATE_BYTE) ||
               (i_xtraData[l_xtraByte] == KEY_ABORT_BYTE))
        {
            l_xtraByte++;

            if (l_xtraByte == ENC_KEY_SIZE)
            {
                l_failed = true;
                break;
            }
        }
        // Replace the invalid byte with the valid extra byte
        o_genData[l_byte] = i_xtraData[l_xtraByte];
    }
    return l_failed;
}

errlHndl_t nvdimm_getRandom(uint8_t* o_genData)
{
    uint8_t l_xtraData[ENC_KEY_SIZE] = {0};
    // Get a random number with the darn instruction
    nvdimm_getDarnNumber(ENC_KEY_SIZE, o_genData);
    // Validate and update the random number
    // Retry if more randomness required
    do
    {
        //Get replacement data
        nvdimm_getDarnNumber(ENC_KEY_SIZE, l_xtraData);
    } while(nvdimm_keyifyRandomNumber(o_genData, l_xtraData));
}

errlHndl_t nvdimm_getDarnNumber(size_t i_genSize, uint8_t* o_genData)
{
    uint64_t* l_darnData = reinterpret_cast<uint64_t*>(o_genData);
    for (uint32_t l_loop = 0; l_loop < (i_genSize / sizeof(uint64_t)); l_loop++)
    {
        // Darn could return an error code
        uint32_t l_darnErrors = 0;
        while (l_darnErrors < 10)
        {
            // Get a 64-bit random number with the darn instruction
            l_darnData[l_loop] = getDarn();
            // DARN_ERROR_CODE = 0xFFFFFFFFFFFFFFFF
            if (l_darnData[l_loop] != DARN_ERROR_CODE)
            {
                break;
            }
            else
            {
                l_darnErrors++;
            }
        }
    }
    return;
}

/** @brief  getDarn - deliver a random number instruction
 *  Returns 64 bits of random data, requires random number generator
 *  configured appropriately + locked down, only available at runtime.
 */
inline uint64_t getDarn()
{
    register uint64_t rt = 0;
    // darn assembly instruction
    // Deliver A Random Number
    // 0xFFFFFFFFFFFFFFFF is an error code
    asm volatile(".long 0x7C0105E6 | ((%0 & 0x1F) << 21)" : "=r" (rt));
    return rt;
}

errlHndl_t nvdimmReadReg(Target* i_nvdimm,
                         uint16_t i_addr,
                         uint8_t & o_data,
                         const bool page_verify)
{
    size_t l_numBytes = 1;
    uint8_t l_reg_addr = i_addr & 0x00FF;
    uint8_t l_reg_page = (i_addr >> 8) & 0x000F;

    // If page_verify is true, make sure the current page is set to the page
    // where i_addr is in and change if needed
    if (page_verify)
    {
        uint8_t l_data = 0;
        nvdimmReadReg(i_nvdimm, OPEN_PAGE, l_data, NO_PAGE_VERIFY);

        if (l_data != l_reg_page)
        {
            nvdimmOpenPage(i_nvdimm, l_reg_page);
        }
    }

    DeviceFW_deviceOp(
        DeviceFW::READ, i_nvdimm, &o_data,
        l_numBytes, DEVICE_NVDIMM_RAW_ADDRESS(l_reg_addr));
}

errlHndl_t nvdimmOpenPage(Target *i_nvdimm,
                          uint8_t i_page)
{
    bool l_success = false;
    uint8_t l_data;
    uint32_t l_poll = 0;
    uint32_t l_target_timeout_values[6];

    uint32_t l_timeout = l_target_timeout_values[PAGE_SWITCH];
    // Open page reg is at the same address of every page
    nvdimmWriteReg(i_nvdimm, OPEN_PAGE, i_page, NO_PAGE_VERIFY);

    // This should not take long, but putting a loop here anyway
    // to make sure it finished within time
    // Not using the nvdimmPollStatus since this is polled differently
    do
    {
        nvdimmReadReg(i_nvdimm, OPEN_PAGE, l_data, NO_PAGE_VERIFY);

        if (l_data == i_page){
            l_success = true;
            return
        }
        sleep(0, 100ns);
        l_poll += 100;
    } while (l_poll < l_timeout*1000000);
}

errlHndl_t nvdimmWriteReg(Target* i_nvdimm,
                         uint16_t i_addr,
                         uint8_t i_data,
                         const bool page_verify)
{
    size_t l_numBytes = 1;
    uint8_t l_reg_addr = i_addr & 0x00FF;
    uint8_t l_reg_page = (i_addr >> 8) & 0x000F;

    // If page_verify is true, make sure the current page is set to the page
    // where i_addr is in and change if needed
    if (page_verify)
    {
        uint8_t l_data = 0;
        nvdimmReadReg(i_nvdimm, OPEN_PAGE, l_data, NO_PAGE_VERIFY);

        if (l_data != l_reg_page)
        {
            nvdimmOpenPage(i_nvdimm, l_reg_page);
        }
    }

    DeviceFW_deviceOp(
        DeviceFW::WRITE, i_nvdimm, &i_data,
        l_numBytes,
        static_cast<uint64_t>(l_reg_addr), 0);
}

errlHndl_t DeviceFW_deviceOp(OperationType i_opType,
                              TARGETING::Target* i_target,
                              void* io_buffer, size_t& io_buflen, ...)
{
    va_list args;
    va_start(args, io_buflen);

    Singleton<Associator>::instance().performOp(
            i_opType, i_target, io_buffer, io_buflen, args);

    va_end(args);
    return errl;

}

errlHndl_t Associator::performOp(OperationType i_opType,
                                     Target* i_target,
                                     void* io_buffer, size_t& io_buflen,
                                     va_list i_addr)
{
    TARGETING::TYPE l_devType =
        (i_target == MASTER_PROCESSOR_CHIP_TARGET_SENTINEL) ?
        TYPE_PROC : i_target->getAttr<ATTR_TYPE>();
    // Function pointer found for this route request.
    deviceOp_t l_devRoute = findDeviceRoute(i_opType, l_devType);
    (*l_devRoute)(i_opType, i_target, io_buffer, io_buflen, DeviceFW::NVDIMM_RAW, i_addr);
}

deviceOp_t Associator::findDeviceRoute(OperationType i_opType,
                                            TARGETING::TYPE i_devType)
{
    // The ranges of the parameters should all be verified by the
    // compiler due to the template specializations in driverif.H.
    // e.g. i_accessType can never be negative
    // No assert-checks will be done here.

    // Pointer to root of the map.
    const AssociationData* routeMap = iv_associations[iv_routeMap];
    const AssociationData* ops = iv_associations[routeMap[DeviceFW::NVDIMM_RAW].offset];

    // Check op type = WILDCARD registrations.
    if (0 != ops[WILDCARD].offset)
    {
        // Check access type = WILDCARD registrations.
        if (ops[WILDCARD].flag)
        {
            return iv_operations[iv_associations[ops[WILDCARD].offset]->offset];
        }
        // Check access type = i_target->type registrations.
        const AssociationData* targets = iv_associations[ops[WILDCARD].offset];
        if (targets[i_devType].flag)
        {
            return iv_operations[targets[i_devType].offset];
        }
    }

    // Check op type = i_opType registrations.
    if (0 != ops[i_opType].offset)
    {
        // Check access type = i_opType registrations.
        if(ops[i_opType].flag)
        {
            return iv_operations[iv_associations[ops[i_opType].offset]->offset];
        }

        // Check access type = i_target->type registrations.
        const AssociationData* targets = iv_associations[ops[i_opType].offset];
        if (targets[i_devType].flag)
        {
            return iv_operations[targets[i_devType].offset];
        }
    }
}
```
