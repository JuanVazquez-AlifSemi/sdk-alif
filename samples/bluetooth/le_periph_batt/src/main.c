/* Copyright (C) 2024 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification of this code is permitted under the
 * terms stated in the Alif Semiconductor Software License Agreement
 *
 * You should have received a copy of the Alif Semiconductor Software
 * License Agreement with this file. If not, please write to:
 * contact@alifsemi.com, or visit: https://alifsemi.com/license
 */

/*
 * This example demonstrates the Battery Service usage
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "alif_ble.h"
#include "gapm.h"
#include "gap_le.h"
#include "gapc_le.h"
#include "gapc_sec.h"
#include "gapm_le.h"
#include "gapm_le_adv.h"
#include "co_buf.h"
#include "prf.h"
#include "bass.h"
#include "bas.h"

#define BATT_INSTANCE 0x00
#define BT_CONN_STATE_CONNECTED	   0x00
#define BT_CONN_STATE_DISCONNECTED 0x01
static uint8_t conn_status = BT_CONN_STATE_DISCONNECTED;

#if !CONFIG_ALIF_BLE_ROM_IMAGE_V1_0 /* ROM version > 1.0 */
/* Notifications bit field */
uint16_t ccc_bf;
#else
static bool READY_TO_SEND_BASS;
#endif /* !CONFIG_ALIF_BLE_ROM_IMAGE_V1_0 */

K_SEM_DEFINE(init_sem, 0, 1);
K_SEM_DEFINE(conn_sem, 0, 1);

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

/**
 * Bluetooth stack configuration
 */
static const gapm_config_t gapm_cfg = {
	.role = GAP_ROLE_LE_PERIPHERAL,
	.pairing_mode = GAPM_PAIRING_DISABLE,
	.privacy_cfg = 0,
	.renew_dur = 1500,
	.private_identity.addr = {0xCA, 0xFE, 0xFB, 0xDE, 0x11, 0x07},
	.irk.key = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	.gap_start_hdl = 0,
	.gatt_start_hdl = 0,
	.att_cfg = 0,
	.sugg_max_tx_octets = GAP_LE_MIN_OCTETS,
	.sugg_max_tx_time = GAP_LE_MIN_TIME,
	.tx_pref_phy = GAP_PHY_ANY,
	.rx_pref_phy = GAP_PHY_ANY,
	.tx_path_comp = 0,
	.rx_path_comp = 0,
	.class_of_device = 0,  /* BT Classic only */
	.dflt_link_policy = 0, /* BT Classic only */
};

 /* Load name from configuration file */
#define DEVICE_NAME CONFIG_BLE_DEVICE_NAME
static const char device_name[] = DEVICE_NAME;

/* Store advertising activity index for re-starting after disconnection */
static uint8_t adv_actv_idx;

static uint16_t start_le_adv(uint8_t actv_idx)
{
	uint16_t err;

	gapm_le_adv_param_t adv_params = {
		.duration = 0, /* Advertise indefinitely */
	};

	err = gapm_le_start_adv(actv_idx, &adv_params);
	if (err) {
		LOG_ERR("Failed to start LE advertising with error %u", err);
	}
	return err;
}

/**
 * Bluetooth GAPM callbacks
 */
static void on_le_connection_req(uint8_t conidx, uint32_t metainfo, uint8_t actv_idx, uint8_t role,
			const gap_bdaddr_t *p_peer_addr,
			const gapc_le_con_param_t *p_con_params, uint8_t clk_accuracy)
{
	LOG_INF("Connection request on index %u", conidx);
	gapc_le_connection_cfm(conidx, 0, NULL);

	LOG_DBG("Connection parameters: interval %u, latency %u, supervision timeout %u",
		 p_con_params->interval, p_con_params->latency, p_con_params->sup_to);

	LOG_HEXDUMP_DBG(p_peer_addr->addr, GAP_BD_ADDR_LEN, "Peer BD address");

	conn_status = BT_CONN_STATE_CONNECTED;

	k_sem_give(&conn_sem);

	LOG_DBG("Please enable notifications on peer device..");
}

