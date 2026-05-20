#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include "alif_ble.h"
#include "gapm.h"
#include "gap_le.h"
#include "gapc_le.h"
#include "gapc_sec.h"
#include "gapm_le.h"
#include "gapm_le_adv.h"
#include "co_buf.h"
#include "address_verification.h"

#include "prf.h"
#include "acc_mcs.h"
#include "acc_ots.h"
#include "shared_control.h"
#include <alif/bluetooth/bt_adv_data.h>
#include <alif/bluetooth/bt_scan_rsp.h>
#include "gapm_api.h"
#include "rwip_task.h"
#include "ble_storage.h"

K_SEM_DEFINE(conn_sem, 0, 1);

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define PLAYER_NAME "Alif MCS Player"
#define TRACK_TITLE "Test Track"

struct app_env {
	bool connected;
	uint8_t mcs_lid;
	uint16_t mcs_start_hdl;
	uint8_t media_state;
	int32_t track_position;
	int32_t track_duration;
	uint8_t ots_trf_lid;
	uint16_t ots_start_hdl;
	uint8_t icon_object_lid;
	uint8_t curr_track_seg_object_lid;
	uint8_t curr_track_object_lid;
	uint8_t next_track_object_lid;
	uint8_t curr_group_object_lid;
	uint8_t parent_group_object_lid;
};

static uint8_t conn_status = BT_CONN_STATE_DISCONNECTED;

static struct connection_status app_con_info = {
	.conidx = GAP_INVALID_CONIDX,
	.addr.addr_type = 0xff,
};

static struct app_env env = {
	.connected = false,
	.mcs_lid = 0,
	.media_state = ACC_MC_MEDIA_STATE_INACTIVE,
	.track_position = 0,
	.track_duration = 0,
	.ots_trf_lid = 0,
	.ots_start_hdl = 0,
	.icon_object_lid = OTP_INVALID_LID,
	.curr_track_seg_object_lid = OTP_INVALID_LID,
	.curr_track_object_lid = OTP_INVALID_LID,
	.next_track_object_lid = OTP_INVALID_LID,
	.curr_group_object_lid = OTP_INVALID_LID,
	.parent_group_object_lid = OTP_INVALID_LID,
};

/**
 * Bluetooth stack configuration
 */
static gapm_config_t gapm_cfg = {
	.role = GAP_ROLE_LE_PERIPHERAL,
	.pairing_mode = GAPM_PAIRING_SEC_CON,
	.privacy_cfg = GAPM_PRIV_CFG_PRIV_ADDR_BIT,
	.renew_dur = 1500,
	.private_identity.addr = {0},
	.irk.key = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x08, 0x11, 0x22, 0x33, 0x44, 0x55,
		    0x66, 0x77, 0x88},
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

/* Forward declarations */
static uint16_t init_ots(void);
static uint16_t init_mcs(void);
static uint16_t add_icon_object(void);

/* MCS callbacks */
static void mcs_cb_bond_data(uint8_t media_lid, uint8_t con_lid, uint32_t cli_cfg_bf)
{
	LOG_INF("MCS bond data: media_lid=%u, con_lid=%u, cli_cfg_bf=0x%08x",
		media_lid, con_lid, cli_cfg_bf);
}

static void mcs_cb_set_object_id_req(uint8_t media_lid, uint8_t con_lid, uint8_t char_type,
				     const acc_mc_object_id_t *p_obj_id)
{
	LOG_INF("MCS set object ID: media_lid=%u, con_lid=%u, char_type=%u",
		media_lid, con_lid, char_type);
	/* Not implemented for basic MCS - object ID characteristics are optional */
}

/* OTS callbacks */
static void app_ots_cb_bond_data(uint8_t transfer_lid, uint8_t con_lid, uint8_t cli_cfg_bf)
{
	LOG_DBG("OTS bond data: transfer_lid=%u, con_lid=%u, cli_cfg_bf=0x%02x",
		transfer_lid, con_lid, cli_cfg_bf);
}

static void app_ots_cb_coc_connected(uint8_t con_lid, uint16_t peer_max_sdu, uint16_t local_max_sdu)
{
	LOG_DBG("OTS CoC connected: con_lid=%u, peer_max_sdu=%u, local_max_sdu=%u", con_lid,
		peer_max_sdu, local_max_sdu);
}

static void app_ots_cb_coc_disconnected(uint8_t con_lid, uint16_t reason)
{
	LOG_DBG("OTS CoC disconnected: con_lid=%u, reason=%u", con_lid, reason);
}

static void app_ots_cb_coc_data(uint8_t con_lid, uint16_t length, const uint8_t *p_sdu)
{
	(void)p_sdu;
	LOG_DBG("OTS CoC data: con_lid=%u, length=%u", con_lid, length);
}

