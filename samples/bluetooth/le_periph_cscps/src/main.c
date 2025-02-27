/* Copyright (C) 2024 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification of this code is permitted under the
 * terms stated in the Alif Semiconductor Software License Agreement
 *
 * You should have received a copy of the Alif Semiconductor Software
 * License Agreement with this file. If not, please write to:
 * contact@alifsemi.com, or visit: https://alifsemi.com/license
 */

/*
 * This example will start an instance of a peripheral CSCPS and send
 * periodic notification updates to the first device that connects to it.
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

/*  Profile definitions */
#include "prf.h"
#include "cscps.h"
#include "cscps_msg.h"
#include "bass.h"
#include "bas.h"

#define BT_CONN_STATE_CONNECTED      0x00
#define BT_CONN_STATE_DISCONNECTED   0x01
#define CSCP_SENSOR_LOCATION_SUPPORT 0x01
#define BATT_INSTANCE 0x00

static uint8_t conn_status = BT_CONN_STATE_DISCONNECTED;

/* Variable to check if peer device is ready to receive data"*/
static bool READY_TO_SEND;
static bool READY_TO_SEND_BASS;

static uint16_t wheel_evt_time;

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
	/*      Dummy address   */
	.private_identity.addr = {0xCB, 0xFE, 0xFB, 0xDE, 0x11, 0x07},
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

/* CGMS callbacks */

static void on_meas_send_complete(uint16_t status)
{
	READY_TO_SEND = true;
}

static void on_bond_data_upd(uint8_t conidx, uint8_t char_code, uint16_t cfg_val)
{
	switch (cfg_val) {
	case PRF_CLI_STOP_NTFIND:
		LOG_INF("Client requested stop notification/indication (conidx: %u)", conidx);
		READY_TO_SEND = false;
		break;
	case PRF_CLI_START_NTF:
	case PRF_CLI_START_IND:
		LOG_INF("Client requested start notification/indication (conidx: %u)", conidx);
		LOG_DBG("Sending measurements");
		READY_TO_SEND = true;
		break;
	default:
		break;
	}
}

static void on_ctnl_pt_req(uint8_t conidx, uint8_t op_code,
			   const union cscp_sc_ctnl_pt_req_val *p_value)
{
}

static void on_cb_ctnl_pt_rsp_send_cmp(uint8_t conidx, uint16_t status)
{
}

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
		break;
	}
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

/*      profile callbacks */
static const cscps_cb_t cscps_cb = {
	.cb_bond_data_upd = on_bond_data_upd,
	.cb_meas_send_cmp = on_meas_send_complete,
	.cb_ctnl_pt_req = on_ctnl_pt_req,
	.cb_ctnl_pt_rsp_send_cmp = on_cb_ctnl_pt_rsp_send_cmp,
};

static const bass_cb_t bass_cb = {
	.cb_batt_level_upd_cmp = on_bass_batt_level_upd_cmp,
	.cb_bond_data_upd = on_bass_bond_data_upd,
};