static void on_key_received(uint8_t conidx, uint32_t metainfo, const gapc_pairing_keys_t *p_keys)
{
	LOG_WRN("Unexpected key received key on conidx %u", conidx);
}

static void on_disconnection(uint8_t conidx, uint32_t metainfo, uint16_t reason)
{
	uint16_t err;

	LOG_INF("Connection index %u disconnected for reason %u", conidx, reason);

	err = start_le_adv(adv_actv_idx);
	if (err) {
		LOG_ERR("Error restarting advertising: %u", err);
	} else {
		LOG_DBG("Restarting advertising");
	}

	conn_status = BT_CONN_STATE_DISCONNECTED;
}

static void on_name_get(uint8_t conidx, uint32_t metainfo, uint16_t token, uint16_t offset,
			uint16_t max_len)
{
	const size_t device_name_len = sizeof(device_name) - 1;
	const size_t short_len = (device_name_len > max_len ? max_len : device_name_len);

	gapc_le_get_name_cfm(conidx, token, GAP_ERR_NO_ERROR, device_name_len, short_len,
			(const uint8_t *)device_name);
}

static void on_appearance_get(uint8_t conidx, uint32_t metainfo, uint16_t token)
{
	/* Send 'unknown' appearance */
	gapc_le_get_appearance_cfm(conidx, token, GAP_ERR_NO_ERROR, 0);
}


static const gapc_connection_req_cb_t gapc_con_cbs = {
	.le_connection_req = on_le_connection_req,
};

static const gapc_security_cb_t gapc_sec_cbs = {
	.key_received = on_key_received,
	/* All other callbacks in this struct are optional */
};

static const gapc_connection_info_cb_t gapc_con_inf_cbs = {
	.disconnected = on_disconnection,
	.name_get = on_name_get,
	.appearance_get = on_appearance_get,
	/* Other callbacks in this struct are optional */
};

/* All callbacks in this struct are optional */
static const gapc_le_config_cb_t gapc_le_cfg_cbs;

#if !CONFIG_ALIF_BLE_ROM_IMAGE_V1_0 /* ROM version > 1.0 */
static void on_gapm_err(uint32_t metainfo, uint8_t code)
{
	LOG_ERR("gapm error %d", code);
}
static const gapm_cb_t gapm_err_cbs = {
	.cb_hw_error = on_gapm_err,
};

static const gapm_callbacks_t gapm_cbs = {
	.p_con_req_cbs = &gapc_con_cbs,
	.p_sec_cbs = &gapc_sec_cbs,
	.p_info_cbs = &gapc_con_inf_cbs,
	.p_le_config_cbs = &gapc_le_cfg_cbs,
	.p_bt_config_cbs = NULL, /* BT classic so not required */
	.p_gapm_cbs = &gapm_err_cbs,
};
#else
static void on_gapm_err(enum co_error err)
{
	LOG_ERR("gapm error %d", err);
}
static const gapm_err_info_config_cb_t gapm_err_cbs = {
	.ctrl_hw_error = on_gapm_err,
};

static const gapm_callbacks_t gapm_cbs = {
	.p_con_req_cbs = &gapc_con_cbs,
	.p_sec_cbs = &gapc_sec_cbs,
	.p_info_cbs = &gapc_con_inf_cbs,
	.p_le_config_cbs = &gapc_le_cfg_cbs,
	.p_bt_config_cbs = NULL, /* BT classic so not required */
	.p_err_info_config_cbs = &gapm_err_cbs,
};
#endif /* !CONFIG_ALIF_BLE_ROM_IMAGE_V1_0 */


static uint8_t battery_level = 99;



