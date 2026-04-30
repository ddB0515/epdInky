| Supported Targets | ESP32-P4 |
| ----------------- | -------- |

# EPD Basic DEMOs

This example shows how to use FASTEPD library with epdInky ESP32-P4C6.r2 board


# Cloning repo

YOu have few ways doing checkout code and compile

```
git clone --recurse-submodules https://github.com/ddB0515/epdInky.git

or if you have cloned it already (you need to update submodule)

git submodule update --init --recursive

```
After this step is done you need manually to create one file in component (this is workaround until FastEPD gets IDF component registy)

Put this content into: components/FastEPD/idf_component.yml
```
version: "2.0.1"
description: "Optimized library for driving parallel eink displays with the ESP32"
url: "https://github.com/bitbank2/FastEPD"
license: "Apache-2.0 license"
```
Without this you will get errors building 
```
CMake Error at /home/user/.espressif/v6.0/esp-idf/tools/cmake/build.cmake:695 (message):
  ERROR: The "path" field in the manifest file
  "/home/user/epdInky/examples/epd/matrix_editor/main/components/FastEPD/idf_component.yml"
  does not point to a directory.  You can safely remove this field from the
  manifest if this project is an example copied from a component repository.
  The dependency will be downloaded from the ESP component registry.
```


## How to use the example

### Hardware Required

* An epdInky ESP32-P4C6.r2 development board, adapter for eInk panel (check main README.md)
* SDCard needs to be present (as is part of demo) but used for test purposes

### Hardware Connection

The connection between epdInky ESP32-P4C6.r2 is provided via 40pin FPC cable to adapter which enable connection to eInk panel


### Configure

To change panel details refer to `main.c` and change details for your panel

```
If you are using 8bit panel you have to change board config (BB_PANEL_EPDINKY_P4 or BB_PANEL_EPDINKY_P4_16)

8Bit
    int rc = bbepInitPanel(&bbep, BB_PANEL_EPDINKY_P4, 20000000);

16Bit
    int rc = bbepInitPanel(&bbep, BB_PANEL_EPDINKY_P4_16, 20000000);

```
Next section is PANEL itself (size W/H and flags) this example I'm using ED113TC1 panel
```
bbepSetPanelSize(&bbep, 2400, 1034, BB_PANEL_FLAG_MIRROR_Y, -1000); // ED113TC1
```

after all is done you can BUILD

Run `idf.py menuconfig` and go to `Example Configuration`:
2 Options needs to be enabled
- PSRAM
- Chip ID to be < 3.0 (old version)
(this should be already enabled in default sdkconfig)

### Build and Flash

Run `idf.py -p PORT build flash monitor` to build, flash and monitor the project. A LVGL widget should show up on the LCD as expected.

The first time you run `idf.py` for the example will cost extra time as the build system needs to address the component dependencies and downloads the missing components from the ESP Component Registry into `managed_components` folder.

(To exit the serial monitor, type ``Ctrl-]``.)

See the [Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html) for full steps to configure and use ESP-IDF to build projects.


# USAGE

In my case when connected to WiFI find your IP address and connect to it (in my case was: 192.168.1.82)
So you need to open
```
http://192.168.1.82/matrix
```
And you should be on Matrix Editor and ready to modify your example as you wish, once you have your matrix for the panel you could open PR for FastEPD or here

 
### Example Output

