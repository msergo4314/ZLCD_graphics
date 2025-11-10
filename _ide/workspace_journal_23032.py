# 2025-10-18T16:22:16.295654
import vitis

client = vitis.create_client()
client.set_workspace(path="LCD_display")

advanced_options = client.create_advanced_options_dict(dt_overlay="0")

platform = client.create_platform_component(name = "LCD_platform",hw_design = "$COMPONENT_LOCATION/../../../Vivado_projects/LCD_base/LCD_base_wrapper.xsa",os = "standalone",cpu = "ps7_cortexa9_0",domain_name = "standalone_ps7_cortexa9_0",generate_dtb = False,advanced_options = advanced_options,compiler = "gcc")

platform = client.get_component(name="LCD_platform")
status = platform.build()

comp = client.create_app_component(name="LCD_app",platform = "$COMPONENT_LOCATION/../LCD_platform/export/LCD_platform/LCD_platform.xpfm",domain = "standalone_ps7_cortexa9_0")

status = platform.build()

comp = client.get_component(name="LCD_app")
comp.build()

status = platform.build()

comp.build()

client.delete_component(name="LCD_platform")

client.delete_component(name="componentName")

advanced_options = client.create_advanced_options_dict(dt_overlay="0")

platform = client.create_platform_component(name = "LCD_platform",hw_design = "$COMPONENT_LOCATION/../../../Vivado_projects/LCD_base/LCD_base_wrapper.xsa",os = "standalone",cpu = "ps7_cortexa9_0",domain_name = "standalone_ps7_cortexa9_0",generate_dtb = False,advanced_options = advanced_options,compiler = "gcc")

status = platform.build()

status = platform.build()

comp.build()

client.delete_component(name="LCD_platform")

comp.build()

advanced_options = client.create_advanced_options_dict(dt_overlay="0")

platform = client.create_platform_component(name = "LCD_platform_3_bits",hw_design = "$COMPONENT_LOCATION/../../../Vivado_projects/LCD_base/LCD_base_wrapper.xsa",os = "standalone",cpu = "ps7_cortexa9_0",domain_name = "standalone_ps7_cortexa9_0",generate_dtb = False,advanced_options = advanced_options,compiler = "gcc")

platform = client.get_component(name="LCD_platform_3_bits")
status = platform.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

component = client.get_component(name="LCD_app")

lscript = component.get_ld_script(path="C:\Users\marti\VItis_workspace\LCD_display\LCD_app\src\lscript.ld")

lscript.regenerate()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

client.delete_component(name="LCD_platform_3_bits")

client.delete_component(name="LCD_platform_3_bits")

comp.build()

status = comp.clean()

comp.build()

advanced_options = client.create_advanced_options_dict(dt_overlay="0")

platform = client.create_platform_component(name = "LCD_platform",hw_design = "$COMPONENT_LOCATION/../../../Vivado_projects/LCD_base/LCD_base_wrapper.xsa",os = "standalone",cpu = "ps7_cortexa9_0",domain_name = "standalone_ps7_cortexa9_0",generate_dtb = False,advanced_options = advanced_options,compiler = "gcc")

platform = client.get_component(name="LCD_platform")
status = platform.build()

status = platform.build()

status = comp.clean()

comp.build()

comp.build()

status = platform.build()

status = platform.build()

comp.build()

client.delete_component(name="LCD_app")

status = platform.build()

comp = client.create_app_component(name="LCD_app",platform = "$COMPONENT_LOCATION/../LCD_platform/export/LCD_platform/LCD_platform.xpfm",domain = "standalone_ps7_cortexa9_0")

comp = client.get_component(name="LCD_app")
status = comp.import_files(from_loc="", files=["F:\Smart_Zynq_Board\LCD_DRIVER\main.c", "F:\Smart_Zynq_Board\LCD_DRIVER\zynq_lcd_st7789.c", "F:\Smart_Zynq_Board\LCD_DRIVER\zynq_lcd_st7789.h"])

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

status = comp.clean()

status = platform.build()

comp.build()

status = comp.clean()

status = platform.build()

comp.build()

client.delete_component(name="LCD_platform")

advanced_options = client.create_advanced_options_dict(dt_overlay="0")

platform = client.create_platform_component(name = "LCD_platform",hw_design = "$COMPONENT_LOCATION/../../../Vivado_projects/LCD_base/LCD_base_wrapper.xsa",os = "standalone",cpu = "ps7_cortexa9_0",domain_name = "standalone_ps7_cortexa9_0",generate_dtb = False,advanced_options = advanced_options,compiler = "gcc")

status = platform.build()

status = platform.build()

comp.build()

client.delete_component(name="LCD_app")

client.delete_component(name="componentName")

client.delete_component(name="componentName")

client.delete_component(name="componentName")

comp = client.create_app_component(name="LCD_app",platform = "$COMPONENT_LOCATION/../LCD_platform/export/LCD_platform/LCD_platform.xpfm",domain = "standalone_ps7_cortexa9_0")

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

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

status = platform.build()

comp.build()

vitis.dispose()

