[Click here](../README.md) to view the README.

## Design and implementation

The design of this application is minimalistic to get started with code examples on PSOC&trade; Edge MCU devices. All PSOC&trade; Edge E84 MCU applications have a dual-CPU three-project structure to develop code for the CM33 and CM55 cores. The CM33 core has two separate projects for the secure processing environment (SPE) and non-secure processing environment (NSPE). A project folder consists of various subfolders, each denoting a specific aspect of the project. The three project folders are as follows:

**Table 1. Application projects**

Project | Description
--------|------------------------
*proj_cm33_s* | Project for CM33 secure processing environment (SPE)
*proj_cm33_ns* | Project for CM33 non-secure processing environment (NSPE)
*proj_cm55* | CM55 project

<br>

In this code example, at device reset, the secure boot process starts from the ROM boot with the secure enclave (SE) as the root of trust (RoT). From the secure enclave, the boot flow is passed on to the system CPU subsystem where the secure CM33 application starts. After all necessary secure configurations, the flow is passed on to the non-secure CM33 application. Resource initialization for this example is performed by this CM33 non-secure project. It configures the system clocks, pins, clock to peripheral connections, and other platform resources. It then enables the CM55 core using the `Cy_SysEnableCM55()` function and the CM55 core is subsequently put to DeepSleep mode.

In the main firmware routine, the USB block is configured to use the communication device class (CDC). After enumeration, the device periodically checks for activity on USB at every 1 ms via an application timer. The `USBD_GetState()` API called in the application timer callback supervises the suspend condition on the USB bus. `USB_OS_GetTickCnt()` function is used for USBD timer in a non-RTOS environment. 

The USB device is suspended if there is no activity on the USB bus for longer than 3 ms. If the USB suspend condition is detected, PSOC&trade; Edge CM33 CPU leaves the active power mode and goes to sleep. This allows the device to reduce the power consumption while the host does not communicate with it. Before going to sleep, the application turns off the user LED1 and disables the USBD and application timers. On detection of any activity on the USB bus, both the timers get enabled and user LED1 is turned back on. 

<br>