static uint16_t set_advertising_data(uint8_t actv_idx)
{
	uint16_t err;

	/* gatt service identifier */
	#if !CONFIG_ALIF_BLE_ROM_IMAGE_V1_0
	uint16_t svc = GATT_SVC_BATTERY;
	#else
	uint16_t svc = GATT_SVC_BATTERY_SERVICE;
	#endif /* !CONFIG_ALIF_BLE_ROM_IMAGE_V1_0 */

	uint8_t num_svc = 1;
	const size_t device_name_len = sizeof(device_name) - 1;
	const uint16_t adv_device_name = GATT_HANDLE_LEN + device_name_len;
	const uint16_t adv_uuid_svc = GATT_HANDLE_LEN + (GATT_UUID_16_LEN * num_svc);

	/* Create advertising data with necessary services */
	const uint16_t adv_len = adv_device_name + adv_uuid_svc;

	co_buf_t *p_buf;

	err = co_buf_alloc(&p_buf, 0, adv_len, 0);
	__ASSERT(err == 0, "Buffer allocation failed");

	uint8_t *p_data = co_buf_data(p_buf);

	p_data[0] = device_name_len + 1;
	p_data[1] = GAP_AD_TYPE_COMPLETE_NAME;
	memcpy(p_data + 2, device_name, device_name_len);

	/* Update data pointer */
	p_data = p_data + adv_device_name;
	p_data[0] = (GATT_UUID_16_LEN * num_svc) + 1;
	p_data[1] = GAP_AD_TYPE_COMPLETE_LIST_16_BIT_UUID;

	/* Copy identifier */
	memcpy(p_data + 2, (void *)&svc, sizeof(svc));

	err = gapm_le_set_adv_data(actv_idx, p_buf);
	co_buf_release(p_buf);
	if (err) {
		LOG_ERR("Failed to set advertising data with error %u", err);
	}

	return err;
}

static uint16_t set_scan_data(uint8_t actv_idx)
{
	co_buf_t *p_buf;
	uint16_t err = co_buf_alloc(&p_buf, 0, 0, 0);

	__ASSERT(err == 0, "Buffer allocation failed");

	err = gapm_le_set_scan_response_data(actv_idx, p_buf);
	co_buf_release(p_buf); /* Release ownership of buffer so stack can free it when done */
	if (err) {
		LOG_ERR("Failed to set scan data with error %u", err);
	}

	return err;
}

/**
 * Advertising callbacks
 */
static void on_adv_actv_stopped(uint32_t metainfo, uint8_t actv_idx, uint16_t reason)
{
	LOG_DBG("Advertising activity index %u stopped for reason %u", actv_idx, reason);
}

static void on_adv_actv_proc_cmp(uint32_t metainfo, uint8_t proc_id, uint8_t actv_idx,
				uint16_t status)
{
	if (status) {
		LOG_ERR("Advertising activity process completed with error %u", status);
	return;
	}

	switch (proc_id) {
	case GAPM_ACTV_CREATE_LE_ADV:
		LOG_DBG("Advertising activity is created");
		adv_actv_idx = actv_idx;
		set_advertising_data(actv_idx);
		break;

	case GAPM_ACTV_SET_ADV_DATA:
		LOG_DBG("Advertising data is set");
		set_scan_data(actv_idx);
		break;

	case GAPM_ACTV_SET_SCAN_RSP_DATA:
		LOG_DBG("Scan data is set");
		start_le_adv(actv_idx);
		break;

	case GAPM_ACTV_START:
		LOG_DBG("Advertising was started");
		k_sem_give(&init_sem);
		break;

	default:
		LOG_WRN("Unexpected GAPM activity complete, proc_id %u", proc_id);
		break;
	}
}

static void on_adv_created(uint32_t metainfo, uint8_t actv_idx, int8_t tx_pwr)
{
LOG_DBG("Advertising activity created, index %u, selected tx power %d", actv_idx, tx_pwr);
}

static const gapm_le_adv_cb_actv_t le_adv_cbs = {
.hdr.actv.stopped = on_adv_actv_stopped,
.hdr.actv.proc_cmp = on_adv_actv_proc_cmp,
.created = on_adv_created,
};

