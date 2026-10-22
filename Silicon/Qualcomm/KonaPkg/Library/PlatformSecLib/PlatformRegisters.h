#ifndef _PLATFORM_REGISTERS_H_
#define _PLATFORM_REGISTERS_H_

//
// WatchDog Timer Offset
//
#define APSS_WDT_OFFSET                0x410000

//
// WatchDog Timer Registers
//
#define APSS_WDT_ENABLE_OFFSET         0x8

//
// MDP PingPong Registers (techpack/display/msm/sde/sde_hw_pingpong.c)
//
#define PP_TEAR_CHECK_EN               0x00
#define PP_SYNC_CONFIG_VSYNC           0x04
#define PP_SYNC_CONFIG_HEIGHT          0x08
#define PP_SYNC_WRCOUNT                0x0C
#define PP_VSYNC_INIT_VAL              0x10
#define PP_SYNC_THRESH                 0x18
#define PP_START_POS                   0x1C
#define PP_RD_PTR_IRQ                  0x20
#define PP_AUTOREFRESH_CONFIG          0x30

#define PP_AUTOREFRESH_ENABLE          BIT31      // low bits, refresh every n te pulses
#define PP_VSYNC_COUNTER_ENABLE        BIT19
#define PP_SYNC_CONFIG_HEIGHT_MAX      0xFFF0
#define PP_SYNC_THRESH_DEFAULT         4          // start and continue thresholds the linux cmd encoder uses

#endif /* _PLATFORM_REGISTERS_H_ */