static void app_ots_cb_get_name(uint8_t con_lid, uint8_t transfer_lid, uint8_t object_lid,
				uint16_t token, uint16_t offset, uint16_t max_len)
{
	LOG_DBG("OTS get name: con_lid=%u, transfer_lid=%u, object_lid=%u, token=%u, offset=%u, max_len=%u",
		con_lid, transfer_lid, object_lid, token, offset, max_len);

	const uint8_t *name = (const uint8_t *)"Icon";
	uint8_t full_len = strlen("Icon");
	uint8_t send_len = (full_len > offset) ? (full_len - offset) : 0;

	if (send_len > max_len) {
		send_len = max_len;
	}

	acc_ots_cfm_get_name(true, con_lid, token, send_len, name + offset);
}

static void app_ots_cb_set_name(uint8_t con_lid, uint8_t transfer_lid, uint8_t object_lid,
				uint16_t token, uint8_t name_len, const uint8_t *p_name)
{
	(void)p_name;
	LOG_DBG("OTS set name: con_lid=%u, transfer_lid=%u, object_lid=%u, token=%u, name_len=%u",
		con_lid, transfer_lid, object_lid, token, name_len);
}

static void app_ots_cb_object_create(uint8_t con_lid, uint8_t transfer_lid, uint16_t token,
				      uint32_t size, uint8_t uuid_type, const void *p_uuid)
{
	(void)p_uuid;
	LOG_DBG("OTS object create: con_lid=%u, transfer_lid=%u, token=%u, size=%u, uuid_type=%u",
		con_lid, transfer_lid, token, size, uuid_type);
}

static void app_ots_cb_object_execute(uint8_t con_lid, uint8_t transfer_lid, uint8_t object_lid,
				       uint16_t token, uint16_t param_len, const uint8_t *p_param)
{
	(void)p_param;
	LOG_DBG("OTS object execute: con_lid=%u, transfer_lid=%u, object_lid=%u, token=%u, param_len=%u",
		con_lid, transfer_lid, object_lid, token, param_len);
}

static void app_ots_cb_object_manipulate(uint8_t con_lid, uint8_t transfer_lid, uint8_t object_lid,
					  uint16_t token, uint8_t opcode, uint32_t offset, uint32_t length,
					  uint8_t mode)
{
	LOG_DBG("OTS object manipulate: con_lid=%u, transfer_lid=%u, object_lid=%u, token=%u, opcode=%u, offset=%u, length=%u, mode=%u",
		con_lid, transfer_lid, object_lid, token, opcode, offset, length, mode);
}

static void app_ots_cb_object_control(uint8_t con_lid, uint8_t transfer_lid, uint8_t object_lid,
				       uint16_t token, uint8_t opcode)
{
	LOG_DBG("OTS object control: con_lid=%u, transfer_lid=%u, object_lid=%u, token=%u, opcode=%u",
		con_lid, transfer_lid, object_lid, token, opcode);

	if (opcode == 0x00 || opcode > 0x07) {
		acc_ots_cfm_object_control(OTP_OACP_RESULT_OPCODE_NOT_SUPP, con_lid, transfer_lid,
					   token, 0);
	}
}

static void app_ots_cb_filter_get(uint8_t con_lid, uint8_t transfer_lid, uint8_t filter_lid,
				   uint32_t ots_token, uint16_t offset, uint16_t max_len)
{
	LOG_DBG("OTS filter get: con_lid=%u, transfer_lid=%u, filter_lid=%u, ots_token=%u, offset=%u, max_len=%u",
		con_lid, transfer_lid, filter_lid, ots_token, offset, max_len);
}

static void app_ots_cb_list(uint16_t req_ind_code, uint8_t con_lid, uint8_t transfer_lid,
			     uint16_t token, uint8_t opcode, const void *p_value)
{
	(void)p_value;
	LOG_DBG("OTS list: req_ind_code=%u, con_lid=%u, transfer_lid=%u, token=%u, opcode=%u",
		req_ind_code, con_lid, transfer_lid, token, opcode);
}

static void app_ots_cb_filter_set(uint16_t req_ind_code, uint8_t con_lid, uint8_t transfer_lid,
				   uint8_t filter_lid, uint16_t token, uint8_t filter_val,
				   const void *p_value1, const void *p_value2)
{
	(void)p_value1;
	(void)p_value2;
	LOG_DBG("OTS filter set: req_ind_code=%u, con_lid=%u, transfer_lid=%u, filter_lid=%u, token=%u, filter_val=%u",
		req_ind_code, con_lid, transfer_lid, filter_lid, token, filter_val);
}

