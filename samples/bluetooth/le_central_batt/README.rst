.. _bluetooth-central-battery-sample:
BLE Battery Central Client Sample
##################################
Overview
********
This sample demonstrates a Bluetooth LE Central device that:
* Scans for BLE peripherals advertising the Battery Service
* Connects to a peripheral device named "ALIF_BATT_BLE"
* Discovers the Battery Service on the connected device
* Reads battery level values
* Enables notifications for battery level changes
The sample acts as a central/client and should be paired with a peripheral device
running the Battery Service (such as the le_periph_batt sample).
Requirements
************
* Alif Balletto Development Kit
* A BLE peripheral device advertising Battery Service with device name "ALIF_BATT_BLE"
  (e.g., another board running the le_periph_batt sample)
Building and Running
********************
This sample can be found under :zephyr_file:`samples/bluetooth/le_central_batt` in the
sdk-alif tree.
Build and flash the sample as follows:
.. code-block:: console
   west build -b <board> samples/bluetooth/le_central_batt
   west flash
After flashing, the device will:
1. Start scanning for BLE peripherals
2. Connect to any device advertising with the name "ALIF_BATT_BLE"
3. Discover and read the battery level
4. Subscribe to battery level notifications
Expected Output
***************
On the serial console, you should see output similar to:
.. code-block:: console
   *** Booting Zephyr OS build v3.x.x ***
   [00:00:00.100,000] <inf> main: Starting BLE central battery client
   [00:00:00.200,000] <inf> main: Scanning for peripherals...
   [00:00:05.000,000] <inf> main: Peripheral found: ALIF_BATT_BLE
   [00:00:05.100,000] <inf> main: Connected
   [00:00:05.500,000] <inf> batt_cli: Battery service discovered
   [00:00:05.600,000] <inf> batt_cli: Battery level: 95%
```
