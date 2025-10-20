#include "prf.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "basc.h"

LOG_MODULE_REGISTER(batt_cli, LOG_LEVEL_DBG);

// Event values for Service Discovery state machine
enum batt_client_discovery_events
{
    // Request to start
    EVENT_SERVICE_DISCOVERY_START = 0U,
    // Discovery of Battery Service instances has been completed
    EVENT_SERVICE_CONTENT_DISCOVERED,
    // Battery Level value has been read
    EVENT_LEVEL_READ,
    // Sending of notifications for Battery Level characteristics has been enabled
    EVENT_SENDING_EVENTS_ENABLED,
};

// Status bit field
enum batt_client_status_bf
{
    // Indicate if discovery is in progress - Position
    STATUS_DISCOVERING_POS = 0U,
    // Indicate if discovery is in progress - Bit
    STATUS_DISCOVERING_BIT = CO_BIT(STATUS_DISCOVERING_POS),
};

static uint8_t discovery_event;

void service_discovery(uint8_t event, uint8_t conidx, uint16_t status);


static void on_cb_batt_level_upd(uint8_t conidx, uint8_t bas_instance, uint8_t level)
{
	LOG_INF("batt level updated %d", level);
}

static void on_cb_enable_cmp(uint8_t conidx, uint16_t status, uint8_t bas_nb,
				const bas_content_t *p_bas)
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

static void on_cb_read_batt_level_cmp(uint8_t conidx, uint16_t status, uint8_t bas_instance,
					uint8_t level)
{
	if (status == GAP_ERR_NO_ERROR)	{
		LOG_INF("batt level read %d", level);
	} else {
		LOG_ERR("error reading batt level");
	}

	service_discovery(EVENT_LEVEL_READ, conidx, status);
}

static void on_cb_read_ntf_cfg_cmp(uint8_t conidx, uint16_t status, uint8_t bas_instance,
					uint16_t ntf_ind_cfg)
{
}

static void on_cb_read_pres_format_cmp(uint8_t conidx, uint16_t status, uint8_t bas_instance,
					const prf_char_pres_fmt_t *p_pres_format)
{
}

static void on_cb_write_ntf_cfg_cmp(uint8_t conidx, uint16_t status, uint8_t bas_instance)
{
	LOG_INF("Enable wrtie ntf callback status 0x%02x", status);

	service_discovery(EVENT_SENDING_EVENTS_ENABLED, conidx, status);
}


static const basc_cb_t cbs_basc = {
	.cb_enable_cmp = on_cb_enable_cmp,
	.cb_read_batt_level_cmp = on_cb_read_batt_level_cmp,
	.cb_read_ntf_cfg_cmp = on_cb_read_ntf_cfg_cmp,
	.cb_read_pres_format_cmp = on_cb_read_pres_format_cmp,
	.cb_write_ntf_cfg_cmp = on_cb_write_ntf_cfg_cmp,
	.cb_batt_level_upd = on_cb_batt_level_upd,
};

uint16_t add_profile_client(void)
{
	uint8_t err;

	err = prf_add_profile(TASK_ID_BASC, 0, 0, NULL, &cbs_basc, NULL);

	if (err) {
		LOG_ERR("error adding profile 0x%02X", err);
		return err;
	} else {
		LOG_INF("profile added!");
	}

	return GAP_ERR_NO_ERROR;
};

void service_discovery(uint8_t event, uint8_t conidx, uint16_t status)
{
	if (status == GAP_ERR_NO_ERROR) {
		switch(event)
		{

		case EVENT_SERVICE_DISCOVERY_START:
			/* add and discover service*/
			uint16_t err = basc_enable(conidx, PRF_CON_DISCOVERY, 0, NULL);
			if (err) {
				LOG_ERR("Error during BASC enable 0x%02x", err);
			}
			break;

		case EVENT_SERVICE_CONTENT_DISCOVERED:
			err = basc_read_batt_level(conidx, 0);
			if (err) {
				LOG_ERR("Error reading 0x%02x", err);
			}
			break;

		case EVENT_LEVEL_READ:
			basc_write_ntf_cfg(conidx, 0, PRF_CLI_START_NTF);
			discovery_event = EVENT_LEVEL_READ;
			break;

		case EVENT_SENDING_EVENTS_ENABLED:
			break;

		default:
			break;
		}
	} else {
		LOG_ERR("service discovery process error 0x%02x\n", status);
	}
	return;
}

void battery_client_process(uint8_t conidx, uint8_t event)
{
		service_discovery(event, conidx, GAP_ERR_NO_ERROR);
};