static void app_ots_cb_coc_connect(uint8_t con_lid, uint16_t token, uint16_t peer_max_sdu)
{
	LOG_DBG("OTS CoC connect: con_lid=%u, token=%u, peer_max_sdu=%u", con_lid, token, peer_max_sdu);
}

static void app_ots_cb_cmp_evt(uint16_t cmd_code, uint16_t status, uint8_t con_lid)
{
	LOG_DBG("OTS command complete: cmd_code=%u, status=%u, con_lid=%u", cmd_code, status,
		con_lid);
}

static const acc_ots_cb_t ots_callbacks = {
	.cb_bond_data = app_ots_cb_bond_data,
	.cb_coc_connected = app_ots_cb_coc_connected,
	.cb_coc_disconnected = app_ots_cb_coc_disconnected,
	.cb_coc_data = app_ots_cb_coc_data,
	.cb_get_name = app_ots_cb_get_name,
	.cb_set_name = app_ots_cb_set_name,
	.cb_object_create = app_ots_cb_object_create,
	.cb_object_execute = app_ots_cb_object_execute,
	.cb_object_manipulate = app_ots_cb_object_manipulate,
	.cb_object_control = app_ots_cb_object_control,
	.cb_filter_get = app_ots_cb_filter_get,
	.cb_list = app_ots_cb_list,
	.cb_filter_set = app_ots_cb_filter_set,
	.cb_coc_connect = app_ots_cb_coc_connect,
	.cb_cmp_evt = app_ots_cb_cmp_evt,
};

static void mcs_cb_control_req(uint8_t media_lid, uint8_t con_lid,
			       uint8_t opcode, int32_t val)
{
	LOG_INF("MCS control request: media_lid=%u, con_lid=%u, opcode=%u, val=%d",
		media_lid, con_lid, opcode, val);

	uint8_t result = ACC_MC_MEDIA_CP_RESULT_SUCCESS;
	uint8_t action = ACC_MCS_ACTION_NO_ACTION;
	int32_t track_pos = env.track_position;
	int8_t seeking_speed = 0;

	switch (opcode) {
	case ACC_MC_OPCODE_PLAY:
		env.media_state = ACC_MC_MEDIA_STATE_PLAYING;
		action = ACC_MCS_ACTION_PLAY;
		acc_mcs_set_position(media_lid, env.track_position);
		break;
	case ACC_MC_OPCODE_PAUSE:
		env.media_state = ACC_MC_MEDIA_STATE_PAUSED;
		action = ACC_MCS_ACTION_PAUSE;
		break;
	case ACC_MC_OPCODE_STOP:
		env.media_state = ACC_MC_MEDIA_STATE_INACTIVE;
		action = ACC_MCS_ACTION_STOP;
		env.track_position = 0;
		acc_mcs_set_position(media_lid, 0);
		break;
	case ACC_MC_OPCODE_FAST_FW:
		env.media_state = ACC_MC_MEDIA_STATE_SEEKING;
		action = ACC_MCS_ACTION_SEEK;
		seeking_speed = 1;
		break;
	case ACC_MC_OPCODE_FAST_RW:
		env.media_state = ACC_MC_MEDIA_STATE_SEEKING;
		action = ACC_MCS_ACTION_SEEK;
		seeking_speed = -1;
		break;
	default:
		result = ACC_MC_MEDIA_CP_RESULT_NOT_SUPPORTED;
		break;
	}

	acc_mcs_cfm_control(result, media_lid, action, track_pos, seeking_speed);
}

static void mcs_cb_search_req(uint8_t media_lid, uint8_t con_lid,
			      uint8_t param_len, const uint8_t *p_param)
{
	LOG_INF("MCS search request: media_lid=%u, con_lid=%u, param_len=%u",
		media_lid, con_lid, param_len);
	/* Not implemented - Search Control Point is optional */
}

static void mcs_cb_get_req(uint8_t media_lid, uint8_t con_lid, uint8_t char_type,
			   uint16_t token, uint16_t offset, uint16_t length)
{
	LOG_INF("MCS get request: media_lid=%u, con_lid=%u, char_type=%u, token=%u, offset=%u, length=%u",
		media_lid, con_lid, char_type, token, offset, length);

	const uint8_t *p_val = NULL;
	uint16_t val_len = 0;

	switch (char_type) {
	case ACC_MC_CHAR_TYPE_PLAYER_NAME: {
		const char *name = PLAYER_NAME;
		p_val = (const uint8_t *)name;
		val_len = strlen(name);
		if (offset >= val_len) {
			val_len = 0;
		} else {
			p_val += offset;
			val_len -= offset;
			if (val_len > length) {
				val_len = length;
			}
		}
		break;
	}
	case ACC_MC_CHAR_TYPE_TRACK_TITLE: {
		const char *title = TRACK_TITLE;
		p_val = (const uint8_t *)title;
		val_len = strlen(title);
		if (offset >= val_len) {
			val_len = 0;
		} else {
			p_val += offset;
			val_len -= offset;
			if (val_len > length) {
				val_len = length;
			}
		}
		break;
	}
	default:
		break;
	}

	acc_mcs_cfm_get(true, media_lid, con_lid, char_type, token, val_len, p_val);
}

