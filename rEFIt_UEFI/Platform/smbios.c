/**
 smbios.c
 original idea of SMBIOS patching by mackerintel
 implementation for UEFI smbios table patching
  Slice 2011.

 portion copyright Intel
 Copyright (c) 2009 - 2010, Intel Corporation. All rights reserved.<BR>
 This program and the accompanying materials
 are licensed and made available under the terms and conditions of the BSD License
 which accompanies this distribution.  The full text of the license may be found at
 http://opensource.org/licenses/bsd-license.php

 THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
 WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

 Module Name:

 SmbiosGen.c
 **/

#include "Platform.h"

#ifndef DEBUG_ALL
#define DEBUG_SMBIOS 1
#else
#define DEBUG_SMBIOS DEBUG_ALL
#endif

#if DEBUG_SMBIOS == 0
#define DBG(...)
#else
#define DBG(...) DebugLog(DEBUG_SMBIOS, __VA_ARGS__)
#endif

#define CPUID_EXTFEATURE_EM64T		_Bit(29)
#define CPUID_EXTFEATURE_XD			_Bit(20)
#define CPUID_FEATURE_VMX			_HBit(5)
#define CPUID_FEATURE_EST			_HBit(7)

EFI_GUID						gUuid;
EFI_GUID						*gTableGuidArray[] = {&gEfiSmbiosTableGuid, &gEfiSmbios3TableGuid};
//
// syscl: implement Dell truncate fix
// remap EFI_SMBIOS_TABLE_1 to new guid to fix Dell
// SMBIOS Table 1 truncate issue credit David Passmore
//
EFI_GUID                        gRemapEfiSmbiosTableGuid = REMAP_SMBIOS_TABLE_GUID;

//EFI_PHYSICAL_ADDRESS			*smbiosTable;
VOID                        *Smbios;  //pointer to SMBIOS data
SMBIOS_TABLE_ENTRY_POINT		*EntryPoint; //SmbiosEps original
SMBIOS_TABLE_ENTRY_POINT		*SmbiosEpsNew; //new SmbiosEps
//for patching
APPLE_SMBIOS_STRUCTURE_POINTER  SmbiosTable;
APPLE_SMBIOS_STRUCTURE_POINTER  newSmbiosTable;
UINT16                      NumberOfRecords;
UINT16                      MaxStructureSize;
UINT8*                      Current; //pointer to the current end of tables
EFI_SMBIOS_TABLE_HEADER			*Record;
EFI_SMBIOS_HANDLE           Handle;
EFI_SMBIOS_TYPE             Type;

UINTN				stringNumber;
UINTN				TableSize;
UINTN				Index, Size, NewSize, MaxSize;
UINT16			CoreCache = 0;
UINT16			gTotalMemoryDevices = 0;
UINT16			L1, L2, L3;
UINT32			MaxMemory = 0;
UINT32			mTotalSystemMemory = 0;
UINT16			mHandle3 = 3 << 8;
UINT16			mHandle16 = 16 << 8;
UINT16			mHandle17[MAX_RAM_SLOTS];
UINT16			mHandle19 = 19 << 8;
UINT16			mMemory17[MAX_RAM_SLOTS];
UINT64			mInstalled[MAX_RAM_SLOTS];
UINT64			mEnabled[MAX_RAM_SLOTS];
BOOLEAN			gMobile;
UINT8       gBootStatus;
BOOLEAN			Once;

VOID GetTableType18();

#define MAX_HANDLE        0xFEFF
#define SMBIOS_PTR        SIGNATURE_32('_','S','M','_')
#define MAX_TABLE_SIZE    512

#define smbios_offsetof(s,m) ( (SMBIOS_TABLE_STRING) ((UINT8*)&((s*)0)->m - (UINT8*)0))

SMBIOS_TABLE_STRING    SMBIOS_TABLE_TYPE0_STR_IDX[] = {
  smbios_offsetof(SMBIOS_TABLE_TYPE0, Vendor),
  smbios_offsetof(SMBIOS_TABLE_TYPE0, BiosVersion),
  smbios_offsetof(SMBIOS_TABLE_TYPE0, BiosReleaseDate),
  0x00
}; // offsets of structures that values are strings for type 0 Bios

SMBIOS_TABLE_STRING    SMBIOS_TABLE_TYPE1_STR_IDX[] = {
  smbios_offsetof(SMBIOS_TABLE_TYPE1, Manufacturer),
  smbios_offsetof(SMBIOS_TABLE_TYPE1, ProductName),
  smbios_offsetof(SMBIOS_TABLE_TYPE1, Version),
  smbios_offsetof(SMBIOS_TABLE_TYPE1, SerialNumber),
  smbios_offsetof(SMBIOS_TABLE_TYPE1, SKUNumber),
  smbios_offsetof(SMBIOS_TABLE_TYPE1, Family),
  0x00
}; // offsets of structures that values are strings for type 1 System

SMBIOS_TABLE_STRING    SMBIOS_TABLE_TYPE2_STR_IDX[] = {
  smbios_offsetof(SMBIOS_TABLE_TYPE2, Manufacturer),
  smbios_offsetof(SMBIOS_TABLE_TYPE2, ProductName),
  smbios_offsetof(SMBIOS_TABLE_TYPE2, Version),
  smbios_offsetof(SMBIOS_TABLE_TYPE2, SerialNumber),
  smbios_offsetof(SMBIOS_TABLE_TYPE2, AssetTag),
  smbios_offsetof(SMBIOS_TABLE_TYPE2, LocationInChassis),
  0x00
}; // offsets of structures that values are strings for type 2 BaseBoard

SMBIOS_TABLE_STRING    SMBIOS_TABLE_TYPE3_STR_IDX[] = {
  smbios_offsetof(SMBIOS_TABLE_TYPE3, Manufacturer),
  smbios_offsetof(SMBIOS_TABLE_TYPE3, Version),
  smbios_offsetof(SMBIOS_TABLE_TYPE3, SerialNumber),
  smbios_offsetof(SMBIOS_TABLE_TYPE3, AssetTag),
  0x00
}; // offsets of structures that values are strings for type 3 Chassis

SMBIOS_TABLE_STRING    SMBIOS_TABLE_TYPE4_STR_IDX[] = {
  smbios_offsetof(SMBIOS_TABLE_TYPE4, Socket),
  smbios_offsetof(SMBIOS_TABLE_TYPE4, ProcessorManufacture),
  smbios_offsetof(SMBIOS_TABLE_TYPE4, ProcessorVersion),
  smbios_offsetof(SMBIOS_TABLE_TYPE4, SerialNumber),
  smbios_offsetof(SMBIOS_TABLE_TYPE4, AssetTag),
  smbios_offsetof(SMBIOS_TABLE_TYPE4, PartNumber),
  0x00
}; // offsets of structures that values are strings for type 3 Chassis

/* Functions */

// validate the SMBIOS entry point structure
BOOLEAN IsEntryPointStructureValid (IN SMBIOS_TABLE_ENTRY_POINT *EntryPointStructure)
{
  UINTN                     I;
  UINT8                     Length;
  UINT8                     Checksum = 0;
  UINT8                     *BytePtr;

  if (!EntryPointStructure)
    return FALSE;

  BytePtr = (UINT8*) EntryPointStructure;
  Length = EntryPointStructure->EntryPointLength;

  for (I = 0; I < Length; I++) {
    Checksum = Checksum + (UINT8) BytePtr[I];
  }

  // a valid SMBIOS EPS must have checksum of 0
  return (Checksum == 0);
}

VOID* FindOemSMBIOSPtr (VOID)
{
	UINTN      Address;

	// Search 0x0f0000 - 0x0fffff for SMBIOS Ptr
	for (Address = 0xf0000; Address < 0xfffff; Address += 0x10) {
		if (*(UINT32 *)(Address) == SMBIOS_PTR && IsEntryPointStructureValid((SMBIOS_TABLE_ENTRY_POINT*)Address)) {
			return (VOID *)Address;
		}
	}
	return NULL;
}

VOID* GetSmbiosTablesFromHob (VOID)
{
	EFI_PHYSICAL_ADDRESS       *Table;
	EFI_PEI_HOB_POINTERS        GuidHob;

	GuidHob.Raw = GetFirstGuidHob (&gEfiSmbiosTableGuid);
	if (GuidHob.Raw != NULL) {
		Table = GET_GUID_HOB_DATA (GuidHob.Guid);
		if (Table != NULL) {
			return (VOID *)(UINTN)*Table;
		}
	}
  GuidHob.Raw = GetFirstGuidHob (&gEfiSmbios3TableGuid);
  if (GuidHob.Raw != NULL) {
    Table = GET_GUID_HOB_DATA (GuidHob.Guid);
    if (Table != NULL) {
      return (VOID *)(UINTN)*Table;
    }
  }
	return NULL;
}

VOID* GetSmbiosTablesFromConfigTables (VOID)
{
	EFI_STATUS              Status;
	EFI_PHYSICAL_ADDRESS    *Table;

	Status = EfiGetSystemConfigurationTable (&gEfiSmbiosTableGuid, (VOID **)&Table);
	if (EFI_ERROR(Status) || Table == NULL) {
		Table = NULL;
    Status = EfiGetSystemConfigurationTable (&gEfiSmbios3TableGuid, (VOID **)&Table);
    if (EFI_ERROR(Status)) {
      Table = NULL;
    }
	}

	return Table;
}

// This function determines ascii string length ending by space.
// search restricted to MaxLen, for example
// iStrLen("ABC    ", 20) == 3
// if MaxLen=0 then as usual strlen but bugless
UINTN iStrLen(CHAR8* String, UINTN MaxLen)
{
	UINTN	Len = 0;
	CHAR8*	BA;
	if(MaxLen > 0) {
		for (Len=0; Len<MaxLen; Len++) {
			if (String[Len] == 0) {
				break;
			}
		}
		BA = &String[Len - 1];
		while ((Len != 0) && ((*BA == ' ') || (*BA == 0))) {
			BA--; Len--;
		}
	} else {
		BA = String;
		while(*BA){BA++; Len++;}
	}
	return Len;
}


// Internal functions for flat SMBIOS

UINT16 SmbiosTableLength (APPLE_SMBIOS_STRUCTURE_POINTER SmbiosTableN)
{
	CHAR8  *AChar;
	UINT16  Length;

	AChar = (CHAR8 *)(SmbiosTableN.Raw + SmbiosTableN.Hdr->Length);
	while ((*AChar != 0) || (*(AChar + 1) != 0)) {
		AChar ++; //stop at 00 - first 0
	}
	Length = (UINT16)((UINTN)AChar - (UINTN)SmbiosTableN.Raw + 2); //length includes 00
	return Length;
}


EFI_SMBIOS_HANDLE LogSmbiosTable (APPLE_SMBIOS_STRUCTURE_POINTER SmbiosTableN)
{
	UINT16  Length = SmbiosTableLength(SmbiosTableN);
	if (Length > MaxStructureSize) {
		MaxStructureSize = Length;
	}
	CopyMem(Current, SmbiosTableN.Raw, Length);
	Current += Length;
	NumberOfRecords++;
	return SmbiosTableN.Hdr->Handle;
}

