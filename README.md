# convertor
firmware instruction via esptool:
esptool --chip esp32c3 --port /dev/ttyACM0 write-flash   0x0 $B/bootloader.bin   0x8000 $B/partitions.bin   0xe000 ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin   0x10000 ~/Downloads/Convertor_UART_CRSF115InvToCRSF115Inv_ESP32C_.bin
or for ready board:
esptool --chip esp32c3 --port /dev/ttyACM0 write-flash  0x10000 ~/Downloads/Convertor_UART_CRSF115InvToCRSF115Inv_ESP32C_.bin