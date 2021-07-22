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
							// Who fills it and when?
							TBD !!!!!!!!!!!!!!!!!!!!!

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
		// Instantiate pstate object
		PlatPmPPB l_pmPPB(proc)
			- this constructor makes a local copy of attributes and prints them

		// -----------------------------------------------------------
		// Clear the PstateSuperStructure and install the magic number
		//----------------------------------------------------------
		memset(stateSupStruct, 0, sizeof(stateSupStruct))

		*stateSupStruct.magic = PSTATE_PARMSBLOCK_MAGIC		// 0x5053544154453030ull, PSTATE00

		// ----------------
		// get VPD data (#V,#W,IQ)
		// ----------------
		l_pmPPB.vpd_init():
			// Read #V data
			get_mvpd_poundV():
				for each functional quad:
					// 'quad' below is index including non-functional ones
					p9_pm_get_poundv_bucket(quad, /* voltageBucketData_t */ poundV_raw_data):
						- 61 bytes "read directly from VPD"
						- "reading directly" parses data differently based on version
						- version is read from (MVPD_RECORD_LRP<quad>, MVPD_KEYWORD_PDV)
						- data read from (MVPD_RECORD_VINI, MVPD_KEYWORD_PR)
						TBD

					bucket_id = poundV_raw_data.bucketId;

					// Whole lot of casting: voltageBucketData_t -> VpdPoint[5]
					// Data in MVPD is big endian, but offsets in TOC are little?

					chk_valid_poundv():
						- check that none of the fields from MVPD is 0
						- check that all fields are >= the same fields for lower performance Pstate
						- note: Pstates in MVPD aren't sorted
						- PowerBus is excluded from tests (not a core Pstate)

					if first functional chiplet:
						first = poundV_raw_data
						poundV_bucket_id = bucket_id
					else:
						// on subsequent chiplets, check that frequencies are same for each operating point for each chiplet
						if ((poundV_raw_data.nomFreq    != first.nomFreq) ||
						    (poundV_raw_data.PSFreq     != first.PSFreq) ||
						    (poundV_raw_data.turboFreq  != first.turboFreq) ||
						    (poundV_raw_data.uTurboFreq != first.uTurboFreq) ||
						    (poundV_raw_data.pbFreq     != first.pbFreq) ):
							die()

					// Check each bucket for max voltage and if max, save bucket's data
					// Relative voltages were tested in chk_valid_poundv() so it is not
					// possible for VddPSVltg > VddNomVltg etc.
					if ((poundV_raw_data.VddNomVltg    > first.VddNomVltg) ||
					    (poundV_raw_data.VddPSVltg     > first.VddPSVltg) ||
					    (poundV_raw_data.VddTurboVltg  > first.VddTurboVltg) ||
					    (poundV_raw_data.VddUTurboVltg > first.VddUTurboVltg) ||
					    (poundV_raw_data.VdnPbVltg     > first.VdnPbVltg) ):
						first = poundV_raw_data
						poundV_bucket_id = bucket_id

			// Apply biased values if any
			apply_biased_values():
				- this function gives the opportunity to change values obtained above
				- does a lot of floating point multiplications by 1
				- does chk_valid_poundv() on "new" values
				- is later recalculated in compute_vpd_pts()
				- nothing changes, can be skipped (at least for Talos)

			// Read #W data

			// ----------------
			// get VDM Parameters data
			// ----------------
			// Note:  the get_mvpd_poundW has the conditional checking for VDM
			// and WOF enablement as #W has both VDM and WOF content
			l_rc = get_mvpd_poundW();
				- Exit if both VDM and WOF is disabled
				for each functional quad:
					- Best to merge with previous loop, poundW needs bucketId from poundV
					p9_pm_get_poundw_bucket(quad, /* vdmData_t */ l_vdmBuf):
						struct vdmData_t {
							u8 version;			/* 0x1, 0x2-0xF or >=0x30 */
							u8 bucketId;
							u8 vdmData[0x87];	/* For biggest version */
						};
						- Structure above is as defined in p9_pm_get_poundw_bucket.H
						- It is returned in that form
						- In VPD this actually is version followed by 5 pairs of bucket ID/data
						- VPD is packed and data size depends on version:
						  - 0x1 -     0x28
						  - 0x2-0xF - 0x3C		// dumped MVPD has version 0x7
						  - >= 0x30 - 0x87
						- copy version and ID/data pair for bucket ID from #V
						- read from (MVPD_RECORD_CRP0, MVPD_KEYWORD_PDW)

					bucket_id   =   l_vdmBuf.bucketId;
					version_id  =   l_vdmBuf.version;
					- returned l_vdmBuf.vdmData is either PoundW_data (versions <0x30) or PoundW_data_per_quad
					if (version < 0x30):
						- for all 4 operating points, copy existing fields 1:1, except:
						vdm_vid_compare_per_quad[0-5] = vdm_vid_compare_ivid
						resistance_data.r_undervolt_allowed = undervolt_tested

					// If we match with the bucket id, then we don't need to continue
					if (poundV_bucket_id == bucket_id)
						break

				// Only now hostboot tests ATTR_POUND_W_STATIC_DATA_ENABLE and
				// discards l_vdmBuf altogether, using hardcoded values instead.
				// This attribute is not set on Talos.
				memcpy(iv_poundW_data, l_vdmBuf.vdmData, sizeof (l_vdmBuf.vdmData))

				// Re-ordering to Natural order
				// When we read the data from VPD image the order will be N,PS,T,UT.
				// But we need the order PS,N,T,UT.. hence we are swapping the data
				// between PS and Nominal.
				swap(iv_poundW_data.poundw[VPD_PV_POWERSAVE], iv_poundW_data.poundw[VPD_PV_NOMINAL])

				// If the #W version is less than 3, validate Turbo VDM large threshold
				// not larger than -32mV. This filters out parts that have bad VPD.  If
				// this check fails, log a recovered error, mark the VDMs disabled and
				// break out of the reset of the checks.
				if (version_id < FULLY_VALID_POUNDW_VERSION):
					// For now, implement if needed
					die()

				// Validate the WOF content is non-zero - assuming WOF is enabled
				- Log if any of the ivdd_tdp_{a,d}c_current_10ma is zero and set iv_wof_enabled = false

				// Assuming VDM is enabled (return otherwise)
				// VDM_ENABLE is OFF in talos.xml, yet code after this point still executes. Why?
				// Validate threshold values
				validate_quad_spec_data():
					for each functional quad:
						- if any of these checks fail: iv_vdm_enabled = false and return
						- check that none of iv_poundW_data.poundw[0-3].vdm_vid_compare_per_quad[quad] is 0
						  - special case for all 0s, needed for lab only?
						- check that iv_poundW_data.poundw[0-3].vdm_vid_compare_per_quad[quad] is not decreasing for higher operating points
						- apply bias from ATTR_VDM_VID_COMPARE_BIAS_0P5PCT
						  - no change, may be skipped

				- check that for all operating points:
				  - ((iv_poundW_data.poundw[p].vdm_overvolt_small_thresholds >> 4) & 0x0F)		<= 7 || == 0xC
				  - ((iv_poundW_data.poundw[p].vdm_overvolt_small_thresholds) & 0x0F)			!= 8 && != 9
				  - ((iv_poundW_data.poundw[p].vdm_large_extreme_thresholds >> 4) & 0x0F)		!= 8 && != 9
				  - ((iv_poundW_data.poundw[p].vdm_large_extreme_thresholds) & 0x0F)			!= 8 && != 9
				  - ((iv_poundW_data.poundw[p].vdm_normal_freq_drop) & 0x0F)					<= 7				// N_L
				  - ((iv_poundW_data.poundw[p].vdm_normal_freq_drop >> 4) & 0x0F)				<= N_L				// N_S
				  - ((iv_poundW_data.poundw[p].vdm_normal_freq_return >> 4) & 0x0F)				<= N_L - S_N		// L_S
				  - ((iv_poundW_data.poundw[p].vdm_normal_freq_return) & 0x0F)					<= N_S				// S_N

				// If we have reached this point, that means VDM is ok to be enabled. Only then we try to
				// enable wov undervolting
				// Based only on resistance_data.r_undervolt_allowed from MVPD
				iv_wov_underv_enabled = false

			if (l_rc):
				- print info about recovered error
				- return from vpd_init() with success

			// Read #IQ data
			get_mvpd_iddq():
				- read from (MVPD_RECORD_CRP0, MVPD_KEYWORD_IQ) to iv_iddqt
				- log warning and return with succes if any of the following is 0:
				  - iddq_version
				  - good_quads_per_sort
				  - good_normal_cores_per_sort
				  - good_caches_per_sort
				if (ivdd_all_cores_off_caches_off[i = 0..5] & 0x8000):
					ivdd_all_cores_off_caches_off[i] = 0

			// Load RAW VPD
			load_mvpd_operating_point(RAW)
				- for all OPs copies data from VpdPoint to VpdOperatingPoint
				- VpdOperatingPoint.pstate = (ultra_turbo_frequency - op_frequency) * 1000 / 16666 /* frequency step in kHz */

			// Load VPD operating point
			load_mvpd_operating_point(BIASED)
				- same as above

		// ----------------
		// get IVRM Parameters data
		// ----------------
		// TBD
		l_pmPPB.get_ivrm_parms():
			// "This is presently hardcoded to FALSE until validation code is in
			// place to ensure turning IVRM on is a good thing."
			- no-op

		// ----------------
		// Compute VPD points for different regions
		// ----------------
		l_pmPPB.compute_vpd_pts():
			- combines 4 sets in one structure (iv_operating_points):
			  - raw OPs - copy of what load_mvpd_operating_point(RAW) produced
			  - system params applied - mostly copy of raw, except voltages:
			    - vdd_mv = raw.vdd_mv + (idd_ma * (vdd_loadline_uohm + vdd_distloss_uohm) / 1000 + vdd_distoffset_uv) / 1000
			      - vdd_loadline_uohm = 254, vdd_distloss_uohm =  0, vdd_distoffset_uv = 0 - from talos.xml
			    - vcs_mv = raw.vcs_mv + (ics_ma * (vcs_loadline_uohm + vcs_distloss_uohm) / 1000 + vcs_distoffset_uv) / 1000
			      - vcs_loadline_uohm =   0, vcs_distloss_uohm = 64, vcs_distoffset_uv = 0 - from talos.xml
			  - biased
			    - idd_100ma, ics_100ma - copy of biased
			    - voltages and frequency are biased AGAIN from raw
			      - different order of floating point math than before, something may be lost due to precision
			    - ultra turbo frequency is saved as a reference frequency
			    - pstate is AGAIN calculated the same way as before
			  - biased with system params applied
			    - same as raw with system parameters, but based on biased values

		// ----------------
		// Safe mode freq and volt init
		// ----------------
		l_pmPPB.safe_mode_init():
			- no-op if ATTR_SAFE_MODE_FREQUENCY_MHZ and ATTR_SAFE_MODE_VOLTAGE_MV are not 0
			- 2152 and 667 respectively
			- probably isn't safe to hardcode
			  - safe_mode_computation() sets them if they are not set previously
			  - compute_boot_safe() sets them in 8.12, called by p9_setup_evid()
			- assuming attributes are not set
			safe_mode_computation():
				- this depends on ATTR_FREQ_CORE_FLOOR_MHZ with default value of 4800
				  - it is set in 6.12 through an alias `setAttr<ATTR_MIN_FREQ_MHZ>`
				  - default value of 4800 doesn't make any sense
				  - description in nest_attributes.xml says it is the highest Power Saving frequency across all cores
				    - get_mvpd_poundV() ensures that all cores have the same frequencies so there is only one PS freq
				    - comments say that only the nominal frequency must be same across cores, code ensures all modes
				jump_value = (iv_poundW_data.poundw[PS].vdm_normal_freq_drop & 0x0F) +	// N_L
				             (((iv_poundW_data.poundw[NOM].vdm_normal_freq_drop & 0x0F) -	// N_L
				               (iv_poundW_data.poundw[PS].vdm_normal_freq_drop & 0x0F)) /	// N_L
				              (ps_ps - nom_ps))
				- vdm_normal_freq_drop is the same for all OPs in current MVPD, can we rely on it?
				  - if yes, the above can be simplified to (iv_poundW_data.poundw[PS].vdm_normal_freq_drop & 0x0F)
				// ref_freq is biased ultra turbo
				// -1 because SM must be greater than PS
				// Use integer math to properly round it down (i.e. towards higher frequency)
				// step_size = 16666 kHz
				rounded_ps_ps = (ref_freq - ps_freq - 1) / step_size
				safe_mode_freq = (ref_freq - (rounded_ps_ps * step_size)) * 32 / (32 - jump_value)		// rounded to closest integer
				// Comment says "Safe frequency must be less than ultra turbo freq" but in code equal is still good
				assert (safe_mode_freq < ref_freq)
				safe_mode_ps = (ref_freq - safe_mode_freq) / step_size		// rounded down - use integer math

				// Now calculate safe voltages
				// Frequency to Pstate relation is always linear, but voltage to Pstate isn't.
				// All calculations done below operate on biased values with sysparams
				- find out in which segment safe_mode_ps is (i.e. between PS and NOM, NOM and TURBO or TURBO and ULTRATURBO)
				- take appropriate iv_operating_points[VPD_PT_SET_BIASED_SYSP][op].{vdd_mv, pstate}
				- do a linear interpolation
				  - hostboot can't do rounding: https://github.com/open-power/hostboot/blob/master/src/import/chips/p9/procedures/hwp/pm/p9_pstate_parameter_block.C#L1803

		// ----------------
		// Initialize  VDM data
		// ----------------
		l_pmPPB.vdm_init():
			- assuming VDM is enabled
			compute_vdm_threshold_pts():
				- this copies data from #W to different structures:
				  - vdm_vid_compare_per_quad to iv_vid_point_set (for each OP for functional quad)
				    - although there is different data per quad, LPPB is written with quad0 data
				  - vdm_overvolt_small_thresholds and vdm_large_extreme_thresholds to iv_threshold_set (for each OP)
				    - split nibbles and applied Grey encoding
				  - vdm_normal_freq_drop and vdm_normal_freq_return to iv_jump_value_set (for each OP)
				    - split nibbles

			// VID slope calculation
			compute_PsVIDCompSlopes_slopes():
				for each functional quad, for each Pstate segment:
					- saves VID slopes: iv_vid_point_set to biased pstate ratio in iv_PsVIDCompSlopes
					  - 4.12 fixed point format

			// VDM threshold slope calculation
			compute_PsVDMThreshSlopes():
				for each Pstate segment:
					- saves VDM slopes: iv_threshold_set to biased pstate ratio in iv_PsVDMThreshSlopes

			// VDM Jump slope calculation
			compute_PsVDMJumpSlopes ():
				for each Pstate segment:
					- saves jump slopes: iv_jump_value_set to biased pstate ratio in iv_PsVDMJumpSlopes

		// ----------------
		// get Resonant clocking attributes
		// ----------------
		l_pmPPB.resclk_init():
			- assuming Resonant Clocks are enabled
			set_resclk_table_attrs():
				- reads data from p9_resclk_defines.H and writes it to attributes:
				  - ATTR_SYSTEM_RESCLK_L3_VALUE
				  - ATTR_SYSTEM_RESCLK_FREQ_REGIONS
				  - ATTR_SYSTEM_RESCLK_FREQ_REGION_INDEX
				  - ATTR_SYSTEM_RESCLK_VALUE
				  - ATTR_SYSTEM_RESCLK_L3_VOLTAGE_THRESHOLD_MV
			res_clock_setup():
				- reads data from attributes and saves it to iv_resclk_setup
				  - all of the p9_resclk_defines.H
				  - ATTR_SYSTEM_RESCLK_STEP_DELAY (= 0?)
				  - none of these is used anywhere else
				  - pstates and indices are capped at ultra turbo, but all entries are still written

		// ----------------
		// Initialize GPPB structure
		// ----------------
		l_pmPPB.gppb_init(&l_globalppb):
			// LHS fields are members of l_globalppb
			// This function is basically one big unnecessary memcpy...
			magic = PSTATE_PARMSBLOCK_MAGIC		// 0x5053544154453030, "PSTATE00"
			options.options = 0
			reference_frequency_khz = reference_frequency_khz	// from compute_vpd_pts()
			nest_frequency_mhz = ATTR_FREQ_PB_MHZ
			frequency_step_khz = ATTR_FREQ_DPLL_REFCLOCK_KHZ / ATTR_PROC_DPLL_DIVIDER
			                     // 133333 / 8 = 16666 from default values

			// VpdBias External and Internal Biases for Global and Local parameter
			// block
			for each OP point:
				ext_biases[op] = iv_bias[op]		// = 0?
				int_biases[op] = iv_bias[op]

			// Those 3 fields are structs, each has 3x u32: loadline_uohm, distloss_uohm, distoffset_uv
			// Defaul values are from talos.xml
			vdd_sysparm = {ATTR_PROC_R_LOADLINE_VDD_UOHM, ATTR_PROC_R_DISTLOSS_VDD_UOHM, ATTR_PROC_VRM_VOFFSET_VDD_UV} = {254, 0, 0}
			vcs_sysparm = {ATTR_PROC_R_LOADLINE_VCS_UOHM, ATTR_PROC_R_DISTLOSS_VCS_UOHM, ATTR_PROC_VRM_VOFFSET_VCS_UV} = {0, 64, 0}
			vdn_sysparm = {ATTR_PROC_R_LOADLINE_VDN_UOHM, ATTR_PROC_R_DISTLOSS_VDN_UOHM, ATTR_PROC_VRM_VOFFSET_VDN_UV} = {0, 50, 0}

			// External VRM parameters
			ext_vrm_transition_start_ns           = ATTR_EXTERNAL_VRM_TRANSITION_START_NS				// 8000 (internal default)
			ext_vrm_transition_rate_inc_uv_per_us = ATTR_EXTERNAL_VRM_TRANSITION_RATE_INC_UV_PER_US		// 10000 (internal default)
			ext_vrm_transition_rate_dec_uv_per_us = ATTR_EXTERNAL_VRM_TRANSITION_RATE_DEC_UV_PER_US		// 10000 (internal default)
			ext_vrm_stabilization_time_us         = ATTR_EXTERNAL_VRM_TRANSITION_STABILIZATION_TIME_NS	// 5 (internal default)
			ext_vrm_step_size_mv                  = ATTR_EXTERNAL_VRM_STEPSIZE							// 50 (internal default)

			// safe_voltage_mv
			safe_voltage_mv = ATTR_SAFE_MODE_VOLTAGE_MV					// from safe_mode_computation()
			safe_frequency_khz = ATTR_SAFE_MODE_FREQUENCY_MHZ * 1000	// from safe_mode_computation()

			// Struct definition in p9_pstates_pgpe.h
			vdm = {ATTR_VDM_VID_COMPARE_OVERRIDE_MV, ATTR_DPLL_VDM_RESPONSE,
			       ATTR_VDM_DROOP_SMALL_OVERRIDE, ATTR_VDM_DROOP_LARGE_OVERRIDE, ATTR_VDM_DROOP_EXTREME_OVERRIDE,
			       ATTR_VDM_OVERVOLT_OVERRIDE, ATTR_VDM_FMIN_OVERRIDE_KHZ, ATTR_VDM_FMAX_OVERRIDE_KHZ}

			// Struct definition in p9_pstates_cmeqm.h
			ivrm = {ATTR_IVRM_STRENGTH_LOOKUP, ATTR_IVRM_VIN_MULTIPLIER, ATTR_IVRM_VIN_MAX_MV,
			        ATTR_IVRM_STEP_DELAY_NS, ATTR_IVRM_STABILIZATION_DELAY_NS, ATTR_IVRM_DEADZONE_MV}

			// Load vpd operating points - use biased values from compute_vpd_pts()
			for each OP point:
				operating_points[op].frequency_mhz  = iv_operating_points[BIASED][op].frequency_mhz
				operating_points[op].vdd_mv         = iv_operating_points[BIASED][op].vdd_mv
				operating_points[op].idd_100ma      = iv_operating_points[BIASED][op].idd_100ma
				operating_points[op].vcs_mv         = iv_operating_points[BIASED][op].vcs_mv
				operating_points[op].ics_100ma      = iv_operating_points[BIASED][op].ics_100ma
				operating_points[op].pstate         = iv_operating_points[BIASED][op].pstate

			// Initialize res clk data
			resclk = iv_resclk_setup		// from res_clock_setup()

			// -----------------------------------------------
			// populate VpdOperatingPoint with biased MVPD attributes
			// -----------------------------------------------
			for each VPD set:
				for each OP point:
					io_globalppb->operating_points_set[set][op].frequency_mhz = iv_operating_points[set][op].frequency_mhz
					io_globalppb->operating_points_set[set][op].vdd_mv        = iv_operating_points[set][op].vdd_mv
					io_globalppb->operating_points_set[set][op].idd_100ma     = iv_operating_points[set][op].idd_100ma
					io_globalppb->operating_points_set[set][op].vcs_mv        = iv_operating_points[set][op].vcs_mv
					io_globalppb->operating_points_set[set][op].ics_100ma     = iv_operating_points[set][op].ics_100ma
					io_globalppb->operating_points_set[set][op].pstate        = iv_operating_points[set][op].pstate

			// Calculate pre-calculated slopes
			compute_PStateV_slope():
				for each VPD set:
					for each Pstate segment:
						PStateVSlopes[set][segment] = vdd_mv / pstate
						VPStateSlopes[set][segment] = pstate / vdd_mv

			// This is Pstate value that would be assigned to frequency of 0
			dpll_pstate0_value = reference_frequency_khz / frequency_step_khz

			//Initializing threshold and jump values for GPPB
			for each OP point:
				vid_point_set[op] = iv_vid_point_set[0][op]		// from compute_vdm_threshold_pts()

			threshold_set  = iv_threshold_set					// from compute_vdm_threshold_pts()
			jump_value_set = iv_jump_value_set					// from compute_vdm_threshold_pts()

			for each Pstate segment:
				PsVIDCompSlopes[segment] = iv_PsVIDCompSlopes[0][segment]	// from compute_PsVIDCompSlopes_slopes()

			PsVDMThreshSlopes = iv_PsVDMThreshSlopes			// from compute_PsVDMThreshSlopes()
			PsVDMJumpSlopes   = iv_PsVDMJumpSlopes				// from compute_PsVDMJumpSlopes()

			// Put the good_normal_cores value into the GPPB for PGPE
			// This is u8 -> u32 conversion
			options.pad = iv_iddqt.good_normal_cores_per_sort	// from get_mvpd_iddq()

			// WOV parameters
			wov_sample_125us                = ATTR_WOV_SAMPLE_125US						// 2 (internal default)
			wov_max_droop_pct               = ATTR_WOV_MAX_DROOP_10THPCT				// 125 (internal default)
			wov_underv_perf_loss_thresh_pct = ATTR_WOV_UNDERV_PERF_LOSS_THRESH_10THPCT	// 5 (internal default)
			wov_underv_step_incr_pct        = ATTR_WOV_UNDERV_STEP_INCR_10THPCT			// 5 (internal default)
			wov_underv_step_decr_pct        = ATTR_WOV_UNDERV_STEP_DECR_10THPCT			// 5 (internal default)
			wov_underv_max_pct              = ATTR_WOV_UNDERV_MAX_10THPCT				// 0 (internal default)
			wov_underv_vmin_mv              = ATTR_WOV_UNDERV_VMIN_MV					// safe_voltage_mv
			wov_overv_vmax_mv               = ATTR_WOV_OVERV_VMAX_SETPOINT_MV			// 1150 (internal default)
			wov_overv_step_incr_pct         = ATTR_WOV_OVERV_STEP_INCR_10THPCT			// 5 (internal default)
			wov_overv_step_decr_pct         = ATTR_WOV_OVERV_STEP_DECR_10THPCT			// 5 (internal default)
			wov_overv_max_pct               = ATTR_WOV_OVERV_MAX_10THPCT				// 100 (internal default)

			// Avs Bus topology - values come from talos.xml
			avs_bus_topology.vdd_avsbus_num  = ATTR_VDD_AVSBUS_BUSNUM	// 0
			avs_bus_topology.vdd_avsbus_rail = ATTR_VDD_AVSBUS_RAIL		// 0
			avs_bus_topology.vdn_avsbus_num  = ATTR_VDN_AVSBUS_BUSNUM	// 1
			avs_bus_topology.vdn_avsbus_rail = ATTR_VDN_AVSBUS_RAIL		// 0
			avs_bus_topology.vcs_avsbus_num  = ATTR_VCS_AVSBUS_BUSNUM	// 0
			avs_bus_topology.vcs_avsbus_rail = ATTR_VCS_AVSBUS_RAIL		// 1

		// ----------------
		// Initialize LPPB structure
		// ----------------
		// l_localppb is an array of 6 (quads per CPU)
		l_pmPPB.lppb_init(&l_localppb[0]):
			for each functional quad:
				// LHS is l_localppb[quad]
				magic = LOCAL_PARMSBLOCK_MAGIC		// 0x434d455050423030, "CMEPPB00"

				// VpdBias External and Internal Biases for Global and Local parameter
				// block
				for each OP point:
					ext_biases[op] = iv_bias[op]		// = 0?
					int_biases[op] = iv_bias[op]

				// Load vpd operating points - use biased values from compute_vpd_pts()
				for each OP point:
					operating_points[op].frequency_mhz  = iv_operating_points[BIASED][op].frequency_mhz
					operating_points[op].vdd_mv         = iv_operating_points[BIASED][op].vdd_mv
					operating_points[op].idd_100ma      = iv_operating_points[BIASED][op].idd_100ma
					operating_points[op].vcs_mv         = iv_operating_points[BIASED][op].vcs_mv
					operating_points[op].ics_100ma      = iv_operating_points[BIASED][op].ics_100ma
					operating_points[op].pstate         = iv_operating_points[BIASED][op].pstate

				// Defaul values are from talos.xml
				vdd_sysparm = {ATTR_PROC_R_LOADLINE_VDD_UOHM, ATTR_PROC_R_DISTLOSS_VDD_UOHM, ATTR_PROC_VRM_VOFFSET_VDD_UV} = {254, 0, 0}

				// IvrmParmBlock
				// Struct definition in p9_pstates_cmeqm.h
				ivrm = {ATTR_IVRM_STRENGTH_LOOKUP, ATTR_IVRM_VIN_MULTIPLIER, ATTR_IVRM_VIN_MAX_MV,
						ATTR_IVRM_STEP_DELAY_NS, ATTR_IVRM_STABILIZATION_DELAY_NS, ATTR_IVRM_DEADZONE_MV}

				// VDMParmBlock
				// WARNING: this is different than in GPPB
				memset(vdm, 0, sizeof(vdm))

				dpll_pstate0_value = reference_frequency_khz / frequency_step_khz

				resclk = iv_resclk_setup		// from res_clock_setup()

				// Code memcpies always from data for first quad, seems like a bug
				for each OP point:
					vid_point_set[op] = iv_vid_point_set[0][op]		// from compute_vdm_threshold_pts()

				threshold_set  = iv_threshold_set					// from compute_vdm_threshold_pts()
				jump_value_set = iv_jump_value_set					// from compute_vdm_threshold_pts()

				// Code memcpies always from data for first quad, seems like a bug
				for each Pstate segment:
					PsVIDCompSlopes[segment] = iv_PsVIDCompSlopes[0][segment]	// from compute_PsVIDCompSlopes_slopes()

				PsVDMThreshSlopes = iv_PsVDMThreshSlopes			// from compute_PsVDMThreshSlopes()
				PsVDMJumpSlopes   = iv_PsVDMJumpSlopes				// from compute_PsVDMJumpSlopes()

		// ----------------
		// WOF initialization
		// ----------------
		l_pmPPB.wof_init(o_buf /* will be homer->ppmr.wof_tables after few more memcpies */, o_size):
			- Search for proper data in WOFDATA PNOR partition
			- WOFDATA is 3M, make sure CBFS_CACHE is big enough
			- search until match is found:
			  - core count
			  - socket power (nominal, as read from #V)
			  - frequency (nominal, as read from #V)
			  - if version >= WOF_TABLE_VERSION_POWERMODE (2):
			    - mode matches current mode (WOF_MODE_NOMINAL = 1) or wildcard (WOF_MODE_UNKNOWN = 0)
			- structures used:
			  - wofImageHeader_t from plat_wof_access.C
			    - check magic and version
			  - wofSectionTableEntry_t from plat_wof_access.C
			  - WofTablesHeader_t from p9_pstates_common.h
			memcpy(o_buf, &WofTablesHeader_t /* for found entry */, wofSectionTableEntry_t[found_entry_idx].size)

			// Just the header, rest needs parsing
			memcpy(homer->ppmr.wof_tables, o_buf, sizeof(WofTablesHeader_t))

			for vfrt_index in 0..((WofTablesHeader_t*)o_buf->vdn_size * (WofTablesHeader_t*)o_buf->vdd_size * ACTIVE_QUADS) -1:
				src = o_buf                  + sizeof(WofTablesHeader_t) + vfrt_index * 128 /* vRTF size */
				dst = homer->ppmr.wof_tables + sizeof(WofTablesHeader_t) + vfrt_index * sizeof(HomerVFRTLayout_t) /* 256B */
				update_vfrt (src, dst):
					- Assumption: no bias, makes this function so much easier
					// Data in src has 8B header followed by 5*24 bytes of frequency information, such that freq = value*step_size + 1GHz.
					// Data in dst has (almost) the same header followed by 5*24 bytes of Pstates.
					// Copy header
					memcpy(dst, src, 8)
					// Flip type from System to Homer
					dst.type_version |= 0x10
					assert(dst.magic = "VT")
					for idx in 0..5*24 -1:
						dst[8+idx] = freq_to_pstate(src[8+idx])		// rounded properly

		// ----------------
		//Initialize OPPB structure
		// ----------------
		l_pmPPB.oppb_init(&l_occppb):
			// LHS is l_occppb, it eventually will be homer->ppmr.occ_parm_block
			magic = OCC_PARMSBLOCK_MAGIC		// 0x4f43435050423030, "OCCPPB00"

			wof.wof_enabled = 1		// Assuming wof_init() succeeded

			vdd_sysparm = {ATTR_PROC_R_LOADLINE_VDD_UOHM, ATTR_PROC_R_DISTLOSS_VDD_UOHM, ATTR_PROC_VRM_VOFFSET_VDD_UV} = {254, 0, 0}
			vcs_sysparm = {ATTR_PROC_R_LOADLINE_VCS_UOHM, ATTR_PROC_R_DISTLOSS_VCS_UOHM, ATTR_PROC_VRM_VOFFSET_VCS_UV} = {0, 64, 0}
			vdn_sysparm = {ATTR_PROC_R_LOADLINE_VDN_UOHM, ATTR_PROC_R_DISTLOSS_VDN_UOHM, ATTR_PROC_VRM_VOFFSET_VDN_UV} = {0, 50, 0}

			// Load vpd operating points - use biased values from compute_vpd_pts()
			for each OP point:
				operating_points[op].frequency_mhz  = iv_operating_points[BIASED][op].frequency_mhz
				operating_points[op].vdd_mv         = iv_operating_points[BIASED][op].vdd_mv
				operating_points[op].idd_100ma      = iv_operating_points[BIASED][op].idd_100ma
				operating_points[op].vcs_mv         = iv_operating_points[BIASED][op].vcs_mv
				operating_points[op].ics_100ma      = iv_operating_points[BIASED][op].ics_100ma
				operating_points[op].pstate         = iv_operating_points[BIASED][op].pstate


			// The minimum Pstate must be rounded down so that core floor constraints are not violated.
			pstate_min = freq_to_pstate(ATTR_SAFE_MODE_FREQUENCY_MHZ * 1000)	// from safe_mode_computation()

			frequency_min_khz = iv_reference_frequency_khz - (pstate_min * iv_frequency_step_khz)
			frequency_max_khz = iv_reference_frequency_khz
			frequency_step_khz = iv_frequency_step_khz


			// Iddq Table
			iddq = iv_iddqt				// from get_mvpd_iddq()

			wof.tdp_rdp_factor = ATTR_TDP_RDP_CURRENT_FACTOR		// 0 from talos.xml
			nest_leakage_percent = ATTR_NEST_LEAKAGE_PERCENT		// 60 (0x3C) from hb_temp_defaults.xml

			lac_tdp_vdd_turbo_10ma =
			         iv_poundW_data.poundw[TURBO].ivdd_tdp_ac_current_10ma
			lac_tdp_vdd_nominal_10ma =
			         iv_poundW_data.poundw[NOMINAL].ivdd_tdp_ac_current_10ma

			// As the Vdn dimension is not supported in the WOF tables,
			// hardcoding this value to the OCC as non-zero to keep it happy.
			ceff_tdp_vdn = 1;

			//Update nest frequency in OPPB
			nest_frequency_mhz = ATTR_FREQ_PB_MHZ				// 1866 from talos.xml

		// ----------------
		//Initialize pstate feature attribute state
		// ----------------
		l_pmPPB.set_global_feature_attributes():
			- just sets attributes based on class' variables
			- no-op

		- copy all local tables to stateSupStruct

	// Assuming >= CPMR_2.0
	buildCmePstateInfo(homer, proc, imgType, &stateSupStruct):
		CmeHdr = &homer->cpmr.cme_sram_region[INT_VECTOR_SIZE]
		CmeHdr->pstate_offset = CmeHdr->core_spec_ring_offset + CmeHdr->max_spec_ring_len
		CmeHdr->custom_length = ROUND_UP(sizeof(LocalPstateParmBlock), 32) / 32 + CmeHdr->max_spec_ring_len
		for each functional CME:
			memcpy(&homer->cpmr.cme_sram_region[cme * CmeHdr->custom_length * 32 + CmeHdr->pstate_offset], stateSupStruct->localppb[cme/2], sizeof(LocalPstateParmBlock))

	memcpy(&homer->ppmr.pgpe_sram_img[homer->ppmr.header.hcode_len], stateSupStruct->globalppb, sizeof(GlobalPstateParmBlock))
	homer->ppmr.header.gppb_offset = homer->ppmr.header.hcode_offset + homer->ppmr.header.hcode_len
	homer->ppmr.header.gppb_len    = ALIGN_UP(sizeof(GlobalPstateParmBlock), 8)

	memcpy(&homer->ppmr.occ_parm_block, stateSupStruct->occppb, sizeof(OCCPstateParmBlock))
	homer->ppmr.header.oppb_offset = offsetof(ppmr, ppmr.occ_parm_block)
	homer->ppmr.header.oppb_len    = ALIGN_UP(sizeof(OCCPstateParmBlock), 8)

	// Assuming >= CPMR_2.0
	homer->ppmr.header.lppb_offset = 0
	homer->ppmr.header.lppb_len = 0

	homer->ppmr.header.pstables_offset = offsetof(ppmr, ppmr.pstate_table)
	homer->ppmr.header.pstables_len    = PSTATE_OUTPUT_TABLES_SIZE		// 16 KiB

	homer->ppmr.header.wof_table_offset = OCC_WOF_TABLES_OFFSET
	homer->ppmr.header.wof_table_len    = OCC_WOF_TABLES_SIZE
	// Instead of this memcpy write it directly to its final destination in wof_init()
	memcpy(homer->ppmr.wof_tables, o_buf/* see wof_init() */, o_size/* see wof_init() */)

	homer->ppmr.header.sram_img_size = homer->ppmr.header.hcode_len + homer->ppmr.header.gppb_len

// Update CPMR Header with Scan Ring details
updateCpmrCmeRegion():
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
updateQpmrHeader():
	// In hostboot, qpmrHdr is a copy of the header, it doesn't operate on HOMER
	// directly until now - it fills the following fields in the copy and then
	// does memcpy() to HOMER. As BAR is set up in next istep, I don't see why.
	homer->qpmr.sgpe.header.sram_img_size =
	              homer->qpmr.sgpe.header.img_len +
	              homer->qpmr.sgpe.header.common_ring_len +
	              homer->qpmr.sgpe.header.spec_ring_len
	homer->qpmr.sgpe.header.max_quad_restore_entry  = 255
	homer->qpmr.sgpe.header.build_ver               = 3
	SgpeHdr = &homer->qpmr.sgpe.sram_image[INT_VECTOR_SIZE]
	SgpeHdr->scom_mem_offset = offsetof(homer, homer.qpmr.cache_scom_region)

//update PPMR Header area in HOMER
updatePpmrHeader( pChipHomer, l_ppmrHdr, i_procTgt );
	PgpeHdr = &homer->ppmr.pgpe_sram_img[INT_VECTOR_SIZE]
	PgpeHdr->core_throttle_assert_cnt   = 0
	PgpeHdr->core_throttle_deassert_cnt = 0
	PgpeHdr->ivpr_addr                  = 0xFFF20000	// OCC_SRAM_PGPE_BASE_ADDR
	                                 // = homer->ppmr.header.sram_region_start
	PgpeHdr->gppb_sram_addr             = 0		// set by PGPE Hcode (or not? It is still 0 in dumped HOMER)
	PgpeHdr->hcode_len                  = homer->ppmr.header.hcode_len
	PgpeHdr->gppb_mem_offset            = 0x80000000 + offsetof(homer, ppmr) + homer->ppmr.header.gppb_offset
	PgpeHdr->gppb_len                   = homer->ppmr.header.gppb_len
	PgpeHdr->gen_pstables_mem_offset    = 0x80000000 + offsetof(homer, ppmr) + homer->ppmr.header.pstables_offset
	PgpeHdr->gen_pstables_len           = homer->ppmr.header.pstables_len
	PgpeHdr->occ_pstables_sram_addr     = 0
	PgpeHdr->occ_pstables_len           = 0
	PgpeHdr->beacon_addr                = 0
	PgpeHdr->quad_status_addr           = 0
	PgpeHdr->wof_state_address          = 0
	PgpeHdr->wof_values_address         = 0
	PgpeHdr->req_active_quad_address    = 0
	PgpeHdr->wof_table_addr             = homer->ppmr.header.wof_table_offset
	PgpeHdr->wof_table_len              = homer->ppmr.header.wof_table_len
	PgpeHdr->timebase_hz                = 1866MHz / 64
	PgpeHdr->doptrace_offset            = homer->ppmr.header.doptrace_offset
	PgpeHdr->doptrace_len               = homer->ppmr.header.doptrace_len

--------------------
//Update L2 Epsilon SCOM Registers
populateEpsilonL2ScomReg( pChipHomer );

//Update L3 Epsilon SCOM Registers
populateEpsilonL3ScomReg( pChipHomer );

//Update L3 Refresh Timer Control SCOM Registers
populateL3RefreshScomReg( pChipHomer, i_procTgt);

//populate HOMER with SCOM restore value of NCU RNG BAR SCOM Register
populateNcuRngBarScomReg( pChipHomer, i_procTgt );
--------------------

//validate SRAM Image Sizes of PPE's
validateSramImageSize():
	- checks if any component overflows of its assigned area
	- can be done as they are written instead

//Update CME/SGPE Flags in respective image header.
updateImageFlags():
	- flags are set based on attributes
	TBD !!!!!!!!!!!!!!!!!

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
