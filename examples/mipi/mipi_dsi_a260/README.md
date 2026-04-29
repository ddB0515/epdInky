| Supported Targets | ESP32-P4 |
| ----------------- | -------- |

# MIPI DSI LCD Panel Example( Samsung A260 display)

This example shows the general process of installing a MIPI DSI LCD driver, and displays a LVGL widget on the screen.

<img src="https://raw.githubusercontent.com/ddB0515/epdInky/main/images/examples/mipi_dsi_a260_demo.jpg" width="600" />

### Hardware Required

* An epdInky ESP32-P4C6.r2 development board, which with MIPI DSI peripheral supported
* Samsung A260 MIPI DSI LCD panel, with 2 data lanes and 1 clock lane, this example support
* Adapter board for 22 pin DSI from board to A260 display
* An USB cable for power supply and programming

### Hardware Connection
Check image

<img src="https://raw.githubusercontent.com/ddB0515/epdInky/main/images/examples/mipi_dsi_a260_a260_adapter.jpg" width="300" />

### Configure

Run `idf.py menuconfig` and go to `Example Configuration`:
2 Options needs to be enabled
- PSRAM
- Chip ID to be < 3.0 (old version)
(this should be already enabled in default sdkconfig)

### Build and Flash

This was tested on ODF v6.0 and don't know if other version works (NOT TESTED)
Run `idf.py -p PORT build flash monitor` to build, flash and monitor the project. A LVGL widget should show up on the LCD as expected.

The first time you run `idf.py` for the example will cost extra time as the build system needs to address the component dependencies and downloads the missing components from the ESP Component Registry into `managed_components` folder.

(To exit the serial monitor, type ``Ctrl-]``.)

See the [Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html) for full steps to configure and use ESP-IDF to build projects.