EFI_STATUS UpdateSmbiosString (APPLE_SMBIOS_STRUCTURE_POINTER SmbiosTableN, SMBIOS_TABLE_STRING* Field, CHAR8* Buffer)
{
	CHAR8*	AString;
	CHAR8*	C1; //pointers for copy
	CHAR8*	C2;
	UINTN	Length = SmbiosTableLength(SmbiosTableN);
	UINTN	ALength, BLength;
	UINT8	IndexStr = 1;

	if ((SmbiosTableN.Raw == NULL) || !Buffer || !Field) {
		return EFI_NOT_FOUND;
	}
	AString = (CHAR8*)(SmbiosTableN.Raw + SmbiosTableN.Hdr->Length); //first string
	while (IndexStr != *Field) {
		if (*AString) {
			IndexStr++;
		}
		while (*AString != 0) AString++; //skip string at index
		AString++; //next string
		if (*AString == 0) {
			//this is end of the table
			if (*Field == 0) {
				AString[1] = 0; //one more zero
			}
			*Field = IndexStr; //index of the next string that  is empty
			if (IndexStr == 1) {
				AString--; //first string has no leading zero
			}
			break;
		}
	}
	// AString is at place to copy
	ALength = iStrLen(AString, 0);
	BLength = iStrLen(Buffer, SMBIOS_STRING_MAX_LENGTH);
//	DBG("Table type %d field %d\n", SmbiosTable.Hdr->Type, *Field);
//	DBG("Old string length=%d new length=%d\n", ALength, BLength);
	if (BLength > ALength) {
		//Shift right
		C1 = (CHAR8*)SmbiosTableN.Raw + Length; //old end
		C2 = C1  + BLength - ALength; //new end
		*C2 = 0;
		while (C1 != AString) *(--C2) = *(--C1);

	} else if (BLength < ALength) {
		//Shift left
		C1 = AString + ALength; //old start
		C2 = AString + BLength; //new start
		while (C1 != ((CHAR8*)SmbiosTableN.Raw + Length)) {
			*C2++ = *C1++;
		}
		*C2 = 0;
		*(--C2) = 0; //end of table
	}
	CopyMem(AString, Buffer, BLength);
	*(AString + BLength) = 0; // not sure there is 0

	return EFI_SUCCESS;
}

APPLE_SMBIOS_STRUCTURE_POINTER GetSmbiosTableFromType (
	SMBIOS_TABLE_ENTRY_POINT *SmbiosPoint, UINT8 SmbiosType, UINTN IndexTable)
{
	APPLE_SMBIOS_STRUCTURE_POINTER SmbiosTableN;
	UINTN                    SmbiosTypeIndex;

	SmbiosTypeIndex = 0;
	SmbiosTableN.Raw = (UINT8 *)((UINTN)SmbiosPoint->TableAddress);
	if (SmbiosTableN.Raw == NULL) {
		return SmbiosTableN;
	}
	while ((SmbiosTypeIndex != IndexTable) || (SmbiosTableN.Hdr->Type != SmbiosType)) {
		if (SmbiosTableN.Hdr->Type == SMBIOS_TYPE_END_OF_TABLE) {
			SmbiosTableN.Raw = NULL;
			return SmbiosTableN;
		}
		if (SmbiosTableN.Hdr->Type == SmbiosType) {
			SmbiosTypeIndex++;
		}
		SmbiosTableN.Raw = (UINT8 *)(SmbiosTableN.Raw + SmbiosTableLength (SmbiosTableN));
	}
	return SmbiosTableN;
}

CHAR8* GetSmbiosString (
	APPLE_SMBIOS_STRUCTURE_POINTER SmbiosTableN, SMBIOS_TABLE_STRING StringN)
{
	CHAR8      *AString;
	UINT8      Ind;

	Ind = 1;
	AString = (CHAR8 *)(SmbiosTableN.Raw + SmbiosTableN.Hdr->Length); //first string
	while (Ind != StringN) {
		while (*AString != 0) {
			AString ++;
		}
		AString ++; //skip zero ending
		if (*AString == 0) {
			return AString; //this is end of the table
		}
		Ind++;
	}

	return AString; //return pointer to Ascii string
}

VOID AddSmbiosEndOfTable()
{
	SMBIOS_STRUCTURE* StructurePtr = (SMBIOS_STRUCTURE*)Current;
	StructurePtr->Type		= SMBIOS_TYPE_END_OF_TABLE;
	StructurePtr->Length	= sizeof(SMBIOS_STRUCTURE);
	StructurePtr->Handle	= SMBIOS_TYPE_INACTIVE; //spec 2.7 p.120
	Current += sizeof(SMBIOS_STRUCTURE);
	*Current++ = 0;
	*Current++ = 0; //double 0 at the end
	NumberOfRecords++;
}

VOID UniquifySmbiosTableStr (APPLE_SMBIOS_STRUCTURE_POINTER SmbiosTableN, SMBIOS_TABLE_STRING* str_idx)
{
  INTN            i, j;
  SMBIOS_TABLE_STRING    cmp_idx;
  SMBIOS_TABLE_STRING    cmp_str;
  SMBIOS_TABLE_STRING    ref_str;

  if (0 == str_idx[0]) return;    // SMBIOS doesn't have string structures, just return;
  for (i = 1; ;i++) {
    cmp_idx = str_idx[i];
    if (0 == cmp_idx) break;
    cmp_str = SmbiosTableN.Raw[cmp_idx];
    if (0 == cmp_str) continue;        // if string is undefine, continue
    for (j = 0; j < i; j++) {
      ref_str = SmbiosTableN.Raw[str_idx[j]];
      if (cmp_str == ref_str) {
        SmbiosTableN.Raw[cmp_idx] = 0;    // pretend the string doesn't exist
        // UpdateSmbiosString(SmbiosTableN, &SmbiosTableN.Raw[cmp_idx], GetSmbiosString(SmbiosTableN, ref_str));
        break;
      }
    }
  }
}

/* Patching Functions */
VOID PatchTableType0()
{
  // BIOS information
	//
	SmbiosTable = GetSmbiosTableFromType (EntryPoint, EFI_SMBIOS_TYPE_BIOS_INFORMATION, 0);
	if (SmbiosTable.Raw == NULL) {
//		DBG("SmbiosTable: Type 0 (Bios Information) not found!\n");
		return;
	}
	TableSize = SmbiosTableLength(SmbiosTable);
	ZeroMem((VOID*)newSmbiosTable.Type0, MAX_TABLE_SIZE);
	CopyMem((VOID*)newSmbiosTable.Type0, (VOID*)SmbiosTable.Type0, TableSize); //can't point to union
/* Real Mac
 BIOS Information (Type 0)
 Raw Data:
 Header and Data:
 00 18 2E 00 01 02 00 00 03 7F 80 98 01 00 00 00
 00 00 C1 02 00 01 FF FF
 Strings:
 Apple Inc.
 MBP81.88Z.0047.B22.1109281426
 09/28/11

 */
	newSmbiosTable.Type0->BiosSegment = 0; //like in Mac
	newSmbiosTable.Type0->SystemBiosMajorRelease = 0;
	newSmbiosTable.Type0->SystemBiosMinorRelease = 1;
//	newSmbiosTable.Type0->BiosCharacteristics.BiosCharacteristicsNotSupported = 0;
//	newSmbiosTable.Type0->BIOSCharacteristicsExtensionBytes[1] |= 8; //UefiSpecificationSupported;
	//Slice: ----------------------
	//there is a bug in AppleSMBIOS-42 v1.7
	//to eliminate this I have to zero first byte in the field
	*(UINT8*)&newSmbiosTable.Type0->BiosCharacteristics = 0;
	//dunno about latest version but there is a way to set good characteristics
	//if use patched AppleSMBIOS
	//----------------
	Once = TRUE;

  UniquifySmbiosTableStr(newSmbiosTable, SMBIOS_TABLE_TYPE0_STR_IDX);

	if(iStrLen(gSettings.VendorName, 64)>0){
		UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type0->Vendor, gSettings.VendorName);
	}
	if(iStrLen(gSettings.RomVersion, 64)>0){
		UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type0->BiosVersion, gSettings.RomVersion);
	}
	if(iStrLen(gSettings.ReleaseDate, 64)>0){
		UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type0->BiosReleaseDate, gSettings.ReleaseDate);
	}

	Handle = LogSmbiosTable(newSmbiosTable);
}

VOID GetTableType1()
{
  CHAR8* s;
  // System Information
  //
	SmbiosTable = GetSmbiosTableFromType (EntryPoint, EFI_SMBIOS_TYPE_SYSTEM_INFORMATION, 0);
	if (SmbiosTable.Raw == NULL) {
		DBG("SmbiosTable: Type 1 (System Information) not found!\n");
		return;
	}

	CopyMem((VOID*)&gSettings.SmUUID, (VOID*)&SmbiosTable.Type1->Uuid, 16);
	s = GetSmbiosString(SmbiosTable, SmbiosTable.Type1->ProductName);
	CopyMem(gSettings.OEMProduct, s, iStrLen(s, 64) + 1); //take ending zero

	return;
}


VOID PatchTableType1()
{
  // System Information
  //
	SmbiosTable = GetSmbiosTableFromType (EntryPoint, EFI_SMBIOS_TYPE_SYSTEM_INFORMATION, 0);
	if (SmbiosTable.Raw == NULL) {
		return;
	}

	//Increase table size
	Size = SmbiosTable.Type1->Hdr.Length; //old size
	TableSize = SmbiosTableLength(SmbiosTable); //including strings
	NewSize = 27; //sizeof(SMBIOS_TABLE_TYPE1);
	ZeroMem((VOID*)newSmbiosTable.Type1, MAX_TABLE_SIZE);
	CopyMem((VOID*)newSmbiosTable.Type1, (VOID*)SmbiosTable.Type1, Size); //copy main table
	CopyMem((CHAR8*)newSmbiosTable.Type1+NewSize, (CHAR8*)SmbiosTable.Type1+Size, TableSize - Size); //copy strings
	newSmbiosTable.Type1->Hdr.Length = (UINT8)NewSize;

  UniquifySmbiosTableStr(newSmbiosTable, SMBIOS_TABLE_TYPE1_STR_IDX);

	newSmbiosTable.Type1->WakeUpType = SystemWakeupTypePowerSwitch;
	Once = TRUE;

	if((gSettings.SmUUID.Data3 & 0xF000) != 0) {
		CopyMem((VOID*)&newSmbiosTable.Type1->Uuid, (VOID*)&gSettings.SmUUID, 16);
	}

	if(iStrLen(gSettings.ManufactureName, 64)>0){
		UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type1->Manufacturer, gSettings.ManufactureName);
	}
	if(iStrLen(gSettings.ProductName, 64)>0){
		UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type1->ProductName, gSettings.ProductName);
	}
	if(iStrLen( gSettings.VersionNr, 64)>0){
		UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type1->Version, gSettings.VersionNr);
	}
	if(iStrLen(gSettings.SerialNr, 64)>0){
		UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type1->SerialNumber, gSettings.SerialNr);
	}
	if(iStrLen(gSettings.BoardNumber, 64)>0){
		UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type1->SKUNumber, gSettings.BoardNumber); //iMac17,1 - there is nothing
	}
	if(iStrLen(gSettings.FamilyName, 64)>0){
		UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type1->Family, gSettings.FamilyName);
	}

	Handle = LogSmbiosTable(newSmbiosTable);
	return;
}