static void mcs_cb_get_position_req(uint8_t media_lid, uint8_t con_lid, uint16_t token)
{
	LOG_INF("MCS get position request: media_lid=%u, con_lid=%u, token=%u",
		media_lid, con_lid, token);
	acc_mcs_cfm_get_position(true, media_lid, con_lid, token, env.track_position);
}

static void mcs_cb_set_req(uint8_t media_lid, uint8_t con_lid, uint8_t char_type, uint32_t val)
{
	LOG_INF("MCS set request: media_lid=%u, con_lid=%u, char_type=%u, val=%u",
		media_lid, con_lid, char_type, val);

	switch (char_type) {
	case ACC_MC_CHAR_TYPE_TRACK_POSITION:
		env.track_position = (int32_t)val;
		acc_mcs_cfm_set(true, media_lid, val);
		break;
	case ACC_MC_CHAR_TYPE_PLAYING_ORDER:
		/* Range / supp_bf validation already done by the stack
		 * (acc_mcs.c: ACC_MC_PLAY_ORDER_MIN..MAX and
		 *  playing_order_supp_bf bit check).
		 */
		acc_mcs_cfm_set(true, media_lid, val);
		break;
	case ACC_MC_CHAR_TYPE_PLAYBACK_SPEED:
		acc_mcs_cfm_set(true, media_lid, val);
		break;
	default:
		acc_mcs_cfm_set(false, media_lid, val);
		break;
	}
}

static acc_mcs_cb_t mcs_cb = {
	.cb_bond_data = mcs_cb_bond_data,
	.cb_set_object_id_req = mcs_cb_set_object_id_req,
	.cb_search_req = mcs_cb_search_req,
	.cb_control_req = mcs_cb_control_req,
	.cb_get_req = mcs_cb_get_req,
	.cb_get_position_req = mcs_cb_get_position_req,
	.cb_set_req = mcs_cb_set_req,
};

/*
 * OTS initialization
 */
static uint16_t init_ots(void)
{
	uint16_t cfg_flags =
		OTS_ADD_CFG_NAME_WRITE_BIT |
		OTS_ADD_CFG_FIRST_CREATED_BIT |
		OTS_ADD_CFG_FIRST_CREATED_WRITE_BIT |
		OTS_ADD_CFG_LAST_MODIFIED_BIT |
		OTS_ADD_CFG_MORE_OBJECTS_BIT |
		OTS_ADD_CFG_PROPERTIES_WRITE_BIT |
		OTS_ADD_CFG_CHANGED_BIT;
	uint32_t oacp_features = OTP_OACP_FEAT_READ_SUPP_BIT;
	uint32_t olcp_features =
		OTP_OLCP_FEAT_GOTO_SUPP_BIT | OTP_OLCP_FEAT_REQ_NUM_OBJECTS_SUPP_BIT;
	uint16_t ret;

	LOG_INF("Initializing OTS service");

	ret = acc_ots_configure(3, &ots_callbacks);
	if (ret) {
		LOG_ERR("OTS configure failed: status=%u", ret);
		return ret;
	}

	ret = acc_ots_add(cfg_flags, GATT_INVALID_HDL, oacp_features, olcp_features,
			  &env.ots_trf_lid, &env.ots_start_hdl);
	if (ret) {
		LOG_ERR("OTS add failed: status=%u", ret);
		return ret;
	}

	LOG_INF("OTS added: trf_lid=%u, start_hdl=0x%04x", env.ots_trf_lid, env.ots_start_hdl);

	/* Add icon object after OTS is configured */
	ret = add_icon_object();
	if (ret) {
		LOG_ERR("Failed to add icon object: %u", ret);
		return ret;
	}

	return 0;
}

