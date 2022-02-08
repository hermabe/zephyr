/* main.c - Application main entry point */

/*
 * Copyright (c) 2022 Nordic Semiconductor
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>

#include <zephyr/types.h>
#include <sys/printk.h>
#include <sys/util.h>
#include <sys/byteorder.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/gatt.h>
#include <bluetooth/uuid.h>
#include <bluetooth/conn.h>

#include "bs_types.h"
#include "bs_tracing.h"
#include "bstests.h"

extern enum bst_result_t bst_result;

#define FAIL(...)                                                                                  \
	do {                                                                                       \
		bst_result = Failed;                                                               \
		bs_trace_error_time_line(__VA_ARGS__);                                             \
	} while (0)

#define PASS(...)                                                                                  \
	do {                                                                                       \
		bst_result = Passed;                                                               \
		bs_trace_info_time(1, __VA_ARGS__);                                                \
	} while (0)

static struct bt_conn *default_conn;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
};

static bool volatile is_connected;
static bool volatile security_done;
static int volatile num_eatt_channels;

static void att_chan_connected(struct bt_conn *conn, uint16_t cid, uint16_t mtu, uint16_t mps)
{
	if (conn != default_conn) {
		FAIL("Wrong connection\n");
	}

	printk("ATT channel connected. cid: 0x%04X, mtu: %d. mps: %d\n", cid, mtu, mps);

	num_eatt_channels++;
	if (num_eatt_channels > CONFIG_BT_EATT_MAX) {
		FAIL("Too many EATT channels connected (%d)\n", num_eatt_channels);
	}
}

static void att_chan_disconnected(struct bt_conn *conn, uint16_t cid)
{
	if (conn != default_conn) {
		FAIL("Wrong connection\n");
	} else if (!cid) {
		FAIL("Failed to connect EATT channel\n");
	} else if (cid >= 0x0040) { /* Do not count the fixed ATT channel */
		num_eatt_channels--;
	}

	printk("ATT channel with cid 0x%04X disconnected\n", cid);
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
		FAIL("Failed to connect to %s (%u)\n", addr, conn_err);
	}

	default_conn = bt_conn_ref(conn);
	printk("Connected: %s\n", addr);
	is_connected = true;
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);

	if (default_conn != conn) {
		FAIL("Conn mismatch disconnect %s %s)\n", default_conn, conn);
	}

	bt_conn_unref(default_conn);
	default_conn = NULL;
	is_connected = false;
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err security_err)
{
	if (security_err) {
		FAIL("Security change failed (err: %d)\n", security_err);
	} else if (level != CONFIG_BT_EATT_SEC_LEVEL) {
		FAIL("Wrong security level (%d)", level);
	}

	security_done = true;
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static void test_peripheral_main(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		FAIL("Can't enable Bluetooth (err %d)\n", err);
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		FAIL("Advertising failed to start (err %d)\n", err);
	}

	while (!is_connected) {
		k_sleep(K_MSEC(100));
	}

	struct bt_att_cb att_cb = {
		.att_chan_connected = att_chan_connected,
		.att_chan_disconnected = att_chan_disconnected,
	};

	bt_att_cb_register(&att_cb);

	while (!security_done) {
		k_sleep(K_MSEC(100));
	}

	while (num_eatt_channels < CONFIG_BT_EATT_MAX) {
		k_sleep(K_MSEC(100));
	}

	/* Disconnect */
	err = bt_conn_disconnect(default_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	if (err) {
		FAIL("Disconnection failed (err %d)\n", err);
	}

	while (is_connected) {
		k_sleep(K_MSEC(100));
	}

	if (num_eatt_channels) {
		FAIL("EATT channels still connected (%d)\n", num_eatt_channels);
	}

	PASS("EATT Peripheral tests Passed\n");
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	struct bt_le_conn_param *param;
	int err;

	err = bt_le_scan_stop();
	if (err) {
		FAIL("Stop LE scan failed (err %d)\n", err);
	}

	param = BT_LE_CONN_PARAM_DEFAULT;
	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, param, &default_conn);

	if (err) {
		FAIL("Create conn failed (err %d)\n", err);
	}
	printk("Device connected\n");
}

static void test_central_main(void)
{
	struct bt_le_scan_param scan_param = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};

	int err;

	err = bt_enable(NULL);
	if (err) {
		FAIL("Can't enable Bluetooth (err %d)\n", err);
	}

	err = bt_le_scan_start(&scan_param, device_found);
	if (err) {
		FAIL("Scanning failed to start (err %d)\n", err);
	}

	while (!is_connected) {
		k_sleep(K_MSEC(100));
	}

	struct bt_att_cb att_cb = {
		.att_chan_connected = att_chan_connected,
		.att_chan_disconnected = att_chan_disconnected,
	};

	bt_att_cb_register(&att_cb);

	err = bt_conn_set_security(default_conn, CONFIG_BT_EATT_SEC_LEVEL);
	if (err) {
		FAIL("Security change failed");
	}

	while (!security_done) {
		k_sleep(K_MSEC(100));
	}

	err = bt_eatt_connect(default_conn, CONFIG_BT_EATT_MAX);
	if (err < 0) {
		FAIL("bt_eatt_connect failed (err: %d)\n", err);
	}

	while (num_eatt_channels < CONFIG_BT_EATT_MAX) {
		k_sleep(K_MSEC(100));
	}

	/* Wait for disconnect */
	while (is_connected) {
		k_sleep(K_MSEC(100));
	}

	if (num_eatt_channels) {
		FAIL("EATT channels still connected (%d)\n", num_eatt_channels);
	}

	PASS("EATT Central tests Passed\n");
}

static void test_init(void)
{
	bst_ticker_set_next_tick_absolute(60e6); /* 60 seconds */
	bst_result = In_progress;
}

static void test_tick(bs_time_t HW_device_time)
{
	if (bst_result != Passed) {
		FAIL("Too few EATT channels connected\n");
	}
}

static const struct bst_test_instance test_def[] = {
	{
		.test_id = "peripheral_eatt",
		.test_descr = "Peripheral EATT",
		.test_post_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = test_peripheral_main
	},
	{
		.test_id = "central_eatt",
		.test_descr = "Central EATT",
		.test_post_init_f = test_init,
		.test_tick_f = test_tick,
		.test_main_f = test_central_main
	},
	BSTEST_END_MARKER
};

struct bst_test_list *test_main_eatt_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_def);
}
