# Suggested Code Fixes for Commit 7299f821d40e

This document provides specific code changes to address the issues identified in COMMIT_REVIEW.md.

## 1. Fix Critical Memory Issue in batt_cli.c

### Current Code (CRITICAL BUG):
```c
static batt_client_env_t* p_batt_env;  // Uninitialized pointer!
```

### Fixed Code:
```c
static batt_client_env_t batt_env = {0};
static batt_client_env_t* p_batt_env = &batt_env;
```

**Impact:** Prevents segmentation fault/crash from dereferencing NULL pointer.

---

## 2. Fix batt_cli.h - Add Header Guards and Documentation

### Current Code:
```c
uint16_t add_profile_client(void);
void profile_client_process(uint8_t conidx, uint8_t event);
```

### Fixed Code:
```c
/* Copyright (C) 2025 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification of this code is permitted under the
 * terms stated in the Alif Semiconductor Software License Agreement
 *
 * You should have received a copy of the Alif Semiconductor Software
 * License Agreement with this file. If not, please write to:
 * contact@alifsemi.com, or visit: https://alifsemi.com/license
 */

#ifndef BATT_CLI_H
#define BATT_CLI_H

#include <stdint.h>

/**
 * @brief Add battery client profile to BLE stack
 * 
 * Registers the Battery Service Client (BASC) profile with the BLE stack.
 * 
 * @return GAP_ERR_NO_ERROR (0x00) on success, error code otherwise
 */
uint16_t add_profile_client(void);

/**
 * @brief Process battery client events and initiate service discovery
 * 
 * @param conidx Connection index for the BLE connection
 * @param event Event type to process (typically EVENT_SERVICE_DISCOVERY_START)
 */
void profile_client_process(uint8_t conidx, uint8_t event);

#endif /* BATT_CLI_H */
```

---

## 3. Clean Up Debug Code in batt_cli.c

### Replace all printk() with proper logging:

```c
// Add at top of file:
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(batt_cli, LOG_LEVEL_DBG);

// Then replace:
printk("batt level updated %d\n", level);
// With:
LOG_INF("Battery level updated: %d%%", level);

printk("BASC ENABLE CALLBACK\n");
// With:
LOG_DBG("BASC enable callback received");

printk("Error during enable 0x%02x\n", status);
// With:
LOG_ERR("Error during enable: 0x%02x", status);
```

### Remove commented code:
```c
// DELETE these lines:
//     ASSERT_WARN(status == GAP_ERR_NO_ERROR, status, 0);
//     if (p_batt_env->status_bf == STATUS_DISCOVERING_POS)
//     {
// 	printk("   under if before calling discovery\n");
//         service_discovery(EVENT_SERVICE_CONTENT_DISCOVERED, conidx, status);
// 	printk("   under if after calling discovery\n");
//     }
```

### Fix debug artifacts:
```c
// CHANGE:
printk("LOOKS GOOD!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!1\n");
// TO:
LOG_DBG("Service discovery successful, %d battery service instances found", bas_nb);
```

---

## 4. Improve Error Handling in batt_cli.c

### Current callback:
```c
static void on_cb_enable_cmp(uint8_t conidx, uint16_t status, uint8_t bas_nb,
                             const bas_content_t* p_bas)
{
    printk("BASC ENABLE CALLBACK\n");
    printk("status 0x%02x\n", status);
    printk("bas_nb 0x%02x\n", bas_nb);

    if ((status == 0) && (bas_nb == 1)) {
        printk("LOOKS GOOD!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!1\n");
        service_discovery(EVENT_SERVICE_CONTENT_DISCOVERED, conidx, GAP_ERR_NO_ERROR);
    }

    if (status != GAP_ERR_NO_ERROR) {
        printk("Error during enable 0x%02x\n", status);
        service_discovery(EVENT_SERVICE_DISCOVERY_START, conidx, GAP_ERR_NO_ERROR);
        return;
    }
    // ... more commented code
}
```

### Improved callback:
```c
static void on_cb_enable_cmp(uint8_t conidx, uint16_t status, uint8_t bas_nb,
                             const bas_content_t* p_bas)
{
    LOG_DBG("BASC enable complete - status: 0x%02x, instances: %d", status, bas_nb);

    if (status != GAP_ERR_NO_ERROR) {
        LOG_ERR("Failed to enable BASC: 0x%02x", status);
        service_discovery(EVENT_SERVICE_DISCOVERY_START, conidx, GAP_ERR_NO_ERROR);
        return;
    }

    if (bas_nb == 0) {
        LOG_WRN("No battery service instances found");
        return;
    }

    if (bas_nb >= 1) {
        LOG_INF("Battery service discovered with %d instance(s)", bas_nb);
        service_discovery(EVENT_SERVICE_CONTENT_DISCOVERED, conidx, GAP_ERR_NO_ERROR);
    }
}
```

