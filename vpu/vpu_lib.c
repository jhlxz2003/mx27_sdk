/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * 
 * Copyright (c) 2006, Chips & Media.  All rights reserved.
 */

/*
 * The code contained herein is licensed under the GNU Lesser General 
 * Public License.  You may obtain a copy of the GNU Lesser General 
 * Public License Version 2.1 or later at the following locations:
 *
 * http://www.opensource.org/licenses/lgpl-license.html
 * http://www.gnu.org/copyleft/lgpl.html
 */

/*!
 * @file vpu_lib.c
 *
 * @brief This file implements codec API funcitons for VPU
 *
 * @ingroup VPU
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "vpu_reg.h"
#include "vpu_lib.h"
#include "vpu_util.h"
#include "vpu_io.h"

#if defined(IMX27ADS) || defined(IMX27MDK27V0)
#include "vpu_codetable_mx27.h"
#elif defined(IMX31ADS)
#include "vpu_codetable_mx32.h"
#elif defined(MXC30031ADS)
#include "vpu_codetable_mxc30031.h"
#else
#error PLATFORM not defined
#endif

extern CodecInst codecInstPool[MAX_NUM_INSTANCE];

/* If a frame is started, pendingInst is set to the proper instance.*/
static CodecInst *pendingInst;

static PhysicalAddress rdPtrRegAddr[] = {
	BIT_RD_PTR_0,
	BIT_RD_PTR_1,
	BIT_RD_PTR_2,
	BIT_RD_PTR_3
};

static PhysicalAddress wrPtrRegAddr[] = {
	BIT_WR_PTR_0,
	BIT_WR_PTR_1,
	BIT_WR_PTR_2,
	BIT_WR_PTR_3
};

/* If a frame is started, pendingInst is set to the proper instance. */
static CodecInst *pendingInst;

static PhysicalAddress workBuffer;
static PhysicalAddress codeBuffer;
static PhysicalAddress paraBuffer;

unsigned long *virt_paraBuf;
unsigned long *virt_paraBuf2;

extern vpu_mem_desc bit_work_addr;

/*!
 * @brief
 * This functure indicate whether processing(encoding/decoding) a frame 
 * is completed or not yet.
 *
 * @return 
 * @li 0: VPU hardware is idle.
 * @li Non-zero value: VPU hardware is busy processing a frame.
 */
int vpu_IsBusy()
{
	return VpuReadReg(BIT_BUSY_FLAG) != 0;
}

int vpu_WaitForInt(int timeout_in_ms)
{
	return IOWaitForInt(timeout_in_ms);
}

/*!
 * @brief VPU initialization.
 * This function initializes VPU hardware and proper data structures/resources. 
 * The user must call this function only once before using VPU codec.
 *
 * @param  workBuf  The physical address of a working space of the codec. 
 *  The size of the space must be at least CODE_BUF_SIZE + WORK_BUF_SIZE
 * + PARA_BUF2_SIZE + PARA_BUF_SIZE in KB.
 *
 * @return  This function always returns RETCODE_SUCCESS.
 */
RetCode vpu_Init(PhysicalAddress workBuf)
{
	int i;
	volatile Uint32 data;
	Uint32 virt_codeBuf;
	CodecInst *pCodecInst;

	codeBuffer = workBuf;
	workBuffer = codeBuffer + CODE_BUF_SIZE;
	paraBuffer = workBuffer + WORK_BUF_SIZE + PARA_BUF2_SIZE;

	virt_codeBuf = (Uint32) (bit_work_addr.virt_uaddr);
	virt_paraBuf = (unsigned long *)(virt_codeBuf + CODE_BUF_SIZE +
					 WORK_BUF_SIZE + PARA_BUF2_SIZE);
	virt_paraBuf2 = (unsigned long *)(virt_codeBuf + CODE_BUF_SIZE +
					  WORK_BUF_SIZE);
	
	/* Copy full Microcode to Code Buffer allocated on SDRAM */
	if (platform_is_mx27to2()) {
		for (i = 0; i < sizeof(bit_code2) / sizeof(bit_code2[0]); 
								i += 2) {
			data = (unsigned int)((bit_code2[i] << 16) | 
							bit_code2[i + 1]);
			((unsigned int *)virt_codeBuf)[i / 2] = data;
		}
	} else {
		for (i = 0; i < sizeof(bit_code) / sizeof(bit_code[0]); 
								i += 2) {
			data = (unsigned int)((bit_code[i] << 16) | 
							bit_code[i + 1]);
			((unsigned int *)virt_codeBuf)[i / 2] = data;
		}
	}

	VpuWriteReg(BIT_WORK_BUF_ADDR, workBuffer);
	VpuWriteReg(BIT_PARA_BUF_ADDR, paraBuffer);
	VpuWriteReg(BIT_CODE_BUF_ADDR, codeBuffer);

	if (!platform_is_mx27()) {
		if (VpuReadReg(BIT_CUR_PC) != 0) {
			/* IRQ is disabled during shutdown */
			VpuWriteReg(BIT_INT_ENABLE, 8);
			return RETCODE_SUCCESS;
		}
	}
	
	VpuWriteReg(BIT_CODE_RUN, 0);

	/* Download BIT Microcode to Program Memory */
	if (platform_is_mx27to2()) {
		for (i = 0; i < 2048; ++i) {
			data = bit_code2[i];
			VpuWriteReg(BIT_CODE_DOWN, (i << 16) | data);
		}
	} else {
		for (i = 0; i < 2048; ++i) {
			data = bit_code[i];
			VpuWriteReg(BIT_CODE_DOWN, (i << 16) | data);
		}
	}

	data = STREAM_FULL_EMPTY_CHECK_DISABLE << 1;
	data |= STREAM_ENDIAN;
	data |= 1 << 2;
	VpuWriteReg(BIT_BIT_STREAM_CTRL, data);
	VpuWriteReg(BIT_FRAME_MEM_CTRL, IMAGE_ENDIAN);
	VpuWriteReg(BIT_INT_ENABLE, 8);	/* PIC_RUN irq enable */

	if (platform_is_mxc30031()) {
		VpuWriteReg(BIT_VPU_PIC_COUNT, 0); /* Init VPU PIC COUNT */
	}

	if (platform_is_mx27()) {
		ResetVpu();
	}

	VpuWriteReg(BIT_CODE_RUN, 1);
	
	pCodecInst = &codecInstPool[0];
	for (i = 0; i < MAX_NUM_INSTANCE; ++i, ++pCodecInst) {
		pCodecInst->instIndex = i;
		pCodecInst->inUse = 0;
	}

	return RETCODE_SUCCESS;
}

/*!
 * @brief Get VPU Firmware Version.
 */
RetCode vpu_GetVersionInfo(vpu_versioninfo * verinfo)
{
	Uint32 ver;
	Uint16 pn, version;
	RetCode ret = RETCODE_SUCCESS;
	char productstr[18] = { 0 };

	if (!isVpuInitialized()) {
		return RETCODE_NOT_INITIALIZED;
	}

	if (pendingInst) {
		return RETCODE_FRAME_NOT_COMPLETE;
	}

	VpuWriteReg(RET_VER_NUM, 0);

	VpuWriteReg(BIT_BUSY_FLAG, 0x1);
	BitIssueCommand(0, 0, FIRMWARE_GET);

	while (VpuReadReg(BIT_BUSY_FLAG)) ;

	ver = VpuReadReg(RET_VER_NUM);

	if (ver == 0)
		return RETCODE_FAILURE;

	pn = (Uint16) (ver >> 16);
	version = (Uint16) ver;

	switch (pn) {
	case PRJ_TRISTAN:
	case PRJ_TRISTAN_REV:
		strcpy(productstr, "i.MX27");
		break;
	case PRJ_PRISM_CX:
	case PRJ_PRISM_EX:
	case PRJ_BODA_CX_4:
	case PRJ_BODA_DX_4V:
		strcpy(productstr, "i.MX32");
		break;
	case PRJ_SHIVA:
		strcpy(productstr, "MXC30031");
		break;
	default:
		printf("Unknown VPU\n");
		ret = RETCODE_FAILURE;
		break;
	}

	if (verinfo != NULL) {
		verinfo->fw_major = (version >> 12) & 0x0f;
		verinfo->fw_minor = (version >> 8) & 0x0f;
		verinfo->fw_release = version & 0xff;

		verinfo->lib_major = (VPU_LIB_VERSION_CODE >> (12)) & 0x0f;
		verinfo->lib_minor = (VPU_LIB_VERSION_CODE >> (8)) & 0x0f;
		verinfo->lib_release = (VPU_LIB_VERSION_CODE) & 0xff;
		printf("Product Info: %s\n", productstr);
	}

	return ret;
}

/*!
 * @brief VPU encoder initialization
 *
 * @param pHandle [Output] Pointer to EncHandle type 
    where a handle will be written by which you can refer 
    to an encoder instance. If no instance is available, 
    null handle is returned via pHandle.
 *
 * @param pop  [Input] Pointer to EncOpenParam type 
 * which describes parameters necessary for encoding.
 *
 * @return 
 * @li RETCODE_SUCCESS: Success in acquisition of an encoder instance.
 * @li RETCODE_FAILURE: Failed in acquisition of an encoder instance. 
 * @li RETCODE_INVALID_PARAM: pop is a null pointer, or some parameter 
 * passed does not satisfy conditions described in the paragraph for 
 * EncOpenParam type.
 */