static uint16_t set_advertising_data(uint8_t actv_idx)
{
	uint16_t err;

	/* gatt service identifier */
	uint16_t svc = GATT_SVC_CYCLING_SPEED_CADENCE;
	uint16_t svc2 = GATT_SVC_BATTERY_SERVICE;
	uint8_t num_svc = 2;
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
	/* Complete local name (TODO: should be defined
	 * somewhere in stack...)
	 */
	p_data[1] = GAP_AD_TYPE_COMPLETE_NAME;
	memcpy(p_data + 2, device_name, device_name_len);

	/* Update data pointer */
	p_data = p_data + adv_device_name;
	p_data[0] = (GATT_UUID_16_LEN * num_svc) + 1;
	p_data[1] = GAP_AD_TYPE_COMPLETE_LIST_16_BIT_UUID;

	/* Copy identifier */
	memcpy(p_data + 2, (void *)&svc, sizeof(svc));
	memcpy(p_data + 4, (void *)&svc2, sizeof(svc2));

	err = gapm_le_set_adv_data(actv_idx, p_buf);
	co_buf_release(p_buf); /* Release ownership of buffer so stack can free it when done */
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
		.max_tx_pwr = 0,
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

/* Add profile to the stack */
static void server_configure(void)
{
	uint16_t err;
	uint16_t start_hdl = 0;
	struct cscps_db_cfg cscps_cfg = {
		.csc_feature = CSCP_FEAT_WHEEL_REV_DATA_SUPP_BIT,
		.sensor_loc = CSCP_LOC_FRONT_WHEEL,
		.sensor_loc_supp = CSCP_SENSOR_LOCATION_SUPPORT,
	};

	err = prf_add_profile(TASK_ID_CSCPS, 0, 0, &cscps_cfg, &cscps_cb, &start_hdl);

	if (err) {
		LOG_ERR("Error %u adding profile", err);
	}
}

void on_gapm_process_complete(uint32_t metainfo, uint16_t status)
{
	if (status) {
		LOG_ERR("gapm process completed with error %u", status);
		return;
	}

	server_configure();

	LOG_DBG("gapm process completed successfully");

	/* After configuration completed, create an advertising activity */
	create_advertising();
}

static uint16_t cumul_crk;
static uint16_t cumul_wheel;

/*  Generate and send dummy data*/
static void send_measurement(uint16_t current_value)
{
	uint16_t err;

	wheel_evt_time += current_value + 2000;
	/*      Dummy measurements values       */
	cscp_csc_meas_t p_meas = {
		.flags = CSCP_MEAS_WHEEL_REV_DATA_PRESENT_BIT |
			CSCP_MEAS_CRANK_REV_DATA_PRESENT_BIT,
		/* Moving above 1000 milliseconds */
		.cumul_wheel_rev = cumul_wheel + 1,
		.last_wheel_evt_time = wheel_evt_time,
		.cumul_crank_rev = cumul_crk + 3,
		.last_crank_evt_time = wheel_evt_time + 2,
	};

	cumul_crk += 3;
	cumul_wheel += 1;

	/* Set bit field to all 1's to send notification
	 * on all connections that are subscribed
	 */
	err = cscps_meas_send(UINT32_MAX, current_value, &p_meas);

	if (err) {
		LOG_ERR("Error %u sending measurement", err);
	}
}

uint16_t read_sensor_value(uint16_t current_value)
{
	/* Generating dummy values between 1 and 5 */
	if (current_value >= 3) {
		current_value = 1;
	} else {
		current_value++;
	}
	return current_value;
}

void cscps_process(uint16_t measurement)
{
	switch (conn_status) {
	case BT_CONN_STATE_CONNECTED:
		if (READY_TO_SEND) {
			send_measurement(measurement);
			READY_TO_SEND = false;
		}

		break;
	case BT_CONN_STATE_DISCONNECTED:
		LOG_DBG("Waiting for peer connection\n");
		k_sem_take(&conn_sem, K_FOREVER);

	default:
		break;
	}
}

static void config_battery_service(void)
{
	uint16_t err;
	struct bass_db_cfg bass_cfg;
	uint16_t start_hdl = 0;

	bass_cfg.bas_nb = 1;
	bass_cfg.features[0] = 1;

	err = prf_add_profile(TASK_ID_BASS, 0, 0, &bass_cfg, &bass_cb, &start_hdl);
}

static void battery_process(void)
{
	uint16_t err;
	/* Fixed value for demonstrating purposes */
	uint8_t battery_level = 99;

	if (READY_TO_SEND_BASS) {
		/* Sending dummy battery level to first battery instance*/
		err = bass_batt_level_upd(BATT_INSTANCE, battery_level);

		if (err) {
			LOG_ERR("Error %u sending battery level", err);
		}
	}
}

int main(void)
{
	uint16_t err;
	uint16_t current_value = 1;

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
		/* Execute process every 1 second */
		k_sleep(K_SECONDS(1));

		current_value = read_sensor_value(current_value);

		cscps_process(current_value);
		battery_process();
	}

	return 0;
}
