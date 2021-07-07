```cpp
fapi2::ReturnCode _fetch_and_insert_vpd_rings(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& procChip,
    void*           i_ringSection,
    uint32_t&       io_ringSectionSize,
    uint32_t        i_maxRingSectionSize,
    void*           i_overlaysSection,
    uint8_t         i_ddLevel,
    uint8_t         i_sysPhase,             // CME/SGPE
    void*           i_vpdRing,
    uint32_t        i_vpdRingSize,
    void*           i_ringBuf2,
    uint32_t        i_ringBufSize2,
    void*           i_ringBuf3,
    uint32_t        i_ringBufSize3,
    uint8_t         i_chipletId,
    uint8_t         i_evenOdd,              // 0
    const RingIdList i_ring,
    uint8_t&        io_ringStatusInMvpd,    // RING_SCAN - unused here, but changed to different status as an output
    bool&           i_bImgOutOfSpace,       // false
    uint32_t&       io_bootCoreMask)        // unused
{
    MvpdKeyword l_mvpdKeyword =
        (i_ring.vpdKeyword == VPD_KEYWORD_PDG) ? fapi2::MVPD_KEYWORD_PDG
                                               : fapi2::MVPD_KEYWORD_PDR;

    memset(i_vpdRing,  0, i_vpdRingSize);
    memset(i_ringBuf2, 0, i_ringBufSize2);
    memset(i_ringBuf3, 0, i_ringBufSize3);

    mvpdRingFunc(
        procChip,
        l_mvpdKeyword,
        i_chipletId,
        i_ring.ringId,
        (uint8_t*)i_vpdRing,
        i_vpdRingSize,
        (uint8_t*)i_ringBuf2,
        i_ringBufSize2);

    memset(i_ringBuf2, 0, i_ringBufSize2);

    io_ringStatusInMvpd = RING_FOUND;
    uint32_t l_vpdChipletId = (be32toh(((CompressedScanData*)i_vpdRing)->iv_scanAddr) >> 24) & 0xFF;

    if(i_ring.vpdRingClass == VPD_RING_CLASS_GPTR_NEST
    || i_ring.vpdRingClass == VPD_RING_CLASS_GPTR_EQ
    || i_ring.vpdRingClass == VPD_RING_CLASS_GPTR_EX
    || i_ring.vpdRingClass == VPD_RING_CLASS_GPTR_EC)
    {
        process_gptr_rings(
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
    i_bImgOutOfSpace = i_maxRingSectionSize < io_ringSectionSize + i_vpdRingSize;

    uint8_t l_chipletTorId =
        (i_chipletId - i_ring.instanceIdMin)
      * (i_ring.vpdRingClass == VPD_RING_CLASS_EX_INS ? 1 : 0)
      + i_chipletId;

    tor_append_ring(
        i_ringSection,
        io_ringSectionSize, // In: Exact size. Out: Updated size.
        i_ringBuf2,
        i_ringBufSize2,  // Max size.
        i_ring.ringId,
        (i_sysPhase == SYSPHASE_RT_CME) ? PT_CME : PT_SGPE,
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
    PpeType_t       i_ppeType,          // CME, SGPE
    uint8_t         i_instanceId,       // Instance ID
    void*           i_rs4Container)     // RS4 ring container
{
    uint32_t   l_buf = 0;
    uint32_t   l_ringBlockSize;
    uint16_t   l_ringOffset16;
    uint32_t   l_torOffsetSlot;

    tor_access_ring(
        i_ringSection,
        i_ringId,
        i_ppeType,
        i_instanceId,
        &l_buf,
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
    PpeType_t       i_ppeType,         // CME, SGPE
    uint8_t&        io_instanceId,     // Instance ID
    void*           io_ringBlockPtr,   // Ring data buffer
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

    if(be32toh(i_ringSection->magic) == TOR_MAGIC_SBE
    || be32toh(i_ringSection->magic) == TOR_MAGIC_CME
    || be32toh(i_ringSection->magic) == TOR_MAGIC_SGPE)
    {
        get_ring_from_ring_section(
            (be32toh(i_ringSection->magic) != TOR_MAGIC_HW) ? i_ringSection
                : postHeaderStart + be32toh((TorPpeBlock_t*)(postHeaderStart + i_ppeType * sizeof(TorPpeBlock_t))->offset);
            i_ringId,
            io_instanceId,
            io_ringBlockPtr,
            io_ringBlockSize);
    }
}

static void get_ring_from_ring_section(
    void*           i_ringSection,     // Ring section ptr
    RingId_t        i_ringId,          // Ring ID
    uint8_t&        io_instanceId,     // Instance ID
    void*           io_ringBlockPtr,   // Output ring buffer
    uint32_t&       io_ringBlockSize)  // Size of ring data
{
    TorHeader_t*      torHeader = (TorHeader_t*)i_ringSection;
    uint8_t           chipType;
    TorCpltBlock_t*   cpltBlock;
    TorCpltOffset_t   cpltOffset; // Offset from ringSection to chiplet section
    TorRingOffset_t   ringOffset; // Offset to actual ring container
    uint32_t          torSlotNum; // TOR slot number (within a chiplet section)
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
                            if(strcmp((ringIdList + ringIndex)->ringName, ringProps[i_ringId].iv_name) == 0
                            && (RV_BASE == ringVariantOrder->variant[varianIndex] || numVariants == 1)
                            && ((bInstCase && ringInstance == io_instanceId) || bInstCase == 0))
                            {
                                ringOffset = be16toh(
                                    *(TorRingOffset_t*)((uint8_t*)i_ringSection
                                  + cpltOffset
                                  + torSlotNum * sizeof(ringOffset)));
                                if (ringOffset)
                                {
                                    return;
                                }
                                memcpy(io_ringBlockPtr, &cpltOffset, sizeof(cpltOffset));
                                io_ringBlockSize = cpltOffset + torSlotNum * sizeof(ringOffset);
                                return;
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
            if(i_torMagic == TOR_MAGIC_CEN
            || i_torMagic == TOR_MAGIC_OVRD)
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
            return;
    }
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

/// Determine if an RS4 compressed scan string is all 0
///
/// \param[in] i_data A pointer to the CompressedScanData header + data to be
///
/// \param[out] o_redundant Set to 1 if the RS4 string is the compressed form
/// of a scan string that is all 0; Otherwise set to 0.
///
/// \returns See \ref scan _compression_code

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
        i_overlaysSection,
        i_ddLevel,
        (RingId_t)be16toh(((CompressedScanData*)io_vpdRing)->iv_ringId),
        &l_ovlyRs4Ring,
        &l_ovlyRawRing,
        &l_ovlyUncmpSize);

    if (l_ovlyRs4Ring == io_ringBuf2 && l_ovlyRawRing == io_ringBuf3)
    {
        apply_overlays_ring(
            io_vpdRing,
            io_ringBuf2,
            l_ovlyRawRing,
            l_ovlyUncmpSize);
    }
}

void apply_overlays_ring(
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

    for (int index = 0; index < vpdUncmpSize / 8; index++)
    {
        if (careOvly[index] > 0)
        {
            for (int j = 0; j < 8; j++)
            {
                if (careOvly[index] & (0x80 >> j))
                {
                    if (dataOvly[index] & (0x80 >> j))
                    {
                        dataVpd[index] |= (0x80 >> j);
                        careVpd[index] |= (0x80 >> j);
                    }
                    else
                    {
                        dataVpd[index] &= ~(0x80 >> j);
                        careVpd[index] &= ~(0x80 >> j);
                    }
                }
            }
        }
    }

    if (vpdUncmpSize % 8)
    {
        int index = (int)vpdUncmpSize / 8;
        careOvly[index] &= ~(0xFF << (8 - (vpdUncmpSize % 8)));

        if (careOvly[index] > 0)
        {
            for (int j = 0; j < (int)vpdUncmpSize % 8; j++)
            {
                if (careOvly[index] & (0x80 >> j))
                {
                    if(dataOvly[index] & (0x80 >> j))
                    {
                        dataVpd[index] |= (0x80 >> j);
                        careVpd[index] |= (0x80 >> j);
                    }
                    else
                    {
                        dataVpd[index] &= ~(0x80 >> j);
                        careVpd[index] &= ~(0x80 >> j);
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

/// Compress a scan string using the RS4 compression algorithm
///
/// \param[in,out] io_rs4 This is a pointer to a memory area which must be
/// large enough to hold the worst-case result of compressing \a i_data_str
/// and \a i_care_str (see below). Note that the CompressedScanData is
/// always created in big-endian format, however the caller can use
/// compresed_scan_data_translate() to create a copy of the header in
/// host format.
///
/// \param[in] i_size The size of the buffer pointed to by \a io_rs4.
///
/// \param[in] i_data_str The string to compress.  Scan data to compress is
/// left-justified in this input string.
///
/// \param[in] i_care_str The care mask that identifies which bits in the
/// i_data_str that need to be scanned (written). String is left-justified.
///
/// \param[in] i_length The length of the input string in \e bits.  It is
/// assumed the \a i_string contains at least (\a i_length + 7) / 8 bytes.
///
/// \param[in] i_scanAddr The 32-bit scan address.
///
/// \param[in] i_ringId The ring ID that uniquely identifies the ring. (See
/// <ChipType> ring ID header files for more info.)
///
/// This API is required for integration with PHYP which does not support
/// local memory allocation, like malloc() and new().  Applications in
/// environments supporting local memory allocation can use rs4_compress()
/// instead.
///
/// We always require the worst-case amount of memory including the header and
/// any rounding required to guarantee that the data size is a multiple of 8
/// bytes.  The final image size is also rounded up to a multiple of 8 bytes.
/// If the \a io_size is less than this amount (based on \a i_length) the
/// call will fail.
///
/// \returns See \ref scan_compression_codes

static void _rs4_compress(
    CompressedScanData* io_rs4,
    const uint32_t i_size,
    const uint8_t* i_data_str,
    const uint8_t* i_care_str,
    const uint32_t i_length,
    const uint32_t i_scanAddr,
    const RingId_t i_ringId);

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

/// Decompress a scan string compressed using the RS4 compression algorithm
///
/// \param[in,out] io_data_str A caller-supplied data area to contain the
/// decompressed string. The \a i_stringSize must be large enough to contain
/// the decompressed string, which is the size of the original string in bits
/// rounded up to the nearest byte.
///
/// \param[in,out] io_care_str A caller-supplied data area to contain the
/// decompressed care mask. The \a i_stringSize must be large enough to contain
/// the decompressed care mask, which is the size of the original string in
/// bits rounded up to the nearest byte.
///
/// \param[in] i_size The size in \e bytes of \a o_data_str and \a o_care_str
/// buffers and which represents the max number of raw ring bits x 8 that may
/// fit into the two raw ring buffers.
///
/// \param[out] o_length The length of the decompressed string in \e bits.
///
/// \param[in] i_rs4 A pointer to the CompressedScanData header + data to be
/// decompressed.
///
/// This API is required for integration with PHYP which does not support
/// local memory allocation, such as malloc() and new().  Applications in
/// environments supporting local memory allocation can use rs4_decompress()
/// instead.
///
/// \returns See \ref scan_compression_codes

int _rs4_decompress(
    uint8_t* o_data_str,
    uint8_t* o_care_str,
    uint32_t i_size,
    uint32_t* o_length,
    const CompressedScanData* i_rs4);

void get_overlays_ring(
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

    tor_access_ring(
        i_overlaysSection,
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

static void mvpdRingFunc(
    const fapi2::Target<fapi2::TARGET_TYPE_PROC_CHIP>& procChip,
    const mvpdRingFuncOp i_mvpdRingFuncOp,
    const uint8_t        i_chipletId,
    const RingId_t       i_ringId,
    uint8_t*             o_pRingBuf,
    uint32_t&            io_rRingBufsize,
    uint8_t*             i_pTempBuf,
    uint32_t             i_tempBufsize)
{
    uint32_t                l_recordLen  = 0;
    uint8_t*                l_recordBuf  = i_pTempBuf ? i_pTempBuf
                                                      : malloc(l_recordLen);
    uint8_t*                l_pRing      = NULL;
    uint32_t                l_ringLen    = 0;

    deviceRead(
        procChip.get(),
        NULL,
        l_recordLen,
        DEVICE_MVPD_ADDRESS(MVPD::CRP0, MVPD::ED));

    deviceRead(
        procChip.get(),
        l_recordBuf,
        l_recordLen,
        DEVICE_MVPD_ADDRESS(MVPD::CRP0, MVPD::ED));

    mvpdRingFuncFind(
        i_chipletId,
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
            l_recordBuf,
            l_recordLen,
            l_pRing,
            l_ringLen,
            o_pRingBuf,
            io_rRingBufsize);

        deviceWrite(
            procChip.get(),
            l_recordBuf,
            l_recordLen,
            DEVICE_MVPD_ADDRESS(MVPD::CRP0, MVPD::ED));
    }
    if(i_pTempBuf == nullptr && l_recordBuf)
    {
        free(l_recordBuf);
        l_recordBuf = NULL;
    }
}

static void mvpdRingFuncSet(
    uint8_t*     i_pRecordBuf,
    uint32_t     i_recordLen,
    uint8_t*     i_pRing,
    uint32_t     i_ringLen,
    uint8_t*     i_pCallerRingBuf,
    uint32_t     i_callerRingBufLen)
{
    uint8_t* l_to;
    uint8_t* l_fr;
    uint32_t l_len;
    uint8_t* l_pRingEnd;
    if (i_callerRingBufLen == i_ringLen)
    {
        l_to = i_pRing;
        l_fr = i_pCallerRingBuf;
        l_len = i_callerRingBufLen;
        memcpy(l_to, l_fr, l_len);
        return;
    }

    mvpdRingFuncFind(
        0,
        0,
        i_pRecordBuf,
        i_recordLen,
        l_pRingEnd,
        l_len);

    if (i_ringLen == 0)
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
        l_len = l_pRingEnd - i_pRing - i_ringLen;
        memmove (l_to, l_fr, l_len);

        l_to = l_pRingEnd - i_ringLen + i_callerRingBufLen;
        l_len = i_ringLen - i_callerRingBufLen;
        memset (l_to, 0x00, l_len);
        return;
    }

    if (i_callerRingBufLen > i_ringLen)
    {
        l_to = i_pRing + i_callerRingBufLen;
        l_fr = i_pRing + i_ringLen;
        l_len = l_pRingEnd - i_pRing - i_ringLen;
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
    if (!i_pCallerRingBuf || io_rCallerRingBufLen < i_ringLen)
    {
        io_rCallerRingBufLen = i_ringLen;
        return;
    }
    memcpy(i_pCallerRingBuf, i_pRing, i_ringLen);
    io_rCallerRingBufLen = i_ringLen;
}

static void mvpdRingFuncFind(
    const uint8_t       i_chipletId,
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
            &i_pRecordBuf,
            &l_recordBufLenLeft,
            &l_mvpdEnd);
        if (!l_mvpdEnd && !l_pScanData)
        {
            mvpdRingFuncFindHdr(
                i_chipletId,
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

static void mvpdRingFuncFindEnd(
    uint8_t** io_pBufLeft,
    uint32_t* io_pBufLenLeft,
    bool*      o_mvpdEnd)
{
    *o_mvpdEnd = false;

    if (*io_pBufLenLeft >= 3
    && be32toh(**io_pBufLeft) & 0xffffff00 == MVPD_END_OF_DATA_MAGIC & 0xffffff00)
    {
        *o_mvpdEnd = true;
        *io_pBufLeft    += 3;
        *io_pBufLenLeft -= 3;
    }
}

static void mvpdRingFuncFindHdr(
    const uint8_t       i_chipletId,
    const RingId_t      i_ringId,
    uint8_t**           io_pBufLeft,
    uint32_t*           io_pBufLenLeft,
    CompressedScanData** o_pScanData)
{
    CompressedScanData* l_pScanData = *io_pBufLeft;
    *o_pScanData = NULL;

    if (*io_pBufLenLeft < sizeof(CompressedScanData)
    || be16toh(l_pScanData->iv_magic) != RS4_MAGIC)
    {
        return;
    }

    *io_pBufLeft    += be16toh(l_pScanData->iv_size);
    *io_pBufLenLeft -= be16toh(l_pScanData->iv_size);

    uint32_t l_evenOddMask =
        (i_ringId == ex_l3_repr) ? 0x00001000
      : (i_ringId == ex_l2_repr) ? 0x00000400
      : (i_ringId == ex_l3_refr_time || i_ringId == ex_l3_refr_repr) ? 0x00000040
      : 0;

    if (be16toh(l_pScanData->iv_ringId) == i_ringId
    && (be32toh(l_pScanData->iv_scanAddr) >> 24) & 0xFF == i_chipletId
    && (l_evenOddMask == 0 || be32toh(l_pScanData->iv_scanAddr) & l_evenOddMask))
    {
        *o_pScanData = l_pScanData;
    }
}
```