RetCode vpu_EncOpen(EncHandle * pHandle, EncOpenParam * pop)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	int instIdx;
	RetCode ret;
	Uint32 val;

	if (!isVpuInitialized()) {
		return RETCODE_NOT_INITIALIZED;
	}

	ret = CheckEncOpenParam(pop);
	if (ret != RETCODE_SUCCESS) {
		return ret;
	}

	ret = GetCodecInstance(&pCodecInst);
	if (ret == RETCODE_FAILURE) {
		*pHandle = 0;
		return RETCODE_FAILURE;
	}

	*pHandle = pCodecInst;
	instIdx = pCodecInst->instIndex;
	pEncInfo = &pCodecInst->CodecInfo.encInfo;

	pEncInfo->openParam = *pop;

	pCodecInst->codecMode = pop->bitstreamFormat == STD_AVC ? AVC_ENC : MP4_ENC;

	pEncInfo->streamRdPtr = pop->bitstreamBuffer;
	pEncInfo->streamRdPtrRegAddr = rdPtrRegAddr[instIdx];
	pEncInfo->streamWrPtrRegAddr = wrPtrRegAddr[instIdx];
	pEncInfo->streamBufStartAddr = pop->bitstreamBuffer;
	pEncInfo->streamBufSize = pop->bitstreamBufferSize;
	pEncInfo->streamBufEndAddr = pop->bitstreamBuffer + pop->bitstreamBufferSize;
	pEncInfo->frameBufPool = 0;

	pEncInfo->rotationEnable = 0;
	pEncInfo->mirrorEnable = 0;
	pEncInfo->mirrorDirection = MIRDIR_NONE;
	pEncInfo->rotationAngle = 0;

	pEncInfo->initialInfoObtained = 0;
	pEncInfo->dynamicAllocEnable = pop->dynamicAllocEnable;
	pEncInfo->ringBufferEnable = pop->ringBufferEnable;
	VpuWriteReg(pEncInfo->streamRdPtrRegAddr, pEncInfo->streamRdPtr);
	VpuWriteReg(pEncInfo->streamWrPtrRegAddr, pEncInfo->streamBufStartAddr);

	val = VpuReadReg(BIT_BIT_STREAM_CTRL);
	val &= 0xFFE7;
	if (pEncInfo->ringBufferEnable == 0) {
		val |= (pEncInfo->dynamicAllocEnable << 4);
		val |= 1 << 3;
	}

	VpuWriteReg(BIT_BIT_STREAM_CTRL, val);

	return RETCODE_SUCCESS;
}

/*!
 * @brief Encoder system close.
 *
 * @param encHandle [Input] The handle obtained from vpu_EncOpen().
 *
 * @return 
 * @li RETCODE_SUCCESS Successful closing.
 * @li RETCODE_INVALID_HANDLE encHandle is invalid. 
 * @li RETCODE_FRAME_NOT_COMPLETE A frame has not been finished.
 */
RetCode vpu_EncClose(EncHandle handle)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	RetCode ret;

	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (pendingInst) {
		return RETCODE_FRAME_NOT_COMPLETE;
	}

	pCodecInst = handle;
	pEncInfo = &pCodecInst->CodecInfo.encInfo;
	if (pEncInfo->initialInfoObtained) {
		VpuWriteReg(BIT_BUSY_FLAG, 0x1);
		BitIssueCommand(pCodecInst->instIndex, pCodecInst->codecMode,
				SEQ_END);
		while (VpuReadReg(BIT_BUSY_FLAG)) ;
	}

	FreeCodecInstance(pCodecInst);
	return RETCODE_SUCCESS;
}

/*!
 * @brief user could allocate frame buffers 
 * according to the information obtained from this function.
 * @param handle [Input] The handle obtained from vpu_EncOpen().
 * @param info [Output] The information required before starting 
 * encoding will be put to the data structure pointed to by initialInfo.
 *
 * @return 
 * @li RETCODE_SUCCESS Successful operation.
 * @li RETCODE_FAILURE There was an error in getting information and 
 *                                    configuring the encoder.
 * @li RETCODE_INVALID_HANDLE encHandle is invalid. 
 * @li RETCODE_FRAME_NOT_COMPLETE A frame has not been finished 
 * @li RETCODE_INVALID_PARAM initialInfo is a null pointer.
 */
RetCode vpu_EncGetInitialInfo(EncHandle handle, EncInitialInfo * info)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	EncOpenParam encOP;
	int picWidth;
	int picHeight;
	Uint32 data;
	RetCode ret;

	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (pendingInst) {
		return RETCODE_FRAME_NOT_COMPLETE;
	}

	if (info == 0) {
		return RETCODE_INVALID_PARAM;
	}

	pCodecInst = handle;
	pEncInfo = &pCodecInst->CodecInfo.encInfo;
	encOP = pEncInfo->openParam;

	if (pEncInfo->initialInfoObtained) {
		return RETCODE_CALLED_BEFORE;
	}

	picWidth = encOP.picWidth;
	picHeight = encOP.picHeight;

	data = (picWidth << 10) | picHeight;
	VpuWriteReg(CMD_ENC_SEQ_SRC_SIZE, data);
	VpuWriteReg(CMD_ENC_SEQ_SRC_F_RATE, encOP.frameRateInfo);

	if (encOP.bitstreamFormat == STD_MPEG4) {
		VpuWriteReg(CMD_ENC_SEQ_COD_STD, 0);
		data = encOP.EncStdParam.mp4Param.mp4_intraDcVlcThr << 2 |
		    encOP.EncStdParam.mp4Param.mp4_reversibleVlcEnable << 1 |
		    encOP.EncStdParam.mp4Param.mp4_dataPartitionEnable;
		data |= ((encOP.EncStdParam.mp4Param.mp4_hecEnable > 0) 
				? 1 : 0) << 5;
		data |= ((encOP.EncStdParam.mp4Param.mp4_verid == 2) 
				? 0 : 1) << 6;
		VpuWriteReg(CMD_ENC_SEQ_MP4_PARA, data);
	} else if (encOP.bitstreamFormat == STD_H263) {
		VpuWriteReg(CMD_ENC_SEQ_COD_STD, 1);
		data = encOP.EncStdParam.h263Param.h263_annexJEnable << 2 |
		    encOP.EncStdParam.h263Param.h263_annexKEnable << 1 |
		    encOP.EncStdParam.h263Param.h263_annexTEnable;
		VpuWriteReg(CMD_ENC_SEQ_263_PARA, data);
	} else if (encOP.bitstreamFormat == STD_AVC) {
		VpuWriteReg(CMD_ENC_SEQ_COD_STD, 2);
		data = (encOP.EncStdParam.avcParam.avc_deblkFilterOffsetBeta &
			15) << 12 |
		    (encOP.EncStdParam.avcParam.avc_deblkFilterOffsetAlpha
		     & 15) << 8 |
		    encOP.EncStdParam.avcParam.avc_disableDeblk << 6 |
		    encOP.EncStdParam.avcParam.avc_constrainedIntraPredFlag
		    << 5 | (encOP.EncStdParam.avcParam.avc_chromaQpOffset & 31);
		VpuWriteReg(CMD_ENC_SEQ_264_PARA, data);
	}

	data = encOP.slicemode.sliceSize << 2 |
		encOP.slicemode.sliceSizeMode << 1 | encOP.slicemode.sliceMode;
	
	VpuWriteReg(CMD_ENC_SEQ_SLICE_MODE, data);
	VpuWriteReg(CMD_ENC_SEQ_GOP_NUM, encOP.gopSize);

	if (encOP.bitRate) {	/* rate control enabled */
		data = (!encOP.enableAutoSkip) << 31 |
		    encOP.initialDelay << 16 | encOP.bitRate << 1 | 1;
		VpuWriteReg(CMD_ENC_SEQ_RC_PARA, data);
	} else {
		VpuWriteReg(CMD_ENC_SEQ_RC_PARA, 0);
	}

	VpuWriteReg(CMD_ENC_SEQ_RC_BUF_SIZE, encOP.vbvBufferSize);
	VpuWriteReg(CMD_ENC_SEQ_INTRA_REFRESH, encOP.intraRefresh);

	VpuWriteReg(CMD_ENC_SEQ_BB_START, pEncInfo->streamBufStartAddr);
	VpuWriteReg(CMD_ENC_SEQ_BB_SIZE, pEncInfo->streamBufSize / 1024);

	data = (encOP.sliceReport << 1) | encOP.mbReport;
	data |= (encOP.mbQpReport << 3);
	
	if (encOP.rcIntraQp >= 0)
		data |= (1 << 5);
	
	VpuWriteReg(CMD_ENC_SEQ_INTRA_QP, encOP.rcIntraQp);
	
	if (pCodecInst->codecMode == AVC_ENC) {
		data |= (encOP.EncStdParam.avcParam.avc_audEnable << 2);
		data |= (encOP.EncStdParam.avcParam.avc_fmoEnable << 4);
	}

	VpuWriteReg(CMD_ENC_SEQ_OPTION, data);

	if (pCodecInst->codecMode == AVC_ENC) {
		data = (encOP.EncStdParam.avcParam.avc_fmoType << 4) |
		    (encOP.EncStdParam.avcParam.avc_fmoSliceNum & 0x0f);
		data |= (FMO_SLICE_SAVE_BUF_SIZE << 7);
		
	}
	VpuWriteReg(CMD_ENC_SEQ_FMO, data); /* FIXME */

	if (platform_is_mxc30031()) {
		VpuWriteReg(BIT_FRAME_MEM_CTRL,
			    ((encOP.chromaInterleave << 1) | IMAGE_ENDIAN));
	}

	VpuWriteReg(BIT_BUSY_FLAG, 0x1);
	BitIssueCommand(pCodecInst->instIndex, pCodecInst->codecMode, SEQ_INIT);
	while (VpuReadReg(BIT_BUSY_FLAG)) ;

	if (VpuReadReg(RET_ENC_SEQ_SUCCESS) == 0) {
		return RETCODE_FAILURE;
	}

	/* reconstructed frame + reference frame */
	info->minFrameBufferCount = 2;

	pEncInfo->initialInfo = *info;
	pEncInfo->initialInfoObtained = 1;

	return RETCODE_SUCCESS;
}