static uint16_t add_icon_object(void)
{
	prf_date_time_t time = {.year = 2020, .month = 1, .day = 1, .hour = 0, .min = 0, .sec = 0};
	uint32_t properties = OTP_PROP_READ_PERM_BIT;
	uint16_t obj_type = 0x2ABF; /* Icon type */
	uint16_t ret;

	/* Add Icon Object ID object */
	ot_object_id_t icon_obj_id = {.object_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06}};
	ret = acc_ots_object_add(&icon_obj_id, 100, 1024, &time, &time, properties,
				  OTP_UUID_TYPE_16_BIT, &obj_type, &env.icon_object_lid);
	if (ret) {
		LOG_ERR("OTS icon object add failed: status=%u", ret);
		return ret;
	}
	LOG_INF("OTS icon object added: object_lid=%u", env.icon_object_lid);

	/* Add Current Track Segments Object ID */
	ot_object_id_t curr_track_seg_obj_id = {.object_id = {0x11, 0x12, 0x13, 0x14, 0x15, 0x16}};
	ret = acc_ots_object_add(&curr_track_seg_obj_id, 100, 1024, &time, &time, properties,
				  OTP_UUID_TYPE_16_BIT, &obj_type, &env.curr_track_seg_object_lid);
	if (ret) {
		LOG_ERR("OTS curr track seg object add failed: status=%u", ret);
		return ret;
	}
	LOG_INF("OTS curr track seg object added: object_lid=%u", env.curr_track_seg_object_lid);

	/* Add Current Track Object ID */
	ot_object_id_t curr_track_obj_id = {.object_id = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26}};
	ret = acc_ots_object_add(&curr_track_obj_id, 100, 1024, &time, &time, properties,
				  OTP_UUID_TYPE_16_BIT, &obj_type, &env.curr_track_object_lid);
	if (ret) {
		LOG_ERR("OTS curr track object add failed: status=%u", ret);
		return ret;
	}
	LOG_INF("OTS curr track object added: object_lid=%u", env.curr_track_object_lid);

	/* Add Next Track Object ID */
	ot_object_id_t next_track_obj_id = {.object_id = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36}};
	ret = acc_ots_object_add(&next_track_obj_id, 100, 1024, &time, &time, properties,
				  OTP_UUID_TYPE_16_BIT, &obj_type, &env.next_track_object_lid);
	if (ret) {
		LOG_ERR("OTS next track object add failed: status=%u", ret);
		return ret;
	}
	LOG_INF("OTS next track object added: object_lid=%u", env.next_track_object_lid);

	/* Add Current Group Object ID */
	ot_object_id_t curr_group_obj_id = {.object_id = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46}};
	ret = acc_ots_object_add(&curr_group_obj_id, 100, 1024, &time, &time, properties,
				  OTP_UUID_TYPE_16_BIT, &obj_type, &env.curr_group_object_lid);
	if (ret) {
		LOG_ERR("OTS curr group object add failed: status=%u", ret);
		return ret;
	}
	LOG_INF("OTS curr group object added: object_lid=%u", env.curr_group_object_lid);

	return 0;
}

/*
 * Service initialization
 */
