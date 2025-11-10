# 2025-10-24T14:30:19.285709
import vitis

client = vitis.create_client()
client.set_workspace(path="LCD_display")

advanced_options = client.create_advanced_options_dict(dt_overlay="0")

platform = client.create_platform_component(name = "LCD_platform",hw_design = "$COMPONENT_LOCATION/../../../Vivado_projects/LCD_base/LCD_base_wrapper.xsa",os = "standalone",cpu = "ps7_cortexa9_0",domain_name = "standalone_ps7_cortexa9_0",generate_dtb = False,advanced_options = advanced_options,compiler = "gcc")

comp = client.create_app_component(name="LCD_app",platform = "$COMPONENT_LOCATION/../LCD_platform/export/LCD_platform/LCD_platform.xpfm",domain = "standalone_ps7_cortexa9_0")

comp = client.get_component(name="LCD_app")
status = comp.import_files(from_loc="", files=["C:\Users\marti\Vitis_workspace\LCD_display\C_code_backups\fonts.h", "C:\Users\marti\Vitis_workspace\LCD_display\C_code_backups\images.h", "C:\Users\marti\Vitis_workspace\LCD_display\C_code_backups\lvgl_compat.h", "C:\Users\marti\Vitis_workspace\LCD_display\C_code_backups\main.c", "C:\Users\marti\Vitis_workspace\LCD_display\C_code_backups\zynq_lcd_fonts.h", "C:\Users\marti\Vitis_workspace\LCD_display\C_code_backups\zynq_lcd_st7789.c", "C:\Users\marti\Vitis_workspace\LCD_display\C_code_backups\zynq_lcd_st7789.h"])

platform = client.get_component(name="LCD_platform")
status = platform.build()

comp = client.get_component(name="LCD_app")
comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

status = comp.clean()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = comp.clean()

status = platform.build()

comp.build()

status = comp.clean()

status = platform.build()

comp.build()

vitis.dispose()

