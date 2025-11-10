# 2025-10-21T19:45:34.838407
import vitis

client = vitis.create_client()
client.set_workspace(path="LCD_display")

platform = client.get_component(name="LCD_platform")
status = platform.build()

comp = client.get_component(name="LCD_app")
comp.build()

status = platform.build()

comp.build()

vitis.dispose()

