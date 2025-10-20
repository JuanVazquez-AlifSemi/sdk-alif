/* Copyright (C) 2025 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification of this code is permitted under the
 * terms stated in the Alif Semiconductor Software License Agreement
 *
 * You should have received a copy of the Alif Semiconductor Software
 * License Agreement with this file. If not, please write to:
 * contact@alifsemi.com, or visit: https://alifsemi.com/license
 */

/*
 * This example demonstrates the Battery Client usage
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
#include "batt_svc.h"
#include "shared_control.h"
#include "address_verification.h"

#include "gapm_le_scan.h"
#include "gapm_le_init.h"
#include "co_time.h"
#include "co_utils.h"

#include "batt_cli.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

K_SEM_DEFINE(init_sem, 0, 1);

typedef struct central_env_struct
{
	gap_bdaddr_t periph_addr;
	bool periph_found;
	uint8_t conidx;
	uint8_t scan_actv_idx;
	uint8_t init_actv_idx;
	co_timer_t timer; 
	bool connected;
} central_env_t;

static central_env_t central_env;

static gapm_le_scan_param_t param = {
	.type				= GAPM_SCAN_TYPE_GEN_DISC,
	.prop				= GAPM_SCAN_PROP_PHY_1M_BIT,
	.dup_filt_pol			= GAPM_DUP_FILT_DIS,
	.scan_param_1m.scan_intv	= 60, // 37.5,s
	.scan_param_1m.scan_wd		= 60, // 37.5ms
	.scan_param_coded.scan_intv	= 0, // disabled
	.scan_param_coded.scan_wd	= 0, // disabled
	.duration			= 0,
	.period				= 0,
};

enum central_events
{
	DEVICE_CONFIGURED,
	PERIPHERAL_FOUND,
	PERIPHERAL_CONNECTED,
	INITIATION_STOPPED,
};

#define APPEARANCE  (0x0000)

static void le_central_process(uint8_t event, uint16_t status, const void *p_param);

gapm_config_t gapm_cfg = {
        .role = GAP_ROLE_LE_CENTRAL,
        .pairing_mode = GAPM_PAIRING_DISABLE,
        .privacy_cfg = 0,
        .renew_dur = 1500,
        .private_identity.addr = {0, 0, 0, 0, 0, 0},
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
        .class_of_device = 0,
        .dflt_link_policy = 0,
};

static void on_le_connection_req(uint8_t conidx, uint32_t metainfo, uint8_t actv_idx, uint8_t role,
				const gap_bdaddr_t *p_peer_addr,
				const gapc_le_con_param_t *p_con_params, uint8_t clk_accuracy)
{
	LOG_INF("Connection request on index %u", conidx);
	uint16_t err = gapc_le_connection_cfm(conidx, 0, NULL);
	if (err) {
		printk("error accepting connection 0x%02X\n", err);
	} else {
		printk("accepted ith no error from ON_CONNECTED\n");
		central_env.connected = true;
	}

	LOG_DBG("Connection parameters: interval %u, latency %u, supervision timeout %u",
		p_con_params->interval, p_con_params->latency, p_con_params->sup_to);

	LOG_HEXDUMP_DBG(p_peer_addr->addr, GAP_BD_ADDR_LEN, "Peer BD address");

	LOG_DBG("Please enable notifications on peer device..");

	printk("calling le cnetral process peripheral connected\n");
	le_central_process(PERIPHERAL_CONNECTED, 0, NULL);
}

static void on_disconnection(uint8_t conidx, uint32_t metainfo, uint16_t reason)
{
	LOG_INF("Connection index %u disconnected for reason 0x%02X", conidx, reason);
	central_env.connected = false;

	uint16_t status = gapm_le_start_scan(central_env.scan_actv_idx, &param);
	if (status) {
		printk("Error restarting scan from disconnect\n");
	} else {
		printk("sacen restarted--------------------------------------------------------------\n");
	}
}

static void on_appearance_get(uint8_t conidx, uint32_t metainfo, uint16_t token)
{
	gapc_le_get_appearance_cfm(conidx, token, GAP_ERR_NO_ERROR, 0);
}

static void on_appearance_set(uint8_t conidx, uint32_t metainfo, uint16_t token, uint16_t appearance)
{
}

static void on_name_get(uint8_t conidx, uint32_t metainfo, uint16_t token, uint16_t offset,
			uint16_t max_len)
{
}

static const gapc_connection_info_cb_t gapc_con_inf_cbs = {
	.disconnected = on_disconnection,
	.name_get = on_name_get,
	.appearance_get = on_appearance_get,
	.appearance_set = on_appearance_set,
	/* Other callbacks in this struct are optional */
};

