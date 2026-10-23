/* Beetle PCE PS5: every external name of the vendored 7z decoder (LZMA SDK 26.05) gets the prefix pce7z_, so it can't
   meet the older LZMA SDK the core links for libchdr (deps/lzma-19.00: LzmaDec_*, Bra86, Delta, CpuArch...): the two
   copies would clash in the link or, worse, silently mix two versions. Force-included into each file of this folder
   (-include fe7z_names.h) and included by ps5/frontend/fe_7z.cpp before the SDK's headers. The Makefile checks after
   each build that these objects define no other global name (lzma7z/names.ok): a name a newer SDK adds goes here.
   The list is what `nm -g --defined-only` prints for the objects.
   SPDX-License-Identifier: MIT */
#ifndef FE7Z_NAMES_H
#define FE7Z_NAMES_H
#define Bcj2Dec_Decode pce7z_Bcj2Dec_Decode
#define Bcj2Dec_Init pce7z_Bcj2Dec_Init
#define Buf_Create pce7z_Buf_Create
#define Buf_Free pce7z_Buf_Free
#define Buf_Init pce7z_Buf_Init
#define CrcCalc pce7z_CrcCalc
#define CrcGenerateTable pce7z_CrcGenerateTable
#define CrcUpdate pce7z_CrcUpdate
#define CrcUpdateT12 pce7z_CrcUpdateT12
#define Delta_Decode pce7z_Delta_Decode
#define Delta_Encode pce7z_Delta_Encode
#define Delta_Init pce7z_Delta_Init
#define LookInStream_LookRead pce7z_LookInStream_LookRead
#define LookInStream_Read pce7z_LookInStream_Read
#define LookInStream_Read2 pce7z_LookInStream_Read2
#define LookInStream_SeekTo pce7z_LookInStream_SeekTo
#define LookToRead2_CreateVTable pce7z_LookToRead2_CreateVTable
#define Lzma2Dec_Allocate pce7z_Lzma2Dec_Allocate
#define Lzma2Dec_AllocateProbs pce7z_Lzma2Dec_AllocateProbs
#define Lzma2Dec_DecodeToBuf pce7z_Lzma2Dec_DecodeToBuf
#define Lzma2Dec_DecodeToDic pce7z_Lzma2Dec_DecodeToDic
#define Lzma2Dec_Init pce7z_Lzma2Dec_Init
#define Lzma2Dec_Parse pce7z_Lzma2Dec_Parse
#define Lzma2Decode pce7z_Lzma2Decode
#define LzmaDec_Allocate pce7z_LzmaDec_Allocate
#define LzmaDec_AllocateProbs pce7z_LzmaDec_AllocateProbs
#define LzmaDec_DecodeToBuf pce7z_LzmaDec_DecodeToBuf
#define LzmaDec_DecodeToDic pce7z_LzmaDec_DecodeToDic
#define LzmaDec_Free pce7z_LzmaDec_Free
#define LzmaDec_FreeProbs pce7z_LzmaDec_FreeProbs
#define LzmaDec_Init pce7z_LzmaDec_Init
#define LzmaDec_InitDicAndState pce7z_LzmaDec_InitDicAndState
#define LzmaDecode pce7z_LzmaDecode
#define LzmaProps_Decode pce7z_LzmaProps_Decode
#define Ppmd7_Alloc pce7z_Ppmd7_Alloc
#define Ppmd7_Construct pce7z_Ppmd7_Construct
#define Ppmd7_Free pce7z_Ppmd7_Free
#define Ppmd7_Init pce7z_Ppmd7_Init
#define Ppmd7_MakeEscFreq pce7z_Ppmd7_MakeEscFreq
#define Ppmd7_Update1 pce7z_Ppmd7_Update1
#define Ppmd7_Update1_0 pce7z_Ppmd7_Update1_0
#define Ppmd7_Update2 pce7z_Ppmd7_Update2
#define Ppmd7_UpdateModel pce7z_Ppmd7_UpdateModel
#define Ppmd7z_DecodeSymbol pce7z_Ppmd7z_DecodeSymbol
#define Ppmd7z_RangeDec_Init pce7z_Ppmd7z_RangeDec_Init
#define SecToLook_CreateVTable pce7z_SecToLook_CreateVTable
#define SecToRead_CreateVTable pce7z_SecToRead_CreateVTable
#define SeqInStream_ReadByte pce7z_SeqInStream_ReadByte
#define SeqInStream_ReadMax pce7z_SeqInStream_ReadMax
#define SzArEx_Extract pce7z_SzArEx_Extract
#define SzArEx_Free pce7z_SzArEx_Free
#define SzArEx_GetFileNameUtf16 pce7z_SzArEx_GetFileNameUtf16
#define SzArEx_Init pce7z_SzArEx_Init
#define SzArEx_Open pce7z_SzArEx_Open
#define SzAr_DecodeFolder pce7z_SzAr_DecodeFolder
#define SzAr_GetFolderUnpackSize pce7z_SzAr_GetFolderUnpackSize
#define SzGetNextFolderItem pce7z_SzGetNextFolderItem
#define g_CrcTable pce7z_g_CrcTable
#define k7zSignature pce7z_k7zSignature
#define z7_BranchConvSt_X86_Dec pce7z_z7_BranchConvSt_X86_Dec
#define z7_BranchConv_ARM64_Dec pce7z_z7_BranchConv_ARM64_Dec
#define z7_BranchConv_ARMT_Dec pce7z_z7_BranchConv_ARMT_Dec
#define z7_BranchConv_ARM_Dec pce7z_z7_BranchConv_ARM_Dec
#define z7_BranchConv_IA64_Dec pce7z_z7_BranchConv_IA64_Dec
#define z7_BranchConv_PPC_Dec pce7z_z7_BranchConv_PPC_Dec
#define z7_BranchConv_RISCV_Dec pce7z_z7_BranchConv_RISCV_Dec
#define z7_BranchConv_RISCV_Enc pce7z_z7_BranchConv_RISCV_Enc
#define z7_BranchConv_SPARC_Dec pce7z_z7_BranchConv_SPARC_Dec
#define z7_GetFunc_CrcUpdate pce7z_z7_GetFunc_CrcUpdate
#endif