VOID GetTableType2()
{
  CHAR8* s;
  // System Information
  //
	SmbiosTable = GetSmbiosTableFromType (EntryPoint, EFI_SMBIOS_TYPE_BASEBOARD_INFORMATION, 0);
	if (SmbiosTable.Raw == NULL) {
		return;
	}

  s = GetSmbiosString(SmbiosTable, SmbiosTable.Type2->ProductName);
  CopyMem(gSettings.OEMBoard, s, iStrLen(s, 64) + 1);
  s = GetSmbiosString(SmbiosTable, SmbiosTable.Type2->Manufacturer);
	CopyMem(gSettings.OEMVendor, s, iStrLen(s, 64) + 1);

}


VOID PatchTableType2()
{
  // BaseBoard Information
	//
	NewSize = 0x10; //sizeof(SMBIOS_TABLE_TYPE2);
	ZeroMem((VOID*)newSmbiosTable.Type2, MAX_TABLE_SIZE);

	SmbiosTable = GetSmbiosTableFromType (EntryPoint, EFI_SMBIOS_TYPE_BASEBOARD_INFORMATION, 0);
	if (SmbiosTable.Raw == NULL) {
		MsgLog("SmbiosTable: Type 2 (BaseBoard Information) not found, create new\n");
		//Create new one
		newSmbiosTable.Type2->Hdr.Type = 2;
		newSmbiosTable.Type2->Hdr.Handle = 0x0200; //common rule

	} else {
		Size = SmbiosTable.Type2->Hdr.Length; //old size
		TableSize = SmbiosTableLength(SmbiosTable); //including strings

		if (NewSize > Size) {
			CopyMem((VOID*)newSmbiosTable.Type2, (VOID*)SmbiosTable.Type2, Size); //copy main table
			CopyMem((CHAR8*)newSmbiosTable.Type2 + NewSize, (CHAR8*)SmbiosTable.Type2 + Size, TableSize - Size); //copy strings
		} else {
			CopyMem((VOID*)newSmbiosTable.Type2, (VOID*)SmbiosTable.Type2, TableSize); //copy full table
		}
	}

	newSmbiosTable.Type2->Hdr.Length = (UINT8)NewSize;
	newSmbiosTable.Type2->ChassisHandle = mHandle3;	//from GetTableType3
	newSmbiosTable.Type2->BoardType = gSettings.BoardType;
	ZeroMem((VOID*)&newSmbiosTable.Type2->FeatureFlag, sizeof(BASE_BOARD_FEATURE_FLAGS));
	newSmbiosTable.Type2->FeatureFlag.Motherboard = 1;
	newSmbiosTable.Type2->FeatureFlag.Replaceable = 1;
	if (gSettings.BoardType == 11) {
		newSmbiosTable.Type2->FeatureFlag.Removable = 1;
	}
	Once = TRUE;

  UniquifySmbiosTableStr(newSmbiosTable, SMBIOS_TABLE_TYPE2_STR_IDX);

	if(iStrLen(gSettings.BoardManufactureName, 64)>0){
		UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type2->Manufacturer, gSettings.BoardManufactureName);
	}
	if(iStrLen(gSettings.BoardNumber, 64)>0){
		UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type2->ProductName, gSettings.BoardNumber);
	}
	if(iStrLen( gSettings.BoardVersion, 64)>0){
		UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type2->Version, gSettings.BoardVersion); //iMac17,1 - there is ProductName
	}
	if(iStrLen(gSettings.BoardSerialNumber, 64)>0){
		UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type2->SerialNumber, gSettings.BoardSerialNumber);
	}
	if(iStrLen(gSettings.LocationInChassis, 64)>0){
		UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type2->LocationInChassis, gSettings.LocationInChassis);
	}
	//what about Asset Tag??? Not used in real mac. till now.

	//Slice - for the table2 one patch more needed
	/* spec
	Field 0x0E - Identifies the number (0 to 255) of Contained Object Handles that follow
	Field 0x0F - A list of handles of other structures (for example, Baseboard, Processor, Port, System Slots, Memory Device) that are contained by this baseboard
	It may be good before our patching but changed after. We should at least check if all tables mentioned here are present in final structure
	 I just set 0 as in iMac11
	*/
	newSmbiosTable.Type2->NumberOfContainedObjectHandles = 0;

	Handle = LogSmbiosTable(newSmbiosTable);
	return;
}

VOID GetTableType3()
{
  // System Chassis Information
  //
	SmbiosTable = GetSmbiosTableFromType (EntryPoint, EFI_SMBIOS_TYPE_SYSTEM_ENCLOSURE, 0);
	if (SmbiosTable.Raw == NULL) {
		DBG("SmbiosTable: Type 3 (System Chassis Information) not found!\n");
		gMobile = FALSE; //default value
		return;
	}
	mHandle3 = SmbiosTable.Type3->Hdr.Handle;
	gMobile = ((SmbiosTable.Type3->Type) >= 8) && (SmbiosTable.Type3->Type != 0x0D); //iMac is desktop!

	return;
}

VOID PatchTableType3()
{
  // System Chassis Information
  //
	SmbiosTable = GetSmbiosTableFromType (EntryPoint, EFI_SMBIOS_TYPE_SYSTEM_ENCLOSURE, 0);
	if (SmbiosTable.Raw == NULL) {
//		DBG("SmbiosTable: Type 3 (System Chassis Information) not found!\n");
		return;
	}
	Size = SmbiosTable.Type3->Hdr.Length; //old size
	TableSize = SmbiosTableLength(SmbiosTable); //including strings
	NewSize = 0x15; //sizeof(SMBIOS_TABLE_TYPE3);
	ZeroMem((VOID*)newSmbiosTable.Type3, MAX_TABLE_SIZE);

	if (NewSize > Size) {
		CopyMem((VOID*)newSmbiosTable.Type3, (VOID*)SmbiosTable.Type3, Size); //copy main table
		CopyMem((CHAR8*)newSmbiosTable.Type3 + NewSize, (CHAR8*)SmbiosTable.Type3 + Size, TableSize - Size); //copy strings
		newSmbiosTable.Type3->Hdr.Length = (UINT8)NewSize;
	} else {
		CopyMem((VOID*)newSmbiosTable.Type3, (VOID*)SmbiosTable.Type3, TableSize); //copy full table
	}

	newSmbiosTable.Type3->BootupState = ChassisStateSafe;
	newSmbiosTable.Type3->PowerSupplyState = ChassisStateSafe;
	newSmbiosTable.Type3->ThermalState = ChassisStateOther;
	newSmbiosTable.Type3->SecurityStatus = ChassisSecurityStatusOther; //ChassisSecurityStatusNone;
	newSmbiosTable.Type3->NumberofPowerCords = 1;
	newSmbiosTable.Type3->ContainedElementCount = 0;
	newSmbiosTable.Type3->ContainedElementRecordLength = 0;
	Once = TRUE;

  UniquifySmbiosTableStr(newSmbiosTable, SMBIOS_TABLE_TYPE3_STR_IDX);

	if (gSettings.ChassisType != 0) {
		newSmbiosTable.Type3->Type = gSettings.ChassisType;
	}

	if(iStrLen(gSettings.ChassisManufacturer, 64)>0){
		UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type3->Manufacturer, gSettings.ChassisManufacturer);
	}
//SIC! According to iMac there must be the BoardNumber
	if(iStrLen(gSettings.BoardNumber, 64)>0){
		UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type3->Version, gSettings.BoardNumber);
	}
	if(iStrLen(gSettings.SerialNr, 64)>0){
		UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type3->SerialNumber, gSettings.SerialNr);
	}
	if(iStrLen(gSettings.ChassisAssetTag, 64)>0){
		UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type3->AssetTag, gSettings.ChassisAssetTag);
	}

	Handle = LogSmbiosTable(newSmbiosTable);
	return;
}

VOID GetTableType4()
{
  // Processor Information
  //
	INTN res = 0;

	SmbiosTable = GetSmbiosTableFromType (EntryPoint, EFI_SMBIOS_TYPE_PROCESSOR_INFORMATION, 0);
	if (SmbiosTable.Raw == NULL) {
		DBG("SmbiosTable: Type 4 (Processor Information) not found!\n");
		return;
	}

	res = (SmbiosTable.Type4->ExternalClock * 3 + 2) % 100;
	if (res > 2) {
		res = 0;
	} else {
		res = SmbiosTable.Type4->ExternalClock % 10;
	}

	gCPUStructure.ExternalClock = (UINT32)((SmbiosTable.Type4->ExternalClock * 1000) + (res * 110));//MHz->kHz

	//UnicodeSPrint(gSettings.BusSpeed, 10, L"%d", gCPUStructure.ExternalClock);
	//gSettings.BusSpeed = gCPUStructure.ExternalClock; //why duplicate??
	gCPUStructure.CurrentSpeed = SmbiosTable.Type4->CurrentSpeed;
	gCPUStructure.MaxSpeed = SmbiosTable.Type4->MaxSpeed;

	if (Size > 0x23) {  //Smbios <=2.3
		gSettings.EnabledCores = SmbiosTable.Type4->EnabledCoreCount;
	} else {
		gSettings.EnabledCores = 0; //to change later
	}

	//UnicodeSPrint(gSettings.CpuFreqMHz, 10, L"%d", gCPUStructure.CurrentSpeed);
	//gSettings.CpuFreqMHz = gCPUStructure.CurrentSpeed;

	return;
}

