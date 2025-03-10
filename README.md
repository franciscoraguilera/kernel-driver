# Apple Xserve Front Panel USB Driver

This repository contains a Linux kernel USB driver for the Apple Xserve Front Panel. It provides support for basic bulk IN/OUT transfers, device‑specific IOCTL commands, and asynchronous event handling via an interrupt endpoint.

## Overview

The driver is designed to interface with the Apple Xserve Front Panel (Vendor ID: `0x05AC`, Product ID: `0x821B`) and provides:
- **Bulk Transfers:** Data read and write through USB bulk endpoints.
- **Interrupt Handling:** Asynchronous event notifications via an interrupt endpoint.
- **IOCTL Interface:** Device‑specific commands for operations such as retrieving device status and setting LED brightness.
- **Character Device Interface:** Creation of `/dev/driver*` nodes for user-space interaction.

## Features

- **USB Bulk Transfers:**  
  Read from and write to the device using bulk IN and OUT endpoints.

- **Interrupt Endpoint Support:**  
  Continuously monitors and processes asynchronous events from the device.

- **Device‑Specific IOCTL Commands:**  
  - `XSERVE_FP_IOCTL_GET_STATUS`: Retrieve the device status using a vendor-specific control message.
  - `XSERVE_FP_IOCTL_SET_LED`: Set LED brightness (or similar hardware functionality) via a vendor-specific control message.

- **Character Device Interface:**  
  Exposes device functionality through a standard character device interface.

## Requirements

- **Kernel Version:** Linux Kernel 3.x or later.
- **Development Tools:** Kernel headers, build tools, and a working Linux build environment.
- **Hardware:** Apple Xserve Front Panel with Vendor ID `0x05AC` and Product ID `0x821B`.

## Building and Installation

1. **Clone the Repository:**

   ```bash
   git clone https://github.com/franciscoraguilera/driver.git
   cd driver
   ```

2. **Build the Driver:**

   Use the kernel build system to compile the module:

   ```bash
   make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
   ```

3. **Insert the Module:**

   Load the driver into the kernel:

   ```bash
   sudo insmod driver.ko
   dmesg | tail -n 20  # Verify that the device was attached successfully.
   ```

4. **Create Device Nodes (if not automatically created):**

   The driver registers a minor number (starting at 192) and should create `/dev/driver0`, `/dev/driver1`, etc. If needed, manually create a node:

   ```bash
   sudo mknod /dev/driver0 c 192 0
   sudo chmod 666 /dev/driver0
   ```

## Usage

### Reading Data:

Read from the device using standard file operations:

```bash
cat /dev/driver0
```

### Writing Data:

Write data to the device:

```bash
echo "your data" > /dev/driver0
```

### Using IOCTL Commands:

Use the provided IOCTL interface in your application to perform device‑specific operations. For example:

```c
// Example snippet in C
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include "driver_ioctl.h"  // Ensure this header defines XSERVE_FP_IOCTL_GET_STATUS and XSERVE_FP_IOCTL_SET_LED

int main(void) {
    int fd = open("/dev/driver0", O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    int status;
    if (ioctl(fd, XSERVE_FP_IOCTL_GET_STATUS, &status) < 0) {
        perror("ioctl GET_STATUS failed");
    } else {
        printf("Device status: %d\n", status);
    }

    int led_value = 128;  // Example LED brightness value
    if (ioctl(fd, XSERVE_FP_IOCTL_SET_LED, &led_value) < 0) {
        perror("ioctl SET_LED failed");
    }

    close(fd);
    return 0;
}
```

## Driver Structure

### driver.c:
Contains the full implementation of the USB driver.

#### Key Components:

- **Probe/Disconnect Functions:**
  Handle device initialization and cleanup.
- **Bulk Transfer Handling:**
  Implement data transfers through bulk IN and OUT endpoints.
- **Interrupt Handling:**
  Manage asynchronous events from the device via an interrupt endpoint.
- **IOCTL Interface:**
  Support device-specific commands for enhanced functionality.
- **Character Device Registration:**
  Automatically registers a device node under `/dev/driver*`.

## Uninstallation

To remove the driver from the kernel, execute:

```bash
sudo rmmod driver
```

Remove the device node if it was manually created:

```bash
sudo rm /dev/driver0
```

## License

This project is licensed under the GNU General Public License (GPL).

## Author

Fran (franciscoaguilera@ieee.org)