static uint16_t init_mcs(void)
{
	uint16_t status;
	uint32_t cfg_bf = 0;
	uint32_t playing_order_supp_bf = ACC_MC_PLAY_ORDER_SINGLE_ONCE | ACC_MC_PLAY_ORDER_SINGLE_REPEAT |
					   ACC_MC_PLAY_ORDER_ORDER_ONCE | ACC_MC_PLAY_ORDER_ORDER_REPEAT;
	uint8_t ccid = 1; /* Content Control ID */

	LOG_INF("Initializing MCS service");

	/* Configure MCS with mandatory characteristics plus Media Player Icon Object ID
	 * From MCS ICS spec Table 2, mandatory characteristics are:
	 * - Media Player Name (M)
	 * - Track Changed (M)
	 * - Track Title (M)
	 * - Track Duration (M)
	 * - Track Position (M)
	 * - Media State (M)
	 * - Media Control Point (M)
	 * - Media Control Point Opcodes Supported (M)
	 * - Content Control ID (M)
	 *
	 * Optional characteristics included:
	 * - Media Player Icon Object ID (requires OTS)
	 * - Media Player Icon URL
	 *
	 * Optional characteristics not included (to stay within GATT database resource limits):
	 * - Playback Speed
	 * - Seeking Speed
	 * - Current Track Object ID
	 * - Next Track Object ID
	 * - Current Group Object ID
	 * - Parent Group Object ID
	 * - Playing Order
	 * - Playing Order Supported
	 * - Search Results Object ID
	 * - Search Control Point
	 */

	/* Enable notification support for mandatory notification-capable characteristics */
	cfg_bf |= ACC_MCS_CFG_PLAYER_NAME_NTF_SUPP_BIT;
	cfg_bf |= ACC_MCS_CFG_TRACK_TITLE_NTF_SUPP_BIT;
	cfg_bf |= ACC_MCS_CFG_TRACK_DURATION_NTF_SUPP_BIT;
	cfg_bf |= ACC_MCS_CFG_TRACK_POSITION_NTF_SUPP_BIT;
	cfg_bf |= ACC_MCS_CFG_MEDIA_CP_SUPP_BIT;
	cfg_bf |= ACC_MCS_CFG_MEDIA_CP_OPCODE_NTF_SUPP_BIT;

	/* Enable Media Player Icon Object ID characteristic (requires OTS) - PTS requirement */
	cfg_bf |= ACC_MCS_CFG_PLAYER_ICON_OBJ_ID_SUPP_BIT;

	/* Enable Media Player Icon URL characteristic - PTS requirement TSPC_MCS_2_6=TRUE */
	cfg_bf |= ACC_MCS_CFG_PLAYER_ICON_URL_SUPP_BIT;

	/* Enable Playback Speed - ICS TSPC_MCS_2_12=TRUE */
	cfg_bf |= ACC_MCS_CFG_PLAYBACK_SPEED_SUPP_BIT;
	cfg_bf |= ACC_MCS_CFG_PLAYBACK_SPEED_NTF_SUPP_BIT;

	/* Enable Seeking Speed - ICS TSPC_MCS_2_13=TRUE */
	cfg_bf |= ACC_MCS_CFG_SEEKING_SPEED_SUPP_BIT;
	cfg_bf |= ACC_MCS_CFG_SEEKING_SPEED_NTF_SUPP_BIT;

	/* Enable Playing Order - ICS TSPC_MCS_2_19=TRUE, TSPC_MCS_2_20=TRUE */
	cfg_bf |= ACC_MCS_CFG_PLAYING_ORDER_SUPP_BIT;
	cfg_bf |= ACC_MCS_CFG_PLAYING_ORDER_SUPP_SUPP_BIT;
	cfg_bf |= ACC_MCS_CFG_PLAYING_ORDER_NTF_SUPP_BIT;

	/* Enable Object ID characteristics - ICS TSPC_MCS_2_14=TRUE, 2_15=TRUE, 2_16=TRUE, 2_17=TRUE, 2_18=TRUE
	 * Note: CURR_TRACK_OBJ_ID_SUPP enables all: Current Track Segments, Current Track, Next Track,
	 * Current Group, and Parent Group Object ID characteristics together
	 */
	cfg_bf |= ACC_MCS_CFG_CURR_TRACK_OBJ_ID_SUPP_BIT;
	cfg_bf |= ACC_MCS_CFG_CURR_TRACK_NTF_SUPP_BIT;
	cfg_bf |= ACC_MCS_CFG_NEXT_TRACK_NTF_SUPP_BIT;
	cfg_bf |= ACC_MCS_CFG_CURR_GROUP_NTF_SUPP_BIT;
	cfg_bf |= ACC_MCS_CFG_PARENT_GROUP_NTF_SUPP_BIT;

	/* Enable Search Results Object ID - ICS TSPC_MCS_2_23=TRUE.
	 * Per acc_mcs.h, this bit also adds the Search Control Point
	 * characteristic (ICS TSPC_MCS_2_24=TRUE) to the service.
	 */
	cfg_bf |= ACC_MCS_CFG_SEARCH_RESULT_OBJ_ID_SUPP_BIT;

	/* Exposing both Generic Media Control Service (GMCS, UUID 0x1849) and
	 * Media Control Service (MCS, UUID 0x1848) on the Alif/RivieraWaves
	 * stack uses an asymmetric two-step API:
	 *
	 *   1. acc_mcs_configure(nb_mcs, ...) reserves storage for `nb_mcs + 1`
	 *      media instances (see acc_mcs.c: `nb_medias = nb_mcs + 1`). The
	 *      "+1" slot is the mandatory GMCS singleton; `nb_mcs` is the
	 *      number of additional MCS instances. So nb_mcs=1 → two slots
	 *      (one GMCS + one MCS).
	 *
	 *   2. The UUID is hardcoded by media_lid inside the stack:
	 *      lid==0 → 0x1849 (GMCS), lid>=1 → 0x1848 (MCS)
	 *      (acc_mcs.c: `uuid = (lid == 0) ? GMCS : MCS`).
	 *      acc_mcs_add() allocates the next free lid in order, so the
	 *      first add always becomes GMCS and the second becomes MCS.
	 *
	 * Each instance needs its own Content Control ID (CCID) per spec.
	 */
	status = acc_mcs_configure(1, &mcs_cb, 0);
	if (status != GAP_ERR_NO_ERROR) {
		LOG_ERR("MCS configure failed: status=%u", status);
		return status;
	}
	LOG_INF("MCS configure successful");

	/* First add → lid=0 → Generic Media Control Service (0x1849). */
	acc_mc_object_id_t icon_obj_id = {.obj_id = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06}};
	status = acc_mcs_add(cfg_bf, playing_order_supp_bf, env.ots_start_hdl, ccid, &icon_obj_id, 0,
			     &env.mcs_lid);
	if (status != GAP_ERR_NO_ERROR) {
		LOG_ERR("MCS add failed: status=%u", status);
		return status;
	}
	LOG_INF("MCS add successful (mcs_lid=%u) - Generic Media Control Service (0x1849)", env.mcs_lid);

	/* Second add → lid=1 → Media Control Service (0x1848). Uses a distinct
	 * CCID. Best-effort: the test target (GMCS) is already up if this fails.
	 */
	uint8_t mcs_lid_1848;
	status = acc_mcs_add(cfg_bf, playing_order_supp_bf, env.ots_start_hdl, ccid + 1, &icon_obj_id, 0,
			     &mcs_lid_1848);
	if (status != GAP_ERR_NO_ERROR) {
		LOG_ERR("MCS add (0x1848) failed: status=%u", status);
	} else {
		LOG_INF("MCS add successful (mcs_lid=%u) - Media Control Service (0x1848)", mcs_lid_1848);
	}

	/* Set initial values for mandatory characteristics */

	/* Set Media Player Name */
	acc_mcs_set_player_name(env.mcs_lid, strlen(PLAYER_NAME), (const uint8_t *)PLAYER_NAME);
	LOG_INF("Media Player Name set: %s", PLAYER_NAME);

	/* Set Track Title */
	acc_mcs_track_change_basic(env.mcs_lid, env.track_duration, strlen(TRACK_TITLE),
				   (const uint8_t *)TRACK_TITLE);
	LOG_INF("Track Title set: %s", TRACK_TITLE);

	/* Set Track Duration (0 = unknown) */
	acc_mcs_set_position(env.mcs_lid, 0);
	LOG_INF("Track Duration set: %d (unknown)", env.track_duration);

	/* Set Track Position */
	acc_mcs_set_position(env.mcs_lid, env.track_position);
	LOG_INF("Track Position set: %d", env.track_position);

	/* Set Media State to Inactive */
	acc_mcs_action(env.mcs_lid, ACC_MCS_ACTION_INACTIVE, 0, 0);
	LOG_INF("Media State set: Inactive");

	/* Set Media Control Point Opcodes Supported
	 * At least one opcode must be supported (ICS Table 3)
	 * Support Play, Pause, Stop, Fast Forward, Fast Rewind
	 */
	uint32_t opcodes_supp = 0;
	opcodes_supp |= ACC_MC_OPCODE_SUPP_PLAY_BIT;
	opcodes_supp |= ACC_MC_OPCODE_SUPP_PAUSE_BIT;
	opcodes_supp |= ACC_MC_OPCODE_SUPP_STOP_BIT;
	opcodes_supp |= ACC_MC_OPCODE_SUPP_FAST_FW_BIT;
	opcodes_supp |= ACC_MC_OPCODE_SUPP_FAST_RW_BIT;
	acc_mcs_set(env.mcs_lid, ACC_MC_CHAR_TYPE_MEDIA_CP_OPCODES_SUPP, opcodes_supp);
	LOG_INF("Media Control Point Opcodes Supported: 0x%08x", opcodes_supp);

	return GAP_ERR_NO_ERROR;
}