static uint16_t create_advertising(void)
{
	uint16_t err;

	gapm_le_adv_create_param_t adv_create_params = {
		.prop = GAPM_ADV_PROP_UNDIR_CONN_MASK,
		.disc_mode = GAPM_ADV_MODE_GEN_DISC,
		#if !CONFIG_ALIF_BLE_ROM_IMAGE_V1_0 /* ROM version > 1.0 */
		.tx_pwr = 0,
		#else
		.max_tx_pwr = 0,
		#endif /* !CONFIG_ALIF_BLE_ROM_IMAGE_V1_0 */
		.filter_pol = GAPM_ADV_ALLOW_SCAN_ANY_CON_ANY,
		.prim_cfg = {
			.adv_intv_min = 160, /* 100 ms */
			.adv_intv_max = 800, /* 500 ms */
			.ch_map = ADV_ALL_CHNLS_EN,
			.phy = GAPM_PHY_TYPE_LE_1M,
		},
	};

	err = gapm_le_create_adv_legacy(0, GAPM_STATIC_ADDR, &adv_create_params, &le_adv_cbs);
	if (err) {
		LOG_ERR("Error %u creating advertising activity", err);
	}
	return err;
}

void on_gapm_process_complete(uint32_t metainfo, uint16_t status)
{
if (status) {
	LOG_ERR("gapm process completed with error %u", status);
return;
}

LOG_DBG("gapm process completed successfully");

create_advertising();
}

#if !CONFIG_ALIF_BLE_ROM_IMAGE_V1_0 /* ROM version > 1.0 */

__STATIC co_buf_t *battery_server_prepare_buf_level(void)
{
	co_buf_t *p_buf;

	prf_buf_alloc(&p_buf, BAS_LEVEL_SIZE_MAX);
	*co_buf_data(p_buf) = battery_level;

	return p_buf;
}

static void on_value_req(uint8_t conidx, uint8_t instance_idx, uint8_t char_type, uint16_t token)
{
	co_buf_t *p_buf = NULL;

	switch (char_type) {
	case BASS_CHAR_TYPE_LEVEL:
		p_buf = battery_server_prepare_buf_level();
		break;
	default:
		LOG_WRN("REQUEST NOT SUPPORTED");
		break;
	}

	if (p_buf != NULL) {
		bass_value_cfm(conidx, token, p_buf);
		co_buf_release(p_buf);
	}
}

static void on_get_cccd_req(uint8_t conidx, uint8_t instance_idx, uint8_t char_type, uint16_t token)
{
	co_buf_t *p_buf;
	uint16_t value = ((ccc_bf & CO_BIT(char_type)) != 0u)
				? PRF_CLI_START_NTF : PRF_CLI_STOP_NTFIND;

	prf_buf_alloc(&p_buf, PRF_CCC_DESC_LEN);
	co_write16(co_buf_data(p_buf), co_htole16(value));
	bass_value_cfm(conidx, token, p_buf);
	co_buf_release(p_buf);

	LOG_INF("Get CCCD request for 0x%02x characteristic",
		char_type);
}

static void on_set_cccd_req(uint8_t conidx, uint8_t instance_idx, uint8_t char_type, uint16_t token,
			co_buf_t *p_buf)
{
	LOG_INF("Set char_type: 0x%02x", char_type);

	uint16_t value = co_letoh16(co_read16(co_buf_data(p_buf)));

	if (value != PRF_CLI_STOP_NTFIND) {
		LOG_INF("Enable CCCD request for %02x characteristic (0x%04X)", char_type, value);

		ccc_bf |= CO_BIT(char_type);
	} else {
		LOG_INF("Disable CCCD request for %02x characteristic", char_type);
		ccc_bf &= ~CO_BIT(char_type);
	}

	bass_set_cccd_cfm(conidx, GAP_ERR_NO_ERROR, token);
}

static void on_sent(uint8_t conidx, uint8_t instance_idx, uint8_t char_type, uint16_t status)
{
	if (status) {
		LOG_WRN("Value sent with status: 0x%02x", status);
	}
}

static const bass_cbs_t bass_cb = {
	.cb_value_req = on_value_req,
	.cb_get_cccd_req = on_get_cccd_req,
	.cb_set_cccd_req = on_set_cccd_req,
	.cb_sent = on_sent,
};
#else

