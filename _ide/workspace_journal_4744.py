# 2025-11-06T17:58:01.110365
import vitis

client = vitis.create_client()
client.set_workspace(path="LCD_display")

platform = client.get_component(name="LCD_platform")
status = platform.build()

vitis.dispose()

