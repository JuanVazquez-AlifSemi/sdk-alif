#include "client_profile_manager.h"
#include "gatt.h"
#include "batt_cli.h"

void profile_client_process(uint16_t svc_id, uint8_t conidx, uint8_t event)
{
	switch (svc_id)	{
	case PRF_ID_BASC:
		battery_client_process(conidx, event);
		break;

	default:
		break;
	}
}
