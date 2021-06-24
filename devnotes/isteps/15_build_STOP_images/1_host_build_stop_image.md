NOTE: 15.1 and 15.2 are swapped in [IPL flow](https://wiki.raptorcs.com/w/images/b/bd/IPL-Flow-POWER9.pdf)

This isn't the whole isteps, earlier parts were implemented without previous
analysis.

```
getPpeScanRings(hw, PLAT_CME, proc, ringData /* buffers */, imgType /* build/rebuild */)

	p9_xip_customize(proc, hw, hw, hw_size, ###pRingBuffer###, ###ringBufSize###, CME/SGPE, IPL, ###pBuf1###, ###buf1Size###, ###pBuf2###, ###buf2Size###, ###pBuf3###, ###buf3Size###, ENABLE_ALL_CORE(0xFFFF))
		- ringBufSize = 307200
		- bufXSize = 60000 * 3
		- ENABLE_ALL_CORE is used only for SBE, otherwise bootCoreMask = 0xFFFFFF (24 cores)

		tor_get_block_of_rings(ringSection = hw->rings->offset(DD), dd, CME/SGPE, UNDEFINED_RING_VARIANT, pRingBuf, &ringBufSize)
			tor_access_ring(ringSection, UNDEFINED_RING_ID, dd, CME/SGPE, UNDEFINED_RING_VARIANT, &instanceID, GET_PPE_LEVEL_RINGS, pRingBuf, &ringBufSize, &ringName)
				- check magic, version, dd etc.
				- ppeBlock[3] = ringSection + header;   0:SBE, 1:CME, 2:SGPE
				- memcpy(pRingBuf, ppeBlock[CME/SGPE].offset, ppeBlock[CME/SGPE].size)

		fetch_and_insert_vpd_rings(proc, pRingBuf, ringBufSize, maxRingSectionSize = ringBufSize, hw, CME/SGPE, buf1, buf1s, buf2, buf2s, buf3, buf3s, bootCoreMask)
			// no-op if MVPD is empty?
			resolve_gptr_overlays(proc, hw, &overlaySection, dd, bGptrMvpdSupport)
				- if dd < 20 || hw->overlays->dd_support == 0: return overlaySection = NULL
				- overlaySection = hw + hw->overlays...->offset, bGptrMvpdSupport = true

			// 1. Add all common rings
			for both VPD types (PDG, PDR) -> l_ringIdList:
				- CME: only EC (+ GPTR_EC if bGptrMvpdSupport)
				- SGPE: only EX + EQ (+ GPTR_E{X,Q} if bGptrMvpdSupport)
				for (iRing: l_ringIdList[iRing].vpdRingClass): for (chiplet_id: l_ringIdList[iRing].instanceIdMin..l_ringIdList[iRing].instanceIdMax):
					_fetch_and_insert_vpd_rings (proc, pRingBuf, ringBufSize, maxRingSectionSize, overlaySection, dd, CME/SGPE, buf1, buf1s, buf2, buf2s, buf3, buf3s,
												 chiplet_id, evenOdd = 0, l_ringIdList[iRing], RING_SCAN, bImgOutOfSpace = false, bootCoreMask)
						- clear bu1, buf2, buf3
						getMvpdRing(proc, MVPD_RECORD_CP00, l_mvpdKeyword, chiplet_id, evenOdd, l_ringIdList[iRing].ringId, buf1, buf1s, buf2, buf2s)
							// This reads from MVPD PNOR partition. This partition is clear in freshly built image, but not in image read from platform that already run.
							// Who fills it and when? Can we assume it isn't necessary and return "not found" every time? This would make most of this loop no-op...
							TBD !!!!!!!!!!!!!!!!!!!!! (maybe)

						- clear buf2
						- if MVPD ring not found - return with success
						- if GPTR section:
							process_gptr_rings(proc, overlaySection, dd, buf1, buf2, buf3)
								get_overlays_ring(proc, overlaySection, dd, (RingId_t)((CompressedScanData*)buf1)->iv_ringId, buf2, buf3, &ovlyUncmpSize)
									tor_get_single_ring(overlaySection, dd, ringId, UNDEFINED_PPE_TYPE, UNDEFINED_RING_VARIANT, instanceID=0 /*unused*/, buf2, ringBlockSize=0xffffffff /*unused*/)
										tor_access_ring(overlaySection, ringId, dd, UNDEFINED_PPE_TYPE, UNDEFINED_RING_VARIANT, instanceID /*unused*/, GET_SINGLE_RING, buf2, ringBlockSize /*unused*/)
											- check magic, version, dd etc.
											get_ring_from_ring_section(overlaySection, ringId, UNDEFINED_RING_VARIANT, instanceID /*unused?*/, GET_SINGLE_RING, buf2, ringBlockSize /*unused?*/)
												ringid_get_noof_chiplets(...)
													- return 16 (SBE,OVRD,OVLY - 16, CME,SPGE - 1)
												hdr = (TorHeader_t)overlaySection
												- assert(hdr->version == 7) // older version are treated differently, skipped for now
												- hostboot uses pointer juggling, but the following struct simplifies this
												struct tmp { TorHeader_t hdr; TorCpltBlock_t blk[] }; tmp *X = (tmp*) hdr;
												for each chiplet:
													ringid_get_properties(hdr->chipType (0), hdr->magic (TORL), hdr->version (7), chiplet,
																		  &cpltData, &ringIdListCommon, &ringIdListInstance, &ringVariantOrder, &ringProps, &numVariants );
														- returns pointers to data from import/chips/p9/utils/imageProcs/p9_ringId.C
														- for OVLY (and OVRD) numVariants = 1, regardless of what cpltData says

													// traverse common sections - assuming it always exists based on p9_ringId.C, hostboot checks for it
													torSlotNum = 0
													// for instance in ringIdListCommon->instanceIdMin..ringIdListCommon->instanceIdMax, but for common this is always one instance
													for each ring in cpltData.iv_num_common_rings:
														// for each variant - only one for OVLY
														if (ringIdListCommon[ring].ringName == ringProps[ringId] /* strcmp */)
															ringOffset = *((u8*)X + sizeof(TorHeader_t) + X->blk[chiplet].cmnOffset + torSlotNum * sizeof(u16))
															assert(ringOffset != 0)   // or return not found? Not sure, check
															CompressedScanData *rs = (u8*)X + X->blk[chiplet].cmnOffset + ringOffset
															assert(ringBlockSize >= rs->iv_size)    // ringBlockSize is 0xffffffff, iv_size is u16 so this never asserts
															memcpy(buf2, rs, rs->iv_size)
															ringBlockSize = rs->iv_size	// discarded
															instanceID = instance		// discarded
															return with success
														torSlotNum++ (per each variant)

													// traverse instance sections - assuming it always exists based on p9_ringId.C, hostboot checks for it
													torSlotNum = 0
													for instance in ringIdListCommon->instanceIdMin..ringIdListCommon->instanceIdMax:
														for each ring in cpltData.iv_num_common_rings:
															// for each variant - only one for OVLY
															if ((ringIdListInstance[ring].ringName == ringProps[ringId] /* strcmp */) && (instance == instanceId))
																ringOffset = *((u8*)X + sizeof(TorHeader_t) + X->blk[chiplet].cmnOffset + torSlotNum * sizeof(u16))
																assert(ringOffset != 0)   // or return not found? Not sure, check
																CompressedScanData *rs = (u8*)X + X->blk[chiplet].cmnOffset + ringOffset
																assert(ringBlockSize >= rs->iv_size)    // ringBlockSize is 0xffffffff, iv_size is u16 so this never asserts
																memcpy(buf2, rs, rs->iv_size)
																ringBlockSize = rs->iv_size	// discarded
																instanceID = instance		// discarded
																return with success
															torSlotNum++ (per each variant)

									// back in get_overlays_ring()
									// if previous function returned not found: buf2 = NULL, buf3 = NULL, return
									_rs4_decompress(buf3, buf3 + MAX_RING_BUF_SIZE/2, MAX_RING_BUF_SIZE/2, &ovlyUncmpSize, buf2)
										- MAX_RING_BUF_SIZE is buf3s, but it wasn't passed as an argument
										- buf2 was copied from rs, do we need a copy or can we pass address in hw image directly?
										rs = (CompressedScanData *)buf2
										assert (rs->magic == "RS" && rs->version == 3)
										clear buf3
										// just copy __rs_decompress - it is a simple RLE extension that skips zeroes, relying on buf3 being zeroed earlier

								apply_overlays_ring(proc, buf1, buf2, buf3, ovlyUncmpSize)
									TBD !!!!!!!!

						- assert ( ((CompressedScanData*)buf1)->iv_magic == RS4_MAGIC )  // 0x5253, "RS"
						rs4_redundant((CompressedScanData*)buf1, &redundant)
							// A compressed scan string is redundant if the initial rotate is
							// followed by the end-of-string marker, and any remaining mod-4 bits
							// are also 0.
						- if redundant - return with RC_MVPD_RING_REDUNDANT_DATA (success?)
						- check for overflows (?)
						chipletTorId = chiplet_id + evenOdd    // + (chipletId - i_ring.instanceIdMin) if EX_INS - not for CME or SGPE
						tor_append_ring(pRingBuf, ringBufSize, buf2, buf2s, l_ringIdList[iRing].ringId, CME/SGPE, RV_BASE, chipletTorId, buf1)
							tor_access_ring(pRingBuf, l_ringIdList[iRing].ringId, UNDEFINED_DD_LEVEL, CME/SGPE, RV_BASE, chipletTorId, PUT_SINGLE_RING, &(uint32_t)l_buf, l_torOffsetSlot, &ringName)
								- check magic, version, dd etc.
								get_ring_from_ring_section(pRingBuf, l_ringIdList[iRing].ringId, RV_BASE, chipletTorId, PUT_SINGLE_RING, &(uint32_t)l_buf, l_torOffsetSlot, &ringName)
									ringid_get_noof_chiplets(...)
										- return 1 (SBE,OVRD,OVLY - 16, CME,SPGE - 1)
									- this is technically in loop for each chiplet, but there is only one...
									hdr = (TorHeader_t)pRingBuf
									ringid_get_properties(hdr->chipType (0), hdr->magic (TORM - CME, TORG - SGPE), hdr->version, iCplt = 0,
														  &cpltData, &ringIdListCommon, &ringIdListInstance, &ringVariantOrder, &ringProps, &numVariants );
										- returns pointers to data from import/chips/p9/utils/imageProcs/p9_ringId.C
									TBD !!!!!!!!

							TBD !!!!!!!!!

			// 2. Add all instance rings - significantly different for CME and SGPE, this is for CME
			l_ringIdList = PDR
			l_ringEC = l_ringIdList[iRing] for which vpdRingClass == VPD_RING_CLASS_EC_INS
				- return if none
			for each quad:
				// hostboot has additional layer for ex in quad, but it isn't used for anything
				for ec in 4*quad..4*quad+3:
					chipletId = l_ringEC.instanceIdMin + ec
					// if (bootCoreMask & PPC_BIT(8+chipletId)) -> bootCoreMask is all set, so always true
					_fetch_and_insert_vpd_rings (proc, pRingBuf, ringBufSize, maxRingSectionSize, overlaySection, dd, CME, buf1, buf1s, buf2, buf2s, buf3, buf3s,
					                             chipletId, evenOdd = 0 /* suspicious, but it is 0 for all */, l_RingEC, RING_SCAN, bImgOutOfSpace = false, bootCoreMask)
						- same as before, only l_RingEC is different

FAPI_ATTR_GET(fapi2::ATTR_RISK_LEVEL, FAPI_SYSTEM, l_riskLevel);
	- RISK_LEVEL = 0 ( <= DD2.2), 4 (DD2.3)

// create a layout of rings in HOMER for consumption of CME
layoutRingsForCME(homer, __proc__, ringData /* buffers */, l_ringDebug, RISK_LEVEL, imgType, PNOR->RINGOVD);
	// RINGOVD is empty, null is passed and some logic doesn't execute
	CpmrHdr = &homer->cpmr.header
	CmeHdr = &homer->cpmr.cme_sram_region[CME_INT_VECTOR_SIZE]
	ringLen = CmeHdr->hcode_offset + CmeHdr->hcode_len
	assert(CpmrHdr->magic == CPMR_VDM_PER_QUAD) // >= maybe? older have different layout, skipped for now
	layoutCmnRingsForCme(homer, __proc__, ringData, l_ringDebug, ringVariant = RISK_LEVEL, imgType, cmeRings /* list of rings in yet another format... */, &ringLength)
		start = &homer->cpmr.cme_sram_region[ringLength]
		payload = start + CORE_COMMON_RING_INDEX_SIZE         // sizeof(CoreCmnRingsList_t) = 16 B
		ringIds = [ec_func, ec_gptr, ec_time, ec_mode]
		for id in ringIds:		// MAX_HOMER_CORE_CMN_RINGS = 4
			if id == ec_gptr || ec_time:
				ringVariant = RV_BASE		// else RISK_LEVEL
			tor_get_single_ring(pRingBuf, dd, id, PT_CME, ringVariant, CORE0_CHIPLET_ID = 0x20, buf1, buf1s, l_debugMode)
				- described earlier, but watch out for differences like ringid_get_noof_chiplets()
			- ring not found is not an error, continue in that case
			ALIGN_UP(ringSize, 8)
			ALIGN_UP(pRingPayload, 8)		// wrt pRingStart, so (pRingPayload - pRingStart) % 8 == 0
			----------------

            uint16_t* pScanRingIndex = (uint16_t*) pRingStart;
            uint32_t ringStartToHdrOffset = ( TOR_VER_ONE == tor_version() ) ? RING_START_TO_RS4_OFFSET : 0;
            memcpy( pRingPayload, i_ringData.iv_pWorkBuf1, ringSize );
            *(pScanRingIndex + ringIndex) = SWIZZLE_2_BYTE((pRingPayload - pRingStart) + ringStartToHdrOffset);


            io_cmeRings.setRingOffset( pRingPayload, io_cmeRings.getCommonRingId( ringIndex ));
            io_cmeRings.setRingSize( io_cmeRings.getCommonRingId( ringIndex ), ringSize );
            io_cmeRings.extractRing( i_ringData.iv_pWorkBuf1, ringSize, io_cmeRings.getCommonRingId( ringIndex ) );

            pRingPayload = pRingPayload + ringSize;

            //cleaning up what we wrote in temp buffer last time.
            memset( i_ringData.iv_pWorkBuf1, 0x00, ringSize );
        }

        ringSize = (pRingPayload - pRingStart);

        if( ringSize > CORE_COMMON_RING_INDEX_SIZE )
        {
            io_cmnRingSize += (pRingPayload - pRingStart);
            ALIGN_DWORD(tempSize, io_cmnRingSize)
        }
        ----------------
			TBD !!!!!!

	----------------
	pCmeHdr->g_cme_common_ring_length = ringLength - (CmeHdr->hcode_offset + CmeHdr->hcode_len)

	if( !pCmeHdr->g_cme_common_ring_length )
	{
		//No common ring , so force offset to be 0
		pCmeHdr->g_cme_common_ring_offset = 0;
	}

	tempLength = ringLength;
	tempLength = (( tempLength + CME_BLOCK_READ_LEN - 1 ) >> CME_BLK_SIZE_SHIFT ); //multiple of 32B
	ringLength = tempLength << CME_BLK_SIZE_SHIFT; //start position of instance rings

	layoutInstRingsForCme(homer, __proc__, l_ringData, l_ringDebug, ringVariant = RV_BASE, imgType, cmeRings, &ringLength)
		TBD !!!!!!

	if( ringLength )
	{
		CmeHdr->max_spec_ring_len = ALIGN_UP(ringLength, 32) / 32
		pCmeHdr->core_spec_ring_offset    =   tempLength;
	}
	----------------

l_ringData.iv_ringBufSize = i_sizeBuf1;

ppeImgRc = getPpeScanRings( i_pImageIn,
							PLAT_SGPE,
							i_procTgt,
							l_ringData,
							i_imgType );

// create a layout of rings in HOMER for consumption of SGPE
layoutRingsForSGPE( pChipHomer, i_pRingOverride, l_chipFuncModel,
							  l_ringData, (RingDebugMode_t)l_ringDebug,
							  l_riskLevel, l_qpmrHdr, i_imgType );

populateUnsecureHomerAddress( i_procTgt, pChipHomer, l_chipFuncModel.isSmfEnabled() );

//Update P State parameter block info in HOMER
buildParameterBlock( pChipHomer, i_procTgt, l_ppmrHdr, i_imgType, i_pBuf1, i_sizeBuf1 );

//Update CPMR Header with Scan Ring details
updateCpmrCmeRegion( pChipHomer, i_procTgt );


//Update QPMR Header area in HOMER
updateQpmrHeader( pChipHomer, l_qpmrHdr );

//update PPMR Header area in HOMER
updatePpmrHeader( pChipHomer, l_ppmrHdr, i_procTgt );

//Update L2 Epsilon SCOM Registers
populateEpsilonL2ScomReg( pChipHomer );

//Update L3 Epsilon SCOM Registers
populateEpsilonL3ScomReg( pChipHomer );

//Update L3 Refresh Timer Control SCOM Registers
populateL3RefreshScomReg( pChipHomer, i_procTgt);

//populate HOMER with SCOM restore value of NCU RNG BAR SCOM Register
populateNcuRngBarScomReg( pChipHomer, i_procTgt );

//validate SRAM Image Sizes of PPE's
validateSramImageSize( pChipHomer, sramImgSize );

//Update CME/SGPE Flags in respective image header.
updateImageFlags( pChipHomer, i_procTgt );

//Set the Fabric IDs
setFabricIds( pChipHomer, i_procTgt );

//Update the attributes storing PGPE and SGPE's boot copier offset.
updateGpeAttributes( pChipHomer, i_procTgt );

//customize magic word based on endianess
customizeMagicWord( pChipHomer );

addUrmorRestore( i_pHomerImage, fuseModeState, l_chipFuncModel );

verifySprSelfSave( i_pHomerImage, fuseModeState, l_chipFuncModel );

initUnsecureHomer( i_pBuf2, i_sizeBuf2 );
```