VOID PatchTableType4()
{
  // Processor Information
  //
	UINTN               AddBrand = 0;
	CHAR8               BrandStr[48];
	UINT16							ProcChar = 0;
  CHAR8               *SocketDesignationMac = "U2E1";

	//Note. iMac11,2 has four tables for CPU i3
	UINTN		CpuNumber;

	CopyMem(BrandStr, gCPUStructure.BrandString, 48);
	BrandStr[47] = '\0';
//	DBG("BrandString=%a\n", BrandStr);
	for (CpuNumber = 0; CpuNumber < gCPUStructure.Cores; CpuNumber++) {
		// Get Table Type4
		SmbiosTable = GetSmbiosTableFromType (EntryPoint, EFI_SMBIOS_TYPE_PROCESSOR_INFORMATION, CpuNumber);
		if (SmbiosTable.Raw == NULL) {
			break;
		}
		// we make SMBios v2.4 while it may be older so we have to increase size
		Size = SmbiosTable.Type4->Hdr.Length; //old size
		TableSize = SmbiosTableLength(SmbiosTable); //including strings
		AddBrand = 0;
		if (SmbiosTable.Type4->ProcessorVersion == 0) { //if no BrandString we can add
			AddBrand = 48;
		}
		NewSize = sizeof(SMBIOS_TABLE_TYPE4);
		ZeroMem((VOID*)newSmbiosTable.Type4, MAX_TABLE_SIZE);
		CopyMem((VOID*)newSmbiosTable.Type4, (VOID*)SmbiosTable.Type4, Size); //copy main table
		CopyMem((CHAR8*)newSmbiosTable.Type4+NewSize, (CHAR8*)SmbiosTable.Type4+Size, TableSize - Size); //copy strings
		newSmbiosTable.Type4->Hdr.Length = (UINT8)NewSize;

		newSmbiosTable.Type4->MaxSpeed = (UINT16)gCPUStructure.MaxSpeed;
		//old version has no such fields. Fill now
		if (Size <= 0x20){
			//sanity check and clear
			newSmbiosTable.Type4->SerialNumber = 0;
			newSmbiosTable.Type4->AssetTag = 0;
			newSmbiosTable.Type4->PartNumber = 0;
		}
		if (Size <= 0x23) {  //Smbios <=2.3
			newSmbiosTable.Type4->CoreCount = gCPUStructure.Cores;
			newSmbiosTable.Type4->ThreadCount = gCPUStructure.Threads;
			newSmbiosTable.Type4->ProcessorCharacteristics = (UINT16)gCPUStructure.Features;
		} //else we propose DMI data is better then cpuid().
//		if (newSmbiosTable.Type4->CoreCount < newSmbiosTable.Type4->EnabledCoreCount) {
//			newSmbiosTable.Type4->EnabledCoreCount = gCPUStructure.Cores;
//		}

        //DBG("insert ExternalClock: %d MHz\n", (INT32)(DivU64x32(gCPUStructure.ExternalClock, kilo)));
        newSmbiosTable.Type4->ExternalClock = (UINT16)DivU64x32 (gCPUStructure.ExternalClock, kilo);
		newSmbiosTable.Type4->EnabledCoreCount = gSettings.EnabledCores;
    //some verifications
    if ((newSmbiosTable.Type4->ThreadCount < newSmbiosTable.Type4->CoreCount) ||
        newSmbiosTable.Type4->ThreadCount > newSmbiosTable.Type4->CoreCount * 2) {
      newSmbiosTable.Type4->ThreadCount = gCPUStructure.Threads;
    }

    UniquifySmbiosTableStr(newSmbiosTable, SMBIOS_TABLE_TYPE4_STR_IDX);

		// TODO: Set SmbiosTable.Type4->ProcessorFamily for all implemented CPU models
		Once = TRUE;
		if (gCPUStructure.Model == CPU_MODEL_ATOM) {
			newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelAtom;
		}
		if ((gCPUStructure.Model == CPU_MODEL_DOTHAN) ||
        (gCPUStructure.Model == CPU_MODEL_YONAH)) {
			if (gCPUStructure.Mobile) {
        newSmbiosTable.Type4->ProcessorUpgrade = ProcessorUpgradeSocket478;
				newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelCoreSoloMobile;
				if (gCPUStructure.Cores == 2) {
					newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelCoreDuoMobile;
				}
			} else {
				newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelCoreSolo;
				if (gCPUStructure.Cores == 2) {
					newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelCoreDuo;
				}
			}
		}
		if (gCPUStructure.Model == CPU_MODEL_MEROM) {
			if (gCPUStructure.Mobile) {
				if (gCPUStructure.Cores==2) {
					newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelCore2DuoMobile;
				}
				if (gCPUStructure.Cores==1) {
					newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelCore2SoloMobile;
				}
			} else {  // Conroe
        newSmbiosTable.Type4->ProcessorUpgrade = ProcessorUpgradeSocketLGA775;
				if (gCPUStructure.Cores>2) {
					newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelCore2Quad;
				} else
          if (gCPUStructure.Cores==2) {
					newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelCore2Extreme;
				}
				if (gCPUStructure.Cores==1) {
					newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelCore2Solo;
				}
			}
		}
		if (gCPUStructure.Model == CPU_MODEL_PENRYN) {
			if (gCPUStructure.Mobile) {
				if (gCPUStructure.Cores>2) {
					newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelCore2ExtremeMobile;
				}
				if (gCPUStructure.Cores==2) {
					newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelCore2DuoMobile;
				}

			} else {
        newSmbiosTable.Type4->ProcessorUpgrade = ProcessorUpgradeSocketLGA775;
				if (gCPUStructure.Cores>2) {
					newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelCore2Quad;
				} else if (gCPUStructure.Cores==2) {
					newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelCore2;
				} else {
					newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelCore2Solo;
        }
        if (AsciiStrStr(gCPUStructure.BrandString, "Celeron")) {
          newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyDualCoreIntelCeleron;
        } else if (AsciiStrStr(gCPUStructure.BrandString, "Extreme")) {
          newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelCore2Extreme;
        }
			}
    }
     if (gCPUStructure.Model >= CPU_MODEL_NEHALEM) {
       if (AsciiStrStr(gCPUStructure.BrandString, "i3"))
     	  newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelCoreI3;
       if (AsciiStrStr(gCPUStructure.BrandString, "i5"))
     	  newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelCoreI5;
       if (AsciiStrStr(gCPUStructure.BrandString, "i7"))
         newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelCoreI7;
	  }
		//spec 2.7 page 48 note 3
		if ((newSmbiosTable.Type4->ProcessorFamily == ProcessorFamilyIntelCore2)
			&& gCPUStructure.Mobile) {
				newSmbiosTable.Type4->ProcessorFamily = ProcessorFamilyIntelCore2DuoMobile;
		}

		// Set CPU Attributes
		newSmbiosTable.Type4->L1CacheHandle = L1;
		newSmbiosTable.Type4->L2CacheHandle = L2;
		newSmbiosTable.Type4->L3CacheHandle = L3;
		newSmbiosTable.Type4->ProcessorType = CentralProcessor;
		newSmbiosTable.Type4->ProcessorId.Signature.ProcessorSteppingId = gCPUStructure.Stepping;
		newSmbiosTable.Type4->ProcessorId.Signature.ProcessorModel		= (gCPUStructure.Model & 0xF);
		newSmbiosTable.Type4->ProcessorId.Signature.ProcessorFamily		= gCPUStructure.Family;
		newSmbiosTable.Type4->ProcessorId.Signature.ProcessorType		= gCPUStructure.Type;
		newSmbiosTable.Type4->ProcessorId.Signature.ProcessorXModel		= gCPUStructure.Extmodel;
		newSmbiosTable.Type4->ProcessorId.Signature.ProcessorXFamily	= gCPUStructure.Extfamily;
//    CopyMem((VOID*)&newSmbiosTable.Type4->ProcessorId.FeatureFlags, (VOID*)&gCPUStructure.Features, 4);
//		newSmbiosTable.Type4->ProcessorId.FeatureFlags = (PROCESSOR_FEATURE_FLAGS)(UINT32)gCPUStructure.Features;
		if (Size <= 0x26) {
			newSmbiosTable.Type4->ProcessorFamily2 = newSmbiosTable.Type4->ProcessorFamily;
			ProcChar |= (gCPUStructure.ExtFeatures & CPUID_EXTFEATURE_EM64T)?0x04:0;
			ProcChar |= (gCPUStructure.Cores > 1)?0x08:0;
			ProcChar |= (gCPUStructure.Cores < gCPUStructure.Threads)?0x10:0;
			ProcChar |= (gCPUStructure.ExtFeatures & CPUID_EXTFEATURE_XD)?0x20:0;
			ProcChar |= (gCPUStructure.Features & CPUID_FEATURE_VMX)?0x40:0;
			ProcChar |= (gCPUStructure.Features & CPUID_FEATURE_EST)?0x80:0;
			newSmbiosTable.Type4->ProcessorCharacteristics = ProcChar;
		}

    UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type4->Socket, SocketDesignationMac);

		if (AddBrand) {
			UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type4->ProcessorVersion, BrandStr);
		}

		UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type4->AssetTag, BrandStr); //like mac
		// looks to be MicroCode revision
		if(gCPUStructure.MicroCode > 0){
      AsciiSPrint(BrandStr, 20, "%X", gCPUStructure.MicroCode);
			UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type4->SerialNumber, BrandStr);
		}

		Handle = LogSmbiosTable(newSmbiosTable);
	}
	return;
}

VOID PatchTableType7()
{
  // Cache Information
  //
    //TODO - should be separate table for each CPU core
    //new handle for each core and attach Type4 tables for individual Type7
    // Handle = 0x0700 + CoreN<<2 + CacheN (4-level cache is supported
    // L1[CoreN] = Handle

	CHAR8 SSocketD[9];
	BOOLEAN correctSD = FALSE;

	//according to spec for Smbios v2.0 max handle is 0xFFFE, for v>2.0 (we made 2.6) max handle=0xFEFF.
	// Value 0xFFFF means no cache
	L1 = 0xFFFF; // L1 Cache
	L2 = 0xFFFF; // L2 Cache
	L3 = 0xFFFF; // L3 Cache

	// Get Table Type7 and set CPU Caches
	for (Index = 0; Index < MAX_CACHE_COUNT; Index++) {
		SmbiosTable = GetSmbiosTableFromType (EntryPoint, EFI_SMBIOS_TYPE_CACHE_INFORMATION, Index);
		if (SmbiosTable.Raw == NULL) {
			break;
		}
		TableSize = SmbiosTableLength(SmbiosTable);
		ZeroMem((VOID*)newSmbiosTable.Type7, MAX_TABLE_SIZE);
		CopyMem((VOID*)newSmbiosTable.Type7, (VOID*)SmbiosTable.Type7, TableSize);
		correctSD = (newSmbiosTable.Type7->SocketDesignation == 0);
		CoreCache = newSmbiosTable.Type7->CacheConfiguration & 3;
		Once = TRUE;

		//SSocketD = "L1-Cache";
    CopyMem(SSocketD, "L1-Cache", 9);
		if(correctSD) {
			SSocketD[1] = (CHAR8)(0x31 + CoreCache);
			UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type7->SocketDesignation, SSocketD);
		}
		Handle = LogSmbiosTable(newSmbiosTable);
		switch (CoreCache) {
			case 0:
				L1 = Handle;
				break;
			case 1:
				L2 = Handle;
				break;
			case 2:
				L3 = Handle;
				break;
			default:
				break;
		}
	}

	return;
}

