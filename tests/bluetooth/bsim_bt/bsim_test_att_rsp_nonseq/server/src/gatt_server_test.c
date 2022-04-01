/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "common.h"

extern enum bst_result_t bst_result;

CREATE_FLAG(flag_is_connected);

static struct bt_conn *g_conn;

/* Call to not send next packet immediately, but after the next packet */
extern void test_set_store_next(void);
extern void test_set_load_next(void);

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err != 0) {
		FAIL("Failed to connect to %s (%u)\n", addr, err);
		return;
	}

	printk("Connected to %s\n", addr);

	g_conn = bt_conn_ref(conn);
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

static ssize_t read_short_test_chrc(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				    void *buf, uint16_t len, uint16_t offset)
{
	printk("Short read\n");
	test_set_store_next();
	return bt_gatt_attr_read(conn, attr, buf, len, offset, (void *)short_chrc_data,
				 sizeof(short_chrc_data));
}

static ssize_t read_long_test_chrc(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
				   uint16_t len, uint16_t offset)
{
	printk("Long read\n");
	test_set_load_next();
	return bt_gatt_attr_read(conn, attr, buf, len, offset, (void *)long_chrc_data,
				 sizeof(long_chrc_data));
}

BT_GATT_SERVICE_DEFINE(test_svc, BT_GATT_PRIMARY_SERVICE(TEST_SERVICE_UUID),
		       BT_GATT_CHARACTERISTIC(TEST_SHORT_CHRC_UUID, BT_GATT_CHRC_READ,
					      BT_GATT_PERM_READ, read_short_test_chrc, NULL, NULL),
		       BT_GATT_CHARACTERISTIC(TEST_LONG_CHRC_UUID, BT_GATT_CHRC_READ,
					      BT_GATT_PERM_READ, read_long_test_chrc, NULL, NULL));

static void test_main(void)
{
	int err;
	const struct bt_data ad[] = {
		BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	};

	err = bt_enable(NULL);
	if (err != 0) {
		FAIL("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

	err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err != 0) {
		FAIL("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Advertising successfully started\n");

	WAIT_FOR_FLAG(flag_is_connected);
	WAIT_FOR_FLAG_UNSET(flag_is_connected);

	PASS("GATT server passed\n");
}

static const struct bst_test_instance test_gatt_server[] = {
	{
		.test_id = "gatt_server",
		.test_post_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = test_main,
	},
	BSTEST_END_MARKER,
};

struct bst_test_list *test_gatt_server_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_gatt_server);
}
