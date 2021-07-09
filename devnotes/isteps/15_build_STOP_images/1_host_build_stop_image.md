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
		- again, hostboot uses pointer juggling for something simple...
		struct cmnRingList { u16 ring[8]; u8 payload[]; } *tmp;		// In order: ec_func, ec_gprt, ec_time, ec_mode, ec_abst, 3x reserved
		- size of what would be the above struct is defined as 2K
		tmp = &homer->cpmr.cme_sram_region[ringLength]
		start = &homer->cpmr.cme_sram_region[ringLength]
		payload = tmp->payload
		ringIds = [ec_func, ec_gptr, ec_time, ec_mode]
		for id in ringIds:		// MAX_HOMER_CORE_CMN_RINGS = 4
			if id == ec_gptr || ec_time:
				ringVariant = RV_BASE		// else RISK_LEVEL
			ringSize = buf1s
			tor_get_single_ring(pRingBuf, dd, id, PT_CME, ringVariant, CORE0_CHIPLET_ID = 0x20, buf1, &ringSize, l_debugMode)
				- described earlier, but watch out for differences like ringid_get_noof_chiplets()
				- ringSize size of data copied to buf1 on return
			- ring not found is not an error, continue in that case

			ALIGN_UP(ringSize, 8)
			ALIGN_UP(payload, 8)
				- hostboot calculates this wrt start, so (payload - start) % 8 == 0
				- can we assume that start is always aligned?

			memcpy(payload, buf1, ringSize)
			tmp->ring[id] = payload - start;

			- hostboot fills additional data in cmeRings (offset and size of each ring), AFAICT this is only for debug

			payload += ringSize;
			memset(buf1, 0, ringSize)

		if (payload - start > sizeof(struct cmnRingList))
			ringLength += (payload - start);
			ALIGN_UP(ringLength, 8)

	CmeHdr->common_ring_len = ringLength - (CmeHdr->hcode_offset + CmeHdr->hcode_len)

	if(!CmeHdr->common_ring_length)
		//No common ring , so force offset to be 0
		CmeHdr->common_ring_offset = 0

	ALIGN_UP(ringLength, 32)
	tempLength = ringLength / 32

	layoutInstRingsForCme(homer, __proc__, l_ringData, l_ringDebug, ringVariant = RV_BASE, imgType, cmeRings, &ringLength)
		// This is done in two passes. First one reads rings to finds out the maximal
		// size used by any of functional EXs, and the second one reads rings again,
		// this time saving them in their final destination. The resulting rings are
		// uniformly laid out in memory. Unfortunately, RS4 header doesn't hold size
		// of uncompressed rings.
		//
		// Possible improvements:
		// - don't actually save decompressed data in the first pass, we just want the size
		// - first EX (of all, not only functional) can be read directly to target on first pass, its offset is known
		// - divide all available space into 12 equally sized chunks (12 EXs per CPU) and skip first pass altogether
		// - use CORE_SPECIFIC_RING_SIZE_PER_CORE (1K) as the size

		maxLen = 0
		struct specRingList { u16 ring[4]; u8 payload[]; } *tmp;		// In order: ec_repr0, ec_repr1, 2x reserved

		// 1st pass
		for each ex:		// 12 total
			if ex is not functional: continue
			tmpLen = 0
			for each core:
				if core is not functional: continue
				tmp2 = buf1s
				tor_get_single_ring(pRingBuf, dd, ec_repr = (0xE4? 0x80?), PT_CME, RV_BASE, CORE0_CHIPLET_ID + ((2 * ex) + core), buf1, &tmp2)
				- continue if not found

				ALIGN_UP(tmp2, 8)
				tmpLen += tmp2

			maxLen = max(tmpLen, maxLen)

		if maxLen > 0:
			// Maybe can be 0 if no instance rings are present? Why there is no early return in that case?
			maxLen += sizeof(specRingList)	// 8B
			ALIGN_UP(maxLen, 32)

		// 2nd pass
		for each ex:		// 12 total
			if ex is not functional: continue

			start = homer->cpmr.cme_sram_region[ringLength + ex * (maxLen + ALIGN_UP(sizeof(LocalPstateParmBlock), 32))]
				- LocalPstateParmBlock is big (> 600B), not packed so there is additional padding
				- seems risky for cross-architecture communication...
				- is it included in CORE_SPECIFIC_RING_SIZE_PER_CORE? probably yes
			tmp = start
			payload = tmp->payload

			for each core:
				if core is not functional: continue
				tmp2 = buf1s
				tor_get_single_ring(pRingBuf, dd, ec_repr = (0xE4? 0x80?), PT_CME, RV_BASE, CORE0_CHIPLET_ID + ((2 * ex) + core), buf1, &tmp2)
				- continue if not found

				ALIGN_UP(payload, 8)
					- hostboot calculates this wrt start, so (payload - start) % 8 == 0
					- can we assume that start is always aligned?

				memcpy(payload, buf1, tmp2)

				tmp->ring[core] = payload - start

				payload += tmp2
				memset(buf1, 0, ringSize)

		ringLength = maxLen

	if (ringLength)
		CmeHdr->max_spec_ring_len = ALIGN_UP(ringLength, 32) / 32
		// I hope all alignments are in order so far...
		CmeHdr->core_spec_ring_offset = CmeHdr->g_cme_common_ring_offset + CmeHdr->g_cme_common_ring_len