static uint16_t create_advertising(void)
{
	uint16_t err;

	err = bt_gaf_create_adv(DEVICE_NAME, strlen(DEVICE_NAME), &app_con_info.addr);
	if (err != GAF_ERR_NO_ERROR) {
		LOG_ERR("Unable to configure GAF advertiser! Error %u (0x%02X)", err, err);
		return err;
	}
	LOG_INF("GAF advertiser configured");

	return err;
}

static void server_configure(void)
{
	uint16_t err;

	/* Initialize OTS first (required for Media Player Icon Object ID) */
	err = init_ots();
	if (err != GAP_ERR_NO_ERROR) {
		LOG_ERR("Error %u initializing OTS service", err);
		return;
	}

	/* Initialize MCS */
	err = init_mcs();
	if (err != GAP_ERR_NO_ERROR) {
		LOG_ERR("Error %u initializing MCS service", err);
	}
}

/*
 * GAPM callbacks
 */
void app_connection_status_update(enum gapm_connection_event con_event, uint8_t con_idx,
				  uint16_t status)
{
	switch (con_event) {
	case GAPM_API_SEC_CONNECTED_KNOWN_DEVICE:
		conn_status = BT_CONN_STATE_CONNECTED;
		env.connected = true;
		LOG_INF("Connection index %u connected to known device", con_idx);
		/* Restore OTS bond data */
		acc_ots_restore_bond_data(con_idx, env.ots_trf_lid, 0, 0, 0, NULL);
		/* Expose all OTS objects to connection */
		if (env.icon_object_lid != OTP_INVALID_LID) {
			acc_ots_object_change(con_idx, env.ots_trf_lid, env.icon_object_lid);
		}
		if (env.curr_track_seg_object_lid != OTP_INVALID_LID) {
			acc_ots_object_change(con_idx, env.ots_trf_lid, env.curr_track_seg_object_lid);
		}
		if (env.curr_track_object_lid != OTP_INVALID_LID) {
			acc_ots_object_change(con_idx, env.ots_trf_lid, env.curr_track_object_lid);
		}
		if (env.next_track_object_lid != OTP_INVALID_LID) {
			acc_ots_object_change(con_idx, env.ots_trf_lid, env.next_track_object_lid);
		}
		if (env.curr_group_object_lid != OTP_INVALID_LID) {
			acc_ots_object_change(con_idx, env.ots_trf_lid, env.curr_group_object_lid);
		}
		k_sem_give(&conn_sem);
		break;
	case GAPM_API_DEV_CONNECTED:
		conn_status = BT_CONN_STATE_CONNECTED;
		env.connected = true;
		LOG_INF("Connection index %u connected to new device", con_idx);
		/* Restore OTS bond data */
		acc_ots_restore_bond_data(con_idx, env.ots_trf_lid, 0, 0, 0, NULL);
		/* Expose all OTS objects to connection */
		if (env.icon_object_lid != OTP_INVALID_LID) {
			acc_ots_object_change(con_idx, env.ots_trf_lid, env.icon_object_lid);
		}
		if (env.curr_track_seg_object_lid != OTP_INVALID_LID) {
			acc_ots_object_change(con_idx, env.ots_trf_lid, env.curr_track_seg_object_lid);
		}
		if (env.curr_track_object_lid != OTP_INVALID_LID) {
			acc_ots_object_change(con_idx, env.ots_trf_lid, env.curr_track_object_lid);
		}
		if (env.next_track_object_lid != OTP_INVALID_LID) {
			acc_ots_object_change(con_idx, env.ots_trf_lid, env.next_track_object_lid);
		}
		if (env.curr_group_object_lid != OTP_INVALID_LID) {
			acc_ots_object_change(con_idx, env.ots_trf_lid, env.curr_group_object_lid);
		}
		k_sem_give(&conn_sem);
		break;
	case GAPM_API_DEV_DISCONNECTED:
		LOG_INF("Connection index %u disconnected for reason %u", con_idx, status);
		conn_status = BT_CONN_STATE_DISCONNECTED;
		env.connected = false;
		/* Restart advertising */
		create_advertising();
		break;
	case GAPM_API_PAIRING_FAIL:
		LOG_INF("Connection pairing index %u fail for reason %u", con_idx, status);
		break;
	}
}