VOID PatchTableType9()
{
	//
	// System Slots (Type 9)
	/*
	 SlotDesignation: PCI1
	 System Slot Type: PCI
	 System Slot Data Bus Width:  32 bit
	 System Slot Current Usage:  Available
	 System Slot Length:  Short length
	 System Slot Type: PCI
	 Slot Id: the value present in the Slot Number field of the PCI Interrupt Routing table entry that is associated with this slot is: 1
	 Slot characteristics 1:  Provides 3.3 Volts |  Slot's opening is shared with another slot, e.g. PCI/EISA shared slot. |
	 Slot characteristics 2:  PCI slot supports Power Management Enable (PME#) signal |
	 SegmentGroupNum: 0x4350
	 BusNum: 0x49
	 DevFuncNum: 0x31

	 Real Mac always contain Airport table 9 as
	 09 0D xx xx 01 A5 08 03 03 00 00 04 06 "AirPort"
	 */
  //usage in macOS:
  // SlotID == value of Name(_SUN, SlotID) 8bit
  // SlotDesignation == name to "AAPL,slot-name"
  // SlotType = 32bit PCI/SlotTypePciExpressX1/x4/x16
  // real PC -> PCI, real Mac -> PCIe

  for (Index = 0; Index < 15; Index++) {
    if (SlotDevices[Index].Valid) {
      INTN Dev, Func;
      ZeroMem((VOID*)newSmbiosTable.Type9, MAX_TABLE_SIZE);
      newSmbiosTable.Type9->Hdr.Type = EFI_SMBIOS_TYPE_SYSTEM_SLOTS;
      newSmbiosTable.Type9->Hdr.Length = sizeof(SMBIOS_TABLE_TYPE9);
      newSmbiosTable.Type9->Hdr.Handle = (UINT16)(0x0900 + Index);
      newSmbiosTable.Type9->SlotDesignation = 1;
      newSmbiosTable.Type9->SlotType = SlotDevices[Index].SlotType;
      newSmbiosTable.Type9->SlotDataBusWidth = SlotDataBusWidth1X;
      newSmbiosTable.Type9->CurrentUsage = SlotUsageAvailable;
      newSmbiosTable.Type9->SlotLength = SlotLengthShort;
      newSmbiosTable.Type9->SlotID = SlotDevices[Index].SlotID;
      newSmbiosTable.Type9->SlotCharacteristics1.Provides33Volts = 1;
      newSmbiosTable.Type9->SlotCharacteristics2.HotPlugDevicesSupported = 1;
      // take this from PCI bus for WiFi card
      newSmbiosTable.Type9->SegmentGroupNum = SlotDevices[Index].SegmentGroupNum;
      newSmbiosTable.Type9->BusNum = SlotDevices[Index].BusNum;
      newSmbiosTable.Type9->DevFuncNum = SlotDevices[Index].DevFuncNum;
      //
      Dev = SlotDevices[Index].DevFuncNum >> 3;
      Func = SlotDevices[Index].DevFuncNum & 7;
      DBG("insert table 9 for dev %x:%x\n", Dev, Func);
      UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type9->SlotDesignation, SlotDevices[Index].SlotName);
      LogSmbiosTable(newSmbiosTable);
    }
  }

	return;
}

VOID PatchTableType11()
{
	CHAR8		*OEMString = "Apple inc. uses Clover"; //something else here?
  // System Information
  //
	SmbiosTable = GetSmbiosTableFromType (EntryPoint, EFI_SMBIOS_TYPE_OEM_STRINGS, 0);
	if (SmbiosTable.Raw != NULL) {
		MsgLog("Table 11 present, but rewritten for us\n");
	}
//	TableSize = SmbiosTableLength(SmbiosTable);
	ZeroMem((VOID*)newSmbiosTable.Type11, MAX_TABLE_SIZE);
//	CopyMem((VOID*)newSmbiosTable.Type11, (VOID*)SmbiosTable.Type11, 5); //minimum, other bytes = 0
	newSmbiosTable.Type11->Hdr.Type = EFI_SMBIOS_TYPE_OEM_STRINGS;
	newSmbiosTable.Type11->Hdr.Length = sizeof(SMBIOS_STRUCTURE)+2;
	newSmbiosTable.Type11->Hdr.Handle = 0x0B00; //common rule

	newSmbiosTable.Type11->StringCount = 1;
	UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type11->StringCount, OEMString);

	LogSmbiosTable(newSmbiosTable);
	return;
}

VOID PatchTableTypeSome()
{
	//some unused but interesting tables. Just log as is
#define NUM_OTHER_TYPES 13
	UINT8 tableTypes[NUM_OTHER_TYPES] = {8, 10, 13, 18, 21, 22, 27, 28, 32, 33, 129, 217, 219};
	UINTN	IndexType;
	//
	// Different types
	for (IndexType = 0; IndexType < NUM_OTHER_TYPES; IndexType++) {
		for (Index = 0; Index < 32; Index++) {
			SmbiosTable = GetSmbiosTableFromType(EntryPoint, tableTypes[IndexType], Index);
			if (SmbiosTable.Raw == NULL) {
				continue;
			}
			LogSmbiosTable(SmbiosTable);
		}
	}
	return;
}

VOID GetTableType16()
{
  for ( Index = 0 ; Index < 8 ; ++Index ) {

    SmbiosTable = GetSmbiosTableFromType( EntryPoint, EFI_SMBIOS_TYPE_PHYSICAL_MEMORY_ARRAY, Index );

    if ( SmbiosTable.Raw ) {
      DBG("Type 16 Index = %d\n", Index);
      gTotalMemoryDevices += SmbiosTable.Type16->NumberOfMemoryDevices;
    }
    else {
      DBG("SmBiosTable: Type 16 (Physical Memory Array) found %d devices.\n", gTotalMemoryDevices );
      continue;
    }

  }

  if ( ( ! gTotalMemoryDevices )
    || ( gTotalMemoryDevices > MAX_RAM_SLOTS ) ) {
    gTotalMemoryDevices = MAX_RAM_SLOTS;
  }
  DBG("Total Memory Slots Count = %d\n", gTotalMemoryDevices);
}


VOID PatchTableType16()
{
  SmbiosTable = GetSmbiosTableFromType (EntryPoint, EFI_SMBIOS_TYPE_PHYSICAL_MEMORY_ARRAY, 0);

  if ( SmbiosTable.Raw ) {

    ZeroMem((VOID*)newSmbiosTable.Type16, MAX_TABLE_SIZE);

    // Table 69 – Physical Memory Array (Type 16) structure

    // Physical Memory Array type

    // Length of the structure.
    //
    // * 0x0F for version 2.1
    // * 0x17 for version 2.7 and later
    //
    TableSize = SmbiosTableLength(SmbiosTable);
    CopyMem((VOID*)newSmbiosTable.Type16, (VOID*)SmbiosTable.Type16, TableSize);

    // Handle, or instance number, associated with the structure
    newSmbiosTable.Type16->Hdr.Handle = mHandle16;

    // Physical location of the Memory Array, whether on the system board or
    // an add-in board.
    //
    // 7.17.1 Memory Array — Location
    // See available types in MEMORY_ARRAY_LOCATION in SmBios.h
    //
    newSmbiosTable.Type16->Location = MemoryArrayLocationSystemBoard;

    // Function for which the array is used See 7.17.2 for definitions.
    //
    // 7.17.2 Memory Array — Use
    // See available types in MEMORY_ARRAY_USE in SmBios.h
    //
    newSmbiosTable.Type16->Use = MemoryArrayUseSystemMemory;

    // Primary hardware error correction or detection method supported by this
    // memory array.
    //
    // 7.17.3 Memory Array — Error Correction Types
    // See available types in MEMORY_ERROR_CORRECTION in SmBios.h
    //
    newSmbiosTable.Type16->MemoryErrorCorrection = MemoryErrorCorrectionMultiBitEcc;

    // Maximum memory capacity, in kilobytes, for this array
    //
    // * If the capacity is not represented in this field, then this field
    //   contains 0x8000 0x0000 and the Extended Maximum Capacity field should
    //   be used.
    // * Values 2 TB (0x8000 0x0000) or greater must be represented in the
    //   Extended Maximum Capacity field.
    newSmbiosTable.Type16->MaximumCapacity = 0x20000000; // 512 GB

    // Handle, or instance number, associated with any error that was previously
    // detected for the array.
    //
    // * If the system does not provide the error information structure,
    //   the field contains FFFEh; otherwise, the field contains either 0xFFFF
    //   (if no error was detected) or the handle of the error-information
    //   structure.
    //
    // See 7.18.4 and 7.34.
    //
    newSmbiosTable.Type16->MemoryErrorInformationHandle = SMBIOS_MEMORY_OK;

    // Number of slots or sockets available for Memory Devices in this array
    // This value represents the number of Memory Device structures that
    // compose this Memory Array. Each Memory Device has a reference to the
    // “owning” Memory Array.
    //
    newSmbiosTable.Type16->NumberOfMemoryDevices = gTotalMemoryDevices;

    // Maximum memory capacity, in bytes, for this array This field is only
    // valid when the Maximum Capacity field contains 8000 0000h. When
    // Maximum Capacity contains a value that is not 8000 0000h, Extended
    // Maximum Capacity must contain zeros.
    newSmbiosTable.Type16->ExtendedMaximumCapacity = 0x0;

    DBG("NumberOfMemoryDevices = %d\n", gTotalMemoryDevices);
    LogSmbiosTable(newSmbiosTable);
  }
  else {
    DBG("SmbiosTable: Type 16 (Physical Memory Array) not found!\n");
    return;
  }

}

VOID GetTableType17()
{
  DBG("GetTableType17()\n");

  // Get Table Type17 and count Size
  for ( Index = 0; Index < gTotalMemoryDevices; ++Index ) {

    SmbiosTable = GetSmbiosTableFromType(EntryPoint, EFI_SMBIOS_TYPE_MEMORY_DEVICE, Index);

    if ( SmbiosTable.Raw ) {

      DBG("Type 17 Index = %d\n", Index);

      if (SmbiosTable.Type17->MemoryErrorInformationHandle < 0xFFFE) {
        DBG("GetTableType18: Table has error informationn (%x), checking\n", SmbiosTable.Type17->MemoryErrorInformationHandle);
        GetTableType18();
      }

    }
    else {
			DBG("SmbiosTable: Type 17 (Memory Device number %d) not found!\n", Index);
      continue;
    }

  }
}