l_ringData.iv_ringBufSize = i_sizeBuf1;

getPpeScanRings(hw, PLAT_SGPE, proc, ringData /* buffers */, imgType /* build/rebuild */)
	- same as before, but for SGPE

// create a layout of rings in HOMER for consumption of SGPE
layoutRingsForSGPE(homer, PNOR->RINGOVD, __proc__, ringData /* buffers */, l_ringDebug, RISK_LEVEL, qpmrHdr, imgType)
	// RINGOVD is empty, null is passed and some logic doesn't execute
	// Manage the Quad Common rings in HOMER
	layoutCmnRingsForSgpe(homer, __proc__, ringData, l_ringDebug, RISK_LEVEL, qpmrHdr, imgType, sgpeRings /* also debug only? */)
		resolveEqInexBucket(eqInexBucketId)
			// This return one of EQ_INEX_BUCKET_n, depending on core floor to nest frequency ratio:
			// ASYNC_SAFE_MODE:  EQ_INEX_BUCKET_1 = 62
			// 2/8:              EQ_INEX_BUCKET_2 = 63
			// {7,6,5,4}/8:      EQ_INEX_BUCKET_3 = 64
			// 8/8:              EQ_INEX_BUCKET_4 = 65
			// Note that these values are next indices that would be located in rindIds below

		// get core common rings
		ringIds = [eq_fure, eq_gptr, eq_time, eq_inex,
		           ex_l3_fure, ex_l3_gptr, ex_l3_time,
		           ex_l2_mode, ex_l2_fure, ex_l2_gptr, ex_l2_time,
		           ex_l3_refr_fure, ex_l3_refr_gptr,
		           eq_ana_func, eq_ana_gptr,
		           eq_dpll_func, eq_dpll_gptr, eq_dpll_mode,
		           eq_ana_bndy,
		           eq_ana_bndy_bucket_{0-25},
		           eq_ana_bndy_bucket_l3dcc, eq_ana_mode,
		           eq_ana_bndy_bucket_{26-41}]

		//!!!!!!!!!!!!!!!!!!!!!!!!!!!!! WARNING !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//
		// The following is based on QuadCmnRingsList_t which is defined in   //
		// p9_hcode_image_defines.H. It has the same order of rings as        //
		// ringIds above, which are based on p9_ringId.H (not to be confused  //
		// with p9_ring_id.h), except ***there is no eq_ana_bndy*** in the    //
		// struct below. Further rings are shifted back by one because of it. //
		//                                                                    //
		// That being said, the hostboot's code doesn't care, it writes all   //
		// rings as they go (skipping the ones that are not present). This    //
		// results in bad names in the struct (which may be bad if whatever   //
		// reads it depends on the same definition) and worse, bad sizeof,    //
		// which would be catastrophic if it weren't for ALIGN_UP(payload, 8) //
		// just before memcpy and the fact that the size in the current (bad) //
		// form is not a multiply of 8 already.                               //
		//                                                                    //
		// I strongly suggest implementing it as `u16 ring[62]`, not using    //
		// multiple structs/unions/enums to describe one thing, and, above    //
		// all, don't play with pointers when using an array is sufficient.   //
		//                                                                    //
		// Either that, or I totally don't understand what happens in this    //
		// code...                                                            //
		//!!!!!!!!!!!!!!!!!!!!!!!!!!!!! WARNING !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!//
		struct cmnRingList { u16 ring[61]; u8 payload[]; } *tmp;

		start = &homer->qpmr.sgpe.sram_image[qpmrHdr.img_len]
		tmp = &homer->qpmr.sgpe.sram_image[qpmrHdr.img_len]
		payload = tmp->payload

		for id in ringIds:		// MAX_HOMER_QUAD_CMN_RINGS = 66-4 = 62
			tempBufSize = buf1s

			//For eq_inex, request selected ring bucket else query
			//the ring that needs to be at this position in the layout.

			torRingId = (id == eq_inex) ? eqInexBucketId : id

			l_ringVariant = RISK_LEVEL
			if ((torRingId == eq_gptr)         || // EQ GPTR
			    (torRingId == eq_ana_gptr)     ||
			    (torRingId == eq_dpll_gptr)    ||
			    (torRingId == ex_l3_gptr)      || // EX GPTR
			    (torRingId == ex_l2_gptr)      ||
			    (torRingId == ex_l3_refr_gptr) ||
			    (torRingId == eq_time)         || // EQ TIME
			    (torRingId == ex_l3_time)      || // EX TIME
			    (torRingId == ex_l2_time)):
							l_ringVariant = RV_BASE

			tor_get_single_ring(pRingBuffer, dd, torRingId, PT_SGPE, l_ringVariant, CACHE0_CHIPLET_ID = 0x10, buf1, tempBufSize, l_ringDebug)
			- continue if not found, there are some not found in log

			ALIGN_UP(tempBufSize, 8)
			ALIGN_UP(payload, 8)
				- hostboot calculates this wrt start, so (payload - start) % 8 == 0
				- can we assume that start is always aligned?

			memcpy(payload, buf1, tempBufSize)
			tmp->ring[id] = payload - start
			payload += tempBufSize

			//cleaning up what we wrote in temp buffer last time.
			memset(buf1, 0x00, tempBufSize)

		qpmrHdr.quadCommonRingLength = payload - start
		qpmrHdr.quadCommonRingOffset = offsetof(homer, qpmr.sgpe.sram_image) + qpmrHdr.img_len

	// Manage the Quad Override rings in HOMER
	layoutSgpeScanOverride(...)
		- contrary to CME, this function is always called, but returns early if RINGOVD is empty
		- this leaves inconsistent traces in debug log

	// Manage the Quad specific rings in HOMER
	layoutInstRingsForSgpe(homer, __proc__, ringData, l_ringDebug, RV_BASE, qpmrHdr, imgType, sgpeRings)
		TBD !!!!!!

	if qpmrHdr.quadCommonRingLength == 0
		//If quad common rings don't exist ensure its offset in image header  is zero
		SgpeImgHdr->cmn_ring_occ_offset = 0

	if qpmrHdr.quadSpecRingLength > 0
		SgpeImgHdr->spec_ring_occ_offset = qpmrHdr.sgpeImgLength + qpmrHdr.quadCommonRingLength
		SgpeImgHdr->scom_offset = SgpeImgHdr->spec_ring_occ_offset + qpmrHdr.quadSpecRingLength