static gapm_user_cb_t gapm_user_cb = {
	.connection_status_update = app_connection_status_update,
};

int main(void)
{
	uint16_t err;

	LOG_INF("MCS Peripheral Sample");
	LOG_INF("Device Name: %s", DEVICE_NAME);

	/* Initialize BLE storage */
	ble_storage_init();

	/* Start up bluetooth host stack */
	alif_ble_enable(NULL);

	/* Define Private identity */
	bt_generate_private_identity(&gapm_cfg);

	/* Configure Bluetooth Stack */
	LOG_INF("Init gapm service");
	err = bt_gapm_init(&gapm_cfg, &gapm_user_cb, DEVICE_NAME, strlen(DEVICE_NAME));
	if (err != GAP_ERR_NO_ERROR) {
		LOG_ERR("gapm_configure error %u", err);
		return -1;
	}
	LOG_INF("BLE stack initialized");

	/* Get and print device identity address */
	gap_bdaddr_t identity;
	gapm_get_identity(&identity);
	LOG_INF("Device address: %02X:%02X:%02X:%02X:%02X:%02X",
		identity.addr[5], identity.addr[4], identity.addr[3],
		identity.addr[2], identity.addr[1], identity.addr[0]);

	/* Configure services */
	server_configure();

	/* Start advertising */
	err = create_advertising();
	if (err != GAF_ERR_NO_ERROR) {
		LOG_ERR("Advertising creation failed: %u", err);
		return err;
	}

	err = bt_gaf_adv_start(&app_con_info.addr);
	if (err) {
		LOG_ERR("Advertisement start fail %u", err);
		return err;
	}

	LOG_INF("Advertising started");

	/* Wait for connection */
	k_sem_take(&conn_sem, K_FOREVER);
	LOG_INF("Connected");

	return 0;
}
