/**
  Copyright (c) 2011-2012, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/IoLib.h>
#include <Library/ArmLib.h>
#include <Library/PcdLib.h>
#include <Library/PlatformSecLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/ArmSmmuDetachLib.h>

#include "PlatformRegisters.h"

VOID
DisableWatchDogTimer ()
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR ApssRegion;

  // Locate "APSS_HM" Memory Region
  Status = LocateMemoryRegionByName ("APSS_HM", &ApssRegion);
  if (EFI_ERROR (Status)) {
    return;
  }

  // Disable WatchDog Timer
  MmioWrite32 (ApssRegion.Address + APSS_WDT_OFFSET + APSS_WDT_ENABLE_OFFSET, 0x0);
}

// a command mode panel shows the splash only because the loader pushed one frame, with pingpong autorefresh on the mdp resends the
// buffer on every te pulse and the frame buffer behaves like a video mode one. autorefresh counts te pulses so tearcheck has to run,
// when the loader left it off the block gets the values the linux cmd encoder programs (external te, the hw vsync counter unused).
// runs before the mmu is on, so the mdp registers need no mapping here.
VOID
EnableCommandModePanelRefresh ()
{
  UINTN  PingPong = FixedPcdGet32 (PcdMdpPingPongBase);
  UINT32 Height   = FixedPcdGet32 (PcdFrameBufferHeight);

  // video mode panel, the dsi controller scans the buffer out by itself
  if (PingPong == 0) {
    return;
  }

  if (!(MmioRead32 (PingPong + PP_TEAR_CHECK_EN) & 1)) {
    MmioWrite32 (PingPong + PP_SYNC_CONFIG_VSYNC,  PP_VSYNC_COUNTER_ENABLE | FixedPcdGet32 (PcdMdpTearCheckVsyncCount));
    MmioWrite32 (PingPong + PP_SYNC_CONFIG_HEIGHT, PP_SYNC_CONFIG_HEIGHT_MAX);
    MmioWrite32 (PingPong + PP_VSYNC_INIT_VAL,     Height);
    MmioWrite32 (PingPong + PP_RD_PTR_IRQ,         Height + 1);
    MmioWrite32 (PingPong + PP_START_POS,          Height);
    MmioWrite32 (PingPong + PP_SYNC_THRESH,        (PP_SYNC_THRESH_DEFAULT << 16) | PP_SYNC_THRESH_DEFAULT);
    MmioWrite32 (PingPong + PP_SYNC_WRCOUNT,       Height + PP_SYNC_THRESH_DEFAULT + 1);
    MmioWrite32 (PingPong + PP_TEAR_CHECK_EN,      1);
  }

  MmioWrite32 (PingPong + PP_AUTOREFRESH_CONFIG, PP_AUTOREFRESH_ENABLE | 1);
  ArmDataSynchronizationBarrier ();
}

VOID
PlatformInitialize ()
{
  // Disable WatchDog Timer
  DisableWatchDogTimer ();

  // Keep the Panel refreshing
  EnableCommandModePanelRefresh ();

  // Set MDP SIDs
  CONST UINT16 MdpStreams[] = { 0x820, 0x821, 0x822, 0xC20, 0xC21, 0xC22 };

  // Detach IOMMU Domains
  ArmSmmuDetach (MdpStreams, ARRAY_SIZE (MdpStreams));
}