/*!
 * @brief Registers frame buffers 
 * @param handle [Input] The handle obtained from vpu_EncOpen().
 * @param bufArray [Input] Pointer to the first element of an array
 *			of FrameBuffer data structures.
 * @param num [Input] Number of elements of the array.
 * @param stride [Input] Stride value of frame buffers being registered.
 *
 * @return 
 * @li RETCODE_SUCCESS Successful operation.
 * @li RETCODE_INVALID_HANDLE encHandle is invalid. 
 * @li RETCODE_FRAME_NOT_COMPLETE A frame has not been finished 
 * @li RETCODE_WRONG_CALL_SEQUENCE Function call in wrong sequence.
 * @li RETCODE_INVALID_FRAME_BUFFER pBuffer is a null pointer.
 * @li RETCODE_INSUFFICIENT_FRAME_BUFFERS num is smaller than requested.
 * @li RETCODE_INVALID_STRIDE stride is smaller than the picture width.
 */
RetCode vpu_EncRegisterFrameBuffer(EncHandle handle,
				   FrameBuffer * bufArray, int num, int stride)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	int i;
	RetCode ret;

	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (pendingInst) {
		return RETCODE_FRAME_NOT_COMPLETE;
	}

	pCodecInst = handle;
	pEncInfo = &pCodecInst->CodecInfo.encInfo;

	if (pEncInfo->frameBufPool) {
		return RETCODE_CALLED_BEFORE;
	}

	if (!pEncInfo->initialInfoObtained) {
		return RETCODE_WRONG_CALL_SEQUENCE;
	}

	if (bufArray == 0) {
		return RETCODE_INVALID_FRAME_BUFFER;
	}

	if (num < pEncInfo->initialInfo.minFrameBufferCount) {
		return RETCODE_INSUFFICIENT_FRAME_BUFFERS;
	}

	if (stride % 8 != 0 || stride == 0) {
		return RETCODE_INVALID_STRIDE;
	}

	pEncInfo->frameBufPool = bufArray;
	pEncInfo->numFrameBuffers = num;
	pEncInfo->stride = stride;

	/* Let the codec know the addresses of the frame buffers. */
	for (i = 0; i < num; ++i) {
		virt_paraBuf[i * 3] = bufArray[i].bufY;
		virt_paraBuf[i * 3 + 1] = bufArray[i].bufCb;
		virt_paraBuf[i * 3 + 2] = bufArray[i].bufCr;
	}

	/* Tell the codec how much frame buffers were allocated. */
	VpuWriteReg(CMD_SET_FRAME_BUF_NUM, num);
	VpuWriteReg(CMD_SET_FRAME_BUF_STRIDE, stride);

	VpuWriteReg(BIT_BUSY_FLAG, 0x1);
	BitIssueCommand(pCodecInst->instIndex, pCodecInst->codecMode,
			SET_FRAME_BUF);

	while (VpuReadReg(BIT_BUSY_FLAG)) ;
	return RETCODE_SUCCESS;
}

RetCode vpu_EncGetBitstreamBuffer(EncHandle handle,
				  PhysicalAddress * prdPrt,
				  PhysicalAddress * pwrPtr, Uint32 * size)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	PhysicalAddress rdPtr;
	PhysicalAddress wrPtr;
	Uint32 room;
	RetCode ret;

	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (prdPrt == 0 || pwrPtr == 0 || size == 0) {
		return RETCODE_INVALID_PARAM;
	}

	pCodecInst = handle;
	pEncInfo = &pCodecInst->CodecInfo.encInfo;
	rdPtr = pEncInfo->streamRdPtr;
	wrPtr = VpuReadReg(pEncInfo->streamWrPtrRegAddr);

	if (pEncInfo->ringBufferEnable == 1) {
		if (wrPtr >= rdPtr) {
			room = wrPtr - rdPtr;
		} else {
			room = (pEncInfo->streamBufEndAddr - rdPtr) +
			    (wrPtr - pEncInfo->streamBufStartAddr);
		}
	} else {
		if (rdPtr == pEncInfo->streamBufStartAddr && wrPtr >= rdPtr)
			room = wrPtr - rdPtr;
		else
			return RETCODE_INVALID_PARAM;
	}

	*prdPrt = rdPtr;
	*pwrPtr = wrPtr;
	*size = room;

	return RETCODE_SUCCESS;
}

RetCode vpu_EncUpdateBitstreamBuffer(EncHandle handle, Uint32 size)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	PhysicalAddress wrPtr;
	PhysicalAddress rdPtr;
	RetCode ret;
	int room = 0;

	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	pCodecInst = handle;
	pEncInfo = &pCodecInst->CodecInfo.encInfo;
	rdPtr = pEncInfo->streamRdPtr;

	wrPtr = VpuReadReg(pEncInfo->streamWrPtrRegAddr);
	if (rdPtr < wrPtr) {
		if (rdPtr + size > wrPtr)
			return RETCODE_INVALID_PARAM;
	}

	if (pEncInfo->ringBufferEnable == 1) {
		rdPtr += size;
		if (rdPtr > pEncInfo->streamBufEndAddr) {
			room = rdPtr - pEncInfo->streamBufEndAddr;
			rdPtr = pEncInfo->streamBufStartAddr;
			rdPtr += room;
		}
		if (rdPtr == pEncInfo->streamBufEndAddr) {
			rdPtr = pEncInfo->streamBufStartAddr;
		}
	} else {
		rdPtr = pEncInfo->streamBufStartAddr;
	}
	
	pEncInfo->streamRdPtr = rdPtr;

	VpuWriteReg(pEncInfo->streamRdPtrRegAddr, rdPtr);
	return RETCODE_SUCCESS;
}

/*!
 * @brief Starts encoding one frame. 
 *
 * @param handle [Input] The handle obtained from vpu_EncOpen().
 * @param pParam [Input] Pointer to EncParam data structure.
 *
 * @return 
 * @li RETCODE_SUCCESS Successful operation.
 * @li RETCODE_INVALID_HANDLE encHandle is invalid. 
 * @li RETCODE_FRAME_NOT_COMPLETE A frame has not been finished.
 * @li RETCODE_WRONG_CALL_SEQUENCE Wrong calling sequence.
 * @li RETCODE_INVALID_PARAM pParam is invalid. 
 * @li RETCODE_INVALID_FRAME_BUFFER skipPicture in EncParam is 0 
 * and sourceFrame in EncParam is a null pointer.
 */
RetCode vpu_EncStartOneFrame(EncHandle handle, EncParam * param)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	FrameBuffer *pSrcFrame;
	Uint32 rotMirEnable;
	Uint32 rotMirMode;
	RetCode ret;

	/* When doing pre-rotation, mirroring is applied first and rotation 
	 * later, vice versa when doing post-rotation.
	 * For consistency, pre-rotation is converted to post-rotation 
	 * orientation.
	 */
	static Uint32 rotatorModeConversion[] = {
		0, 1, 2, 3, 4, 7, 6, 5,
		6, 5, 4, 7, 2, 3, 0, 1
	};

	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (pendingInst) {
		return RETCODE_FRAME_NOT_COMPLETE;
	}

	pCodecInst = handle;
	pEncInfo = &pCodecInst->CodecInfo.encInfo;

	/* This means frame buffers have not been registered. */
	if (pEncInfo->frameBufPool == 0) {
		return RETCODE_WRONG_CALL_SEQUENCE;
	}

	ret = CheckEncParam(pCodecInst, param);
	if (ret != RETCODE_SUCCESS) {
		return ret;
	}

	pSrcFrame = param->sourceFrame;
	rotMirEnable = 0;
	rotMirMode = 0;

	if (!platform_is_mxc30031()) {
		if (pEncInfo->rotationEnable) {
			rotMirEnable = 0x10;	/* Enable rotator */
			switch (pEncInfo->rotationAngle) {
			case 0:
				rotMirMode |= 0x0;
				break;

			case 90:
				rotMirMode |= 0x1;
				break;

			case 180:
				rotMirMode |= 0x2;
				break;

			case 270:
				rotMirMode |= 0x3;
				break;
			}
		}
		if (pEncInfo->mirrorEnable) {
			rotMirEnable = 0x10;	/* Enable mirror */
			switch (pEncInfo->mirrorDirection) {
			case MIRDIR_NONE:
				rotMirMode |= 0x0;
				break;

			case MIRDIR_VER:
				rotMirMode |= 0x4;
				break;

			case MIRDIR_HOR:
				rotMirMode |= 0x8;
				break;

			case MIRDIR_HOR_VER:
				rotMirMode |= 0xc;
				break;

			}
		}
		rotMirMode = rotatorModeConversion[rotMirMode];
		rotMirMode |= rotMirEnable;
		VpuWriteReg(CMD_ENC_PIC_ROT_MODE, rotMirMode);
	}

	VpuWriteReg(CMD_ENC_PIC_QS, param->quantParam);

	if (param->skipPicture) {
		VpuWriteReg(CMD_ENC_PIC_OPTION, 1);
	} else {
		VpuWriteReg(CMD_ENC_PIC_SRC_ADDR_Y, pSrcFrame->bufY);
		VpuWriteReg(CMD_ENC_PIC_SRC_ADDR_CB, pSrcFrame->bufCb);
		VpuWriteReg(CMD_ENC_PIC_SRC_ADDR_CR, pSrcFrame->bufCr);

		VpuWriteReg(CMD_ENC_PIC_OPTION,
			    param->forceIPicture << 1 & 0x2);
	}

	if (platform_is_mxc30031()) {
		pEncInfo->vpuCountEnable = param->vpuCountEnable;

		if (param->vpuCountEnable == 1) {
			VpuWriteReg(BIT_VPU_PIC_COUNT, VPU_PIC_COUNT_ENABLE);
		} else {
			VpuWriteReg(BIT_VPU_PIC_COUNT, VPU_PIC_COUNT_DISABLE);
		}
	}
	
	if (pEncInfo->dynamicAllocEnable == 1) {
		VpuWriteReg(CMD_ENC_PIC_BB_START, param->picStreamBufferAddr);
		VpuWriteReg(CMD_ENC_PIC_BB_SIZE, 
				param->picStreamBufferSize / 1024);
	}

	VpuWriteReg(BIT_BUSY_FLAG, 0x1);
	BitIssueCommand(pCodecInst->instIndex, pCodecInst->codecMode, PIC_RUN);

	pendingInst = pCodecInst;

	return RETCODE_SUCCESS;
}