static void on_bass_batt_level_upd_cmp(uint16_t status)
{
	READY_TO_SEND_BASS = true;
}

static void on_bass_bond_data_upd(uint8_t conidx, uint8_t ntf_ind_cfg)
{
	switch (ntf_ind_cfg) {
	case PRF_CLI_STOP_NTFIND:
		LOG_INF("Client requested BASS stop notification/indication (conidx: %u)", conidx);
		READY_TO_SEND_BASS = false;
		break;

	case PRF_CLI_START_NTF:
	case PRF_CLI_START_IND:
		LOG_INF("Client requested BASS start notification/indication (conidx: %u)", conidx);
		READY_TO_SEND_BASS = true;
		LOG_DBG("Sending battery level");
		break;
	default:
		LOG_WRN("Unknown notification/indocation update");
		break;
	}
}

static const bass_cb_t bass_cb = {
	.cb_batt_level_upd_cmp = on_bass_batt_level_upd_cmp,
	.cb_bond_data_upd = on_bass_bond_data_upd,
};
#endif /* !CONFIG_ALIF_BLE_ROM_IMAGE_V1_0 */

static void config_battery_service(void)
{
	uint16_t err;
	uint16_t start_hdl = 0;

	#if !CONFIG_ALIF_BLE_ROM_IMAGE_V1_0 /* ROM version > 1.0 */
	uint8_t bass_cfg_bf = 0;

	err = prf_add_profile(TASK_ID_BASS, 0, 0, &bass_cfg_bf, &bass_cb, &start_hdl);

	#else
	struct bass_db_cfg bass_cfg;

	bass_cfg.bas_nb = 1;
	bass_cfg.features[0] = 1;
	err = prf_add_profile(TASK_ID_BASS, 0, 0, &bass_cfg, &bass_cb, &start_hdl);
	#endif /* !CONFIG_ALIF_BLE_ROM_IMAGE_V1_0 */

	if (err) {
		LOG_ERR("Error adding service: 0x%02x", err);
	}
}

static void battery_process(void)
{
	uint16_t err;
	/* Fixed value for demonstrating purposes */


	if (battery_level <= 1) {
		battery_level = 99;
	} else {
		battery_level--;
	}

	#if !CONFIG_ALIF_BLE_ROM_IMAGE_V1_0 /* ROM version > 1.0 */
	if ((ccc_bf & CO_BIT(BASS_CHAR_TYPE_LEVEL)) != 0u) {
		co_buf_t *p_buf;
		uint8_t evt_type;

		evt_type = GATT_NOTIFY;
		p_buf = battery_server_prepare_buf_level();

		/* Sending dummy battery level to first battery instance*/
		err = bass_update_value(0, BATT_INSTANCE, BASS_CHAR_TYPE_LEVEL, evt_type, p_buf);

		if (err) {
			LOG_ERR("Error %u sending battery level", err);
		}

		co_buf_release(p_buf);
	}
	#else
	if (READY_TO_SEND_BASS) {
		/* Sending dummy battery level to first battery instance*/
		err = bass_batt_level_upd(BATT_INSTANCE, battery_level);

		if (err) {
			LOG_ERR("Error %u sending battery level", err);
		}
	}
	#endif /* !CONFIG_ALIF_BLE_ROM_IMAGE_V1_0 */
}

int main(void)
{
	uint16_t err;

	/* Start up bluetooth host stack */
	alif_ble_enable(NULL);

	err = gapm_configure(0, &gapm_cfg, &gapm_cbs, on_gapm_process_complete);
	if (err) {
		LOG_ERR("gapm_configure error %u", err);
		return -1;
	}

	config_battery_service();

	LOG_DBG("Waiting for init...\n");
	k_sem_take(&init_sem, K_FOREVER);

	LOG_DBG("Init complete!\n");

	while (1) {
		k_sleep(K_SECONDS(2));

		battery_process();
	}
}