---

## 5. Fix README.rst

### Current (Incorrect):
```rst
.. _bluetooth-periph-battery-sample:

BLE Battery Sample
#################

Overview
********

Application to demonstrate the use of the BLE Battery service.

Requirements
************

* Alif Balletto Development Kit

Building and Running
********************

This sample can be found under :zephyr_file:`samples/bluetooth/le_periph_batt` in the
sdk-alif tree.
```

### Fixed (Correct):
```rst
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

---

## 6. Add Constants for Magic Numbers in main.c

### Add these near the top of main.c:
```c
/* Scan parameters */
#define SCAN_INTERVAL_UNITS        60    /* Scan interval in 0.625ms units (37.5ms) */
#define SCAN_WINDOW_UNITS          60    /* Scan window in 0.625ms units (37.5ms) */
#define SCAN_DURATION_MS           0     /* Scan indefinitely */
#define SCAN_PERIOD_MS             0     /* No periodic scanning */

/* Device name to search for */
#define PERIPH_DEVICE_NAME         "ALIF_BATT_BLE"

/* Connection parameters */
#define CONN_INTERVAL_MIN          6     /* 7.5ms */
#define CONN_INTERVAL_MAX          200   /* 250ms */
#define CONN_LATENCY              0
#define CONN_SUPERVISION_TIMEOUT   300   /* 3000ms */

/* Application states */
#define DEVICE_CONFIGURED          0
#define PERIPHERAL_FOUND           1
#define PERIPHERAL_CONNECTED       2
#define INITIATION_STOPPED         3
#define TIMER_EXPIRED              4
```

### Then update the scan parameter structure:
```c
static gapm_le_scan_param_t param = {
    .type                       = GAPM_SCAN_TYPE_GEN_DISC,
    .prop                       = GAPM_SCAN_PROP_PHY_1M_BIT,
    .dup_filt_pol               = GAPM_DUP_FILT_DIS,
    .scan_param_1m.scan_intv    = SCAN_INTERVAL_UNITS,
    .scan_param_1m.scan_wd      = SCAN_WINDOW_UNITS,
    .scan_param_coded.scan_intv = 0,
    .scan_param_coded.scan_wd   = 0,
    .duration                   = SCAN_DURATION_MS,
    .period                     = SCAN_PERIOD_MS,
};
```

---

## 7. Fix Typos in main.c

### Line ~483:
```c
// CHANGE:
printk("sacen restarted--------------------------------------------------------------\n");
// TO:
LOG_INF("Scan restarted after disconnection");
```

### Line ~521:
```c
// CHANGE:
printk("acceptinc connection on state machine\n");
// TO:
LOG_DBG("Accepting connection in state machine");
```

### Line ~152:
```c
// CHANGE:
printk("accepted ith no error from ON_CONNECTED\n");
// TO:
LOG_DBG("Connection accepted successfully");
```

---

## 8. Clean Up Main Loop

### Current Code:
```c
int i = 0;
while (1) {
    k_sleep(K_MSEC(100));
    // printk("blinky %u\n", i);
    i++;
}
return 0;
```

### Fixed Code:
```c
/* Main thread can sleep - all processing is done in callbacks */
while (1) {
    k_sleep(K_FOREVER);
}

return 0;
```

---

## 9. Remove Commented Code in main.c

### Delete these entire blocks:

Around line 50:
```c
// DELETE:
// typedef struct app_env_
// {
//     /// Auto disconnect timer
//     co_timer_t timer;
//     /// Peripheral BD address
//     gap_bdaddr_t    periph_addr;
//     /// SCAN activity index
//     uint8_t         scan_actv_idx;
//     /// INIT activity index
//     uint8_t         init_actv_idx;
//     /// Connection index
//     uint8_t         conidx;
//     /// Used to know if peripheral found
//     bool            periph_found;
// } app_env_t;
```

Around line 480:
```c
// DELETE:
// err = start_le_adv(adv_actv_idx);
// if (err) {
//     LOG_ERR("Error restarting advertising: %u", err);
// } else {
//     LOG_DBG("Restarting advertising");
// }
```

---

## 10. Improve le_central_process() Error Handling

### Current Code (line ~520):
```c
case PERIPHERAL_CONNECTED:
    printk("acceptinc connection on state machine\n");
    status = gapc_le_connection_cfm(central_env.conidx, 0, NULL);
    printk("CONNECTED, WAITING\n");
    if (status) {
        printk("failed stop\n");
        break;
    }