/*!
 * @brief Get information of the output of encoding. 
 *
 * @param encHandle [Input] The handle obtained from vpu_EncOpen().
 * @param info [Output] Pointer to EncOutputInfo data structure.
 *
 * @return 
 * @li RETCODE_SUCCESS Successful operation.
 * @li RETCODE_INVALID_HANDLE encHandle is invalid. 
 * @li RETCODE_WRONG_CALL_SEQUENCE Wrong calling sequence.
 * @li RETCODE_INVALID_PARAM info is a null pointer.
 */
RetCode vpu_EncGetOutputInfo(EncHandle handle, EncOutputInfo * info)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	RetCode ret;
	PhysicalAddress rdPtr;
	PhysicalAddress wrPtr;

	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (info == 0) {
		return RETCODE_INVALID_PARAM;
	}

	pCodecInst = handle;
	pEncInfo = &pCodecInst->CodecInfo.encInfo;

	if (pendingInst == 0) {
		return RETCODE_WRONG_CALL_SEQUENCE;
	}

	if (pCodecInst != pendingInst) {
		return RETCODE_INVALID_HANDLE;
	}

	info->picType = VpuReadReg(RET_ENC_PIC_TYPE);

	if (pEncInfo->ringBufferEnable == 0) {
		if (pEncInfo->dynamicAllocEnable == 1) {
			rdPtr = VpuReadReg(CMD_ENC_PIC_BB_START);
		} else {
			rdPtr = pEncInfo->streamBufStartAddr;
		}

		wrPtr = VpuReadReg(pEncInfo->streamWrPtrRegAddr);
		info->bitstreamBuffer = rdPtr;
		info->bitstreamSize = wrPtr - rdPtr;
	}

	info->numOfSlices = VpuReadReg(RET_ENC_PIC_SLICE_NUM);
	info->sliceInfo = virt_paraBuf + 0x1200;
	info->mbInfo = virt_paraBuf;
	info->bitstreamWrapAround = VpuReadReg(RET_ENC_PIC_FLAG);

	if (pCodecInst->codecMode == MP4_ENC &&
		    pEncInfo->openParam.mbQpReport == 1) {
		int widthInMB;
		int heightInMB;
		PhysicalAddress readPnt;
		PhysicalAddress writePnt;
		PhysicalAddress mbQpPnt;
		int i;
		int j;
		Uint32 val, val1, val2;

		mbQpPnt = (Uint32) virt_paraBuf + 0x1300;
		widthInMB = pEncInfo->openParam.picWidth / 16;
		heightInMB = pEncInfo->openParam.picHeight / 16;
		writePnt = (Uint32) virt_paraBuf - PARA_BUF2_SIZE;
		for (i = 0; i < heightInMB; ++i) {
			readPnt = mbQpPnt + i * 128;
			for (j = 0; j < widthInMB; j += 4) {
				val1 = VpuReadReg(readPnt);
				readPnt += 4;
				val2 = VpuReadReg(readPnt);
				readPnt += 4;
				val = (val1 << 8 & 0xff000000) | (val1 << 16) |
				    (val2 >> 8) | (val2 & 0x000000ff);
				VpuWriteReg(writePnt, val);
				writePnt += 4;
			}
		}
		info->mbQpInfo = virt_paraBuf - PARA_BUF2_SIZE;
	}

	if (platform_is_mxc30031()) {
		if (pEncInfo->vpuCountEnable == 1) {
			info->EncVpuCount = VpuReadReg(BIT_VPU_PIC_COUNT) &
			    0x7FFFFFFF;
		}
	}

	pendingInst = 0;

	return RETCODE_SUCCESS;
}

/*!
 * @brief This function gives a command to the encoder. 
 *
 * @param encHandle [Input] The handle obtained from vpu_EncOpen().
 * @param cmd [Intput] user command.
 * @param param [Intput/Output] param  for cmd. 
 *
 * @return 
 * @li RETCODE_INVALID_COMMAND cmd is not one of 8 values above.
 * @li RETCODE_INVALID_HANDLE encHandle is invalid. 
 * @li RETCODE_FRAME_NOT_COMPLETE A frame has not been finished
 */
RetCode vpu_EncGiveCommand(EncHandle handle, CodecCommand cmd, void *param)
{
	CodecInst *pCodecInst;
	EncInfo *pEncInfo;
	RetCode ret;

	ret = CheckEncInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS) {
		return ret;
	}

	if (pendingInst) {
		return RETCODE_FRAME_NOT_COMPLETE;
	}

	if (platform_is_mxc30031() && ((cmd == ENABLE_ROTATION) ||
				       (cmd == DISABLE_ROTATION)
				       || (cmd == ENABLE_MIRRORING)
				       || (cmd == DISABLE_MIRRORING)
				       || (cmd == SET_MIRROR_DIRECTION)
				       || (cmd == SET_ROTATION_ANGLE))) {
		return RETCODE_NOT_SUPPORTED;
	}

	pCodecInst = handle;
	pEncInfo = &pCodecInst->CodecInfo.encInfo;

	switch (cmd) {
	case ENABLE_ROTATION:
		{
			pEncInfo->rotationEnable = 1;
			break;
		}

	case DISABLE_ROTATION:
		{
			pEncInfo->rotationEnable = 0;
			break;
		}

	case ENABLE_MIRRORING:
		{
			pEncInfo->mirrorEnable = 1;
			break;
		}

	case DISABLE_MIRRORING:
		{
			pEncInfo->mirrorEnable = 0;
			break;
		}

	case SET_MIRROR_DIRECTION:
		{
			MirrorDirection mirDir;

			if (param == 0) {
				return RETCODE_INVALID_PARAM;
			}

			mirDir = *(MirrorDirection *) param;
			if (mirDir < MIRDIR_NONE || mirDir > MIRDIR_HOR_VER) {
				return RETCODE_INVALID_PARAM;
			}

			pEncInfo->mirrorDirection = mirDir;
			break;
		}

	case SET_ROTATION_ANGLE:
		{
			int angle;

			if (param == 0) {
				return RETCODE_INVALID_PARAM;
			}

			angle = *(int *)param;
			if (angle != 0 && angle != 90 &&
			    angle != 180 && angle != 270) {
				return RETCODE_INVALID_PARAM;
			}

			if (pEncInfo->initialInfoObtained &&
			    (angle == 90 || angle == 270)) {
				return RETCODE_INVALID_PARAM;
			}

			pEncInfo->rotationAngle = angle;
			break;
		}

	case ENC_GET_SPS_RBSP:
		{
			if (pCodecInst->codecMode != AVC_ENC) {
				return RETCODE_INVALID_COMMAND;
			}

			if (param == 0) {
				return RETCODE_INVALID_PARAM;
			}

			GetParaSet(handle, 0, param);
			break;
		}

	case ENC_GET_PPS_RBSP:
		{
			if (pCodecInst->codecMode != AVC_ENC) {
				return RETCODE_INVALID_COMMAND;
			}

			if (param == 0) {
				return RETCODE_INVALID_PARAM;
			}

			GetParaSet(handle, 1, param);
			break;
		}

	case ENC_PUT_MP4_HEADER:
		{
			EncHeaderParam *encHeaderParam;

			if (pCodecInst->codecMode != MP4_ENC) {
				return RETCODE_INVALID_COMMAND;
			}

			if (param == 0) {
				return RETCODE_INVALID_PARAM;
			}

			encHeaderParam = (EncHeaderParam *) param;
			if (!(VOL_HEADER <= encHeaderParam->headerType &&
			      encHeaderParam->headerType <= VIS_HEADER)) {
				return RETCODE_INVALID_PARAM;
			}

			EncodeHeader(handle, encHeaderParam);
			break;
		}

	case ENC_PUT_AVC_HEADER:
		{
			EncHeaderParam *encHeaderParam;

			if (pCodecInst->codecMode != AVC_ENC) {
				return RETCODE_INVALID_COMMAND;
			}

			if (param == 0) {
				return RETCODE_INVALID_PARAM;
			}

			encHeaderParam = (EncHeaderParam *) param;
			if (!(SPS_RBSP <= encHeaderParam->headerType &&
			      encHeaderParam->headerType <= PPS_RBSP)) {
				return RETCODE_INVALID_PARAM;
			}

			EncodeHeader(handle, encHeaderParam);
			break;
		}

	case ENC_SET_SEARCHRAM_PARAM:
		{
			SearchRamParam *scRamParam = NULL;
			int EncPicX;
			if (param == 0) {
				return RETCODE_INVALID_PARAM;
			}

			EncPicX =
			    pCodecInst->CodecInfo.encInfo.openParam.picWidth;

			scRamParam = (SearchRamParam *) param;

			if (platform_is_mxc30031()) {
				if (scRamParam->SearchRamSize !=
				    ((((EncPicX + 15) & ~15 ) * 36) + 2048)) {
					return RETCODE_INVALID_PARAM;
				}

				VpuWriteReg(BIT_SEARCH_RAM_SIZE,
					    scRamParam->SearchRamSize);
			}

			VpuWriteReg(BIT_SEARCH_RAM_BASE_ADDR,
				    scRamParam->searchRamAddr);
			break;
		}

	case ENC_GET_VOS_HEADER:
		{
			if (pCodecInst->codecMode != MP4_ENC) {
				return RETCODE_INVALID_COMMAND;
			}
			if (param == 0) {
				return RETCODE_INVALID_PARAM;
			}
			GetParaSet(handle, 1, param);
			break;
		}

	case ENC_GET_VO_HEADER:
		{
			if (pCodecInst->codecMode != MP4_ENC) {
				return RETCODE_INVALID_COMMAND;
			}
			if (param == 0) {
				return RETCODE_INVALID_PARAM;
			}
			GetParaSet(handle, 2, param);
			break;
		}

	case ENC_GET_VOL_HEADER:
		{
			if (pCodecInst->codecMode != MP4_ENC) {
				return RETCODE_INVALID_COMMAND;
			}
			if (param == 0) {
				return RETCODE_INVALID_PARAM;
			}
			GetParaSet(handle, 0, param);
			break;
		}

	case ENC_SET_GOP_NUMBER:
		{
			int *pGopNumber =(int *)param;
			if (pCodecInst->codecMode != MP4_ENC && 
				pCodecInst->codecMode != AVC_ENC ) {
				return RETCODE_INVALID_COMMAND;
			}

			if (*pGopNumber < 0 ||  *pGopNumber > 60) {
				return RETCODE_INVALID_PARAM;
			}
			
			SetGopNumber(handle, (Uint32 *)pGopNumber); 
			break;
		}
		
	case ENC_SET_INTRA_QP:
		{
			int *pIntraQp =(int *)param;
			if (pCodecInst->codecMode != MP4_ENC &&
					pCodecInst->codecMode != AVC_ENC ) {
				return RETCODE_INVALID_COMMAND;
			}

			if (pCodecInst->codecMode == MP4_ENC) {
				if(*pIntraQp < 1 || *pIntraQp > 31)
					return RETCODE_INVALID_PARAM;
			}

			if (pCodecInst->codecMode == AVC_ENC) {
				if(*pIntraQp < 0 || *pIntraQp > 51)
					return RETCODE_INVALID_PARAM;
			}
			SetIntraQp(handle, (Uint32 *)pIntraQp);
			break;
		}

	case ENC_SET_BITRATE:
		{
			int *pBitrate = (int *)param;
			if (pCodecInst->codecMode != MP4_ENC && 
					pCodecInst->codecMode != AVC_ENC ) {
				return RETCODE_INVALID_COMMAND;
			}
			
			if (*pBitrate < 0 || *pBitrate> 32767) {
				return RETCODE_INVALID_PARAM;
			}
			
			SetBitrate(handle, (Uint32 *)pBitrate);
			break;
		}

	case ENC_SET_FRAME_RATE:
		{
			int *pFramerate = (int *)param;
			if (pCodecInst->codecMode != MP4_ENC && 
					pCodecInst->codecMode != AVC_ENC ) {
				return RETCODE_INVALID_COMMAND;
			}

			if (*pFramerate <= 0) {
				return RETCODE_INVALID_PARAM;
			}
			SetFramerate(handle, (Uint32 *)pFramerate);
			break;
		}

	case ENC_SET_INTRA_MB_REFRESH_NUMBER:
		{
			int *pIntraRefreshNum =(int *)param;
			SetIntraRefreshNum(handle, (Uint32 *)pIntraRefreshNum);
			break;
		}

	case ENC_SET_SLICE_INFO:
		{
			EncSliceMode *pSliceMode = (EncSliceMode *)param;
			if (pSliceMode->sliceMode < 0 || 
					pSliceMode->sliceMode > 1) {
				return RETCODE_INVALID_PARAM;
			}

			if (pSliceMode->sliceSizeMode < 0 
					|| pSliceMode->sliceSizeMode > 1) {
				return RETCODE_INVALID_PARAM;
			}

			SetSliceMode(handle, (EncSliceMode *)pSliceMode);
			break;
		}

	case ENC_ENABLE_HEC:
		{
			if (pCodecInst->codecMode != MP4_ENC) {
				return RETCODE_INVALID_COMMAND;
			}

			SetHecMode(handle, 1);
			break;
		}
		
	case ENC_DISABLE_HEC:
		{
			if (pCodecInst->codecMode != MP4_ENC) {
				return RETCODE_INVALID_COMMAND;
			}
			
			SetHecMode(handle, 0);
			break;
		}

	default:
		printf("Invalid encoder command\n");
		return RETCODE_INVALID_COMMAND;
	}

	return RETCODE_SUCCESS;
}

