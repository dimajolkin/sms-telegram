# uart-test

Проверка патча TinyGo ESP32-S3 UART (`../patches/tinygo/`).

```bash
../patches/tinygo/apply.sh   # один раз на TinyGo
make bin
esptool.py --chip esp32s3 --port /dev/cu.usbmodem2101 write-flash 0x0 /tmp/uart-test.bin
make monitor
```

Проводка: **D6→Modem RX**, **D7←Modem TX**. Ожидание: `AT\r\r\nOK\r\n`.