### Example Output
After running on a device you should get something like this
```bash
...
Build:Jul 10 2024
rst:0x17 (CHIP_USB_UART_RESET),boot:0xc (SPI_FAST_FLASH_BOOT)
Core0 Saved PC:0x40009012
--- 0x40009012: s_test_psram at /home/dale/.espressif/v6.0/esp-idf/components/esp_psram/system_layer/esp_psram.c:617
Core1 Saved PC:0x4fc012cc
--- 0x4fc012cc: ets_ds_enable in ROM
SPI mode:DIO, clock div:2
load:0x4ff33ce0,len:0x15e0
load:0x4ff29ed0,len:0xe18
load:0x4ff2cbd0,len:0x359c
entry 0x4ff29eda
I (27) boot: ESP-IDF v6.0 2nd stage bootloader
I (28) boot: compile time Apr 29 2026 21:20:21
I (28) boot: Multicore bootloader
I (30) boot: chip revision: v1.0
I (30) boot: efuse block revision: v0.3
I (34) boot.esp32p4: SPI Speed      : 40MHz
I (38) boot.esp32p4: SPI Mode       : DIO
I (42) boot.esp32p4: SPI Flash Size : 2MB
I (45) boot: Enabling RNG early entropy source...
I (50) boot: Partition Table:
I (53) boot: ## Label            Usage          Type ST Offset   Length
I (59) boot:  0 nvs              WiFi data        01 02 00009000 00006000
I (65) boot:  1 phy_init         RF data          01 01 0000f000 00001000
I (72) boot:  2 factory          factory app      00 00 00010000 00100000
I (79) boot: End of partition table
I (82) esp_image: segment 0: paddr=00010020 vaddr=40060020 size=1550ch ( 87308) map
I (107) esp_image: segment 1: paddr=00025534 vaddr=30100000 size=00068h (   104) load
I (109) esp_image: segment 2: paddr=000255a4 vaddr=4ff00000 size=0aa74h ( 43636) load
I (122) esp_image: segment 3: paddr=00030020 vaddr=40000020 size=56bach (355244) map
I (194) esp_image: segment 4: paddr=00086bd4 vaddr=4ff0aa74 size=04f4ch ( 20300) load
I (200) esp_image: segment 5: paddr=0008bb28 vaddr=4ff0fa00 size=033a0h ( 13216) load
I (208) boot: Loaded app from partition at offset 0x10000
I (208) boot: Disabling RNG early entropy source...
I (221) hex_psram: vendor id    : 0x0d (AP)
I (222) hex_psram: Latency      : 0x01 (Fixed)
I (222) hex_psram: DriveStr.    : 0x00 (25 Ohm)
I (223) hex_psram: dev id       : 0x03 (generation 4)
I (227) hex_psram: density      : 0x07 (256 Mbit)
I (231) hex_psram: good-die     : 0x06 (Pass)
I (236) hex_psram: SRF          : 0x02 (Slow Refresh)
I (240) hex_psram: BurstType    : 0x00 ( Wrap)
I (245) hex_psram: BurstLen     : 0x03 (2048 Byte)
I (249) hex_psram: BitMode      : 0x01 (X16 Mode)
I (253) hex_psram: Readlatency  : 0x04 (14 cycles@Fixed)
I (258) hex_psram: DriveStrength: 0x00 (1/1)
I (262) MSPI Timing: Enter psram timing tuning
I (439) esp_psram: Found 32MB PSRAM device
I (439) esp_psram: Speed: 200MHz
I (443) hex_psram: psram CS IO is dedicated
I (443) cpu_start: Multicore app
I (1428) esp_psram: SPI SRAM memory test OK
I (1439) cpu_start: GPIO 38 and 37 are used as console UART I/O pins
I (1439) cpu_start: Pro cpu start user code
I (1439) cpu_start: cpu freq: 360000000 Hz
I (1441) app_init: Application information:
I (1445) app_init: Project name:     mipi_dsi_a260
I (1450) app_init: App version:      3b1ea4a
I (1454) app_init: Compile time:     Apr 29 2026 21:20:17
I (1459) app_init: ELF file SHA256:  ec6ee88e5...
I (1463) app_init: ESP-IDF:          v6.0
I (1467) efuse_init: Min chip rev:     v0.0
I (1471) efuse_init: Max chip rev:     v1.99 
I (1475) efuse_init: Chip rev:         v1.0
I (1479) heap_init: Initializing. RAM available for dynamic allocation:
I (1485) heap_init: At 4FF248D0 len 000166F0 (89 KiB): RETENT_RAM
I (1491) heap_init: At 4FF3AFC0 len 00004BF0 (18 KiB): RAM
I (1496) heap_init: At 4FF40000 len 00060000 (384 KiB): RAM
I (1502) heap_init: At 50108080 len 00007F80 (31 KiB): RTCRAM
I (1507) heap_init: At 30100068 len 00001F98 (7 KiB): TCM
I (1512) esp_psram: Adding pool of 32768K of PSRAM memory to heap allocator
I (1520) spi_flash: detected chip: generic
I (1522) spi_flash: flash io: dio
W (1526) spi_flash: Detected size(16384k) larger than the size in the binary image header(2048k). Using the size in the binary image header.
I (1539) sleep_gpio: Configure to isolate all GPIO pins in sleep state
I (1544) sleep_gpio: Enable automatic switching of GPIO sleep configuration
I (1552) main_task: Started on CPU0
I (1562) esp_psram: Reserving pool of 32K of internal memory for DMA/internal allocations
I (1562) main_task: Calling app_main()
I (1572) LCD-mipi: i2c_master_init ESP_OK
I (1572) LCD-mipi: tca6408_init...
I (1582) LCD-mipi: sgm37604a_init...
I (1832) LCD-mipi: MIPI DSI LCD initialization
I (1832) LCD-mipi: MIPI DSI PHY Powered on
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 0f 
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
20: 20 21 -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
30: -- -- -- -- -- -- 36 -- -- -- -- -- -- -- 3e -- 
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
50: -- -- 52 -- -- -- -- -- -- -- -- -- -- -- -- -- 
60: -- -- -- -- -- -- -- -- 68 -- -- -- -- -- -- -- 
70: 70 -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
I (1872) LCD-mipi: Install MIPI DSI LCD control IO
I (1872) LCD-mipi: Install MIPI DSI LCD data panel
I (1872) LCD-mipi: esp_lcd_new_panel_td4101 panel
I (1902) LCD-mipi: esp_lcd_panel_reset panel
I (2022) LCD-mipi: esp_lcd_panel_init panel
I (2022) td4101: panel_td4101_init called
I (2282) LCD-mipi: Setting test pattern - if you see color bars, MIPI DSI is working
I (5282) LCD-mipi: Initialize LVGL library
I (5282) LCD-mipi: Allocate separate LVGL draw buffers
I (5282) LCD-mipi: Register DPI panel event callback for LVGL flush ready notification
I (5282) LCD-mipi: Use esp_timer as LVGL tick timer
I (5292) LCD-mipi: Create LVGL task
I (5292) LCD-mipi: Starting LVGL task
I (5392) LCD-mipi: Display LVGL Meter Widget
I (5402) main_task: Returned from app_main()
...
```

## Troubleshooting

For any technical queries, please open an [issue](https://github.com/ddB0515/epdInky/issues) on GitHub. We will get back to you soon.