VOID GetTableType18()
{
  // 7.19 32 Bit Error Information (Type 18)

  // 7.19.1 Memory Error - Type
  // See available types in MEMORY_ERROR_TYPE in SmBios.h


  // 7.19.2 Memory Error - Granularity
  // See available types in MEMORY_ERROR_GRANULARITY in SmBios.h

  // 7.19.3 Memory Error - Operation
  // See available types in MEMORY_ERROR_OPERATION in SmBios.h

  BOOLEAN Found = FALSE;

  INTN Index;
  for ( Index = 0; Index < 24; ++Index ) {

    newSmbiosTable = GetSmbiosTableFromType (EntryPoint, EFI_SMBIOS_TYPE_32BIT_MEMORY_ERROR_INFORMATION, Index);

    if (newSmbiosTable.Raw ) {

      if (newSmbiosTable.Type18->Hdr.Handle == SmbiosTable.Type17->MemoryErrorInformationHandle) {
        Found = TRUE;
        DBG("Found memory information in table 18/%d, type=0x%x, operation=0x%x syndrome=0x%x\n", Index,
            newSmbiosTable.Type18->ErrorType,
            newSmbiosTable.Type18->ErrorOperation,
            newSmbiosTable.Type18->VendorSyndrome);
        switch (newSmbiosTable.Type18->ErrorType) {
          case MemoryErrorOk:
            DBG("...memory OK\n");
            break;
          case MemoryErrorCorrected:
            DBG("...memory errors corrected\n");
            break;
          case MemoryErrorChecksum:
            DBG("...error type: Checksum\n");
            break;
          default:
            DBG("...error type not shown\n");
            break;
        }
        break;
      }
    }
    else {
      continue;
    }

    if ( Found
     && ( newSmbiosTable.Type18->ErrorType != MemoryErrorOk )
     && ( newSmbiosTable.Type18->ErrorType != MemoryErrorCorrected ) ) {
      DBG("Module has uncorrected memory errors.\n");
      continue;
    }

  }

}

VOID PatchTableType17()
{
  UINT8   channelMap[MAX_RAM_SLOTS];
  UINT8   channels = 4;

  channelMap[0] = 0;
  channelMap[1] = 4;
  channelMap[2] = 1;
  channelMap[3] = 5;
  channelMap[4] = 2;
  channelMap[5] = 6;
  channelMap[6] = 3;
  channelMap[7] = 7;

  Once = TRUE;

  for (Index = 0; Index < gTotalMemoryDevices; Index++) {

    DBG("Patching SMBIOS Table 17 record for Index %d\n", Index);

    CHAR8   deviceLocator[10];
    CHAR8   bankLocator[10];
    UINTN   SMBIOSIndex = channelMap[Index];

    SmbiosTable = GetSmbiosTableFromType( EntryPoint, EFI_SMBIOS_TYPE_MEMORY_DEVICE, SMBIOSIndex );

    ZeroMem( (VOID*)newSmbiosTable.Type17, MAX_TABLE_SIZE );

    if ( SmbiosTable.Type17->TotalWidth ) {

      DBG( "RAM Slot %d in use.\n", Index );

      /********** Start Slot **********/

      /*

          Ordered in parallel with SMBIOS Reference Specification 3.1.1

          https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_3.1.1.pdf

      */

      // Table 73 – Memory Device (Type 17) structure

      // Length of the structure.
      //
      // * 0x15 for version 2.1
      // * 0x1B for version 2.3
      // * 0x1C for version 2.6
      // * 0x22 for version 2.7
      // * 0x28 for version 2.8
      //
      TableSize = SmbiosTableLength(SmbiosTable);
      CopyMem( (VOID*)newSmbiosTable.Type17, (VOID *)SmbiosTable.Type17, TableSize );

      // Handle, or instance number, associated with the structure.
      //
      newSmbiosTable.Type17->Hdr.Handle = (UINT16)(mHandle16 + 0x100 + Index);
      // Asher: how do we know offset of 0x100 or starting base for mHandle16 of 0x1000?

      // Handle, or instance number, associated with the Physical Memory Array
      // to which this device belongs
      //
      newSmbiosTable.Type17->MemoryArrayHandle = mHandle16;

      // Handle, or instance number, associated with any error that was previously detected for the device.
      //
      // If the system does not provide the error information structure, the field contains 0xFFFE;
      // otherwise, the field contains either 0xFFFF (if no error was detected) or the handle of the
      // error-information structure.
      //
      // See 7.18.4 and 7.34.
      //
      newSmbiosTable.Type17->MemoryErrorInformationHandle = SMBIOS_MEMORY_OK;

      // Total width, in bits, of this memory device, including any check or error-correction bits.
      //
      // If there are no error-correction bits, this value should be equal to Data Width.
      // If the width is unknown, the field is set to FFFFh.
      //
      newSmbiosTable.Type17->TotalWidth = 0x48;

      // Data width, in bits, of this memory device.
      //
      // A Data Width of 0 and a Total Width of 8 indicates that the device is being used solely
      // to provide 8 error-correction bits.
      //
      // If the width is unknown, the field is set to FFFFh.
      //
      newSmbiosTable.Type17->DataWidth = 0x40;

      // Size of the memory device.
      //
      // * If the value is 0, no memory device is installed in the socket.
      // * If the size is unknown, the field value is 0xFFFF.
      // * If the size is 32 GB-1 MB or greater, the field value is 0x7FFF
      //   and the actual size is stored in the Extended Size field.
      //
      // The granularity in which the value is specified depends on the setting
      // of the most-significant bit (bit 15).
      //
      // * If the bit is 0, the value is specified in megabyte units.
      // * If the bit is 1, the value is specified in kilobyte units.
      //
      // For example:
      //
      //  The value 0x8100 identifies 256 KB memory device and
      //  0x0100 identifies a 256 MB memory device.
      //
      newSmbiosTable.Type17->Size         = 0x7FFF;

      // 7.18.1
      // See available types in MEMORY_FORM_FACTOR in SmBios.h
      newSmbiosTable.Type17->FormFactor = MemoryFormFactorDimm;

      // String number of the string that identifies the physically-labeled
      // socket or board position where the memory device is located
      //
      // Example: “SIMM 3”
      //
      AsciiSPrint(deviceLocator, 10, "DIMM_%d", Index % channels);
      UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type17->DeviceLocator, (CHAR8*)&deviceLocator[0]);

      // String number of the string that identifies the physically labeled bank
      // where the memory device is located
      //
      // Example: “Bank 0” or “A”
      //
      AsciiSPrint(bankLocator, 10, "BANK_%d", Index / channels);
      UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type17->BankLocator, (CHAR8*)&bankLocator[0]);

      // Identifies when the Memory Device is one of a set of Memory Devices
      // that must be populated with all devices of the same type and size,
      // and the set to which this device belongs.
      //
      // * A value of 0x00 indicates that the device is not part of a set;
      // * A value of 0xFF indicates that the attribute is unknown.
      //
      // NOTE: A Device Set number must be unique within the context of the
      //       Memory Array containing this Memory Device.
      //
      newSmbiosTable.Type17->DeviceSet = 0x01;

      // 7.18.2
      // See available types in MEMORY_DEVICE_TYPE in SmBios.h
      newSmbiosTable.Type17->MemoryType = MemoryTypeDdr4;

      // 7.18.3
      // See available bits in MEMORY_DEVICE_TYPE_DETAIL
      newSmbiosTable.Type17->TypeDetail.Synchronous = TRUE;
      newSmbiosTable.Type17->TypeDetail.LrDimm = TRUE;

      // 7.18.4
      // Speed in MT/s
      newSmbiosTable.Type17->Speed = 2400;

      // String number for the manufacturer of this memory device
      //
      UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type17->Manufacturer, "Nemix");

      // String number for the serial number of this memory device.
      //
      // This value is set by the manufacturer and normally is not changeable.
      //
      CHAR8   serial[10];
      AsciiSPrint(serial, 10, "0000000%d", Index);
      UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type17->SerialNumber, serial );

      // String number for the asset tag of this memory device.
      newSmbiosTable.Type17->AssetTag = 0;
      UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type17->AssetTag, "64GB LRDIMM");

      // String number for the part number of this memory device.
      //
      // This value is set by the manufacturer and normally is not changeable.
      UpdateSmbiosString(newSmbiosTable, &newSmbiosTable.Type17->PartNumber,  serial);

      // Attributes
      //
      // Bits 7-4: reserved
      // Bits 3-0: rank
      //
      // Value=0 for unknown rank information
      //
      newSmbiosTable.Type17->Attributes = 0x04;

      // 7.18.5
      // Sizes > 32GB specified here, in MB, in tandem with value of 0x7fff for Size.
      //
      // Bit 31 is reserved for future use and must be set to 0.
      // Bits 30:0 represent the size of the memory device in megabytes.
      //
      // Examples:
      //
      //   32GB => 0x08000
      //   64GB => 0x10000
      //  128GB => 0x20000
      //
      newSmbiosTable.Type17->ExtendedSize = 0x10000;

      // Configured Memory Clock Speed
      //
      // Identifies the configured speed of the memory device,
      // in megatransfers per second (MT/s).
      //
      // * 0x0000 = the speed is unknown.
      // * 0xFFFF = reserved for future use.
      //
      // See 7.18.4 for details.
      //
      newSmbiosTable.Type17->ConfiguredMemoryClockSpeed = newSmbiosTable.Type17->Speed;

      // Minimum operating voltage for this device, in millivolts.
      //
      // If the value is 0, the voltage is unknown.
      //
      newSmbiosTable.Type17->MinimumVoltage = 0x4B0;

      // Maximum operating voltage for this device, in millivolts.
      //
      // If the value is 0, the voltage is unknown.
      //
      newSmbiosTable.Type17->MaximumVoltage = 0x4B0;

      // Configured operating voltage for this device, in millivolts.
      //
      // If the value is 0, the voltage is unknown.
      //
      newSmbiosTable.Type17->ConfiguredVoltage = 0x4B0;

      /********** End Slot **********/

      mTotalSystemMemory += newSmbiosTable.Type17->ExtendedSize; //Mb
      mMemory17[ Index ] = mTotalSystemMemory;

      AsciiSPrint(gSettings.MemorySpeed, 64, "%d", newSmbiosTable.Type17->Speed);

      DBG("SmbiosTable.Type17->Speed = %dMHz\n", newSmbiosTable.Type17->Speed);
      DBG("SmbiosTable.Type17->ExtendedSize = %dMB\n", newSmbiosTable.Type17->ExtendedSize );
      DBG("SmbiosTable.Type17->Bank/Device = %a %a\n", GetSmbiosString(newSmbiosTable, newSmbiosTable.Type17->BankLocator), GetSmbiosString(newSmbiosTable, newSmbiosTable.Type17->DeviceLocator));
      DBG("SmbiosTable.Type17->Vendor = %a\n", GetSmbiosString(newSmbiosTable, newSmbiosTable.Type17->Manufacturer) );
      DBG("SmbiosTable.Type17->SerialNumber = %a\n", GetSmbiosString(newSmbiosTable, newSmbiosTable.Type17->SerialNumber) );
      DBG("SmbiosTable.Type17->AssetTag = %a\n", GetSmbiosString(newSmbiosTable, newSmbiosTable.Type17->AssetTag) );
      DBG("SmbiosTable.Type17->PartNumber = %a\n", GetSmbiosString(newSmbiosTable, newSmbiosTable.Type17->PartNumber) );

      DBG("%a %a %dMHz %dMB\n", bankLocator, deviceLocator, newSmbiosTable.Type17->Speed, newSmbiosTable.Type17->ExtendedSize);
    }
    else {

      DBG( "RAM Slot %d is empty.\n", Index );

      newSmbiosTable.Type17->Hdr.Type = EFI_SMBIOS_TYPE_MEMORY_DEVICE;
      newSmbiosTable.Type17->Hdr.Length = sizeof(SMBIOS_TABLE_TYPE17);
      newSmbiosTable.Type17->TotalWidth = SMBIOS_UNKONWN;
      newSmbiosTable.Type17->DataWidth  = SMBIOS_UNKONWN;
    }

    mHandle17[ Index ] = LogSmbiosTable(newSmbiosTable);

  }

  DBG("mTotalSystemMemory = %d\n", mTotalSystemMemory);
}

