#include "prf.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "basc.h"
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



typedef struct batt_client_env
{
	uint8_t status_bf;
	uint8_t level;
}batt_client_env_t;

static batt_client_env_t* p_batt_env;

static uint8_t discovery_event;

void service_discovery(uint8_t event, uint8_t conidx, uint16_t status);


static void on_cb_batt_level_upd(uint8_t conidx, uint8_t bas_instance, uint8_t level)
{
	printk("batt level updated %d\n", level);
}

static void on_cb_enable_cmp(uint8_t conidx, uint16_t status, uint8_t bas_nb,
                                                  const bas_content_t* p_bas)
{
//     ASSERT_WARN(status == GAP_ERR_NO_ERROR, status, 0);
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


    printk("on enable cmp checkoing bit\n");
//     if (p_batt_env->status_bf == STATUS_DISCOVERING_POS)
//     {
// 	printk("   under if before calling discovery\n");
//         service_discovery(EVENT_SERVICE_CONTENT_DISCOVERED, conidx, status);
// 	printk("   under if after calling discovery\n");
//     }
    printk("endof cb_enable_cmp\n");
}

static void on_cb_read_batt_level_cmp(uint8_t conidx, uint16_t status, uint8_t bas_instance,
                                                           uint8_t level)
{

    if (status == GAP_ERR_NO_ERROR)
    {
        on_cb_batt_level_upd(conidx, bas_instance, level);
    } else {
	printk("error reading batt level\n");
    }

    service_discovery(EVENT_LEVEL_READ, conidx, status);
}

static void on_cb_read_ntf_cfg_cmp(uint8_t conidx, uint16_t status, uint8_t bas_instance,
                                                        uint16_t ntf_ind_cfg)
{
}

static void on_cb_read_pres_format_cmp(uint8_t conidx, uint16_t status, uint8_t bas_instance,
                                                            const prf_char_pres_fmt_t* p_pres_format)
{
//     ASSERT_WARN(status == GAP_ERR_NO_ERROR, status, 0);
	printk("error read pres format\n");
}

static void on_cb_write_ntf_cfg_cmp(uint8_t conidx, uint16_t status, uint8_t bas_instance)
{
//     ASSERT_WARN(status == GAP_ERR_NO_ERROR, status, 0);
    printk("error write ntf cfg\n");

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
		printk("error adding profile 0x%02X\n", err);
		return err;
	} else {
		printk("profile added!\n");
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
			printk("DISC basc discovery\n");
			uint16_t err = basc_enable(conidx, PRF_CON_DISCOVERY, 0, NULL);
			if (err) {
				printk("ERR ENABLING BASC\n");
			} else {
				printk("basc enable ok\n");
			}
			p_batt_env->status_bf |= STATUS_DISCOVERING_POS;
			printk(" DISC end discovery start\n");
			break;

		case EVENT_SERVICE_CONTENT_DISCOVERED:
			printk("DISC discovered\n");
			printk("     read\n");
			err = basc_read_batt_level(conidx, 0);
			if (err) {
				printk("     error reading 0x%02x\n", err);
			}
			else {
				printk("     read ok\n");
			}
			break;

		case EVENT_LEVEL_READ:
			printk("DISClevel read\n");
			basc_write_ntf_cfg(conidx, 0, PRF_CLI_START_NTF);
			discovery_event = EVENT_LEVEL_READ;
			break;

		case EVENT_SENDING_EVENTS_ENABLED:
			printk("DISC sending events enabled\n");
			// OUTPUT_DEBUG(p_batt_env->p_output_intf, APPLET_ID(BATTERY_CLIENT), "BAS discovered");
			// SETB(p_batt_env->status_bf, STATUS_DISCOVERING, 0);
			break;

		default:
			break;
		}
	} else {
		printk("service discovery process error\n");
		// SETB(p_batt_env->status_bf, STATUS_DISCOVERING, 0);
	}
	return;
}

void profile_client_process(uint8_t conidx, uint8_t event)
{
	if(discovery_event != EVENT_LEVEL_READ){
		printk("starting process\n");
		printk("event 0x%02x\n", event);
		printk("starting discovery in 5 seconds...\n");
		// k_sleep(K_SECONDS(5));
		service_discovery(event, conidx, GAP_ERR_NO_ERROR);
	}
	printk("end of process #####################################\n");
};

