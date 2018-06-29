/*
Copyright (c) 2010 - 2011, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

*/

#include "entry_scan.h"
#include "device_tree.h"
#include "kernel_patcher.h"
#include <Protocol/AppleEvent.h>

#define PATCH_DEBUG 0
#define MEM_DEB 0

#if PATCH_DEBUG
#define DBG(...)	Print(__VA_ARGS__);
#else
#define DBG(...)
#endif

EFI_EVENT   mVirtualAddressChangeEvent = NULL;
EFI_EVENT   OnReadyToBootEvent = NULL;
EFI_EVENT   ExitBootServiceEvent = NULL;
EFI_EVENT   mSimpleFileSystemChangeEvent = NULL;
EFI_HANDLE  mHandle = NULL;


extern EFI_GUID gAppleEventProtocolGuid;

STATIC APPLE_EVENT_PROTOCOL mAppleEventProtocol = {
  1,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

VOID
EFIAPI
OnExitBootServices(IN EFI_EVENT Event, IN VOID *Context)
{
  if (gCPUStructure.Vendor == CPU_VENDOR_INTEL &&
      (gCPUStructure.Family == 0x06 && gCPUStructure.Model >= CPU_MODEL_SANDY_BRIDGE)
       ) {
    UINT64 msr = 0;

    msr = AsmReadMsr64(MSR_PKG_CST_CONFIG_CONTROL); //0xE2
    //  AsciiPrint("MSR 0xE2 on Exit BS %08x\n", msr);

  }

  gST->ConOut->OutputString (gST->ConOut, L"+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
	//
	// Patch kernel and kexts if needed
	//
	KernelAndKextsPatcherStart((LOADER_ENTRY *)Context);
	
	if (gSettings.USBFixOwnership) {
		// Note: blocks on Aptio
//		DisableUsbLegacySupport();
    FixOwnership();
	}
  // Unlock boot screen
  // apianti - This may cause issues since it frees memory, there's
  //  no need to free it at this point since it was all allocated as
  //  boot memory so it will be gone anyway after exit boot services
  /*
  if (EFI_ERROR(Status = UnlockBootScreen())) {
    DBG("Failed to unlock boot screen!\n");
  }
  // */
}

VOID
EFIAPI
OnReadyToBoot (
               IN      EFI_EVENT   Event,
               IN      VOID        *Context
               )
{
//
  if ((gCPUStructure.Vendor == CPU_VENDOR_INTEL &&
       (gCPUStructure.Family == 0x06 && gCPUStructure.Model >= CPU_MODEL_SANDY_BRIDGE)
       )) {
    UINT64 msr = 0;

    msr = AsmReadMsr64(MSR_PKG_CST_CONFIG_CONTROL); //0xE2

  }
//  AsciiPrint("MSR 0xE2 on ReadyToBoot %08x\n", msr);

}

VOID
EFIAPI
VirtualAddressChangeEvent (
                           IN EFI_EVENT  Event,
                           IN VOID       *Context
                           )
{
//  EfiConvertPointer (0x0, (VOID **) &mProperty);
//  EfiConvertPointer (0x0, (VOID **) &mSmmCommunication);
}

VOID
EFIAPI
OnSimpleFileSystem (
                    IN      EFI_EVENT  Event,
                    IN      VOID       *Context
                    )
{
	EFI_TPL		OldTpl;

	OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
	gEvent = 1;
	//  ReinitRefitLib();
	//ScanVolumes();
	//enter GUI
	// DrawMenuText(L"OnSimpleFileSystem", 0, 0, UGAHeight-40, 1);
	// MsgLog("OnSimpleFileSystem occured\n");

	gBS->RestoreTPL (OldTpl);

}

EFI_STATUS
GuiEventsInitialize ()
{
	EFI_STATUS				Status;
	EFI_EVENT				Event;
	VOID*				  	RegSimpleFileSystem = NULL;

	gEvent = 0;
	Status = gBS->CreateEvent (
							   EVT_NOTIFY_SIGNAL,
							   TPL_NOTIFY,
							   OnSimpleFileSystem,
							   NULL,
							   &Event);
	if(!EFI_ERROR(Status))
	{
		Status = gBS->RegisterProtocolNotify (
											  &gEfiSimpleFileSystemProtocolGuid,
											  Event,
											  &RegSimpleFileSystem);
	}


	return Status;
}

EFI_STATUS
EventsInitialize (IN LOADER_ENTRY *Entry)
{
	EFI_STATUS			Status;
	VOID*           Registration = NULL;

	//
	// Register the event to reclaim variable for OS usage.
	//
	//EfiCreateEventReadyToBoot(&OnReadyToBootEvent);
	/*  EfiCreateEventReadyToBootEx (
	 TPL_NOTIFY,
	 OnReadyToBoot,
	 NULL,
	 &OnReadyToBootEvent
	 );  */

	//
	// Register notify for exit boot services
	//
	Status = gBS->CreateEvent (EVT_SIGNAL_EXIT_BOOT_SERVICES,
							   TPL_CALLBACK,
							   OnExitBootServices,
							   Entry,
							   &ExitBootServiceEvent);

	if(!EFI_ERROR(Status))
	{
		/*Status = */gBS->RegisterProtocolNotify (
                 &gEfiStatusCodeRuntimeProtocolGuid,
                 ExitBootServiceEvent,
                 &Registration);
	}



	//
	// Register the event to convert the pointer for runtime.
	//
    /*
	 gBS->CreateEventEx (
	 EVT_NOTIFY_SIGNAL,
	 TPL_NOTIFY,
	 VirtualAddressChangeEvent,
	 NULL,
	 &gEfiEventVirtualAddressChangeGuid,
	 &mVirtualAddressChangeEvent
	 );
     */
  Status = gBS->InstallProtocolInterface(&mHandle, &gAppleEventProtocolGuid, EFI_NATIVE_INTERFACE, &mAppleEventProtocol);
	// and what if EFI_ERROR?
	return Status;
}

EFI_STATUS EjectVolume(IN REFIT_VOLUME *Volume)
{
	EFI_SCSI_IO_PROTOCOL            *ScsiIo = NULL;
	EFI_SCSI_IO_SCSI_REQUEST_PACKET CommandPacket;
//	UINT64                          Lun = 0;
//	UINT8                           *Target;
//	UINT8                           TargetArray[EFI_SCSI_TARGET_MAX_BYTES];
	EFI_STATUS                      Status; // = EFI_UNSUPPORTED;
	UINT8                           Cdb[EFI_SCSI_OP_LENGTH_SIX];
	USB_MASS_DEVICE                 *UsbMass = NULL;
	EFI_BLOCK_IO_PROTOCOL           *BlkIo	= NULL;
	EFI_BLOCK_IO_MEDIA              *Media;
	UINT32                          Timeout;
	UINT32                          CmdResult;

	//
	// Initialize SCSI REQUEST_PACKET and 6-byte Cdb
	//
	ZeroMem (&CommandPacket, sizeof (EFI_SCSI_IO_SCSI_REQUEST_PACKET));
	ZeroMem (Cdb, EFI_SCSI_OP_LENGTH_SIX);

	Status = gBS->HandleProtocol(Volume->DeviceHandle, &gEfiScsiIoProtocolGuid, (VOID **) &ScsiIo);
	if (ScsiIo) {
//		Target = &TargetArray[0];
//		ScsiIo->GetDeviceLocation (ScsiIo, &Target, &Lun);


		Cdb[0]  = EFI_SCSI_OP_START_STOP_UNIT;
//		Cdb[1]  = (UINT8) (LShiftU64 (Lun, 5) & EFI_SCSI_LOGICAL_UNIT_NUMBER_MASK);
//		Cdb[1] |= 0x01;
    Cdb[1]  = 0x01;
		Cdb[4]  = ATA_CMD_SUBOP_EJECT_DISC;
		CommandPacket.Timeout = EFI_TIMER_PERIOD_SECONDS (3);
		CommandPacket.Cdb = Cdb;
		CommandPacket.CdbLength = (UINT8) sizeof (Cdb);

		Status = ScsiIo->ExecuteScsiCommand (ScsiIo, &CommandPacket, NULL);
	} else {
		Status = gBS->HandleProtocol(Volume->DeviceHandle, &gEfiBlockIoProtocolGuid, (VOID **) &BlkIo);
		if (BlkIo) {
			UsbMass = USB_MASS_DEVICE_FROM_BLOCK_IO (BlkIo);
			if (!UsbMass) {
				MsgLog("no UsbMass\n");
				Status = EFI_NOT_FOUND;
				goto ON_EXIT;
			}
			Media   = &UsbMass->BlockIoMedia;
			if (!Media) {
				MsgLog("no BlockIoMedia\n");
				Status = EFI_NO_MEDIA;
				goto ON_EXIT;
			}

			//
			// If it is a removable media, such as CD-Rom or Usb-Floppy,
			// need to detect the media before each read/write. While some of
			// Usb-Flash is marked as removable media.
			//
      //TODO - DetectMedia will appear automatically. Do nothing?
			if (!Media->RemovableMedia) {
				//Status = UsbBootDetectMedia (UsbMass);
        //	if (EFI_ERROR (Status)) {
				Status = EFI_UNSUPPORTED;
        goto ON_EXIT;
        //	}
			}

			if (!(Media->MediaPresent)) {
				Status = EFI_NO_MEDIA;
				goto ON_EXIT;
			}
      //TODO - remember previous state
      /*		if (MediaId != Media->MediaId) {
       Status = EFI_MEDIA_CHANGED;
       goto ON_EXIT;
       }*/

			Timeout = USB_BOOT_GENERAL_CMD_TIMEOUT;
			Cdb[0]  = EFI_SCSI_OP_START_STOP_UNIT;
	//		Cdb[1]  = (UINT8) (USB_BOOT_LUN(UsbMass->Lun) & EFI_SCSI_LOGICAL_UNIT_NUMBER_MASK);
	//		Cdb[1] |= 0x01;
      Cdb[1] = 0x01;
			Cdb[4] = ATA_CMD_SUBOP_EJECT_DISC; //eject command.
		//	Status = EFI_UNSUPPORTED;
			Status    = UsbMass->Transport->ExecCommand (
                                                   UsbMass->Context,
                                                   &Cdb,
                                                   sizeof(Cdb),
                                                   EfiUsbDataOut,
                                                   NULL, 0,
                                                   UsbMass->Lun,
                                                   Timeout,
                                                   &CmdResult
                                                   );

      //ON_EXIT:
      //			gBS->RestoreTPL (OldTpl);
		}
	}
ON_EXIT:
  return Status;
}