populateUnsecureHomerAddress(proc, homer, SmfEnabled = false):
	// For SMF disabled
	CmeHdr->unsec_cpmr_PhyAddr = CmeHdr->cpmr_PhyAddr;

// Update P State parameter block info in HOMER
buildParameterBlock(homer, proc, ppmrHdr = homer.ppmr.header, imgType, buf1, buf1s):
	p9_pstate_parameter_block(proc, &stateSupStruct /*13K struct */, buf1, wofTableSize = buf1s):
		TBD			- this prints a lot
	// Assuming >= CPMR_2.0
	buildCmePstateInfo(homer, proc, imgType, &stateSupStruct):
		TBD
	TBD !!

// Update CPMR Header with Scan Ring details
updateCpmrCmeRegion(homer, proc):
	// This function for each entry does one of:
	// - write constant value
	// - copy value form other field
	// - one or both of the above with arithmetic operations
	// Consider writing these fields in previous functions instead.
	CpmrHdr = &homer->cpmr.header
	CmeHdr = &homer->cpmr.cme_sram_region[CME_INT_VECTOR_SIZE]
	CpmrHdr->img_offset            = offsetof(cpmr_st, cme_sram_region) / 32
	CpmrHdr->cme_pstate_offset     = offsetof(cpmr_st, cme_sram_region) + CmeHdr->pstate_region_offset
	CpmrHdr->cme_pstate_len        = CmeHdr->pstate_region_len
	CpmrHdr->img_len               = CmeHdr->hcode_len
	CpmrHdr->core_scom_offset      = offsetof(cpmr_st, core_scom)
	CpmrHdr->core_scom_len         = CORE_SCOM_RESTORE_SIZE			// 6k
	CpmrHdr->core_max_scom_entry   = 15

	if CmeHdr->common_ring_len:
		CpmrHdr->cme_common_ring_offset = offsetof(cpmr_st, cme_sram_region) + CmeHdr->common_ring_offset
		CpmrHdr->cme_common_ring_len    = CmeHdr->common_ring_len

	if CmeHdr->max_spec_ring_len:
		CpmrHdr->core_spec_ring_offset  = ALIGN_UP(CpmrHdr->img_offset * 32 +
		                                           CpmrHdr->img_len) +
		                                           CpmrHdr->cme_pstate_len +
		                                           CpmrHdr->cme_common_ring_len,
		                                           32) / 32
		CpmrHdr->core_spec_ring_len     = CmeHdr->max_spec_ring_len

	for each functional CME:
		// CME index/position is the same as EX, however this means that Pstate
		// offset is overwritten when there are 2 functional CMEs in one quad.
		// Maybe we can use "for each functional quad" instead, but maybe
		// 'cme * CmeHdr->custom_length' points to different data, based on
		// whether there is one or two functional CMEs (is that even possible?).
		// Assuming >= CPMR_2.0
		CpmrHdr->quad<cme/2>_pstate_offset  = CpmrHdr->core_spec_ring_offset +
		                                      CpmrHdr->core_spec_ring_len +
		                                      cme * CmeHdr->custom_length

	// Updating CME Image header
	// Assuming >= CPMR_2.0
	// sizeof(LocalPstateParmBlock) = 0x280?
	CmeHdr->scom_offset   = CmeHdr->pstate_offset + sizeof(LocalPstateParmBlock) / 32

	// Adding to it instance ring length which is already a multiple of 32B
	CmeHdr->scom_len      = 512

	// Timebase frequency
	CmeHdr->timebase_hz = 1866MHz / 64

