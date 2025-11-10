# 2025-11-06T13:00:29.155251
import vitis

client = vitis.create_client()
client.set_workspace(path="LCD_display")

platform = client.get_component(name="LCD_platform")
status = platform.build()

comp = client.get_component(name="LCD_app")
comp.build()

status = platform.build()

status = platform.update_hw(hw_design = "$COMPONENT_LOCATION/../../../Vivado_projects/LCD_base/LCD_base_wrapper.xsa")

vitis.dispose()