```bash
...
Build:Jul 10 2024
rst:0x17 (CHIP_USB_UART_RESET),boot:0xc (SPI_FAST_FLASH_BOOT)
Core0 Saved PC:0x40009034
--- 0x40009034: s_test_psram at /home/dale/.espressif/v6.0/esp-idf/components/esp_psram/system_layer/esp_psram.c:617
Core1 Saved PC:0x4fc012c6
--- 0x4fc012c6: ets_ds_enable in ROM
SPI mode:DIO, clock div:1
load:0x4ff33ce0,len:0x15e0
load:0x4ff29ed0,len:0xe18
load:0x4ff2cbd0,len:0x359c
entry 0x4ff29eda
I (27) boot: ESP-IDF v6.0 2nd stage bootloader
I (28) boot: compile time Apr 30 2026 13:01:58
I (28) boot: Multicore bootloader
I (30) boot: chip revision: v1.0
I (30) boot: efuse block revision: v0.3
I (34) boot.esp32p4: SPI Speed      : 80MHz
I (38) boot.esp32p4: SPI Mode       : DIO
I (42) boot.esp32p4: SPI Flash Size : 2MB
I (45) boot: Enabling RNG early entropy source...
I (50) boot: Partition Table:
I (53) boot: ## Label            Usage          Type ST Offset   Length
I (59) boot:  0 nvs              WiFi data        01 02 00009000 00006000
I (65) boot:  1 phy_init         RF data          01 01 0000f000 00001000
I (72) boot:  2 factory          factory app      00 00 00010000 00100000
I (79) boot: End of partition table
I (82) esp_image: segment 0: paddr=00010020 vaddr=40030020 size=17998h ( 96664) map
I (104) esp_image: segment 1: paddr=000279c0 vaddr=30100000 size=00068h (   104) load
I (106) esp_image: segment 2: paddr=00027a30 vaddr=4ff00000 size=085e8h ( 34280) load
I (114) esp_image: segment 3: paddr=00030020 vaddr=40000020 size=24e7ch (151164) map
I (138) esp_image: segment 4: paddr=00054ea4 vaddr=4ff085e8 size=076c0h ( 30400) load
I (145) esp_image: segment 5: paddr=0005c56c vaddr=4ff0fd00 size=03258h ( 12888) load
I (152) boot: Loaded app from partition at offset 0x10000
I (153) boot: Disabling RNG early entropy source...
I (165) hex_psram: vendor id    : 0x0d (AP)
I (165) hex_psram: Latency      : 0x01 (Fixed)
I (166) hex_psram: DriveStr.    : 0x00 (25 Ohm)
I (166) hex_psram: dev id       : 0x03 (generation 4)
I (171) hex_psram: density      : 0x07 (256 Mbit)
I (175) hex_psram: good-die     : 0x06 (Pass)
I (179) hex_psram: SRF          : 0x02 (Slow Refresh)
I (184) hex_psram: BurstType    : 0x00 ( Wrap)
I (188) hex_psram: BurstLen     : 0x03 (2048 Byte)
I (193) hex_psram: BitMode      : 0x01 (X16 Mode)
I (197) hex_psram: Readlatency  : 0x04 (14 cycles@Fixed)
I (202) hex_psram: DriveStrength: 0x00 (1/1)
I (206) MSPI Timing: Enter psram timing tuning
I (383) esp_psram: Found 32MB PSRAM device
I (384) esp_psram: Speed: 200MHz
I (387) hex_psram: psram CS IO is dedicated
I (387) cpu_start: Multicore app
I (1372) esp_psram: SPI SRAM memory test OK
I (1383) cpu_start: GPIO 38 and 37 are used as console UART I/O pins
I (1383) cpu_start: Pro cpu start user code
I (1383) cpu_start: cpu freq: 360000000 Hz
I (1385) app_init: Application information:
I (1389) app_init: Project name:     basic
I (1393) app_init: App version:      1eac6ff-dirty
I (1397) app_init: Compile time:     Apr 30 2026 13:01:56
I (1402) app_init: ELF file SHA256:  042d7002f...
I (1407) app_init: ESP-IDF:          v6.0
I (1411) efuse_init: Min chip rev:     v1.0
I (1415) efuse_init: Max chip rev:     v1.99 
I (1419) efuse_init: Chip rev:         v1.0
I (1423) heap_init: Initializing. RAM available for dynamic allocation:
I (1429) heap_init: At 4FF15970 len 00025650 (149 KiB): RETENT_RAM
I (1435) heap_init: At 4FF3AFC0 len 00004BF0 (18 KiB): RAM
I (1440) heap_init: At 4FF40000 len 00060000 (384 KiB): RAM
I (1446) heap_init: At 50108080 len 00007F80 (31 KiB): RTCRAM
I (1451) heap_init: At 30100068 len 00001F98 (7 KiB): TCM
I (1456) esp_psram: Adding pool of 32768K of PSRAM memory to heap allocator
I (1463) spi_flash: detected chip: generic
I (1466) spi_flash: flash io: dio
W (1469) spi_flash: Detected size(16384k) larger than the size in the binary image header(2048k). Using the size in the binary image header.
I (1482) sleep_gpio: Configure to isolate all GPIO pins in sleep state
I (1488) sleep_gpio: Enable automatic switching of GPIO sleep configuration
I (1495) main_task: Started on CPU0
I (1505) esp_psram: Reserving pool of 32K of internal memory for DMA/internal allocations
I (1505) main_task: Calling app_main()
I (1505) eInky-P4: i2c_master_init ESP_OK
I (1515) eInky-P4: tca6408_init...
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 0f 
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
20: -- 21 -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
50: -- -- 52 -- -- -- -- -- -- -- -- -- -- -- -- -- 
60: -- -- -- -- -- -- -- -- 68 -- -- -- -- -- -- -- 
70: 70 -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
W (1595) i2c.master: Timeout value exceeds the maximum supported value, rounded down to maximum supported value: 53687091 us
...
W (46865) i2c.master: Timeout value exceeds the maximum supported value, rounded down to maximum supported value: 53687091 us
I (48315) main_task: Returned from app_main()
...
```

## Troubleshooting

For any technical queries, please open an [issue](https://github.com/ddB0515/epdInky/issues) on GitHub. We will get back to you soon.

