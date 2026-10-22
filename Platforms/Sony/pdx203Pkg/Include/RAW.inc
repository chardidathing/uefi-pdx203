# the raw files are the stock sony uefi's own, none of them exist in this tree (docs/pdx203.md)
!if $(PDX203_VENDOR_HW) == 1
  FILE FREEFORM = A1E235DE-E825-4591-9623-C43175811826 {
    SECTION UI = "SecParti.cfg"
    SECTION RAW = Binaries/pdx203/RawFiles/SecParti.cfg
  }

  FILE FREEFORM = 21E9BDD9-6C3F-4F10-84A5-BBEC322741F1 {
    SECTION UI = "uefipil.cfg"
    SECTION RAW = Binaries/pdx203/RawFiles/uefipil.cfg
  }

  FILE FREEFORM = 45FE4B7C-150C-45DA-A021-4BEB2048EC6F {
    SECTION UI = "QcomChargerCfg.cfg"
    SECTION RAW = Binaries/pdx203/RawFiles/QcomChargerCfg.cfg
  }

  FILE FREEFORM = F780C779-DD7C-47CD-BD1A-5EB414C51704 {
    SECTION UI = "BATTERY.PROVISION"
    SECTION RAW = Binaries/pdx203/RawFiles/BATTERY.PROVISION
  }
!endif