/*!
 * @brief Decoder initialization
 *
 * @param pHandle [Output] Pointer to DecHandle type
 * @param pop [Input] Pointer to DecOpenParam type.
 *
 * @return 
 * @li RETCODE_SUCCESS Success in acquisition of a decoder instance.
 * @li RETCODE_FAILURE Failed in acquisition of a decoder instance. 
 * @li RETCODE_INVALID_PARAM pop is a null pointer or invalid.
 */
RetCode vpu_DecOpen(DecHandle * pHandle, DecOpenParam * pop)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	int instIdx;
	RetCode ret;

	if (!isVpuInitialized()) {
		return RETCODE_NOT_INITIALIZED;
	}

	ret = CheckDecOpenParam(pop);
	if (ret != RETCODE_SUCCESS) {
		return ret;
	}

	ret = GetCodecInstance(&pCodecInst);
	if (ret == RETCODE_FAILURE) {
		*pHandle = 0;
		return RETCODE_FAILURE;
	}

	*pHandle = pCodecInst;
	instIdx = pCodecInst->instIndex;
	pDecInfo = &pCodecInst->CodecInfo.decInfo;

	pDecInfo->openParam = *pop;

	if (platform_is_mx27()) {
		pCodecInst->codecMode =
		    pop->bitstreamFormat == STD_AVC ? AVC_DEC : MP4_DEC;
	} else {
		if (pop->bitstreamFormat == STD_MPEG4) {
			pCodecInst->codecMode = MP4_DEC;
		} else if (pop->bitstreamFormat == STD_AVC) {
			pCodecInst->codecMode = AVC_DEC;
		} else if (pop->bitstreamFormat == STD_VC1) {
			pCodecInst->codecMode = VC1_DEC;
		}
	}

	pDecInfo->streamWrPtr = pop->bitstreamBuffer;
	pDecInfo->streamRdPtrRegAddr = rdPtrRegAddr[instIdx];
	pDecInfo->streamWrPtrRegAddr = wrPtrRegAddr[instIdx];
	pDecInfo->streamBufStartAddr = pop->bitstreamBuffer;
	pDecInfo->streamBufSize = pop->bitstreamBufferSize;
	pDecInfo->streamBufEndAddr =
	    pop->bitstreamBuffer + pop->bitstreamBufferSize;
	pDecInfo->frameBufPool = 0;

	pDecInfo->rotationEnable = 0;
	pDecInfo->mirrorEnable = 0;
	pDecInfo->mirrorDirection = MIRDIR_NONE;
	pDecInfo->rotationAngle = 0;
	pDecInfo->rotatorOutputValid = 0;
	pDecInfo->rotatorStride = 0;

	pDecInfo->filePlayEnable = pop->filePlayEnable;
	if (pop->filePlayEnable == 1) {
		pDecInfo->picSrcSize = (pop->picWidth << 10) | pop->picHeight;
		pDecInfo->dynamicAllocEnable = pop->dynamicAllocEnable;
	}

	pDecInfo->initialInfoObtained = 0;

	VpuWriteReg(pDecInfo->streamRdPtrRegAddr, pDecInfo->streamBufStartAddr);
	VpuWriteReg(pDecInfo->streamWrPtrRegAddr, pDecInfo->streamWrPtr);

	return RETCODE_SUCCESS;
}

/*!
 * @brief Decoder close function
 *
 * @param  handle [Input] The handle obtained from vpu_DecOpen().
 *
 * @return 
 * @li RETCODE_SUCCESS Successful closing.
 * @li RETCODE_INVALID_HANDLE decHandle is invalid. 
 * @li RETCODE_FRAME_NOT_COMPLETE A frame has not been finished.
 */
RetCode vpu_DecClose(DecHandle handle)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	RetCode ret;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (pendingInst) {
		return RETCODE_FRAME_NOT_COMPLETE;
	}

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo.decInfo;

	if (pDecInfo->initialInfoObtained) {
		VpuWriteReg(BIT_BUSY_FLAG, 0x1);
		BitIssueCommand(pCodecInst->instIndex, pCodecInst->codecMode,
				SEQ_END);
		while (VpuReadReg(BIT_BUSY_FLAG)) ;
	}
	FreeCodecInstance(pCodecInst);
	return RETCODE_SUCCESS;
}

RetCode vpu_DecSetEscSeqInit(DecHandle handle, int escape)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	RetCode ret;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo.decInfo;

	VpuWriteReg(CMD_DEC_SEQ_INIT_ESCAPE, (escape & 0x01));

	return RETCODE_SUCCESS;
}

/*!
 * @brief Get header information of bitstream.
 *
 * @param handle [Input] The handle obtained from vpu_DecOpen().
 * @param info [Output] Pointer to DecInitialInfo data structure.
 *
 * @return 
 * @li RETCODE_SUCCESS Successful operation.
 * @li RETCODE_FAILURE There was an error in getting initial information.
 * @li RETCODE_INVALID_HANDLE decHandle is invalid.
 * @li RETCODE_INVALID_PARAM info is an invalid pointer.
 * @li RETCODE_FRAME_NOT_COMPLETE A frame has not been finished.
 * @li RETCODE_WRONG_CALL_SEQUENCE Wrong calling sequence.
 */
