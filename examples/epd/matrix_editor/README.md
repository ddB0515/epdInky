| Supported Targets | ESP32-P4 |
| ----------------- | -------- |

# EPD Matrix Editor

This example shows how to use Web GUI for custom Matrix editor needed to make for new panels to get 16 levels (4bit) gray colours.

# Cloning repo

YOu have few ways doing checkout code and compile

```
git clone --recurse-submodules https://github.com/ddB0515/epdInky.git

or if you have cloned it already (you need to update submodule)

git submodule update --init --recursive

```


## How to use the example

### Hardware Required

* An epdInky ESP32-P4C6.r2 development board, adapter for eInk panel (check main README.md)
* SDCard needs to be present (as is part of demo) but used for test purposes
### Hardware Connection

The connection between epdInky ESP32-P4C6.r2 is provided via 40pin FPC cable to adapter which enable connection to eInk panel


### Configure

In `main.h` before you build change default WIFI settings to your network so you can connect to WiFi and use editor

```
#define EXAMPLE_ESP_WIFI_SSID      "WIFI_SSID"
#define EXAMPLE_ESP_WIFI_PASS      "WIFI_PASSWORD"
```

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

### Example Output

```bash
...
Build:Jul 10 2024
rst:0x17 (CHIP_USB_UART_RESET),boot:0x31c (SPI_FAST_FLASH_BOOT)
Core0 Saved PC:0x40009024
--- 0x40009024: s_test_psram at /home/user/.espressif/v6.0/esp-idf/components/esp_psram/system_layer/esp_psram.c:617
Core1 Saved PC:0x4fc012ca
--- 0x4fc012ca: ets_ds_enable in ROM
SPI mode:DIO, clock div:1
load:0x4ff33ce0,len:0x15e0
load:0x4ff29ed0,len:0xe18
load:0x4ff2cbd0,len:0x359c
entry 0x4ff29eda
I (28) boot: ESP-IDF v6.0 2nd stage bootloader
I (28) boot: compile time Apr 29 2026 22:49:13
I (28) boot: Multicore bootloader
I (30) boot: chip revision: v1.0
I (31) boot: efuse block revision: v0.3
I (34) boot.esp32p4: SPI Speed      : 80MHz
I (38) boot.esp32p4: SPI Mode       : DIO
I (42) boot.esp32p4: SPI Flash Size : 2MB
I (46) boot: Enabling RNG early entropy source...
I (50) boot: Partition Table:
I (53) boot: ## Label            Usage          Type ST Offset   Length
I (59) boot:  0 nvs              WiFi data        01 02 00009000 00006000
I (66) boot:  1 phy_init         RF data          01 01 0000f000 00001000
I (72) boot:  2 factory          factory app      00 00 00010000 00100000
I (79) boot: End of partition table
I (82) esp_image: segment 0: paddr=00010020 vaddr=40080020 size=3df24h (253732) map
I (127) esp_image: segment 1: paddr=0004df4c vaddr=30100000 size=00068h (   104) load
I (129) esp_image: segment 2: paddr=0004dfbc vaddr=4ff00000 size=0205ch (  8284) load
I (133) esp_image: segment 3: paddr=00050020 vaddr=40000020 size=7aa58h (502360) map
I (212) esp_image: segment 4: paddr=000caa80 vaddr=4ff0205c size=0e7cch ( 59340) load
I (223) esp_image: segment 5: paddr=000d9254 vaddr=4ff10880 size=035f8h ( 13816) load
I (231) boot: Loaded app from partition at offset 0x10000
I (231) boot: Disabling RNG early entropy source...
I (243) hex_psram: vendor id    : 0x0d (AP)
I (243) hex_psram: Latency      : 0x01 (Fixed)
I (243) hex_psram: DriveStr.    : 0x00 (25 Ohm)
I (244) hex_psram: dev id       : 0x03 (generation 4)
I (248) hex_psram: density      : 0x07 (256 Mbit)
I (253) hex_psram: good-die     : 0x06 (Pass)
I (257) hex_psram: SRF          : 0x02 (Slow Refresh)
I (262) hex_psram: BurstType    : 0x00 ( Wrap)
I (266) hex_psram: BurstLen     : 0x03 (2048 Byte)
I (270) hex_psram: BitMode      : 0x01 (X16 Mode)
I (275) hex_psram: Readlatency  : 0x04 (14 cycles@Fixed)
I (280) hex_psram: DriveStrength: 0x00 (1/1)
I (284) MSPI Timing: Enter psram timing tuning
I (461) esp_psram: Found 32MB PSRAM device
I (462) esp_psram: Speed: 200MHz
I (465) hex_psram: psram CS IO is dedicated
I (465) cpu_start: Multicore app
I (1450) esp_psram: SPI SRAM memory test OK
I (1461) cpu_start: GPIO 38 and 37 are used as console UART I/O pins
I (1461) cpu_start: Pro cpu start user code
I (1461) cpu_start: cpu freq: 360000000 Hz
I (1463) app_init: Application information:
I (1467) app_init: Project name:     matrix_editor
I (1472) app_init: App version:      e2e2744
I (1476) app_init: Compile time:     Apr 29 2026 22:49:11
I (1481) app_init: ELF file SHA256:  e76bba324...
I (1485) app_init: ESP-IDF:          v6.0
I (1489) efuse_init: Min chip rev:     v1.0
I (1493) efuse_init: Max chip rev:     v1.99 
I (1497) efuse_init: Chip rev:         v1.0
I (1501) heap_init: Initializing. RAM available for dynamic allocation:
I (1507) heap_init: At 4FF17810 len 000237B0 (141 KiB): RETENT_RAM
I (1513) heap_init: At 4FF3AFC0 len 00004BF0 (18 KiB): RAM
I (1518) heap_init: At 4FF40000 len 00060000 (384 KiB): RAM
I (1524) heap_init: At 50108080 len 00007F80 (31 KiB): RTCRAM
I (1529) heap_init: At 30100068 len 00001F98 (7 KiB): TCM
I (1534) esp_psram: Adding pool of 32768K of PSRAM memory to heap allocator
I (1542) spi_flash: detected chip: generic
I (1545) spi_flash: flash io: dio
W (1548) spi_flash: Detected size(16384k) larger than the size in the binary image header(2048k). Using the size in the binary image header.
I (1579) sleep_gpio: Configure to isolate all GPIO pins in sleep state
I (1584) sleep_gpio: Enable automatic switching of GPIO sleep configuration
I (1591) H_SDIO_DRV: sdio_data_to_rx_buf_task started
I (1591) main_task: Started on CPU0
I (1601) esp_psram: Reserving pool of 32K of internal memory for DMA/internal allocations
I (1611) main_task: Calling app_main()
I (1611) eInky-P4: i2c_master_init ESP_OK
I (1611) eInky-P4: tca6408_init...
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 0f 
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
20: -- 21 -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
50: -- -- 52 -- -- -- -- -- -- -- -- -- -- -- -- -- 
60: -- -- -- -- -- -- -- -- 68 -- -- -- -- -- -- -- 
70: 70 -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
I (1661) epd_functions: FastEPD init
I (1661) epd_functions: bbepInitPanel returned 0
W (1671) epd_functions: EPD not initialized; call epd_init() first
W (1671) epd_functions: bbepSetCustomMatrix OK
W (1741) i2c.master: Timeout value exceeds the maximum supported value, rounded down to maximum supported value: 53687091 us
W (1741) i2c.master: Timeout value exceeds the maximum supported value, rounded down to maximum supported value: 53687091 us
W (3731) epd_functions: bbepFullUpdate OK
I (3731) sd_card_fns: Initializing SD card
I (3731) sd_card_fns: Using SDMMC peripheral
I (3731) sd_card_fns: Mounting filesystem
I (3771) sd_card_fns: Filesystem mounted
Name: SD32G
Type: SDHC
Speed: 20.00 MHz (limit: 20.00 MHz)
Size: 29819MB
CSD: ver=2, sector_size=512, capacity=61069312 read_bl_len=9
SSR: bus_width=4
I (3781) sd_card_fns: Opening file /sdcard/hello.txt
I (3821) sd_card_fns: File written
I (3821) transport: Attempt connection with slave: retry[0]
W (3821) H_SDIO_DRV: Reset slave using GPIO[54]
I (3821) os_wrapper_esp: GPIO [54] configured
I (5351) sdio_wrapper: SDIO master: Slot 1, Data-Lines: 4-bit Freq(KHz)[40000 KHz]
I (5351) sdio_wrapper: GPIOs: CLK[18] CMD[19] D0[23] D1[22] D2[21] D3[20] Slave_Reset[54]
I (5351) sdio_wrapper: Queues: Tx[20] Rx[20] SDIO-Rx-Mode[1]
I (5391) sdio_wrapper: Function 0 Blocksize: 512
I (5391) sdio_wrapper: Function 1 Blocksize: 512
I (5491) H_SDIO_DRV: Card init success, TRANSPORT_RX_ACTIVE
I (5491) transport: set_transport_state: 1
I (5491) transport: Waiting for esp_hosted slave to be ready
I (5591) H_SDIO_DRV: SDIO Host operating in STREAMING MODE
I (5591) H_SDIO_DRV: Open data path at slave
I (5591) H_SDIO_DRV: Starting SDIO process rx task
I (5611) H_SDIO_DRV: Received ESP_PRIV_IF type message
I (5611) transport: Received INIT event from ESP32 peripheral
I (5611) transport: EVENT: 12
I (5621) transport: Identified slave [esp32c6]
I (5621) transport: SDIO mode: slave: streaming, host: streaming
I (5631) transport: EVENT: 11
I (5631) transport: capabilities: 0xd
I (5631) transport: Features supported are:
I (5641) transport:      * WLAN
I (5641) transport:        - HCI over SDIO
I (5641) transport:        - BLE only
I (5641) transport: EVENT: 13
I (5651) transport: ESP board type is : 13 

I (5651) transport: Base transport is set-up, TRANSPORT_TX_ACTIVE
I (5661) H_API: Transport active
I (5661) transport: Slave chip Id[12]
I (5661) transport: raw_tp_dir[-], flow_ctrl: low[60] high[80]
I (5671) transport: transport_delayed_init
I (5671) esp_cli: Registering command: crash
I (5681) esp_cli: Registering command: reboot
I (5681) esp_cli: Registering command: mem-dump
I (5691) esp_cli: Registering command: task-dump
I (5691) esp_cli: Registering command: cpu-dump
I (5691) esp_cli: Registering command: heap-trace
I (5701) esp_cli: Registering command: sock-dump
I (5701) esp_cli: Registering command: host-power-save
I (5711) hci_stub_drv: Host BT Support: Disabled
I (5711) H_SDIO_DRV: Received INIT event
I (5721) H_SDIO_DRV: Event type: 0x22
I (5721) H_SDIO_DRV: Write thread started
I (6591) RPC_WRAP: Coprocessor Boot-up
W (6751) sd_card_wifi: esp_wifi_init
W (6761) sd_card_wifi: Co-processor Name   : network_adapter
W (6761) sd_card_wifi: Co-processor Version: 2.12.3
W (6761) sd_card_wifi: Co-processor IDF Ver: v6.0
W (6761) sd_card_wifi: Co-processor Time   : 
W (6771) sd_card_wifi: Co-processor Date   : 
I (6891) eInky-P4: Doing Wi-Fi Scan
I (6891) rpc_req: Scan start Req

I (6891) RPC_WRAP: ESP Event: wifi station started
I (9431) RPC_WRAP: ESP Event: wifi station started
I (9431) RPC_WRAP: ESP Event: StaScanDone
I (9461) sd_card_wifi: Total APs scanned = 9, actual AP number ap_info holds = 9
I (9461) sd_card_wifi: AP 1 | SSID: TellMyWiFiILoveHer | RSSI: -35 dBm
I (9461) sd_card_wifi: AP 2 | SSID: SKYJKIJV | RSSI: -59 dBm
I (9471) sd_card_wifi: AP 3 | SSID: SKYJKIJV | RSSI: -65 dBm
I (9471) sd_card_wifi: AP 4 | SSID: Hyperoptic Fibre 2533 | RSSI: -78 dBm
I (9481) sd_card_wifi: AP 5 | SSID: TV network | RSSI: -79 dBm
I (9491) sd_card_wifi: AP 6 | SSID: BT-KZF8GS | RSSI: -80 dBm
I (9491) sd_card_wifi: AP 7 | SSID: EE WiFi | RSSI: -80 dBm
I (9501) sd_card_wifi: AP 8 | SSID: Cooleer_945516 | RSSI: -83 dBm
I (9501) sd_card_wifi: AP 9 | SSID: 8347 Hyperoptic Fibre Broadband | RSSI: -88 dBm
I (9511) eInky-P4: Connecting to WiFi
I (9511) sd_card_wifi: Connecting to TellMyWiFiILoveHer...
I (9541) H_API: esp_wifi_remote_connect
I (9561) sd_card_wifi: wifi_init_sta finished.
I (12311) RPC_WRAP: ESP Event: Station mode: Connected
I (12311) esp_wifi_remote: esp_wifi_internal_reg_rxcb: sta: 0x400622d2
--- 0x400622d2: wifi_sta_receive at /home/user/.espressif/v6.0/esp-idf/components/esp_wifi/src/wifi_netif.c:38
I (13341) esp_netif_handlers: sta ip: 192.168.1.82, mask: 255.255.255.0, gw: 192.168.1.1
I (13341) sd_card_wifi: got ip:192.168.1.82
I (13341) sd_card_wifi: connected to ap SSID:TellMyWiFiILoveHer
I (13341) web_server: HTTP/WebSocket server started on port 80
I (13351) main_task: Returned from app_main()
...
```

## Troubleshooting

For any technical queries, please open an [issue](https://github.com/ddB0515/epdInky/issues) on GitHub. We will get back to you soon.

