/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <bluetooth/bluetooth.h>
#include <bluetooth/gatt.h>

#include "common.h"

CREATE_FLAG(flag_is_connected);
CREATE_FLAG(flag_discover_complete);
CREATE_FLAG(flag_long_read_complete);
CREATE_FLAG(flag_short_read_complete);

static struct bt_conn *g_conn;
static uint16_t short_chrc_handle;
static uint16_t long_chrc_handle;
static struct bt_uuid *test_svc_uuid = TEST_SERVICE_UUID;

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err != 0) {
		FAIL("Failed to connect to %s (%u)\n", addr, err);
		return;
	}

	printk("Connected to %s\n", addr);

	g_conn = conn;
	SET_FLAG(flag_is_connected);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (conn != g_conn) {
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);

	bt_conn_unref(g_conn);

	g_conn = NULL;
	UNSET_FLAG(flag_is_connected);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type, struct net_buf_simple *ad)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	int err;

	if (g_conn != NULL) {
		return;
	}

	/* We're only interested in connectable events */
	if (type != BT_HCI_ADV_IND && type != BT_HCI_ADV_DIRECT_IND) {
		return;
	}

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	printk("Device found: %s (RSSI %d)\n", addr_str, rssi);

	printk("Stopping scan\n");
	err = bt_le_scan_stop();
	if (err != 0) {
		FAIL("Could not stop scan: %d\n");
		return;
	}

	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT, &g_conn);
	if (err != 0) {
		FAIL("Could not connect to peer: %d\n", err);
	}
}

static uint8_t discover_func(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     struct bt_gatt_discover_params *params)
{
	int err;

	if (attr == NULL) {
		if (short_chrc_handle == 0 || long_chrc_handle == 0) {
			FAIL("Did not discover chrc (%x) or long_chrc (%x)\n", short_chrc_handle,
			     long_chrc_handle);
		}

		(void)memset(params, 0, sizeof(*params));

		SET_FLAG(flag_discover_complete);

		return BT_GATT_ITER_STOP;
	}

	printk("[ATTRIBUTE] handle %u\n", attr->handle);

	if (params->type == BT_GATT_DISCOVER_PRIMARY &&
	    bt_uuid_cmp(params->uuid, TEST_SERVICE_UUID) == 0) {
		printk("Found test service\n");
		params->uuid = NULL;
		params->start_handle = attr->handle + 1;
		params->type = BT_GATT_DISCOVER_CHARACTERISTIC;

		err = bt_gatt_discover(conn, params);
		if (err != 0) {
			FAIL("Discover failed (err %d)\n", err);
		}

		return BT_GATT_ITER_STOP;
	} else if (params->type == BT_GATT_DISCOVER_CHARACTERISTIC) {
		struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;

		if (bt_uuid_cmp(chrc->uuid, TEST_SHORT_CHRC_UUID) == 0) {
			printk("Found chrc\n");
			short_chrc_handle = chrc->value_handle;
		} else if (bt_uuid_cmp(chrc->uuid, TEST_LONG_CHRC_UUID) == 0) {
			printk("Found long_chrc\n");
			long_chrc_handle = chrc->value_handle;
		}
	}

	return BT_GATT_ITER_CONTINUE;
}

static void gatt_discover(void)
{
	static struct bt_gatt_discover_params discover_params;
	int err;

	printk("Discovering services and characteristics\n");

	discover_params.uuid = test_svc_uuid;
	discover_params.func = discover_func;
	discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	discover_params.type = BT_GATT_DISCOVER_PRIMARY;

	err = bt_gatt_discover(g_conn, &discover_params);
	if (err != 0) {
		FAIL("Discover failed(err %d)\n", err);
	}

	WAIT_FOR_FLAG(flag_discover_complete);
	printk("Discover complete\n");
}

static void print_hex(const uint8_t *data, size_t length)
{
	for (size_t i = 0; i < length; i++) {
		printk("%02X ", data[i]);
		if (length % 16 == 0) {
			printk("\n");
		}
	}
}

static uint8_t gatt_read_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
			    const void *data, uint16_t length)
{
	if (err != BT_ATT_ERR_SUCCESS) {
		FAIL("Read failed: 0x%02X\n", err);
	}

	if (params->single.handle == short_chrc_handle) {
		if (length != SHORT_CHRC_SIZE || memcmp(data, short_chrc_data, length) != 0) {
			FAIL("short chrc data different than expected\n", err);
		}

		printk("Short read complete\n");
		SET_FLAG(flag_short_read_complete);
	} else if (params->single.handle == long_chrc_handle) {
		if (length != LONG_CHRC_SIZE || memcmp(data, long_chrc_data, length) != 0) {
			print_hex((const uint8_t *)data, length);
			FAIL("long chrc data different than expected (length %d)\n", err, length);
		}

		printk("Long read complete\n");
		SET_FLAG(flag_long_read_complete);
	}

	(void)memset(params, 0, sizeof(*params));

	return 0;
}

static void gatt_short_read(void)
{
	static struct bt_gatt_read_params read_params;
	int err;

	printk("Reading short chrc\n");

	read_params.func = gatt_read_cb;
	read_params.handle_count = 1;
	read_params.single.handle = short_chrc_handle;
	read_params.single.offset = 0;

	err = bt_gatt_read(g_conn, &read_params);
	if (err != 0) {
		FAIL("bt_gatt_read failed: %d\n", err);
	}

	printk("success\n");
}

static void gatt_long_read(void)
{
	static struct bt_gatt_read_params read_params;
	int err;

	printk("Reading long chrc\n");

	read_params.func = gatt_read_cb;
	read_params.handle_count = 1;
	read_params.single.handle = long_chrc_handle;
	read_params.single.offset = 0;

	err = bt_gatt_read(g_conn, &read_params);
	if (err != 0) {
		FAIL("bt_gatt_read failed: %d\n", err);
	}

	printk("success\n");
}

#define EATT_CHANNELS 1

static void test_main(void)
{
	int err;

	err = bt_enable(NULL);
	if (err != 0) {
		FAIL("Bluetooth discover failed (err %d)\n", err);
	}

	err = bt_le_scan_start(BT_LE_SCAN_PASSIVE, device_found);
	if (err != 0) {
		FAIL("Scanning failed to start (err %d)\n", err);
	}

	printk("Scanning successfully started\n");

	WAIT_FOR_FLAG(flag_is_connected);

	err = bt_eatt_connect(g_conn, EATT_CHANNELS);
	if (err) {
		FAIL("bt_eatt_connect failed (%d)\n", err);
	}

	gatt_discover();

	if (!long_chrc_handle) {
		FAIL("Did not discover long chrc handle\n");
	}

	if (!short_chrc_handle) {
		FAIL("Did not discover short chrc handle\n");
	}

	while (bt_eatt_count(g_conn) < EATT_CHANNELS) {
		k_sleep(K_MSEC(100));
	}

	gatt_short_read();
	gatt_long_read();

	/* What we want */
	WAIT_FOR_FLAG(flag_short_read_complete);
	if (!flag_long_read_complete) {
		FAIL("Expected second read to finish first\n");
	}

	/* Disconnect */
	err = bt_conn_disconnect(g_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	if (err) {
		FAIL("Disconnection failed (err %d)\n", err);
	}

	WAIT_FOR_FLAG_UNSET(flag_is_connected)

	PASS("GATT client Passed\n");
}

static const struct bst_test_instance test_vcs[] = {
	{
		.test_id = "gatt_client",
		.test_post_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = test_main,
	},
	BSTEST_END_MARKER,
};

struct bst_test_list *test_gatt_client_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_vcs);
}
