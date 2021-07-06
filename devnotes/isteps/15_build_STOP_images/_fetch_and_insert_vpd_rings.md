```cpp
fapi2::ReturnCode _fetch_and_insert_vpd_rings(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_procTarget,
    void*           i_ringSection,
    uint32_t&       io_ringSectionSize,
    uint32_t        i_maxRingSectionSize,
    void*           i_overlaysSection,
    uint8_t         i_ddLevel,
    uint8_t         i_sysPhase,
    void*           i_vpdRing,
    uint32_t        i_vpdRingSize,
    void*           i_ringBuf2,
    uint32_t        i_ringBufSize2,
    void*           i_ringBuf3,
    uint32_t        i_ringBufSize3,
    uint8_t         i_chipletId,
    uint8_t         i_evenOdd,
    const RingIdList i_ring,
    uint8_t&        io_ringStatusInMvpd,
    bool&           i_bImgOutOfSpace,
    uint32_t&       io_bootCoreMask)
{
    MvpdKeyword l_mvpdKeyword = i_ring.vpdKeyword == VPD_KEYWORD_PDG ?
        fapi2::MVPD_KEYWORD_PDG
      : fapi2::MVPD_KEYWORD_PDR;

    memset(i_vpdRing,  0, i_vpdRingSize);
    memset(i_ringBuf2, 0, i_ringBufSize2);
    memset(i_ringBuf3, 0, i_ringBufSize3);

    getMvpdRing(
        i_procTarget,
        l_mvpdKeyword,
        i_chipletId,
        i_evenOdd,
        i_ring.ringId,
        (uint8_t*)i_vpdRing,
        i_vpdRingSize,
        (uint8_t*)i_ringBuf2,
        i_ringBufSize2);

    memset(i_ringBuf2, 0, i_ringBufSize2);

    io_ringStatusInMvpd = RING_FOUND;
    auto l_vpdChipletId = (be32toh(((CompressedScanData*)i_vpdRing)->iv_scanAddr) >> 24) & 0xFF;

    if(i_ring.vpdRingClass == VPD_RING_CLASS_GPTR_NEST
    || i_ring.vpdRingClass == VPD_RING_CLASS_GPTR_EQ
    || i_ring.vpdRingClass == VPD_RING_CLASS_GPTR_EX
    || i_ring.vpdRingClass == VPD_RING_CLASS_GPTR_EC)
    {
        process_gptr_rings(
            i_procTarget,
            i_overlaysSection,
            i_ddLevel,
            i_vpdRing,
            i_ringBuf2,
            i_ringBuf3);

        i_vpdRingSize = be16toh(((CompressedScanData*)i_vpdRing)->iv_size);
    }

    if (((CompressedScanData*)i_vpdRing)->iv_magic == htobe16(RS4_MAGIC))
    {
        int redundant = 0;
        rs4_redundant((CompressedScanData*)i_vpdRing, &redundant);
        if (redundant)
        {
            io_ringStatusInMvpd = RING_REDUNDANT;
            return;
        }
    }

    if(i_bImgOutOfSpace == true)
    {
        return;
    }

    if(io_ringSectionSize + i_vpdRingSize > i_maxRingSectionSize)
    {
        i_bImgOutOfSpace = true;
    }

    uint8_t l_chipletTorId =
        i_chipletId
        + ((i_chipletId - i_ring.instanceIdMin)
        * (i_ring.vpdRingClass == VPD_RING_CLASS_EX_INS ? 1 : 0))
        + i_evenOdd;

    PpeType_t l_ppeType;

    switch (i_sysPhase)
    {
        case SYSPHASE_HB_SBE:
            l_ppeType = PT_SBE;
            break;

        case SYSPHASE_RT_CME:
            l_ppeType = PT_CME;
            break;

        case SYSPHASE_RT_SGPE:
            l_ppeType = PT_SGPE;
            break;
    }

    tor_append_ring(
        i_ringSection,
        io_ringSectionSize, // In: Exact size. Out: Updated size.
        i_ringBuf2,
        i_ringBufSize2,  // Max size.
        i_ring.ringId,
        l_ppeType,
        RV_BASE,         // All VPD rings are Base ringVariant
        l_chipletTorId,  // Chiplet instance TOR Index
        i_vpdRing);      // The VPD RS4 ring container
}

static void tor_append_ring(
    void*           i_ringSection,      // Ring section ptr
    uint32_t&       io_ringSectionSize, // In: Exact size of ring section.
                                        // Out: Updated size of ring section.
    void*           i_ringBuffer,       // Ring work buffer
    const uint32_t  i_ringBufferSize,   // Max size of ring work buffer
    RingId_t        i_ringId,           // Ring ID
    PpeType_t       i_ppeType,          // SBE, CME, SGPE
    RingVariant_t   i_ringVariant,      // Base,CC,RL
    uint8_t         i_instanceId,       // Instance ID
    void*           i_rs4Container)     // RS4 ring container
{
    uint32_t   l_buf = 0;
    uint32_t*  l_buf_ptr = &l_buf;
    uint32_t   l_ringBlockSize;
    uint16_t   l_ringOffset16;
    uint32_t   l_torOffsetSlot;

    tor_access_ring(
        i_ringSection,
        i_ringId,
        UNDEFINED_DD_LEVEL,
        i_ppeType,
        i_ringVariant,
        i_instanceId,
        PUT_SINGLE_RING,
        &l_buf_ptr,
        l_torOffsetSlot);

    if(io_ringSectionSize - l_buf <= MAX_TOR_RING_OFFSET)
    {
        l_ringOffset16 = htobe16(io_ringSectionSize - l_buf);
        memcpy(i_ringSection + l_torOffsetSlot, &l_ringOffset16, l_ringOffset16);
        l_ringBlockSize = be16toh(((CompressedScanData*)i_rs4Container)->iv_size);
        memcpy(i_ringSection + io_ringSectionSize, i_rs4Container, l_ringBlockSize);
        io_ringSectionSize += l_ringBlockSize;
        TorHeader_t* torHeader = (TorHeader_t*)i_ringSection;
        torHeader->size = htobe32(be32toh(torHeader->size) + l_ringBlockSize);
    }
}

static void tor_access_ring(
    TorHeader_t*    i_ringSection,     // Ring section ptr
    RingId_t        i_ringId,          // Ring ID
    uint8_t         i_ddLevel,         // DD level
    PpeType_t       i_ppeType,         // SBE,CME,SGPE
    RingVariant_t   i_ringVariant,     // Base,CC,RL (SBE,CME,SGPE only)
    uint8_t&        io_instanceId,     // Instance ID
    RingBlockType_t i_ringBlockType,   // GET_SINGLE_RING,GET_PPE_LEVEL_RINGS,etc
    void**          io_ringBlockPtr,   // Ring data buffer
    uint32_t&       io_ringBlockSize)  // Size of ring data
{
    uint8_t* postHeaderStart = (uint8_t*)i_ringSection + sizeof(TorHeader_t);

    if (be32toh(i_ringSection->magic) >> 8 != TOR_MAGIC
    || i_ringSection->version > TOR_VERSION
    || i_ringSection->version == 0
    || i_ringSection->chipType >= NUM_CHIP_TYPES)
    {
        return;
    }

    if(i_ddLevel != i_ringSection->ddLevel
    && i_ddLevel != UNDEFINED_DD_LEVEL)
    {
        return;
    }

    if (i_ringBlockType == GET_SINGLE_RING
    || (i_ringBlockType == PUT_SINGLE_RING
        && (be32toh(i_ringSection->magic) == TOR_MAGIC_SBE
         || be32toh(i_ringSection->magic) == TOR_MAGIC_CME
         || be32toh(i_ringSection->magic) == TOR_MAGIC_SGPE)))
    {
        void* l_ringSection = i_ringSection;
        if (be32toh(i_ringSection->magic) == TOR_MAGIC_HW)
        {
            TorPpeBlock_t*  torPpeBlock;
            torPpeBlock = (TorPpeBlock_t*)(postHeaderStart + i_ppeType * sizeof(TorPpeBlock_t));
            l_ringSection = (void*)(postHeaderStart + be32toh(torPpeBlock->offset));
        }

        get_ring_from_ring_section(
            l_ringSection,
            i_ringId,
            i_ringVariant,
            io_instanceId,
            i_ringBlockType,
            io_ringBlockPtr,
            io_ringBlockSize);
    }
}

static void get_ring_from_ring_section(
    void*           i_ringSection,     // Ring section ptr
    RingId_t        i_ringId,          // Ring ID
    RingVariant_t   i_ringVariant,     // Base,CC,RL (SBE,CME,SGPE only)
    uint8_t&        io_instanceId,     // Instance ID
    RingBlockType_t i_ringBlockType,   // Single ring, Block
    void**          io_ringBlockPtr,   // Output ring buffer
    uint32_t&       io_ringBlockSize)  // Size of ring data
{
    uint8_t           iInst, iRing, iVariant;
    TorHeader_t*      torHeader = (TorHeader_t*)i_ringSection;
    uint32_t          torMagic;
    uint8_t           torVersion;
    uint8_t           chipType;
    TorCpltBlock_t*   cpltBlock;
    TorCpltOffset_t   cpltOffset; // Offset from ringSection to chiplet section
    TorRingOffset_t   ringOffset; // Offset to actual ring container
    uint32_t          torSlotNum; // TOR slot number (within a chiplet section)
    uint32_t          ringSize;   // Size of whole ring container/block.
    RingVariantOrder* ringVariantOrder;
    RingId_t          numRings;
    GenRingIdList*    ringIdListCommon;
    GenRingIdList*    ringIdListInstance;
    GenRingIdList*    ringIdList;
    uint8_t           bInstCase = 0;
    ChipletData_t*    cpltData;
    uint8_t           numVariants;
    ChipletType_t     numChiplets;
    RingProperties_t* ringProps;

    ringid_get_noof_chiplets(
        torHeader->chipType,
        be32toh(torHeader->magic),
        &numChiplets);

    for (ChipletType_t iCplt = 0; iCplt < numChiplets; iCplt++)
    {
        ringid_get_properties(
            torHeader->chipType,
            be32toh(torHeader->magic),
            torHeader->version,
            iCplt,
            &cpltData,
            &ringIdListCommon,
            &ringIdListInstance,
            &ringVariantOrder,
            &ringProps,
            &numVariants );
        for (bInstCase = 0; bInstCase <= 1; bInstCase++)
        {
            numRings = bInstCase ? cpltData->iv_num_instance_rings : cpltData->iv_num_common_rings;
            ringIdList = bInstCase ? ringIdListInstance : ringIdListCommon;
            if (torHeader->version >= 7)
            {
                numVariants = bInstCase ? 1 : numVariants;
            }

            if (ringIdList)
            {
                cpltOffset =
                    sizeof(TorHeader_t)
                  + be32toh(
                      *(uint32_t*)((uint8_t*)i_ringSection
                    + sizeof(TorHeader_t)
                    + iCplt * sizeof(TorCpltBlock_t)
                    + bInstCase * sizeof(cpltBlock->cmnOffset)));

                torSlotNum = 0;

                for(ringInstance = ringIdList->instanceIdMin;
                    ringInstance <= ringIdList->instanceIdMax;
                    ringInstance++)
                {
                    for (ringIndex = 0; ringIndex < numRings; ringIndex++)
                    {
                        for (varianIndex = 0; varianIndex < numVariants; varianIndex++)
                        {
                            if(strcmp((ringIdList + ringIndex)->ringName, ringProps[i_ringId].iv_name ) == 0
                            && (i_ringVariant == ringVariantOrder->variant[varianIndex] || numVariants == 1)
                            && (!bInstCase || (bInstCase && ringInstance == io_instanceId)))
                            {
                                ringOffset = cpltOffset + torSlotNum * sizeof(ringOffset);
                                ringOffset = *(TorRingOffset_t*)((uint8_t*)i_ringSection + ringOffset);
                                ringOffset = be16toh(ringOffset);

                                if (i_ringBlockType == GET_SINGLE_RING)
                                {
                                    ringSize = 0;
                                    if (ringOffset)
                                    {
                                        ringOffset = cpltOffset + ringOffset;
                                        ringSize = be16toh(((CompressedScanData*)((uint8_t*)i_ringSection + ringOffset))->iv_size);

                                        if (io_ringBlockSize == 0)
                                        {
                                            io_ringBlockSize = ringSize;
                                            return;
                                        }

                                        if (io_ringBlockSize < ringSize)
                                        {
                                            return;
                                        }

                                        memcpy(*io_ringBlockPtr, (uint8_t*)i_ringSection + ringOffset, ringSize);
                                        io_ringBlockSize = ringSize;
                                        io_instanceId = (bInstCase) ? io_instanceId : (ringIdList + ringIndex)->instanceIdMin;
                                    }
                                    return;
                                }
                                else if (i_ringBlockType == PUT_SINGLE_RING)
                                {
                                    if (ringOffset)
                                    {
                                        return;
                                    }
                                    memcpy(*io_ringBlockPtr, &cpltOffset, sizeof(cpltOffset));
                                    io_ringBlockSize = cpltOffset + torSlotNum * sizeof(ringOffset);

                                    return;
                                }
                                else
                                {
                                    return;
                                }
                            }
                            torSlotNum++;
                        }
                    }
                }
            }
        }
    }
}

static void ringid_get_properties(
    ChipType_t         i_chipType
    uint32_t           i_torMagic
    uint8_t            i_torVersion
    ChipletType_t      i_chipletType
    ChipletData_t**    o_chipletData
    GenRingIdList**    o_ringIdListCommon
    GenRingIdList**    o_ringIdListInstance
    RingVariantOrder** o_ringVariantOrder
    RingProperties_t** o_ringProps
    uint8_t*           o_numVariants)
{
    switch (i_chipType)
    {
        case CT_P9N:
        case CT_P9C:
        case CT_P9A:
            if(i_torMagic == TOR_MAGIC_SBE
            || i_torMagic == TOR_MAGIC_OVRD
            || i_torMagic == TOR_MAGIC_OVLY)
            {
                P9_RID::ringid_get_chiplet_properties(
                    i_chipletType,
                    o_chipletData,
                    o_ringIdListCommon,
                    o_ringIdListInstance,
                    o_ringVariantOrder,
                    o_numVariants);

                if(i_torVersion < 7
                && (i_chipletType == P9_RID::EQ_TYPE
                    || i_chipletType == P9_RID::EC_TYPE))
                {
                    *o_numVariants = *o_numVariants - 3;
                }

                if(i_torMagic == TOR_MAGIC_OVRD
                || i_torMagic == TOR_MAGIC_OVLY)
                {
                    *o_numVariants = 1;
                }
            }
            else if(i_torMagic == TOR_MAGIC_CME)
            {
                *o_chipletData        = (ChipletData_t*)&P9_RID::EC::g_chipletData;
                *o_ringIdListCommon   = (GenRingIdList*)P9_RID::EC::RING_ID_LIST_COMMON;
                *o_ringIdListInstance = (GenRingIdList*)P9_RID::EC::RING_ID_LIST_INSTANCE;
                *o_ringVariantOrder   = (RingVariantOrder*)P9_RID::EC::RING_VARIANT_ORDER;
                *o_numVariants        = P9_RID::EC::g_chipletData.iv_num_common_ring_variants;

                if (i_torVersion < 7)
                {
                    *o_numVariants = *o_numVariants - 3;
                }
            }
            else if(i_torMagic == TOR_MAGIC_SGPE)
            {
                *o_chipletData        = (ChipletData_t*)&P9_RID::EQ::g_chipletData;
                *o_ringIdListCommon   = (GenRingIdList*)P9_RID::EQ::RING_ID_LIST_COMMON;
                *o_ringIdListInstance = (GenRingIdList*)P9_RID::EQ::RING_ID_LIST_INSTANCE;
                *o_ringVariantOrder   = (RingVariantOrder*)P9_RID::EQ::RING_VARIANT_ORDER;
                *o_numVariants        = P9_RID::EQ::g_chipletData.iv_num_common_ring_variants;

                if (i_torVersion < 7)
                {
                    *o_numVariants = *o_numVariants - 3;
                }
            }
            else
            {
                return;
            }
            *o_ringProps = (RingProperties_t*)P9_RID::RING_PROPERTIES;
            break;
        case CT_CEN:
            if ( i_torMagic == TOR_MAGIC_CEN ||
                 i_torMagic == TOR_MAGIC_OVRD )
            {
                CEN_RID::ringid_get_chiplet_properties(
                    i_chipletType,
                    o_chipletData,
                    o_ringIdListCommon,
                    o_ringIdListInstance,
                    o_ringVariantOrder,
                    o_numVariants);

                if(i_torMagic == TOR_MAGIC_OVRD)
                {
                    *o_numVariants = 1;
                }
            }
            else
            {
                return;
            }
            *o_ringProps = (RingProperties_t*)CEN_RID::RING_PROPERTIES;
            break;
        default:
            return;
    }
    return;
}

void CEN_RID::ringid_get_chiplet_properties(
    ChipletType_t      i_chipletType,
    ChipletData_t**    o_cpltData,
    GenRingIdList**    o_ringComm,
    GenRingIdList**    o_ringInst,
    RingVariantOrder** o_varOrder,
    uint8_t*           o_numVariants)
{
    switch (i_chipletType)
    {
        case CEN_TYPE :
            *o_cpltData = (ChipletData_t*)   &CEN::g_chipletData;
            *o_ringComm = (GenRingIdList*)    CEN::RING_ID_LIST_COMMON;
            *o_ringInst = NULL;
            *o_varOrder = (RingVariantOrder*) CEN::RING_VARIANT_ORDER;
            *o_numVariants = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        default :
            *o_cpltData = NULL;
            *o_ringComm = NULL;
            *o_ringInst = NULL;
            *o_varOrder = NULL;
            *o_numVariants = 0;
            break;
    }
}

void P9_RID::ringid_get_chiplet_properties(
    ChipletType_t      i_chipletType,
    ChipletData_t**    o_cpltData,
    GenRingIdList**    o_ringComm,
    GenRingIdList**    o_ringInst,
    RingVariantOrder** o_varOrder,
    uint8_t*           o_numVariants)
{
    switch (i_chipletType)
    {
        case PERV_TYPE :
            *o_cpltData = (ChipletData_t*)   &PERV::g_chipletData;
            *o_ringComm = (GenRingIdList*)    PERV::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    PERV::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) PERV::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case N0_TYPE :
            *o_cpltData = (ChipletData_t*)   &N0::g_chipletData;
            *o_ringComm = (GenRingIdList*)    N0::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    N0::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) N0::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case N1_TYPE :
            *o_cpltData = (ChipletData_t*)   &N1::g_chipletData;
            *o_ringComm = (GenRingIdList*)    N1::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    N1::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) N1::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case N2_TYPE :
            *o_cpltData = (ChipletData_t*)   &N2::g_chipletData;
            *o_ringComm = (GenRingIdList*)    N2::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    N2::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) N2::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case N3_TYPE :
            *o_cpltData = (ChipletData_t*)   &N3::g_chipletData;
            *o_ringComm = (GenRingIdList*)    N3::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    N3::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) N3::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case XB_TYPE :
            *o_cpltData = (ChipletData_t*)   &XB::g_chipletData;
            *o_ringComm = (GenRingIdList*)    XB::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    XB::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) XB::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case MC_TYPE :
            *o_cpltData = (ChipletData_t*)   &MC::g_chipletData;
            *o_ringComm = (GenRingIdList*)    MC::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    MC::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) MC::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case OB0_TYPE :
            *o_cpltData = (ChipletData_t*)   &OB0::g_chipletData;
            *o_ringComm = (GenRingIdList*)    OB0::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    OB0::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) OB0::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case OB1_TYPE :
            *o_cpltData = (ChipletData_t*)   &OB1::g_chipletData;
            *o_ringComm = (GenRingIdList*)    OB1::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    OB1::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) OB1::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case OB2_TYPE :
            *o_cpltData = (ChipletData_t*)   &OB2::g_chipletData;
            *o_ringComm = (GenRingIdList*)    OB2::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    OB2::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) OB2::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case OB3_TYPE :
            *o_cpltData = (ChipletData_t*)   &OB3::g_chipletData;
            *o_ringComm = (GenRingIdList*)    OB3::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    OB3::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) OB3::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case PCI0_TYPE :
            *o_cpltData = (ChipletData_t*)   &PCI0::g_chipletData;
            *o_ringComm = (GenRingIdList*)    PCI0::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    PCI0::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) PCI0::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case PCI1_TYPE :
            *o_cpltData = (ChipletData_t*)   &PCI1::g_chipletData;
            *o_ringComm = (GenRingIdList*)    PCI1::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    PCI1::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) PCI1::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case PCI2_TYPE :
            *o_cpltData = (ChipletData_t*)   &PCI2::g_chipletData;
            *o_ringComm = (GenRingIdList*)    PCI2::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    PCI2::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) PCI2::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case EQ_TYPE :
            *o_cpltData = (ChipletData_t*)   &EQ::g_chipletData;
            *o_ringComm = (GenRingIdList*)    EQ::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    EQ::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) EQ::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        case EC_TYPE :
            *o_cpltData = (ChipletData_t*)   &EC::g_chipletData;
            *o_ringComm = (GenRingIdList*)    EC::RING_ID_LIST_COMMON;
            *o_ringInst = (GenRingIdList*)    EC::RING_ID_LIST_INSTANCE;
            *o_varOrder = (RingVariantOrder*) EC::RING_VARIANT_ORDER;
            *o_numVariants  = (*(*o_cpltData)).iv_num_common_ring_variants;
            break;

        default :
            *o_cpltData = NULL;
            *o_ringComm = NULL;
            *o_ringInst = NULL;
            *o_varOrder = NULL;
            *o_numVariants = 0;
            break;
    }
}

void ringid_get_noof_chiplets(
    ChipType_t  i_chipType,
    uint32_t    i_torMagic,
    uint8_t*    o_numChiplets )
{
    switch (i_chipType)
    {
        case CT_P9N:
        case CT_P9C:
        case CT_P9A:
            if(i_torMagic == TOR_MAGIC_SBE
            || i_torMagic == TOR_MAGIC_OVRD
            || i_torMagic == TOR_MAGIC_OVLY)
            {
                *o_numChiplets = P9_RID::SBE_NOOF_CHIPLETS;
            }
            else if ( i_torMagic == TOR_MAGIC_CME)
            {
                *o_numChiplets = P9_RID::CME_NOOF_CHIPLETS;
            }
            else if ( i_torMagic == TOR_MAGIC_SGPE)
            {
                *o_numChiplets = P9_RID::SGPE_NOOF_CHIPLETS;
            }
            else
            {
                return TOR_INVALID_MAGIC_NUMBER;
            }
            break;

        case CT_CEN:
            if(i_torMagic == TOR_MAGIC_CEN
            || i_torMagic == TOR_MAGIC_OVRD )
            {
                *o_numChiplets = CEN_RID::CEN_NOOF_CHIPLETS;
            }
            else
            {
                return TOR_INVALID_MAGIC_NUMBER;
            }
            break;
    }
}

int rs4_redundant(const CompressedScanData* i_data, int* o_redundant)
{
    uint8_t* data = (uint8_t*)i_data + sizeof(CompressedScanData);
    uint32_t length;
    uint32_t pos = stop_decode(&length, data, 0);

    *o_redundant = 0;

    if (rs4_get_nibble(data, pos) == 0)
    {
        if (rs4_get_nibble(data, pos + 1) == 0)
        {
            *o_redundant = 1;
        }
        else if (rs4_get_nibble(data, pos + 2) == 0)
        {
            *o_redundant = 1;
        }
    }
}

static int rs4_get_nibble(const uint8_t* i_string, const uint32_t index)
{
    uint8_t byte = i_string[index / 2];
    if (index % 2)
    {
        return byte & 0xf;
    }
    else
    {
        return byte >> 4;
    }
    return nibble;
}

static int stop_decode(uint32_t* o_count, const uint8_t* i_string, const uint32_t index)
{
    int digits = 0, nibble;
    uint32_t count = 0;

    for(int nibble; nibble & 0x8 == 0;)
    {
        nibble = rs4_get_nibble(i_string, index);
        count = (count * 8) + (nibble & 0x7);
        index++;
        digits++;
    }
    *o_count = count;
    return digits;
}

void process_gptr_rings(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_procTarget,
    void*   i_overlaysSection,
    uint8_t i_ddLevel,
    void*   io_vpdRing,
    void*   io_ringBuf2,
    void*   io_ringBuf3)
{
    uint32_t l_ovlyUncmpSize = 0;
    void* l_ovlyRs4Ring = io_ringBuf2;
    void* l_ovlyRawRing = io_ringBuf3;

    get_overlays_ring(
        i_procTarget,
        i_overlaysSection,
        i_ddLevel,
        (RingId_t)be16toh(((CompressedScanData*)io_vpdRing)->iv_ringId),
        &l_ovlyRs4Ring,
        &l_ovlyRawRing,
        &l_ovlyUncmpSize);

    if (l_ovlyRs4Ring == io_ringBuf2 && l_ovlyRawRing == io_ringBuf3)
    {
        apply_overlays_ring(
            i_procTarget,
            io_vpdRing,
            io_ringBuf2,
            l_ovlyRawRing,
            l_ovlyUncmpSize);
    }
}

void apply_overlays_ring(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_procTarget,
    void*    io_vpdRing,
    void*    io_ringBuf2,
    void*    i_ovlyRawRing,
    uint32_t i_ovlyUncmpSize)
{
    uint8_t* dataVpd = (uint8_t*)io_ringBuf2;
    uint8_t* careVpd = (uint8_t*)io_ringBuf2 + MAX_RING_BUF_SIZE / 2;
    uint8_t* dataOvly = NULL;
    uint8_t* careOvly = NULL;
    uint32_t vpdUncmpSize = 0;

    dataOvly = (uint8_t*)i_ovlyRawRing;
    careOvly = (uint8_t*)i_ovlyRawRing + MAX_RING_BUF_SIZE / 2;

    _rs4_decompress(
        dataVpd,
        careVpd,
        MAX_RING_BUF_SIZE / 2,
        &vpdUncmpSize,
        (CompressedScanData*)io_vpdRing);

    int i;
    for (i = 0; i < vpdUncmpSize / 8; i++)
    {
        if (careOvly[i] > 0)
        {
            for (int j = 0; j < 8; j++)
            {
                if (careOvly[i] & (0x80 >> j))
                {
                    if (dataOvly[i] & (0x80 >> j))
                    {
                        dataVpd[i] |= (0x80 >> j);
                        careVpd[i] |= (0x80 >> j);
                    }
                    else
                    {
                        dataVpd[i] &= ~(0x80 >> j);
                        careVpd[i] &= ~(0x80 >> j);
                    }
                }
            }
        }
    }

    if (vpdUncmpSize % 8)
    {
        i = (int)vpdUncmpSize / 8;

        careOvly[i] &= ~(0xFF << (8 - (vpdUncmpSize % 8)));

        if (careOvly[i] > 0)
        {
            for (int j = 0; j < (int)vpdUncmpSize % 8; j++)
            {
                if (careOvly[i] & (0x80 >> j))
                {
                    if(dataOvly[i] & (0x80 >> j))
                    {
                        dataVpd[i] |= (0x80 >> j);
                        careVpd[i] |= (0x80 >> j);
                    }
                    else
                    {
                        dataVpd[i] &= ~(0x80 >> j);
                        careVpd[i] &= ~(0x80 >> j);
                    }
                }
            }
        }
    }
    _rs4_compress(
        (CompressedScanData*)io_vpdRing,
        MAX_RING_BUF_SIZE,
        dataVpd,
        careVpd,
        vpdUncmpSize,
        be32toh(((CompressedScanData*)io_vpdRing)->iv_scanAddr),
        be16toh(((CompressedScanData*)io_vpdRing)->iv_ringId));
}

static void _rs4_compress(
    CompressedScanData* io_rs4,
    const uint32_t i_size,
    const uint8_t* i_data_str,
    const uint8_t* i_care_str,
    const uint32_t i_length,
    const uint32_t i_scanAddr,
    const RingId_t i_ringId)
{
    uint32_t nibbles = rs4_max_compressed_nibbles(i_length);
    uint8_t* rs4_str = (uint8_t*)io_rs4 + sizeof(CompressedScanData);

    memset(io_rs4, 0, i_size);
    __rs4_compress(rs4_str, &nibbles, i_data_str, i_care_str, i_length);

    io_rs4->iv_magic    = htobe16(RS4_MAGIC);
    io_rs4->iv_version  = RS4_VERSION;
    io_rs4->iv_type     = RS4_SCAN_DATA_TYPE_NON_CMSK;
    io_rs4->iv_size     = htobe16(rs4_max_compressed_bytes(nibbles));
    io_rs4->iv_ringId   = htobe16(i_ringId);
    io_rs4->iv_scanAddr = htobe32(i_scanAddr);
}

static void __rs4_compress(
    uint8_t* o_rs4_str,
    uint32_t* o_nibbles,
    const uint8_t* i_data_str,
    const uint8_t* i_care_str,
    const uint32_t i_length)
{
    int state = 0;
    uint32_t j = 0;
    uint32_t k = 0;
    uint32_t count = 0;
    int care_nibble = 0;
    int data_nibble = 0;

    for(uint32_t i = 0; i < i_length / 4;)
    {
        care_nibble = rs4_get_nibble(i_care_str, i);
        data_nibble = rs4_get_nibble(i_data_str, i);

        if (state == 0)
        {
            if (care_nibble == 0)
            {
                count++;
                i++;
            }
            else
            {
                j += rs4_stop_encode(count, o_rs4_str, j);
                count = 0;
                k = j;
                j++;

                if ((care_nibble ^ data_nibble) == 0)
                {
                    state = 1;
                }
                else
                {
                    state = 2;
                }
            }
        }
        else if (state == 1)
        {
            if (care_nibble == 0)
            {
                if (((i + 1) < i_length / 4) && (rs4_get_nibble(i_care_str, i + 1) == 0))
                {
                    rs4_set_nibble(o_rs4_str, k, count);
                    count = 0;
                    state = 0;
                }
                else
                {
                    rs4_set_nibble(o_rs4_str, j, 0);
                    count++;
                    i++;
                    j++;
                }
            }
            else if ((care_nibble ^ data_nibble) == 0)
            {
                rs4_set_nibble(o_rs4_str, j, data_nibble);
                count++;
                i++;
                j++;
            }
            else
            {
                rs4_set_nibble(o_rs4_str, k, count);
                count = 0;
                state = 0;
            }

            if (state == 1 && count == 14)
            {
                rs4_set_nibble(o_rs4_str, k, 14);
                count = 0;
                state = 0;
            }
        }
        else
        {
            rs4_set_nibble(o_rs4_str, k, 15);
            rs4_set_nibble(o_rs4_str, j, care_nibble);
            j++;
            rs4_set_nibble(o_rs4_str, j, data_nibble);
            i++;
            j++;
            count = 0;
            state = 0;
        }
    }

    if (state == 0)
    {
        j += rs4_stop_encode(count, o_rs4_str, j);
    }
    else if (state == 1)
    {
        rs4_set_nibble(o_rs4_str, k, count);
        j += rs4_stop_encode(0, o_rs4_str, j);
    }

    rs4_set_nibble(o_rs4_str, j, 0);
    j++;
    if (i_length % 4 == 0)
    {
        rs4_set_nibble(o_rs4_str, j, i_length % 4);
        j++;
    }
    else
    {
        care_nibble = rs4_get_nibble(i_care_str, i_length / 4) & ((0xf >> (4 - i_length % 4)) << (4 - i_length % 4));
        data_nibble = rs4_get_nibble(i_data_str, i_length / 4) & ((0xf >> (4 - i_length % 4)) << (4 - i_length % 4));

        if ((care_nibble ^ data_nibble) == 0)
        {
            rs4_set_nibble(o_rs4_str, j, i_length % 4);
            j++;
            rs4_set_nibble(o_rs4_str, j, data_nibble);
            j++;
        }
        else
        {
            rs4_set_nibble(o_rs4_str, j, i_length % 4 + 8);
            j++;
            rs4_set_nibble(o_rs4_str, j, care_nibble);
            j++;
            rs4_set_nibble(o_rs4_str, j, data_nibble);
            j++;
        }
    }
    *o_nibbles = j;
}

static int rs4_set_nibble(uint8_t* io_string, const uint32_t index, const int i_nibble)
{
    if (index % 2)
    {
        io_string[index / 2] = (io_string[index / 2] & 0xf0) | i_nibble;
    }
    else
    {
        io_string[index / 2] = (io_string[index / 2] & 0x0f) | (i_nibble << 4);
    }
    return i_nibble;
}

static int rs4_stop_encode(const uint32_t i_count, uint8_t* io_string, const uint32_t index)
{
    uint32_t count = i_count >> 3;
    int digits = 1;
    int offset;

    while (count)
    {
        count >>= 3;
        digits++;
    }

    offset = digits - 1;
    rs4_set_nibble(io_string, index + offset, (i_count & 0x7) | 0x8);

    count = i_count >> 3;
    offset--;

    while (count)
    {
        rs4_set_nibble(io_string, index + offset, count & 0x7);
        offset--;
        count >>= 3;
    }

    return digits;
}

static inline uint32_t rs4_max_compressed_nibbles(const uint32_t i_length)
{
    return ((i_length + 3) / 4) * 4 + 3;
}

static inline uint32_t rs4_max_compressed_bytes(uint32_t nibbles)
{
    return (((((nibbles + 1) / 2) + sizeof(CompressedScanData)) + 3) / 4) * 4;
}

int _rs4_decompress(
    uint8_t* o_data_str,
    uint8_t* o_care_str,
    uint32_t i_size,
    uint32_t* o_length,
    const CompressedScanData* i_rs4)
{
    memset(o_data_str, 0, i_size);
    memset(o_care_str, 0, i_size);
    return __rs4_decompress(
        o_data_str,
        o_care_str,
        i_size,
        o_length,
        (uint8_t*)i_rs4 + sizeof(CompressedScanData));
}

static int __rs4_decompress(
    uint8_t* o_data_str,
    uint8_t* o_care_str,
    uint32_t i_size,
    uint32_t* o_length,
    const uint8_t* i_rs4_str)
{
    int state;                  /* 0 : Rotate, 1 : Scan */
    uint32_t i;                 /* Nibble index in i_rs4_str */
    uint32_t j;                 /* Nibble index in o_data_str/o_care_str */
    uint32_t k;                 /* Loop index */
    uint32_t bits;              /* Number of output bits decoded so far */
    uint32_t count;             /* Count of rotate nibbles */
    uint32_t nibbles;           /* Rotate encoding or scan nibbles to process */

    i = 0;
    j = 0;
    bits = 0;
    state = 0;
    if (state == 0)
    {
        nibbles = stop_decode(&count, i_rs4_str, i);
        i += nibbles;
        bits += 4 * count;
        j += count;
        state = 1;
    }
    else
    {
        nibbles = rs4_get_nibble(i_rs4_str, i);
        i++;

        if (nibbles == 0)
        {
            break;
        }

        nibbles = nibbles == 15 ? 1 : nibbles;
        bits += 4 * nibbles;

        for (k = 0; k < nibbles; k++)
        {
            rs4_set_nibble(o_care_str, j, rs4_get_nibble(i_rs4_str, i));
            i = (nibbles == 15 ? i + 1 : i);
            rs4_set_nibble(o_data_str, j, rs4_get_nibble(i_rs4_str, i));
            i++;
            j++;
        }

        state = 0;
    }

    nibbles = rs4_get_nibble(i_rs4_str, i);
    i++;

    bits += nibbles & 0x3;

    if (nibbles & 0x3 != 0)
    {
        rs4_set_nibble(o_care_str, j, rs4_get_nibble(i_rs4_str, i));
        i = (nibbles & 0x8 ? i + 1 : i);
        rs4_set_nibble(o_data_str, j, rs4_get_nibble(i_rs4_str, i));
    }
    *o_length = bits;
    return SCAN_COMPRESSION_OK;
}

void get_overlays_ring(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_procTarget,
    void*     i_overlaysSection,
    uint8_t   i_ddLevel,
    RingId_t  i_ringId,
    void**    io_ringBuf2,
    void**    io_ringBuf3,
    uint32_t* o_ovlyUncmpSize)
{
    ReturnCode l_fapiRc = fapi2::FAPI2_RC_SUCCESS;
    uint32_t l_maxRingByteSize = MAX_RING_BUF_SIZE;
    uint32_t l_ovlyUncmpSize = 0;
    uint8_t  l_instanceId = 0;
    uint32_t l_ringBlockSize = 0xFFFFFFFF;

    tor_get_single_ring(
        i_overlaysSection,
        i_ddLevel,
        i_ringId,
        UNDEFINED_PPE_TYPE,
        UNDEFINED_RING_VARIANT,
        l_instanceId,
        io_ringBuf2,
        l_ringBlockSize);

    _rs4_decompress(
        (uint8_t*)(*io_ringBuf3),
        (uint8_t*)(*io_ringBuf3) + l_maxRingByteSize / 2,
        l_maxRingByteSize / 2,
        &l_ovlyUncmpSize,
        (CompressedScanData*)(*io_ringBuf2));
    *o_ovlyUncmpSize = l_ovlyUncmpSize;
}

static void tor_get_single_ring(
    void*         i_ringSection,     // Ring section ptr
    uint8_t       i_ddLevel,         // DD level
    RingId_t      i_ringId,          // Ring ID
    PpeType_t     i_ppeType,         // SBE, CME, SGPE
    RingVariant_t i_ringVariant,     // Base,CC,RL (SBE/CME/SGPE only)
    uint8_t       i_instanceId,      // Instance ID
    void**        io_ringBlockPtr,   // Output ring buffer
    uint32_t&     io_ringBlockSize)  // Size of ring data
{
    tor_access_ring(
        i_ringSection,
        i_ringId,
        i_ddLevel,
        i_ppeType,
        i_ringVariant,
        i_instanceId,
        GET_SINGLE_RING,
        io_ringBlockPtr,
        io_ringBlockSize);
}

static void getMvpdRing(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_fapiTarget,
    fapi2::MvpdKeyword  i_keyword,
    const uint8_t       i_chipletId,
    const uint8_t       i_evenOdd,
    const RingId_t      i_ringId,
    uint8_t*            o_pRingBuf,
    uint32_t&           io_rRingBufsize,
    uint8_t*            i_pTempBuf,
    uint32_t            i_tempBufsize)
{
    mvpdRingFunc(
        i_fapiTarget,
        i_keyword,
        i_chipletId,
        i_evenOdd,
        i_ringId,
        o_pRingBuf,
        io_rRingBufsize,
        i_pTempBuf,
        i_tempBufsize);
}

static void mvpdRingFunc(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>
    & i_fapiTarget,
    const mvpdRingFuncOp i_mvpdRingFuncOp,
    const uint8_t        i_chipletId,
    const uint8_t        i_evenOdd,
    const RingId_t       i_ringId,
    uint8_t*             o_pRingBuf,
    uint32_t&            io_rRingBufsize,
    uint8_t*             i_pTempBuf,
    uint32_t             i_tempBufsize )
{
    uint32_t                l_recordLen  = 0;
    uint8_t*                l_recordBuf  = NULL;
    uint8_t*                l_pRing      = NULL;
    uint32_t                l_ringLen    = 0;

    getMvpdField(
        i_fapiTarget,
        NULL,
        l_recordLen);

    if(i_pTempBuf)
    {
        l_recordBuf = i_pTempBuf;
    }
    else
    {
        l_recordBuf = malloc(l_recordLen);
    }

    getMvpdField(
        i_fapiTarget,
        l_recordBuf,
        l_recordLen );

    mvpdRingFuncFind(
        i_fapiTarget,
        i_chipletId,
        i_evenOdd,
        i_ringId,
        l_recordBuf,
        l_recordLen,
        l_pRing,
        l_ringLen);

    if (i_mvpdRingFuncOp == MVPD_RING_GET)
    {
        if (l_ringLen != 0)
        {
            mvpdRingFuncGet(
                l_pRing,
                l_ringLen,
                o_pRingBuf,
                io_rRingBufsize);
        }
    }
    else
    {
        mvpdRingFuncSet(
            i_fapiTarget,
            l_recordBuf,
            l_recordLen,
            l_pRing,
            l_ringLen,
            o_pRingBuf,
            io_rRingBufsize);

        setMvpdField(
            i_fapiTarget,
            l_recordBuf,
            l_recordLen);
    }
    if((i_pTempBuf == nullptr) && l_recordBuf)
    {
        free(l_recordBuf);
        l_recordBuf = NULL;
    }
}

static void setMvpdField(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> &i_procTarget,
    const uint8_t * const i_pBuffer,
    const uint32_t i_fieldSize)
{
    uint8_t l_recIndex = MVPD_INVALID_CHIP_UNIT;
    uint8_t l_keyIndex = MVPD_INVALID_CHIP_UNIT;
    errlHndl_t l_errl = NULL;
    MVPD::mvpdRecord l_hbRecord = MVPD::MVPD_INVALID_RECORD;
    fapi2::MvpdRecordXlate(MVPD_RING_GET, l_hbRecord, l_recIndex);
    MVPD::mvpdKeyword l_hbKeyword = MVPD::INVALID_MVPD_KEYWORD;
    fapi2::MvpdKeywordXlate(MVPD_RECORD_CP00, l_hbKeyword, l_keyIndex);
    size_t l_fieldLen = i_fieldSize;
    deviceWrite(i_procTarget.get(), i_pBuffer, l_fieldLen, DEVICE_MVPD_ADDRESS(l_hbRecord, l_hbKeyword));
}

static void MvpdKeywordXlate(
    const fapi2::MvpdKeyword i_fapiKeyword,
    MVPD::mvpdKeyword & o_hbKeyword,
    uint8_t & o_keywordIndex)
{
    struct mvpdKeywordToHb
    {
        MVPD::mvpdKeyword keyword;
        uint8_t keywordIndex;
    };
    static const mvpdKeywordToHb mvpdFapiKeywordToHbKeyword[] =
    {
        {MVPD::VD,  MVPD_KEYWORD_VD},
        {MVPD::ED,  MVPD_KEYWORD_ED},
        {MVPD::TE,  MVPD_KEYWORD_TE},
        {MVPD::DD,  MVPD_KEYWORD_DD},
        {MVPD::INVALID_MVPD_KEYWORD,  MVPD_KEYWORD_PDP},
        {MVPD::INVALID_MVPD_KEYWORD,  MVPD_KEYWORD_ST},
        {MVPD::DN,  MVPD_KEYWORD_DN},
        {MVPD::PG,  MVPD_KEYWORD_PG},
        {MVPD::PK,  MVPD_KEYWORD_PK},
        {MVPD::pdR, MVPD_KEYWORD_PDR},
        {MVPD::pdV, MVPD_KEYWORD_PDV},
        {MVPD::pdH, MVPD_KEYWORD_PDH},
        {MVPD::SB,  MVPD_KEYWORD_SB},
        {MVPD::DR,  MVPD_KEYWORD_DR},
        {MVPD::VZ,  MVPD_KEYWORD_VZ},
        {MVPD::CC,  MVPD_KEYWORD_CC},
        {MVPD::CE,  MVPD_KEYWORD_CE},
        {MVPD::FN,  MVPD_KEYWORD_FN},
        {MVPD::PN,  MVPD_KEYWORD_PN},
        {MVPD::SN,  MVPD_KEYWORD_SN},
        {MVPD::PR,  MVPD_KEYWORD_PR},
        {MVPD::HE,  MVPD_KEYWORD_HE},
        {MVPD::CT,  MVPD_KEYWORD_CT},
        {MVPD::HW,  MVPD_KEYWORD_HW},
        {MVPD::pdM, MVPD_KEYWORD_PDM},
        {MVPD::IN,  MVPD_KEYWORD_IN},
        {MVPD::INVALID_MVPD_KEYWORD,  MVPD_KEYWORD_PD2},
        {MVPD::INVALID_MVPD_KEYWORD,  MVPD_KEYWORD_PD3},
        {MVPD::INVALID_MVPD_KEYWORD,  MVPD_KEYWORD_OC},
        {MVPD::INVALID_MVPD_KEYWORD,  MVPD_KEYWORD_FO},
        {MVPD::pdI, MVPD_KEYWORD_PDI},
        {MVPD::pdG, MVPD_KEYWORD_PDG},
        {MVPD::INVALID_MVPD_KEYWORD,  MVPD_KEYWORD_MK},
        {MVPD::PB,  MVPD_KEYWORD_PB},
        {MVPD::CH,  MVPD_KEYWORD_CH},
        {MVPD::IQ,  MVPD_KEYWORD_IQ},
        {MVPD::L1,  MVPD_KEYWORD_L1},
        {MVPD::L2,  MVPD_KEYWORD_L2},
        {MVPD::L3,  MVPD_KEYWORD_L3},
        {MVPD::L4,  MVPD_KEYWORD_L4},
        {MVPD::L5,  MVPD_KEYWORD_L5},
        {MVPD::L6,  MVPD_KEYWORD_L6},
        {MVPD::L7,  MVPD_KEYWORD_L7},
        {MVPD::L8,  MVPD_KEYWORD_L8},
        {MVPD::pdW, MVPD_KEYWORD_PDW},
        {MVPD::AW,  MVPD_KEYWORD_AW},
    };
    o_hbKeyword    = mvpdFapiKeywordToHbKeyword[i_fapiKeyword].keyword;
    o_keywordIndex = mvpdFapiKeywordToHbKeyword[i_fapiKeyword].keywordIndex;
}

static void MvpdRecordXlate(
    const fapi2::MvpdRecord i_fapiRecord,
    MVPD::mvpdRecord & o_hbRecord,
    uint8_t & o_recordIndex)
{
    struct mvpdRecordToChip
    {
        MVPD::mvpdRecord rec;
        uint8_t recIndex;
    };
    static const mvpdRecordToChip mvpdFapiRecordToHbRecord[] =
    {
        {MVPD::CRP0,MVPD_RECORD_CRP0},
        {MVPD::CP00,MVPD_RECORD_CP00},
        {MVPD::VINI,MVPD_RECORD_VINI},
        {MVPD::LRP0,MVPD_RECORD_LRP0},
        {MVPD::LRP1,MVPD_RECORD_LRP1},
        {MVPD::LRP2,MVPD_RECORD_LRP2},
        {MVPD::LRP3,MVPD_RECORD_LRP3},
        {MVPD::LRP4,MVPD_RECORD_LRP4},
        {MVPD::LRP5,MVPD_RECORD_LRP5},
        {MVPD::MVPD_INVALID_RECORD,MVPD_RECORD_LRP6},
        {MVPD::MVPD_INVALID_RECORD,MVPD_RECORD_LRP7},
        {MVPD::MVPD_INVALID_RECORD,MVPD_RECORD_LRP8},
        {MVPD::MVPD_INVALID_RECORD,MVPD_RECORD_LRP9},
        {MVPD::MVPD_INVALID_RECORD,MVPD_RECORD_LRPA},
        {MVPD::MVPD_INVALID_RECORD,MVPD_RECORD_LRPB},
        {MVPD::MVPD_INVALID_RECORD,MVPD_RECORD_LRPC},
        {MVPD::MVPD_INVALID_RECORD,MVPD_RECORD_LRPD},
        {MVPD::MVPD_INVALID_RECORD,MVPD_RECORD_LRPE},
        {MVPD::LWP0,MVPD_RECORD_LWP0},
        {MVPD::LWP1,MVPD_RECORD_LWP1},
        {MVPD::LWP2,MVPD_RECORD_LWP2},
        {MVPD::LWP3,MVPD_RECORD_LWP3},
        {MVPD::LWP4,MVPD_RECORD_LWP4},
        {MVPD::LWP5,MVPD_RECORD_LWP5},
        {MVPD::MVPD_INVALID_RECORD,MVPD_RECORD_LWP6},
        {MVPD::MVPD_INVALID_RECORD,MVPD_RECORD_LWP7},
        {MVPD::MVPD_INVALID_RECORD,MVPD_RECORD_LWP8},
        {MVPD::MVPD_INVALID_RECORD,MVPD_RECORD_LWP9},
        {MVPD::MVPD_INVALID_RECORD,MVPD_RECORD_LWPA},
        {MVPD::MVPD_INVALID_RECORD,MVPD_RECORD_LWPB},
        {MVPD::MVPD_INVALID_RECORD,MVPD_RECORD_LWPC},
        {MVPD::MVPD_INVALID_RECORD,MVPD_RECORD_LWPD},
        {MVPD::MVPD_INVALID_RECORD,MVPD_RECORD_LWPE},
        {MVPD::VWML,MVPD_RECORD_VWML},
        {MVPD::MER0,MVPD_RECORD_MER0},
    };
    o_hbRecord    = mvpdFapiRecordToHbRecord[i_fapiRecord].rec;
    o_recordIndex = mvpdFapiRecordToHbRecord[i_fapiRecord].recIndex;
}

static void mvpdRingFuncSet(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_fapiTarget,
    uint8_t*     i_pRecordBuf,
    uint32_t     i_recordLen,
    uint8_t*     i_pRing,
    uint32_t     i_ringLen,
    uint8_t*     i_pCallerRingBuf,
    uint32_t     i_callerRingBufLen)
{
    uint8_t*          l_to = NULL;
    uint8_t*          l_fr = NULL;
    uint32_t          l_len = 0;
    uint8_t*          l_pRingEnd;
    if (i_callerRingBufLen == i_ringLen)
    {
        l_to = i_pRing;
        l_fr = i_pCallerRingBuf;
        l_len = i_callerRingBufLen;
        memcpy (l_to, l_fr, l_len);
        return;
    }

    mvpdRingFuncFind(
        i_fapiTarget,
        0x00,
        0x00,
        0x00,
        i_pRecordBuf,
        i_recordLen,
        l_pRingEnd,
        l_len);

    if (i_ringLen == 0 )
    {

        l_to = i_pRing;
        l_fr = i_pCallerRingBuf;
        l_len = i_callerRingBufLen;
        memcpy (l_to, l_fr, l_len);
        return;
    }

    if (i_callerRingBufLen < i_ringLen)
    {
        l_to = i_pRing;
        l_fr = i_pCallerRingBuf;
        l_len = i_callerRingBufLen;
        memcpy (l_to, l_fr, l_len);

        l_to = i_pRing + i_callerRingBufLen;
        l_fr = i_pRing + i_ringLen;
        l_len = (l_pRingEnd) - (i_pRing + i_ringLen);
        memmove (l_to, l_fr, l_len);

        l_to = (l_pRingEnd) - (i_ringLen - i_callerRingBufLen);
        l_len = i_ringLen - i_callerRingBufLen;
        memset (l_to, 0x00, l_len);
        return;
    }

    if (i_callerRingBufLen > i_ringLen)
    {
        l_to = i_pRing + i_callerRingBufLen;
        l_fr = i_pRing + i_ringLen;
        l_len = l_pRingEnd - (i_pRing + i_ringLen);
        memmove (l_to, l_fr, l_len);

        l_to = i_pRing;
        l_fr = i_pCallerRingBuf;
        l_len = i_callerRingBufLen;
        memcpy (l_to, l_fr, l_len);
        return;
    }
}


static void mvpdRingFuncGet(
    uint8_t*     i_pRing,
    uint32_t     i_ringLen,
    uint8_t*     i_pCallerRingBuf,
    uint32_t&    io_rCallerRingBufLen)
{
    if (i_pCallerRingBuf == NULL)
    {
        io_rCallerRingBufLen = i_ringLen;
        break;
    }
    if ( io_rCallerRingBufLen < i_ringLen )
    {
        io_rCallerRingBufLen = i_ringLen;
        return;
    }
    memcpy(i_pCallerRingBuf, i_pRing, i_ringLen);
    io_rCallerRingBufLen = i_ringLen;
}

static void mvpdRingFuncFind(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& i_fapiTarget,
    const uint8_t       i_chipletId,
    const uint8_t       i_evenOdd,
    const RingId_t      i_ringId,
    uint8_t*            i_pRecordBuf,
    uint32_t            i_recordBufLen,
    uint8_t*&           o_rpRing,
    uint32_t&           o_rRingLen)
{
    bool                l_mvpdEnd;
    CompressedScanData* l_pScanData = NULL;
    uint32_t            l_prevLen;
    uint32_t            l_recordBufLenLeft = i_recordBufLen - 1;

    i_pRecordBuf++;
    l_recordBufLenLeft--;

    do
    {
        l_prevLen = l_recordBufLenLeft;
        mvpdRingFuncFindEnd(
            i_fapiTarget,
            &i_pRecordBuf,
            &l_recordBufLenLeft,
            &l_mvpdEnd);
        if (!l_mvpdEnd && !l_pScanData)
        {
            mvpdRingFuncFindHdr(
                i_fapiTarget,
                i_chipletId,
                i_evenOdd,
                i_ringId,
                &i_pRecordBuf,
                &l_recordBufLenLeft,
                &l_pScanData);
        }
    }
    while (!l_mvpdEnd && !l_pScanData && l_recordBufLenLeft);

    if (l_pScanData)
    {
        o_rpRing   = (uint8_t*)l_pScanData;
        o_rRingLen = be16toh(l_pScanData->iv_size);
    }
    else
    {
        o_rpRing   = i_pRecordBuf;
        o_rRingLen = 0;
    }
}

static void getMvpdField(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP> &i_procTarget,
    uint8_t * const i_pBuffer,
    uint32_t &io_fieldSize)
{
    uint8_t l_recIndex = MVPD_INVALID_CHIP_UNIT;
    uint8_t l_keyIndex = MVPD_INVALID_CHIP_UNIT;
    fapi2::ReturnCode l_rc;
    MVPD::mvpdRecord l_hbRecord = MVPD::MVPD_INVALID_RECORD;
    fapi2::MvpdRecordXlate(MVPD_RING_GET, l_hbRecord, l_recIndex);
    MVPD::mvpdKeyword l_hbKeyword = MVPD::INVALID_MVPD_KEYWORD;
    fapi2::MvpdKeywordXlate(MVPD_RECORD_CP00, l_hbKeyword, l_keyIndex);
    size_t l_fieldLen = io_fieldSize;

    deviceRead(
            i_procTarget.get(),
            i_pBuffer,
            l_fieldLen,
            DEVICE_MVPD_ADDRESS(l_hbRecord, l_hbKeyword));
    io_fieldSize = l_fieldLen;
    return  l_rc;
}

```