RetCode vpu_DecGetInitialInfo(DecHandle handle, DecInitialInfo * info)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	Uint32 val, val2;
	RetCode ret;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS) {
		return ret;
	}

	if (info == 0) {
		return RETCODE_INVALID_PARAM;
	}

	if (pendingInst) {
		return RETCODE_FRAME_NOT_COMPLETE;
	}

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo.decInfo;

	if (pDecInfo->initialInfoObtained) {
		return RETCODE_CALLED_BEFORE;
	}

	if (DecBitstreamBufEmpty(pDecInfo)) {
		return RETCODE_WRONG_CALL_SEQUENCE;
	}

	VpuWriteReg(CMD_DEC_SEQ_BB_START, pDecInfo->streamBufStartAddr);
	VpuWriteReg(CMD_DEC_SEQ_BB_SIZE, pDecInfo->streamBufSize / 1024);

	if (pDecInfo->filePlayEnable == 1) {
		VpuWriteReg(CMD_DEC_SEQ_START_BYTE, 
				pDecInfo->openParam.streamStartByteOffset);
	}

	val = ((pDecInfo->dynamicAllocEnable << 3) & 0x8) | 
		((pDecInfo->filePlayEnable << 2) & 0x4) | 
		((pDecInfo->openParam.reorderEnable << 1) & 0x2);
	
	if (platform_is_mx27()) {
		val |= (pDecInfo->openParam.qpReport & 0x1);
	} else {
		val |= (pDecInfo->openParam.mp4DeblkEnable & 0x1);
	}

	VpuWriteReg(CMD_DEC_SEQ_OPTION, val);

	if (platform_is_mxc30031()) {
		VpuWriteReg(BIT_FRAME_MEM_CTRL,
			    ((pDecInfo->openParam.chromaInterleave << 1) |
			     IMAGE_ENDIAN));
	}

	VpuWriteReg(CMD_DEC_SEQ_SRC_SIZE, pDecInfo->picSrcSize);

	VpuWriteReg(BIT_BUSY_FLAG, 0x1);
	BitIssueCommand(pCodecInst->instIndex, pCodecInst->codecMode, SEQ_INIT);
	while (VpuReadReg(BIT_BUSY_FLAG)) ;

	if (VpuReadReg(RET_DEC_SEQ_SUCCESS) == 0) {
		return RETCODE_FAILURE;
	}

	val = VpuReadReg(RET_DEC_SEQ_SRC_SIZE);
	info->picWidth = ((val >> 10) & 0x3ff);
	info->picHeight = (val & 0x3ff);
	
	val = VpuReadReg(RET_DEC_SEQ_SRC_F_RATE);
	info->frameRateInfo = val;

	if (pCodecInst->codecMode == MP4_DEC) {
		val = VpuReadReg(RET_DEC_SEQ_INFO);
		info->mp4_shortVideoHeader = (val >> 2) & 1;
		info->mp4_dataPartitionEnable = val & 1;
		info->mp4_reversibleVlcEnable =
		    info->mp4_dataPartitionEnable ? ((val >> 1) & 1) : 0;
		info->h263_annexJEnable = (val >> 3) & 1;
	}

	info->minFrameBufferCount = VpuReadReg(RET_DEC_SEQ_FRAME_NEED);
	info->frameBufDelay = VpuReadReg(RET_DEC_SEQ_FRAME_DELAY);

	if (pCodecInst->codecMode == AVC_DEC) {
		val = VpuReadReg(RET_DEC_SEQ_CROP_LEFT_RIGHT);	
		val2 = VpuReadReg(RET_DEC_SEQ_CROP_TOP_BOTTOM);
		if (val == 0 && val2 == 0) {
			info->picCropRect.left = 0;
			info->picCropRect.right = 0;
			info->picCropRect.top = 0;
			info->picCropRect.bottom = 0;
		} else {
			info->picCropRect.left = 
				((val >> 10) & 0x3FF) * 2;
			info->picCropRect.right = 
				info->picWidth - ((val & 0x3FF ) * 2);
			info->picCropRect.top = 
				((val2>>10) & 0x3FF )*2;
			info->picCropRect.bottom = 
				info->picHeight - ((val2 & 0x3FF ) *2);
		}
	}

	pDecInfo->initialInfo = *info;
	pDecInfo->initialInfoObtained = 1;

	return RETCODE_SUCCESS;
}

/*!
 * @brief Register decoder frame buffers.
 *
 * @param handle [Input] The handle obtained from vpu_DecOpen().
 * @param bufArray [Input] Pointer to the first element of an array of FrameBuffer.
 * @param num [Input] Number of elements of the array.
 * @param stride [Input] Stride value of frame buffers being registered.
 *
 * @return 
 * @li RETCODE_SUCCESS Successful operation.
 * @li RETCODE_INVALID_HANDLE decHandle is invalid. 
 * @li RETCODE_FRAME_NOT_COMPLETE A frame has not been finished.
 * @li RETCODE_WRONG_CALL_SEQUENCE Wrong calling sequence.
 * @li RETCODE_INVALID_FRAME_BUFFER Buffer is an invalid pointer.
 * @li RETCODE_INSUFFICIENT_FRAME_BUFFERS num is less than 
 * the value requested by vpu_DecGetInitialInfo().
 * @li RETCODE_INVALID_STRIDE stride is less than the picture width.
 */
RetCode vpu_DecRegisterFrameBuffer(DecHandle handle,
				   FrameBuffer * bufArray, int num, int stride)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	int i;
	RetCode ret;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (pendingInst) {
		return RETCODE_FRAME_NOT_COMPLETE;
	}

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo.decInfo;

	if (pDecInfo->frameBufPool) {
		return RETCODE_CALLED_BEFORE;
	}

	if (!pDecInfo->initialInfoObtained) {
		return RETCODE_WRONG_CALL_SEQUENCE;
	}

	if (bufArray == 0) {
		return RETCODE_INVALID_FRAME_BUFFER;
	}

	if (num < pDecInfo->initialInfo.minFrameBufferCount) {
		return RETCODE_INSUFFICIENT_FRAME_BUFFERS;
	}

	if (stride < pDecInfo->initialInfo.picWidth || stride % 8 != 0) {
		return RETCODE_INVALID_STRIDE;
	}

	pDecInfo->frameBufPool = bufArray;
	pDecInfo->numFrameBuffers = num;
	pDecInfo->stride = stride;

	/* Let the codec know the addresses of the frame buffers. */
	for (i = 0; i < num; ++i) {
		virt_paraBuf[i * 3] = bufArray[i].bufY;
		virt_paraBuf[i * 3 + 1] = bufArray[i].bufCb;
		virt_paraBuf[i * 3 + 2] = bufArray[i].bufCr;
	}
	
	/* Tell the decoder how much frame buffers were allocated. */
	VpuWriteReg(CMD_SET_FRAME_BUF_NUM, num);
	VpuWriteReg(CMD_SET_FRAME_BUF_STRIDE, stride);

	VpuWriteReg(BIT_BUSY_FLAG, 0x1);
	BitIssueCommand(pCodecInst->instIndex, pCodecInst->codecMode,
			SET_FRAME_BUF);

	while (VpuReadReg(BIT_BUSY_FLAG)) ;

	return RETCODE_SUCCESS;
}

/*!
 * @brief Get bitstream for decoder. 
 *
 * @param handle [Input] The handle obtained from vpu_DecOpen().
 * @param bufAddr [Output] Bitstream buffer physical address.
 * @param size [Output] Bitstream size.
 *
 * @return 
 * @li RETCODE_SUCCESS Successful operation.
 * @li RETCODE_INVALID_HANDLE decHandle is invalid. 
 * @li RETCODE_INVALID_PARAM buf or size is invalid.
 */
RetCode vpu_DecGetBitstreamBuffer(DecHandle handle,
				  PhysicalAddress * paRdPtr,
				  PhysicalAddress * paWrPtr, Uint32 * size)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	PhysicalAddress rdPtr;
	PhysicalAddress wrPtr;
	Uint32 room;
	RetCode ret;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (paRdPtr == 0 || paWrPtr == 0 || size == 0) {
		return RETCODE_INVALID_PARAM;
	}

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo.decInfo;
	
	rdPtr = VpuReadReg(pDecInfo->streamRdPtrRegAddr);
	wrPtr = pDecInfo->streamWrPtr;

	if (wrPtr < rdPtr) {
		room = rdPtr - wrPtr - 1;
	} else {
		room = (pDecInfo->streamBufEndAddr - wrPtr) +
		    (rdPtr - pDecInfo->streamBufStartAddr) - 1;
	}

	*paRdPtr = rdPtr;
	*paWrPtr = wrPtr;
	*size = room;

	return RETCODE_SUCCESS;
}

/*!
 * @brief Update the current bit stream position.
 *
 * @param handle [Input] The handle obtained from vpu_DecOpen().
 * @param size [Input] Size of bit stream you put.
 *
 * @return 
 * @li RETCODE_SUCCESS Successful operation.
 * @li RETCODE_INVALID_HANDLE decHandle is invalid.
 * @li RETCODE_INVALID_PARAM Invalid input parameters.
 */
RetCode vpu_DecUpdateBitstreamBuffer(DecHandle handle, Uint32 size)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	PhysicalAddress wrPtr;
	PhysicalAddress rdPtr;
	RetCode ret;
	int room = 0, val = 0;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo.decInfo;
	wrPtr = pDecInfo->streamWrPtr;

	if (size == 0) {
		val = VpuReadReg(BIT_BIT_STREAM_PARAM);
		val |= 1 << (pCodecInst->instIndex + 2);
		VpuWriteReg(BIT_BIT_STREAM_PARAM, val);
		return RETCODE_SUCCESS;
	}

	rdPtr = VpuReadReg(pDecInfo->streamRdPtrRegAddr);
	if (wrPtr < rdPtr) {
		if (rdPtr <= wrPtr + size)
			return RETCODE_INVALID_PARAM;
	}

	wrPtr += size;

	if (pDecInfo->filePlayEnable != 1) {
		if (wrPtr > pDecInfo->streamBufEndAddr) {
			room = wrPtr - pDecInfo->streamBufEndAddr;
			wrPtr = pDecInfo->streamBufStartAddr;
			wrPtr += room;
		}

		if (wrPtr == pDecInfo->streamBufEndAddr) {
			wrPtr = pDecInfo->streamBufStartAddr;
		}
	}

	pDecInfo->streamWrPtr = wrPtr;
	VpuWriteReg(pDecInfo->streamWrPtrRegAddr, wrPtr);

	return RETCODE_SUCCESS;
}

/*!
 * @brief Start decoding one frame. 
 *
 * @param handle [Input] The handle obtained from vpu_DecOpen().
 *
 * @return 
 * @li RETCODE_SUCCESS Successful operation.
 * @li RETCODE_INVALID_HANDLE decHandle is invalid. 
 * @li RETCODE_FRAME_NOT_COMPLETE A frame has not been finished.
 * @li RETCODE_WRONG_CALL_SEQUENCE Wrong calling sequence.
 */
