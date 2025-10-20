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
void battery_client_process(uint8_t conidx, uint8_t event);

#endif /* BATT_CLI_H */
