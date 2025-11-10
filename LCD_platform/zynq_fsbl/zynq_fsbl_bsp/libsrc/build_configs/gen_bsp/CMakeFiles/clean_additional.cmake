# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "/home/marti/Vitis_workspace/LCD_display/LCD_platform/zynq_fsbl/zynq_fsbl_bsp/include/diskio.h"
  "/home/marti/Vitis_workspace/LCD_display/LCD_platform/zynq_fsbl/zynq_fsbl_bsp/include/ff.h"
  "/home/marti/Vitis_workspace/LCD_display/LCD_platform/zynq_fsbl/zynq_fsbl_bsp/include/ffconf.h"
  "/home/marti/Vitis_workspace/LCD_display/LCD_platform/zynq_fsbl/zynq_fsbl_bsp/include/sleep.h"
  "/home/marti/Vitis_workspace/LCD_display/LCD_platform/zynq_fsbl/zynq_fsbl_bsp/include/xilffs.h"
  "/home/marti/Vitis_workspace/LCD_display/LCD_platform/zynq_fsbl/zynq_fsbl_bsp/include/xilffs_config.h"
  "/home/marti/Vitis_workspace/LCD_display/LCD_platform/zynq_fsbl/zynq_fsbl_bsp/include/xilrsa.h"
  "/home/marti/Vitis_workspace/LCD_display/LCD_platform/zynq_fsbl/zynq_fsbl_bsp/include/xiltimer.h"
  "/home/marti/Vitis_workspace/LCD_display/LCD_platform/zynq_fsbl/zynq_fsbl_bsp/include/xtimer_config.h"
  "/home/marti/Vitis_workspace/LCD_display/LCD_platform/zynq_fsbl/zynq_fsbl_bsp/lib/libxilffs.a"
  "/home/marti/Vitis_workspace/LCD_display/LCD_platform/zynq_fsbl/zynq_fsbl_bsp/lib/libxilrsa.a"
  "/home/marti/Vitis_workspace/LCD_display/LCD_platform/zynq_fsbl/zynq_fsbl_bsp/lib/libxiltimer.a"
  )
endif()