RetCode vpu_DecStartOneFrame(DecHandle handle, DecParam * param)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	Uint32 rotMir;
	Uint32 val;
	RetCode ret;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (pendingInst) {
		return RETCODE_FRAME_NOT_COMPLETE;
	}

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo.decInfo;

	/* This means frame buffers have not been registered. */
	if (pDecInfo->frameBufPool == 0) {
		return RETCODE_WRONG_CALL_SEQUENCE;
	}

	if (!platform_is_mxc30031()) {
		rotMir = 0;
		if (pDecInfo->rotationEnable) {
			rotMir |= 0x10;	/* Enable rotator */
			switch (pDecInfo->rotationAngle) {
			case 0:
				rotMir |= 0x0;
				break;

			case 90:
				rotMir |= 0x1;
				break;

			case 180:
				rotMir |= 0x2;
				break;

			case 270:
				rotMir |= 0x3;
				break;
			}
		}

		if (pDecInfo->mirrorEnable) {
			rotMir |= 0x10;	/* Enable mirror */
			switch (pDecInfo->mirrorDirection) {
			case MIRDIR_NONE:
				rotMir |= 0x0;
				break;

			case MIRDIR_VER:
				rotMir |= 0x4;
				break;

			case MIRDIR_HOR:
				rotMir |= 0x8;
				break;

			case MIRDIR_HOR_VER:
				rotMir |= 0xc;
				break;

			}
		}

		if (rotMir & 0x10) {	/* rotator enabled */
			VpuWriteReg(CMD_DEC_PIC_ROT_ADDR_Y,
				    pDecInfo->rotatorOutput.bufY);
			VpuWriteReg(CMD_DEC_PIC_ROT_ADDR_CB,
				    pDecInfo->rotatorOutput.bufCb);
			VpuWriteReg(CMD_DEC_PIC_ROT_ADDR_CR,
				    pDecInfo->rotatorOutput.bufCr);
			VpuWriteReg(CMD_DEC_PIC_ROT_STRIDE,
				    pDecInfo->rotatorStride);
		}

		VpuWriteReg(CMD_DEC_PIC_ROT_MODE, rotMir);
	}

	if (!platform_is_mx27()) {
		if (pCodecInst->codecMode == MP4_DEC &&
		    pDecInfo->openParam.mp4DeblkEnable == 1) {
			if (pDecInfo->deBlockingFilterOutputValid) {
				VpuWriteReg(CMD_DEC_PIC_DBK_ADDR_Y,
					    pDecInfo->deBlockingFilterOutput.
					    bufY);
				VpuWriteReg(CMD_DEC_PIC_DBK_ADDR_CB,
					    pDecInfo->deBlockingFilterOutput.
					    bufCb);
				VpuWriteReg(CMD_DEC_PIC_DBK_ADDR_CR,
					    pDecInfo->deBlockingFilterOutput.
					    bufCr);
			} else
				return RETCODE_DEBLOCKING_OUTPUT_NOT_SET;
		}
	}

	if (platform_is_mxc30031()) {
		pDecInfo->vpuCountEnable = param->vpuCountEnable;
		if (param->vpuCountEnable == 1) {
			VpuWriteReg(BIT_VPU_PIC_COUNT, VPU_PIC_COUNT_ENABLE);
		} else {
			VpuWriteReg(BIT_VPU_PIC_COUNT, VPU_PIC_COUNT_DISABLE);
		}
	}

	/* if iframeSearch is Enable, other bit is ignore; */
	if (param->iframeSearchEnable == 1) {
		val = (param->iframeSearchEnable << 2) & 0x4;
	} else {
		val = (param->skipframeMode << 3) |
		    (param->iframeSearchEnable << 2) |
		    (param->prescanMode << 1) | param->prescanEnable;
	}

	VpuWriteReg(CMD_DEC_PIC_OPTION, val);
	VpuWriteReg(CMD_DEC_PIC_SKIP_NUM, param->skipframeNum);

	if (pCodecInst->codecMode == AVC_DEC) {
		if (pDecInfo->openParam.reorderEnable == 1) {
			VpuWriteReg(CMD_DEC_DISPLAY_REORDER,
					param->dispReorderBuf << 1 |
					VpuReadReg(CMD_DEC_DISPLAY_REORDER));
		}
	}

	if (!platform_is_mx27() && (pCodecInst->codecMode == VC1_DEC)) {
		if (pDecInfo->filePlayEnable == 1) {
				VpuWriteReg(CMD_DEC_DISPLAY_REORDER,
					param->dispReorderBuf << 1 |
					VpuReadReg(CMD_DEC_DISPLAY_REORDER));
		}
	}

	if (pDecInfo->filePlayEnable == 1) {
		VpuWriteReg(CMD_DEC_PIC_CHUNK_SIZE, param->chunkSize);
		if (pDecInfo->dynamicAllocEnable == 1) {
			VpuWriteReg(CMD_DEC_PIC_BB_START,
				    param->picStreamBufferAddr);
		}

		VpuWriteReg(CMD_DEC_PIC_START_BYTE, param->picStartByteOffset);
	}

	VpuWriteReg(BIT_BUSY_FLAG, 0x1);
	BitIssueCommand(pCodecInst->instIndex, pCodecInst->codecMode, PIC_RUN);

	pendingInst = pCodecInst;
	return RETCODE_SUCCESS;
}

/*!
 * @brief Get the information of output of decoding. 
 *
 * @param handle [Input] The handle obtained from vpu_DecOpen().
 * @param info [Output] Pointer to DecOutputInfo data structure.
 *
 * @return 
 * @li RETCODE_SUCCESS Successful operation.
 * @li RETCODE_INVALID_HANDLE decHandle is invalid.
 * @li RETCODE_WRONG_CALL_SEQUENCE Wrong calling sequence.
 * @li RETCODE_INVALID_PARAM Info is an invalid pointer.
 */
RetCode vpu_DecGetOutputInfo(DecHandle handle, DecOutputInfo * info)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	RetCode ret;
	Uint32 val = 0;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (info == 0) {
		return RETCODE_INVALID_PARAM;
	}

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo.decInfo;

	if (pendingInst == 0) {
		return RETCODE_WRONG_CALL_SEQUENCE;
	}

	if (pCodecInst != pendingInst) {
		return RETCODE_INVALID_HANDLE;
	}

	if (platform_is_mx32()) {
		vl2cc_flush();
	}

	/* MX27 TO1 is skipping frames during this check */
	if (!platform_is_mx27()) {
		if (VpuReadReg(RET_DEC_PIC_SUCCESS) == 0) {
			pendingInst = 0;
			return RETCODE_FAILURE;
		}
	}

	info->indexFrameDisplay = VpuReadReg(RET_DEC_PIC_FRAME_IDX);
	info->indexFrameDecoded = VpuReadReg(RET_DEC_PIC_CUR_IDX);
	info->picType = VpuReadReg(RET_DEC_PIC_TYPE);
	info->numOfErrMBs = VpuReadReg(RET_DEC_PIC_ERR_MB);
	info->prescanresult = VpuReadReg(RET_DEC_PIC_OPTION);

	if (platform_is_mx27()) {
		if (pCodecInst->codecMode == MP4_DEC &&
		    pDecInfo->openParam.qpReport == 1) {
			int widthInMB;
			int heightInMB;
			int readPnt;
			int writePnt;
			int i;
			int j;
			Uint32 val, val1, val2;

			widthInMB = pDecInfo->initialInfo.picWidth / 16;
			heightInMB = pDecInfo->initialInfo.picHeight / 16;
			writePnt = 0;
			for (i = 0; i < heightInMB; ++i) {
				readPnt = i * 32;
				for (j = 0; j < widthInMB; j += 4) {
					val1 = virt_paraBuf[readPnt];
					readPnt += 1;
					val2 = virt_paraBuf[readPnt];
					readPnt += 1;
					val = (val1 << 8 & 0xff000000) |
					    (val1 << 16) | (val2 >> 8) |
					    (val2 & 0x000000ff);
					virt_paraBuf2[writePnt] = val;
					writePnt += 1;
				}
			}

			info->qpInfo = paraBuffer - PARA_BUF2_SIZE;
		}
	}

	if (platform_is_mxc30031()) {
		if (pCodecInst->codecMode == VC1_DEC) {
			val = VpuReadReg(RET_DEC_PIC_POST);
			info->hScaleFlag = val >> 1 & 1;
			info->vScaleFlag = val >> 2 & 1;
		}

		if (pDecInfo->vpuCountEnable == 1) {
			info->DecVpuCount = (VpuReadReg(BIT_VPU_PIC_COUNT)) &
								    0x7FFFFFFF;
		}
	}

	if (platform_is_mx32()) {
		int i;
		
		for (i = 0 ; i < 3 ; i++) {
			if (i < pDecInfo->initialInfo.nextDecodedIdxNum) {
					info->indexFrameNextDecoded[i] = 
						((val >> (i * 5)) & 0x1f);
				} else {
					info->indexFrameNextDecoded[i] = -1;
			}
		}
	}

	pendingInst = 0;
	return RETCODE_SUCCESS;
}

RetCode vpu_DecBitBufferFlush(DecHandle handle)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	RetCode ret;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (pendingInst) {
		return RETCODE_FRAME_NOT_COMPLETE;
	}

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo.decInfo;

	/* This means frame buffers have not been registered. */
	if (pDecInfo->frameBufPool == 0) {
		return RETCODE_WRONG_CALL_SEQUENCE;
	}

	VpuWriteReg(BIT_BUSY_FLAG, 0x1);
	BitIssueCommand(pCodecInst->instIndex, pCodecInst->codecMode,
			DEC_BUF_FLUSH);

	while (VpuReadReg(BIT_BUSY_FLAG)) ;

	pDecInfo->streamWrPtr = pDecInfo->streamBufStartAddr;
	VpuWriteReg(pDecInfo->streamWrPtrRegAddr, pDecInfo->streamBufStartAddr);

	return RETCODE_SUCCESS;
}