```

### Fixed Code:
```c
case PERIPHERAL_CONNECTED:
    LOG_DBG("Accepting connection in state machine");
    status = gapc_le_connection_cfm(central_env.conidx, 0, NULL);
    if (status != GAP_ERR_NO_ERROR) {
        LOG_ERR("Failed to confirm connection: 0x%02x", status);
        break;
    }
    
    LOG_INF("Connected to peripheral");
    
    if (central_env.connected) {
        LOG_DBG("Starting battery client service discovery");
        profile_client_process(central_env.conidx, EVENT_SERVICE_DISCOVERY_START);
    } else {
        LOG_WRN("Connection confirmed but state shows disconnected");
    }
    break;
```

---

## 11. Fix Config/Code Device Name Inconsistency

### Option 1: Use config in code
In main.c, replace:
```c
static const char le_periph_device_name[] = "ALIF_BATT_BLE";
```

With:
```c
#ifndef CONFIG_BLE_PERIPHERAL_NAME
#define CONFIG_BLE_PERIPHERAL_NAME "ALIF_BATT_BLE"
#endif

static const char le_periph_device_name[] = CONFIG_BLE_PERIPHERAL_NAME;
```

And add to prj.conf:
```
CONFIG_BLE_PERIPHERAL_NAME="ALIF_BATT_BLE"
```

### Option 2: Keep consistent names
Either change prj.conf:
```
CONFIG_BLE_DEVICE_NAME="ALIF_BATT_CTRL"  # This device (central)
```

Or update the search name in code to match a different peripheral.

---

## 12. Add Complete State Handling

### In le_central_process(), add missing state:
```c
case INITIATION_STOPPED:
    LOG_DBG("Initiation stopped, restarting scan");
    status = gapm_le_start_scan(central_env.scan_actv_idx, &param);
    if (status != GAP_ERR_NO_ERROR) {
        LOG_ERR("Failed to restart scan: 0x%02x", status);
    }
    break;

case TIMER_EXPIRED:
    LOG_DBG("Timer expired");
    /* Add timer expiry handling logic here */
    break;

default:
    LOG_WRN("Unhandled event: %d", event);
    break;
```

---

## 13. Add NULL Checks to Callbacks (Optional but Recommended)

### Example for on_cb_enable_cmp:
```c
static void on_cb_enable_cmp(uint8_t conidx, uint16_t status, uint8_t bas_nb,
                             const bas_content_t* p_bas)
{
    /* Validate environment pointer */
    if (p_batt_env == NULL) {
        LOG_ERR("Battery client environment not initialized");
        return;
    }

    LOG_DBG("BASC enable complete - status: 0x%02x, instances: %d", status, bas_nb);
    
    /* Rest of function... */
}
```

---

## Summary of Changes

### Critical (Must Fix):
1. ✅ Initialize p_batt_env pointer
2. ✅ Add header guards to batt_cli.h
3. ✅ Remove debug printk and use LOG_* macros
4. ✅ Fix README.rst reference label and path
5. ✅ Fix typos

### Important (Should Fix):
6. ✅ Remove commented code
7. ✅ Define constants for magic numbers
8. ✅ Improve error handling and logging
9. ✅ Clean up main loop
10. ✅ Add missing state handlers

### Nice to Have:
11. ✅ Add comprehensive function documentation
12. ✅ Add NULL checks in callbacks
13. Consider adding unit tests (if test framework exists)

## Testing Recommendations

After applying fixes:

1. **Build Test**: Ensure code compiles without warnings
   ```bash
   west build -b <board> samples/bluetooth/le_central_batt -p
   ```

2. **Static Analysis**: Run any available static analysis tools

3. **Integration Test**: 
   - Flash le_periph_batt to one board
   - Flash le_central_batt to another board
   - Verify connection and battery level reading

4. **Stress Test**:
   - Test reconnection after disconnection
   - Test with multiple scan/connect cycles
   - Verify no memory leaks

## Next Steps

1. Apply critical fixes first
2. Test each change incrementally
3. Update commit message from "temp_working" to descriptive message
4. Consider squashing commits before final merge
5. Add entry to CHANGELOG if applicable