VOID
PatchTableType19 ()
{
	//
	// Generate Memory Array Mapped Address info (TYPE 19)
	//
	ZeroMem((VOID*)newSmbiosTable.Type19, MAX_TABLE_SIZE);

  // One table should be created for each set of memory devices.
  // Interleaved meory devices form a single contiguous region.

	newSmbiosTable.Type19->Hdr.Type = EFI_SMBIOS_TYPE_MEMORY_ARRAY_MAPPED_ADDRESS;

  // Length of the structure
  // * 0x0F for version 2.1
  // * 0x1F for version 2.7 and later.
  //
	newSmbiosTable.Type19->Hdr.Length = sizeof(SMBIOS_TABLE_TYPE19);

  // Handle, or instance number, associated with the structure
	newSmbiosTable.Type19->Hdr.Handle = (19 << 8) + 0;

  // Starting Address
  //
  // Physical address, in kilobytes, of a range of memory mapped to the
  // specified Physical Memory Array
  //
  // When the field value is 0xFFFF 0xFFFF, the actual address is stored in the
  // Extended Starting Address field. When this field contains a valid address,
  // Ending Address must also contain a valid address.
  //
  // When this field contains 0xFFFF 0xFFFF, Ending Address must also contain
  // 0xFFFF 0xFFFF.
  //
  newSmbiosTable.Type19->StartingAddress = 0x0;

  // Ending Address
  //
  // Physical ending address of the last kilobyte of a range of addresses
  // mapped to the specified Physical Memory Array
  //
  // * When the field value is 0xFFFF 0xFFFF and the Starting Address field also
  //   contains 0xFFFF 0xFFF, the actual address is stored in the Extended
  //   Ending Address field.
  // * When this field contains a valid address, Starting Address must also
  //   contain a valid address.
  newSmbiosTable.Type19->EndingAddress = 0x8000000; // 128 GB in KB

  // Handle, or instance number, associated with the Physical Memory Array to
  // which this address range is mapped.
  //
  // Multiple address ranges can be mapped to a single Physical Memory Array.
  //
	newSmbiosTable.Type19->MemoryArrayHandle = mHandle16;

  // Number of Memory Devices that form a single row of memory for the address
  // partition defined by this structure
	newSmbiosTable.Type19->PartitionWidth = 0x02;

  // Extended Starting Address
  //
  // Physical address, in bytes, of a range of memory mapped to the specified
  // Physical Memory Array
  //
  // * This field is valid when Starting Address contains the value 0xFFFF 0xFFFF.
  // * If Starting Address contains a value other than 0xFFFF 0xFFFF, this field
  //   contains zeros.
  // * When this field contains a valid address, Extended
  //   Ending Address must also contain a valid address.
  //
  newSmbiosTable.Type19->ExtendedStartingAddress = 0x0;

  // Extended Ending Address
  //
  // Physical ending address, in bytes, of the last of a range of addresses
  // mapped to the specified Physical Memory Array
  //
  // * This field is valid when both Starting Address and Ending Address contain
  //   the value 0xFFFF 0xFFFF.
  // * If Ending Address contains a value other than 0xFFFF 0xFFFF,
  //   this field contains zeros.
  // * When this field contains a valid address, Extended Starting Address
  //   must also contain a valid address.
  //
  newSmbiosTable.Type19->ExtendedEndingAddress = 0x0;

	mHandle19 = LogSmbiosTable(newSmbiosTable);
}

VOID PatchTableType20 ()
{
      // 7.20 Mapped Addresses

// 	UINTN	j = 0, k = 0, m = 0;
// 	//
// 	// Generate Memory Array Mapped Address info (TYPE 20)
// 	// not needed neither for Apple nor for EFI
// 	m = 0;
// 	for (Index = 0; Index < gTotalMemoryDevices; Index++) {
// 		SmbiosTable = GetSmbiosTableFromType (EntryPoint, EFI_SMBIOS_TYPE_MEMORY_DEVICE_MAPPED_ADDRESS, Index);
// 		if (SmbiosTable.Raw == NULL) {
// 			return ;
// 		}
// 		TableSize = SmbiosTableLength(SmbiosTable);
// 		ZeroMem((VOID*)newSmbiosTable.Type20, MAX_TABLE_SIZE);
// 		CopyMem((VOID*)newSmbiosTable.Type20, (VOID*)SmbiosTable.Type20, TableSize);
// 		for (j=0; j < gTotalMemoryDevices; j++) {
// 			//EndingAddress in kb while mMemory in Mb
// 			if ((UINT32)(mMemory17[j] << 10) > newSmbiosTable.Type20->EndingAddress) {
// 				newSmbiosTable.Type20->MemoryDeviceHandle = mHandle17[j];
// 				k = newSmbiosTable.Type20->EndingAddress;
//         m += mMemory17[j];
// 				DBG("Type20[%d]->End = 0x%x, Type17[%d] = 0x%x\n",
// 						Index, k, j, m);
// //				DBG(" MemoryDeviceHandle = 0x%x\n", newSmbiosTable.Type20->MemoryDeviceHandle);
// 				mMemory17[j] = 0; // used
// 				break;
// 			}
// 		//	DBG("\n");
// 		}
//
// 		newSmbiosTable.Type20->MemoryArrayMappedAddressHandle = mHandle19;
// 		//
// 		// Record Smbios Type 20
// 		//
// 		LogSmbiosTable(newSmbiosTable);
// 	}
	return ;
}

VOID GetTableType32()
{
		SmbiosTable = GetSmbiosTableFromType (EntryPoint, EFI_SMBIOS_TYPE_SYSTEM_BOOT_INFORMATION, 0);
		if (SmbiosTable.Raw == NULL) {
			return;
		}
		gBootStatus = SmbiosTable.Type32->BootStatus;
}

/**
 * Apple Specific Structures.
 * Firmware Table, FirmwareVolume (TYPE 128).
 */
VOID PatchTableType128()
{
  /**
   * Useful information.
   *
   * FW_REGION_RESERVED   = 0,
   * FW_REGION_RECOVERY   = 1,
   * FW_REGION_MAIN       = 2,
   * gHob->MemoryAbove1MB.PhysicalStart + ResourceLength or fix as 0x200000 - 0x600000,
   * FW_REGION_NVRAM      = 3,
   * FW_REGION_CONFIG     = 4,
   * FW_REGION_DIAGVAULT  = 5.
   */

  // Get the existing 128 table if exists.
  //SmbiosTable = GetSmbiosTableFromType (EntryPoint, 128, 0);

  // initialise new table
	ZeroMem((VOID*)newSmbiosTable.Type128, MAX_TABLE_SIZE);

  // common rules
	newSmbiosTable.Type128->Hdr.Type = 128;
	newSmbiosTable.Type128->Hdr.Length = sizeof(SMBIOS_TABLE_TYPE128);
  newSmbiosTable.Type128->Hdr.Handle = 0x8000;

  // set firmware-features, example: 0x80001417, imac11,2 -> 0x1403.
  newSmbiosTable.Type128->FirmwareFeatures = gFwFeatures;
  // set firmware-features mask
  newSmbiosTable.Type128->FirmwareFeaturesMask = gFwFeaturesMask;

  /**
   * TODO: I have an idea that region should be the same as Efivar.bin
   * @author Slice
	 */
	newSmbiosTable.Type128->RegionCount = 1;
	newSmbiosTable.Type128->RegionType[0] = FW_REGION_MAIN;
		//UpAddress = mTotalSystemMemory << 20; //Mb -> b
  //gHob->MemoryAbove1MB.PhysicalStart;
	newSmbiosTable.Type128->FlashMap[0].StartAddress = 0xFFE00000; //0xF0000;
  //gHob->MemoryAbove1MB.PhysicalStart + gHob->MemoryAbove1MB.ResourceLength - 1;
	newSmbiosTable.Type128->FlashMap[0].EndAddress = 0xFFEFFFFF;
  //newSmbiosTable.Type128->RegionType[1] = FW_REGION_NVRAM; //Efivar
  //newSmbiosTable.Type128->FlashMap[1].StartAddress = 0x15000; //0xF0000;
  //newSmbiosTable.Type128->FlashMap[1].EndAddress = 0x1FFFF;
	//region type=1 also present in mac

  // log the new, 128, sbmios  table
	LogSmbiosTable(newSmbiosTable);
	return ;
}

VOID PatchTableType130()
{
	//
	// MemorySPD (TYPE 130)
	// TODO:  read SPD and place here. But for a what?
	//
	SmbiosTable = GetSmbiosTableFromType (EntryPoint, 130, 0);
	if (SmbiosTable.Raw == NULL) {
		return ;
	}
	//
	// Log Smbios Record Type130
	//
	LogSmbiosTable(SmbiosTable);
	return ;
}



VOID PatchTableType131()
{
	// Get Table Type131
	SmbiosTable = GetSmbiosTableFromType (EntryPoint, 131, 0);
	if (SmbiosTable.Raw != NULL) {
    MsgLog("Table 131 is present, CPUType=%x\n", SmbiosTable.Type131->ProcessorType);
    MsgLog("Change to: %x\n", gSettings.CpuType);
  }

		ZeroMem((VOID*)newSmbiosTable.Type131, MAX_TABLE_SIZE);
		newSmbiosTable.Type131->Hdr.Type = 131;
		newSmbiosTable.Type131->Hdr.Length = sizeof(SMBIOS_STRUCTURE)+2;
		newSmbiosTable.Type131->Hdr.Handle = 0x8300; //common rule
		// Patch ProcessorType
			newSmbiosTable.Type131->ProcessorType = gSettings.CpuType;
		Handle = LogSmbiosTable(newSmbiosTable);
		return;
}

VOID PatchTableType132()
{
	if (!gSettings.SetTable132) {
		//DBG("disabled Table 132\n");
		return;
	}

	// Get Table Type132
	SmbiosTable = GetSmbiosTableFromType (EntryPoint, 132, 0);
	if (SmbiosTable.Raw != NULL) {
		MsgLog("Table 132 is present, QPI=%x\n", SmbiosTable.Type132->ProcessorBusSpeed);
		MsgLog("Change to: %x\n", gSettings.QPI);
	}

	ZeroMem((VOID*)newSmbiosTable.Type132, MAX_TABLE_SIZE);
	newSmbiosTable.Type132->Hdr.Type = 132;
	newSmbiosTable.Type132->Hdr.Length = sizeof(SMBIOS_STRUCTURE)+2;
	newSmbiosTable.Type132->Hdr.Handle = 0x8400; //ugly

	// Patch ProcessorBusSpeed
	if(gSettings.QPI){
		newSmbiosTable.Type132->ProcessorBusSpeed = gSettings.QPI;
	} else {
		newSmbiosTable.Type132->ProcessorBusSpeed = (UINT16)(LShiftU64(DivU64x32(gCPUStructure.ExternalClock, kilo), 2));
	}

	Handle = LogSmbiosTable(newSmbiosTable);
	return;
}