/*!
 * @brief Give command to the decoder. 
 *
 * @param handle [Input] The handle obtained from vpu_DecOpen().
 * @param cmd [Intput] Command.
 * @param param [Intput/Output] param  for cmd. 
 *
 * @return 
 * @li RETCODE_INVALID_COMMANDcmd is not valid.
 * @li RETCODE_INVALID_HANDLE decHandle is invalid. 
 * @li RETCODE_FRAME_NOT_COMPLETE A frame has not been finished.
 */
RetCode vpu_DecGiveCommand(DecHandle handle, CodecCommand cmd, void *param)
{
	CodecInst *pCodecInst;
	DecInfo *pDecInfo;
	RetCode ret;

	ret = CheckDecInstanceValidity(handle);
	if (ret != RETCODE_SUCCESS)
		return ret;

	if (pendingInst) {
		return RETCODE_FRAME_NOT_COMPLETE;
	}

	if (platform_is_mxc30031() && ((cmd == ENABLE_ROTATION) ||
				       (cmd == DISABLE_ROTATION)
				       || (cmd == ENABLE_MIRRORING)
				       || (cmd == DISABLE_MIRRORING)
				       || (cmd == SET_MIRROR_DIRECTION)
				       || (cmd == SET_ROTATION_ANGLE)
				       || (cmd == SET_ROTATOR_OUTPUT)
				       || (cmd == SET_ROTATOR_STRIDE))) {
		return RETCODE_NOT_SUPPORTED;
	}

	if (platform_is_mx27() && (cmd == DEC_SET_DEBLOCK_OUTPUT)) {
		return RETCODE_NOT_SUPPORTED;
	}

	pCodecInst = handle;
	pDecInfo = &pCodecInst->CodecInfo.decInfo;
	
	switch (cmd) {
	case ENABLE_ROTATION:
		{
			if (!pDecInfo->rotatorOutputValid) {
				return RETCODE_ROTATOR_OUTPUT_NOT_SET;
			}
			if (pDecInfo->rotatorStride == 0) {
				return RETCODE_ROTATOR_STRIDE_NOT_SET;
			}
			pDecInfo->rotationEnable = 1;
			break;
		}

	case DISABLE_ROTATION:
		{
			pDecInfo->rotationEnable = 0;
			break;
		}

	case ENABLE_MIRRORING:
		{
			if (!pDecInfo->rotatorOutputValid) {
				return RETCODE_ROTATOR_OUTPUT_NOT_SET;
			}
			if (pDecInfo->rotatorStride == 0) {
				return RETCODE_ROTATOR_STRIDE_NOT_SET;
			}
			pDecInfo->mirrorEnable = 1;
			break;
		}

	case DISABLE_MIRRORING:
		{
			pDecInfo->mirrorEnable = 0;
			break;
		}

	case SET_MIRROR_DIRECTION:
		{
			MirrorDirection mirDir;

			if (param == 0) {
				return RETCODE_INVALID_PARAM;
			}
			mirDir = *(MirrorDirection *) param;
			if (mirDir < MIRDIR_NONE || mirDir > MIRDIR_HOR_VER) {
				return RETCODE_INVALID_PARAM;
			}
			pDecInfo->mirrorDirection = mirDir;
			break;
		}

	case SET_ROTATION_ANGLE:
		{
			int angle;
			int height, width;

			if (param == 0) {
				return RETCODE_INVALID_PARAM;
			}
			angle = *(int *)param;
			if (angle != 0 && angle != 90 &&
			    angle != 180 && angle != 270) {
				return RETCODE_INVALID_PARAM;
			}
			if (pDecInfo->rotatorStride != 0) {
				height = pDecInfo->initialInfo.picHeight;
				width = pDecInfo->initialInfo.picWidth;

				if (angle == 90 || angle == 270) {
					if (height > pDecInfo->rotatorStride) {
						return RETCODE_INVALID_PARAM;
					}
				} else {
					if (width > pDecInfo->rotatorStride) {
						return RETCODE_INVALID_PARAM;
					}
				}
			}

			pDecInfo->rotationAngle = angle;
			break;
		}

	case SET_ROTATOR_OUTPUT:
		{
			FrameBuffer *frame;

			if (param == 0) {
				return RETCODE_INVALID_PARAM;
			}
			frame = (FrameBuffer *) param;
			pDecInfo->rotatorOutput = *frame;
			pDecInfo->rotatorOutputValid = 1;
			break;
		}

	case SET_ROTATOR_STRIDE:
		{
			int stride;

			if (param == 0) {
				return RETCODE_INVALID_PARAM;
			}
			stride = *(int *)param;
			if (stride % 8 != 0 || stride == 0) {
				return RETCODE_INVALID_STRIDE;
			}
			if (pDecInfo->rotationAngle == 90 ||
			    pDecInfo->rotationAngle == 270) {
				if (pDecInfo->initialInfo.picHeight > stride) {
					return RETCODE_INVALID_STRIDE;
				}
			} else {
				if (pDecInfo->initialInfo.picWidth > stride) {
					return RETCODE_INVALID_STRIDE;
				}
			}

			pDecInfo->rotatorStride = stride;
			break;
		}

	case DEC_SET_SPS_RBSP:
		{
			if (pCodecInst->codecMode != AVC_DEC) {
				return RETCODE_INVALID_COMMAND;
			}
			if (param == 0) {
				return RETCODE_INVALID_PARAM;
			}
			SetParaSet(handle, 0, param);
			break;
		}

	case DEC_SET_PPS_RBSP:
		{
			if (pCodecInst->codecMode != AVC_DEC) {
				return RETCODE_INVALID_COMMAND;
			}
			if (param == 0) {
				return RETCODE_INVALID_PARAM;
			}
			SetParaSet(handle, 1, param);
			break;
		}

	case DEC_SET_DEBLOCK_OUTPUT:
		{
			FrameBuffer *frame;
			if (param == 0) {
				return RETCODE_INVALID_PARAM;
			}
			frame = (FrameBuffer *) param;
			pDecInfo->deBlockingFilterOutput = *frame;
			pDecInfo->deBlockingFilterOutputValid = 1;
			break;
		}

	default:
		return RETCODE_INVALID_COMMAND;
	}

	return RETCODE_SUCCESS;
}

void SaveGetEncodeHeader(EncHandle handle, int encHeaderType, char *filename)
{
	FILE *fp = NULL;
	Uint8 *pHeader = NULL;
	EncParamSet encHeaderParam = { 0 };
	int i;
	Uint32 dword1, dword2;
	Uint32 *pBuf;
	Uint32 byteSize;

	if (filename == NULL)
		return;

	vpu_EncGiveCommand(handle, encHeaderType, &encHeaderParam);
	byteSize = ((encHeaderParam.size + 3) & ~3);
	pHeader = (Uint8 *) malloc(sizeof(Uint8) * byteSize);
	if (pHeader) {
		memcpy(pHeader, encHeaderParam.paraSet, byteSize);

		/* ParaBuffer is big endian */
		pBuf = (Uint32 *) pHeader;
		for (i = 0; i < byteSize / 4; i++) {
			dword1 = pBuf[i];
			dword2 = (dword1 >> 24) & 0xFF;
			dword2 |= ((dword1 >> 16) & 0xFF) << 8;
			dword2 |= ((dword1 >> 8) & 0xFF) << 16;
			dword2 |= ((dword1 >> 0) & 0xFF) << 24;
			pBuf[i] = dword2;
		}

		if (encHeaderParam.size > 0) {
			fp = fopen(filename, "wb");
			if (fp) {
				fwrite(pHeader, sizeof(Uint8),
				       encHeaderParam.size, fp);
				fclose(fp);
			}
		}

		free(pHeader);
	}
}

void SaveQpReport(PhysicalAddress qpReportAddr, int picWidth, int picHeight,
		  int frameIdx, char *fileName)
{
	FILE *fp;
	int i, j;
	int MBx, MBy, MBxof4, MBxof1, MBxx;
	Uint32 qp;
	Uint8 lastQp[4];

	if (frameIdx == 0)
		fp = fopen(fileName, "wb");
	else
		fp = fopen(fileName, "a+b");

	if (!fp) {
		printf("Can't open %s in SaveQpReport\n", fileName);
		return;
	}

	MBx = picWidth / 16;
	MBxof1 = MBx % 4;
	MBxof4 = MBx - MBxof1;
	MBy = picHeight / 16;
	MBxx = (MBx + 3) / 4 * 4;
	for (i = 0; i < MBy; i++) {
		for (j = 0; j < MBxof4; j = j + 4) {
			qp = VpuReadReg(qpReportAddr + j + MBxx * i);
			fprintf(fp, " %4d %4d %3d \n", frameIdx,
				MBx * i + j + 0, (Uint8) (qp >> 24));
			fprintf(fp, " %4d %4d %3d \n", frameIdx,
				MBx * i + j + 1, (Uint8) (qp >> 16));
			fprintf(fp, " %4d %4d %3d \n", frameIdx,
				MBx * i + j + 2, (Uint8) (qp >> 8));
			fprintf(fp, " %4d %4d %3d \n", frameIdx,
				MBx * i + j + 3, (Uint8) qp);
		}

		if (MBxof1 > 0) {
			qp = VpuReadReg(qpReportAddr + MBxx * i + MBxof4);
			lastQp[0] = (Uint8) (qp >> 24);
			lastQp[1] = (Uint8) (qp >> 16);
			lastQp[2] = (Uint8) (qp >> 8);
			lastQp[3] = (Uint8) (qp);
		}

		for (j = 0; j < MBxof1; j++) {
			fprintf(fp, " %4d %4d %3d \n", frameIdx,
				MBx * i + j + MBxof4, lastQp[j]);
		}
	}

	fclose(fp);
}

