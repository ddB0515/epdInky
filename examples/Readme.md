# Examples

This directory contains example applications demonstrating the capabilities of **epdInky** and the supported hardware peripherals. The examples range from basic e-paper rendering to Wi-Fi, camera, MIPI DSI, and JSON-based applications.

## Directory Structure

```text
examples/
├── epd/
│   ├── basic/             # Basic e-paper drawing example
│   ├── calendar/          # Calendar application
│   ├── FastJsonBasic/     # FastJson basic usage
│   ├── FastJsonBLE/       # FastJson over Bluetooth LE
│   └── matrix_editor/     # Interactive pixel matrix editor
├── csi/                   # MIPI CSI camera examples
├── mipi/
│   └── mipi_dsi_a260/     # MIPI DSI display example
└── wifi/                  # Wi-Fi networking examples
```

## Requirements

Before building any example, make sure you have:

- ESP-IDF installed and configured
- A supported ESP32 target
- The required display and peripherals connected
- All project dependencies installed

## Building an Example

Each example is an independent ESP-IDF project.

```bash
cd examples/epd/basic

idf.py set-target esp32p4
idf.py build
idf.py flash monitor
```

Replace the directory with the example you want to build.

## Examples

### EPD

| Example | Description |
|---------|-------------|
| **basic** | A minimal example demonstrating how to initialize the e-paper display and render basic graphics and text. |
| **calendar** | Displays a calendar interface using the e-paper display, including text rendering and layout management. |
| **matrix_editor** | Interactive pixel editor demonstrating framebuffer manipulation, drawing primitives, and partial display updates. |
| **FastJsonBasic** | Demonstrates using the FastJson library for parsing and generating JSON data. |
| **FastJsonBLE** | Demonstrates Bluetooth LE communication using FastJson for data serialization and exchange. |

### CSI

Camera interface examples demonstrating:

- MIPI CSI initialization
- Frame capture
- Image processing pipelines

### MIPI

#### `mipi_dsi_a260`

Demonstrates driving a MIPI DSI display, including:

- Display initialization
- Framebuffer rendering
- Screen updates

### Wi-Fi

Networking examples showing how to:

- Connect to a Wi-Fi access point
- Perform network communication
- Build connected applications

## Adding New Examples

New examples should follow the standard ESP-IDF project layout:

```text
example_name/
├── main/
├── CMakeLists.txt
├── README.md
└── sdkconfig.defaults
```

Keeping each example self-contained makes it easier to build, understand, and maintain.

## Hardware Requirements

Some examples require additional hardware, such as:

- E-paper display
- MIPI DSI display
- MIPI CSI camera
- Bluetooth LE device
- Wi-Fi network

Refer to each example's source code and configuration for hardware-specific requirements.