static void on_key_received(uint8_t conidx, uint32_t metainfo, const gapc_pairing_keys_t *p_keys)
{
	LOG_WRN("Unexpected key received key on conidx %u", conidx);
}

static const gapc_connection_req_cb_t gapc_con_cbs = {
	.le_connection_req = on_le_connection_req,
};

static const gapc_security_cb_t gapc_sec_cbs = {
	.key_received = &on_key_received,
	/* All other callbacks in this struct are optional */
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

/* Connection functions */

static void on_init_proc_cmp(uint32_t token, uint8_t proc_id, uint8_t actv_idx, uint16_t status)
{
}

static void on_init_stopped(uint32_t token, uint8_t actv_idx, uint16_t reason)
{
    le_central_process(INITIATION_STOPPED, GAP_ERR_NO_ERROR, NULL);
}


/// Callback structure required to create an init activity
static const gapm_le_init_cb_actv_t app_init_actv_cb_itf =
{
    .hdr.actv.stopped      = on_init_stopped,
    .hdr.actv.proc_cmp     = on_init_proc_cmp,
    .hdr.addr_updated      = NULL,
    .peer_name             = NULL,
};
/// ! [APP_INIT_ACTV_CB]

static uint16_t create_and_start_conn(void)
{
	printk("connecting\n");
	uint16_t status;
	gapm_le_init_param_t params;

	params.prop				= GAPM_INIT_PROP_1M_BIT,
	params.conn_to				= 0,	
	params.scan_param_1m.scan_intv		= 60, // 37.5ms
	params.scan_param_1m.scan_wd		= 60, // 37.5ms
	params.scan_param_coded.scan_intv	= 0, // disabled
	params.scan_param_coded.scan_wd		= 0, // disabled	
	params.conn_param_1m.conn_intv_min	= 40, // 50ms
	params.conn_param_1m.conn_intv_max	= 40, // 50ms
	params.conn_param_1m.conn_latency	= 5,
	params.conn_param_1m.supervision_to	= 100, // 1000 ms
	params.conn_param_1m.ce_len_min		= 5,
	params.conn_param_1m.ce_len_max		= 10,	
	params.conn_param_2m.conn_intv_min	= 40, // 50ms
	params.conn_param_2m.conn_intv_max	= 40, // 50ms
	params.conn_param_2m.conn_latency	= 5,
	params.conn_param_2m.supervision_to	= 100, // 1000 ms
	params.conn_param_2m.ce_len_min		= 5,
	params.conn_param_2m.ce_len_max		= 10,	
	params.conn_param_coded.conn_intv_min	= 40, // 50ms
	params.conn_param_coded.conn_intv_max	= 40, // 50ms
	params.conn_param_coded.conn_latency	= 5,
	params.conn_param_coded.supervision_to	= 100, // 1000 ms
	params.conn_param_coded.ce_len_min	= 5,
	params.conn_param_coded.ce_len_max	= 10,	
	params.peer_addr			= central_env.periph_addr,
	
        // create activity
        status = gapm_le_create_init(0, GAPM_STATIC_ADDR, &app_init_actv_cb_itf, &(central_env.init_actv_idx));
        if(status != GAP_ERR_NO_ERROR) {
		/* log error */
	}
	
        // start it
	printk("initiating connection\n");
        status = gapm_le_start_direct_connection(central_env.init_actv_idx, &params);
	if (status) {
		printk("init connection failed: 0x%02X\n", status);
	}

	return (status);
}

/* SCANNING ACTIVITY MANAGEMENT
 ****************************************************************************************
 */

static void app_scan_proc_cmp(uint32_t token, uint8_t proc_id, uint8_t actv_idx, uint16_t status)
{
}

static void app_scan_stopped(uint32_t token, uint8_t actv_idx, uint16_t reason)
{
}

static const char le_periph_device_name[]  = "ALIF_BATT_BLE";

bool peripheral_name_matches(co_buf_t* p_data, const char* p_exp_name)
{
    bool matches = false;
    uint8_t peer_name_len;
    const uint8_t* p_peer_name = gapm_get_ltv_value(GAP_AD_TYPE_COMPLETE_NAME, co_buf_data_len(p_data),
                                                    co_buf_data(p_data), &peer_name_len);

    if(   (p_peer_name != NULL) && (peer_name_len == strlen(p_exp_name))
       && (memcmp(p_peer_name, p_exp_name, peer_name_len) == 0))
    {
	printk("name matches!\n");
        matches = true;
    }

    return (matches);
}

static void app_scan_adv_report_received(uint32_t metainfo, uint8_t actv_idx, const gapm_le_adv_report_info_t* p_info, co_buf_t* p_report)
{
	printk("scan report\n");
    if(peripheral_name_matches(p_report, le_periph_device_name))
    {
        central_env.periph_addr  = p_info->trans_addr;
        central_env.periph_found = true;

	le_central_process(PERIPHERAL_FOUND, 0, NULL);
    }
}

/// Callback structure required to create a scan activity
static const gapm_le_scan_cb_actv_t scan_actv_cb_itf = {
    .le_actv.actv.stopped      = app_scan_stopped,
    .le_actv.actv.proc_cmp     = app_scan_proc_cmp,
    .le_actv.addr_updated      = NULL,
    .report_received       = app_scan_adv_report_received,
};


uint16_t create_and_start_scan(void)
{
	printk("scanning\n");
	uint16_t status;

        status = gapm_le_create_scan(0, GAPM_STATIC_ADDR, &scan_actv_cb_itf, &central_env.scan_actv_idx);
        if(status != GAP_ERR_NO_ERROR) return status;

        // start it
        status = gapm_le_start_scan(central_env.scan_actv_idx, &param);
	return 0;
}

static void le_central_process(uint8_t event, uint16_t status, const void* p_param)
{
	switch(event) {
	case DEVICE_CONFIGURED:
		/* Create scan and start scanning */
		create_and_start_scan();
		break;

	case PERIPHERAL_FOUND:
		printk("PERIPHERAL FOUND\n");
		printk("starting connection\n");
		/* Stop scan */
		printk("stopping scan\n");
		status = gapm_stop_activity(central_env.scan_actv_idx);
		/* Create and initiate connection */
		status = create_and_start_conn();
		if (status) {
			printk("failed start connection\n");
		}
		break;

	case PERIPHERAL_CONNECTED:
		/* if found stop sacnning */
		printk("acceptinc connection on state machine\n");
		status = gapc_le_connection_cfm(central_env.conidx, 0, NULL);
		printk("CONNECTED, WAITING\n");
		// k_sleep(K_SECONDS(1));
		if (status) {
			printk("failed stop\n");
			break; /* Log also error */
		}
		if (central_env.connected) {
			printk("calling process from main\n");
			profile_client_process(central_env.conidx, 0);
		}
		break;

	default:
		break;
	}
}

static void on_gapm_process_complete(uint32_t metainfo, uint16_t status)
{
	printk("gapm configure complete\n");
	
	k_sem_give(&init_sem);
	
	le_central_process (DEVICE_CONFIGURED, status, NULL);
}

int main (void)
{
	k_sleep(K_SECONDS(2));

	printk("starting_ble\n");
	/* init BLE */
	alif_ble_enable(NULL);

	printk("configuring gapm\n");
	/* Configure device */
	uint16_t err = gapm_configure(DEVICE_CONFIGURED, &gapm_cfg, &gapm_cbs, on_gapm_process_complete);
	if (err) {
		LOG_ERR("gapm_configure error 0x%2X", err);
		return -1;
	}
	
	LOG_DBG("Waiting for init...\n");
	k_sem_take(&init_sem, K_FOREVER);
	/* Add profile to stack*/
	printk("main add profile\n");
	add_profile_client();

	while (1) {
		k_sleep(K_MSEC(100));
	}
	return 0;
}