// Update QPMR Header area in HOMER
updateQpmrHeader(homer, qpmrHdr):
	// In hostboot, qpmrHdr is a copy of the header, it doesn't operate on HOMER
	// directly until now - it fills the following fields in the copy and then
	// does memcpy() to HOMER. As BAR is set up in next istep, I don't see why.
	homer->qpmr.sgpe.header.sram_img_size =
	              homer->qpmr.sgpe.header.img_len +
	              homer->qpmr.sgpe.header.common_ring_len +
	              homer->qpmr.sgpe.header.spec_ring_len
	homer->qpmr.sgpe.header.max_quad_restore_entry  = 255
	homer->qpmr.sgpe.header.build_ver               = 3

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
	- doesn't modify anything?

//Update the attributes storing PGPE and SGPE's boot copier offset.
updateGpeAttributes( pChipHomer, i_procTgt );
	- updates the IVPR attributes for SGPE, PGPE, doesn't touch HW

//customize magic word based on endianess
customizeMagicWord( pChipHomer );

addUrmorRestore():
	- no-op, runs only if __URMOR_TEST is defined

verifySprSelfSave():
	- no-op, runs only if __SELF_SAVE_TEST is defined

initUnsecureHomer():
	- fills all of unsecure HOMER with attn, reset vector with sc2
	- unsecure HOMER isn't actually used without SMF, so no-op

----------------------------------

p9_setup_runtime_wakeup_mode():
	for each functional core:
		TP.TPCHIP.NET.PCBSLEC14.PPMC.PPM_CORE_REGS.CPPM_CPMMR		// 0x200F0106
			// These bits, when set, make core wake up in HV (not UV)
			[3]	CPPM_CPMMR_RESERVED_2_9	= 1
			[4]	CPPM_CPMMR_RESERVED_2_9	= 1
```