VOID PatchTableType133()
{
  if (gPlatformFeature == 0xFFFF) {
    return;
  }
  // Get Table Type133
  SmbiosTable = GetSmbiosTableFromType (EntryPoint, 133, 0);
  if (SmbiosTable.Raw != NULL) {
    MsgLog("Table 133 is present, PlatformFeature=%x\n", SmbiosTable.Type133->PlatformFeature);
    MsgLog("Change to: %x\n", gPlatformFeature);
  }
  ZeroMem((VOID*)newSmbiosTable.Type133, MAX_TABLE_SIZE);
  newSmbiosTable.Type133->Hdr.Type = 133;
  newSmbiosTable.Type133->Hdr.Length = sizeof(SMBIOS_STRUCTURE)+8;
  newSmbiosTable.Type133->Hdr.Handle = 0x8500; //ugly
//  newSmbiosTable.Type133->PlatformFeature = gPlatformFeature;
  CopyMem((VOID*)&newSmbiosTable.Type133->PlatformFeature, (VOID*)&gPlatformFeature, 8);
  Handle = LogSmbiosTable(newSmbiosTable);
  return;
}

EFI_STATUS PrepatchSmbios()
{
	EFI_STATUS				Status = EFI_SUCCESS;
	UINTN					BufferLen;
	EFI_PHYSICAL_ADDRESS     BufferPtr;
//	UINTN					Index;
  DbgHeader("Get Smbios");

	// Get SMBIOS Tables
	Smbios = FindOemSMBIOSPtr();
//	DBG("OEM SMBIOS EPS=%p\n", Smbios);
//	DBG("OEM Tables = %x\n", ((SMBIOS_TABLE_ENTRY_POINT*)Smbios)->TableAddress);
	if (!Smbios) {
		Status = EFI_NOT_FOUND;
//		DBG("Original SMBIOS System Table not found! Getting from Hob...\n");
		Smbios = GetSmbiosTablesFromHob();
//		DBG("HOB SMBIOS EPS=%p\n", Smbios);
		if (!Smbios) {
//			DBG("And here SMBIOS System Table not found! Trying System table ...\n");
			// this should work on any UEFI
			Smbios = GetSmbiosTablesFromConfigTables();
//			DBG("ConfigTables SMBIOS EPS=%p\n", Smbios);
			if (!Smbios) {
//				DBG("And here SMBIOS System Table not found! Exiting...\n");
				return EFI_NOT_FOUND;
			}
		}
	}

	//original EPS and tables
	EntryPoint = (SMBIOS_TABLE_ENTRY_POINT*)Smbios; //yes, it is old SmbiosEPS
//	Smbios = (VOID*)(UINT32)EntryPoint->TableAddress; // here is flat Smbios database. Work with it
	//how many we need to add for tables 128, 130, 131, 132 and for strings?
	BufferLen = 0x20 + EntryPoint->TableLength + 64 * 10;
	//new place for EPS and tables. Allocated once for both
	BufferPtr = EFI_SYSTEM_TABLE_MAX_ADDRESS;
	Status = gBS->AllocatePages (AllocateMaxAddress, EfiACPIMemoryNVS, /*EfiACPIReclaimMemory, 	*/
	EFI_SIZE_TO_PAGES(BufferLen), &BufferPtr);
	if (EFI_ERROR (Status)) {
//		DBG("There is error allocating pages in EfiACPIMemoryNVS!\n");
		Status = gBS->AllocatePages (AllocateMaxAddress,  /*EfiACPIMemoryNVS, */EfiACPIReclaimMemory,
											   ROUND_PAGE(BufferLen)/EFI_PAGE_SIZE, &BufferPtr);
		if (EFI_ERROR (Status)) {
//			DBG("There is error allocating pages in EfiACPIReclaimMemory!\n");
		}
	}
//	DBG("Buffer @ %p\n", BufferPtr);
	if (BufferPtr) {
		SmbiosEpsNew = (SMBIOS_TABLE_ENTRY_POINT *)(UINTN)BufferPtr; //this is new EPS
	} else {
		SmbiosEpsNew = EntryPoint; //is it possible?!
	}
	ZeroMem(SmbiosEpsNew, BufferLen);
//	DBG("New EntryPoint = %p\n", SmbiosEpsNew);
	NumberOfRecords = 0;
	MaxStructureSize = 0;
	//preliminary fill EntryPoint with some data
	CopyMem ((VOID *)SmbiosEpsNew, (VOID *)EntryPoint, sizeof(SMBIOS_TABLE_ENTRY_POINT));


	Smbios = (VOID*)(SmbiosEpsNew + 1); //this is a C-language trick. I hate it but use. +1 means +sizeof(SMBIOS_TABLE_ENTRY_POINT)
	Current = (UINT8*)Smbios; //begin fill tables from here
	SmbiosEpsNew->TableAddress = (UINT32)(UINTN)Current;
	SmbiosEpsNew->EntryPointLength = sizeof(SMBIOS_TABLE_ENTRY_POINT); // no matter on other versions
	SmbiosEpsNew->MajorVersion = 2;
	SmbiosEpsNew->MinorVersion = 4;
	SmbiosEpsNew->SmbiosBcdRevision = 0x24; //Slice - we want to have v2.6 but Apple still uses 2.4

	//Collect information for use in menu
	GetTableType1();
	GetTableType2();
	GetTableType3();
	GetTableType4();
	GetTableType16();
  DBG("Getting Smbios Table 17.\n");
	GetTableType17();
	GetTableType18();
	GetTableType32(); //get BootStatus here to decide what to do
	MsgLog("Boot status=%x\n", gBootStatus);
  //for example the bootloader may go to Recovery is BootStatus is Fail
	return 	Status;
}

VOID PatchSmbios(VOID) //continue
{

  DbgHeader("PatchSmbios");

  newSmbiosTable.Raw = (UINT8*)AllocateZeroPool(MAX_TABLE_SIZE);
	//Slice - order of patching is significant
	PatchTableType0();
	PatchTableType1();
	PatchTableType2();
	PatchTableType3();
	PatchTableType7(); //we should know handles before patch Table4
	PatchTableType4();
//	PatchTableType6();
	PatchTableType9();
 // PatchTableType11();
	PatchTableTypeSome();
	PatchTableType17();
   PatchTableType16();
	PatchTableType19();
	PatchTableType20();
	PatchTableType128();
	PatchTableType130();
	PatchTableType131();
	PatchTableType132();
    PatchTableType133();
	AddSmbiosEndOfTable();
	if(MaxStructureSize > MAX_TABLE_SIZE){
//    DBG("Too long SMBIOS!\n");
	}
	FreePool((VOID*)newSmbiosTable.Raw);

	// there is no need to keep all tables in numeric order. It is not needed
// neither by specs nor by AppleSmbios.kext
}

VOID FinalizeSmbios() //continue
{
	EFI_PEI_HOB_POINTERS	GuidHob;
	EFI_PEI_HOB_POINTERS	HobStart;
	EFI_PHYSICAL_ADDRESS    *Table = NULL;
	//UINTN					TableLength = 0;
  BOOLEAN FoundTable3 = FALSE;

	// Get Hob List
	HobStart.Raw = GetHobList ();

	if (HobStart.Raw != NULL) {
		// find SMBIOS in hob
		for (Index = 0; Index < sizeof (gTableGuidArray) / sizeof (*gTableGuidArray); ++Index) {
			GuidHob.Raw = GetNextGuidHob (gTableGuidArray[Index], HobStart.Raw);
			if (GuidHob.Raw != NULL) {
				Table = GET_GUID_HOB_DATA (GuidHob.Guid);
				//TableLength = GET_GUID_HOB_DATA_SIZE (GuidHob);
				if (Table != NULL) {
          if (Index != 0) {
            FoundTable3 = TRUE;
            DBG("Found SMBIOS3 Table\n");
          }
					break;
				}
			}
		}
	}

	//
	// Install SMBIOS in Config table
	SmbiosEpsNew->TableLength = (UINT16)((UINT32)(UINTN)Current - (UINT32)(UINTN)Smbios);
	SmbiosEpsNew->NumberOfSmbiosStructures = NumberOfRecords;
	SmbiosEpsNew->MaxStructureSize = MaxStructureSize;
	SmbiosEpsNew->IntermediateChecksum = 0;
	SmbiosEpsNew->IntermediateChecksum = (UINT8)(256 - Checksum8((UINT8*)SmbiosEpsNew + 0x10, SmbiosEpsNew->EntryPointLength - 0x10));
	SmbiosEpsNew->EntryPointStructureChecksum = 0;
	SmbiosEpsNew->EntryPointStructureChecksum = (UINT8)(256 - Checksum8((UINT8*)SmbiosEpsNew, SmbiosEpsNew->EntryPointLength));
//	DBG("SmbiosEpsNew->EntryPointLength = %d\n", SmbiosEpsNew->EntryPointLength);
//	DBG("DMI checksum = %d\n", Checksum8((UINT8*)SmbiosEpsNew, SmbiosEpsNew->EntryPointLength));

    //
    // syscl: one more step: check if we need remap SMBIOS Table Type 1 Guid
    //
    // to fix Dell's SMBIOS truncate credit David Passmore
    //
    if (gRemapSmBiosIsRequire)
    {
        //
        // syscl: remap smbios table 1 guid
        //
        DBG("Remap smbios table type 1 guid.\n");
        gBS->InstallConfigurationTable (&gRemapEfiSmbiosTableGuid, (VOID*)SmbiosEpsNew);
    }
    else
    {
        //
        // use origin smbios guid table
        //
        DBG("Use origin smbios table type 1 guid.\n");
        gBS->InstallConfigurationTable (&gEfiSmbiosTableGuid, (VOID*)SmbiosEpsNew);
    }

	gST->Hdr.CRC32 = 0;
	gBS->CalculateCrc32 ((UINT8 *) &gST->Hdr, gST->Hdr.HeaderSize, &gST->Hdr.CRC32);

	//
	// Fix it in Hob list
	//
	// No smbios in Hob list on Aptio, so no need to update it there.
	// But even if it would be there, loading of macOS would overwrite it
	// since this list on my board is inside space needed for kernel
	// (ha! like many other UEFI stuff).
	// It's enough to add it to Conf.table.
	//
	if (Table != NULL) {
		//PauseForKey(L"installing SMBIOS in Hob\n");
		*Table = (UINT32)(UINTN)SmbiosEpsNew;
	}
//	egSaveFile(NULL, L"EFI\\CLOVER\\misc\\smbios.bin", (UINT8*)(UINTN)SmbiosEpsNew, SmbiosEpsNew->TableLength);
	return;
}
