/** @file
 * @brief Bluetooth UPF shell functions
 *
 */

/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/zephyr.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include <zephyr/shell/shell.h>

#include "bt.h"

#define NAME_LEN 30

static bool name_to_connect_set;
static char name_to_connect[NAME_LEN];

static bool data_cb(struct bt_data *data, void *user_data)
{
	char *name = user_data;

	switch (data->type) {
	case BT_DATA_NAME_SHORTENED:
	case BT_DATA_NAME_COMPLETE:
		memcpy(name, data->data, MIN(data->data_len, NAME_LEN - 1));
		return false;
	default:
		return true;
	}
}

static void scan_recv(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
		      struct net_buf_simple *buf)
{
	char name[NAME_LEN] = {};
	int err;
	struct bt_conn *conn;

	bt_data_parse(buf, data_cb, name);
	if (name_to_connect_set && !strcmp(name_to_connect, name)) {
		return;
	}

	shell_print(ctx_shell, "Found device to connect %s", name_to_connect);

	err = bt_le_scan_stop();
	if (err) {
		shell_error(ctx_shell, "Stopping scanning failed (err %d)", err);
		return;
	} else {
		shell_print(ctx_shell, "Scan successfully stopped");
	}

	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT, &conn);
	if (err) {
		shell_error(ctx_shell, "Connection failed (%d)", err);
		return;
	} else {
		shell_print(ctx_shell, "Connection pending");

		/* unref connection obj in advance as app user */
		bt_conn_unref(conn);
	}

	return;
}

static int cmd_connect_name(const struct shell *sh, size_t argc, char *argv[])
{
	const char *name_arg = argv[1];
	int err;

	if (strlen(name_arg) >= sizeof(name_to_connect)) {
		shell_error(ctx_shell, "Name is too long (max %zu): %s\n", sizeof(name_to_connect),
			    name_arg);
		return -ENOEXEC;
	}

	strcpy(name_to_connect, name_arg);
	name_to_connect_set = true;

	err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, scan_recv);
	if (err) {
		shell_error(sh,
			    "Bluetooth set active scan failed "
			    "(err %d)",
			    err);
		return err;
	} else {
		shell_print(sh, "Bluetooth active scan enabled");
	}

	return 0;
}

static void eatt_connected(const struct bt_eatt_chan_info *info)
{
	shell_print(ctx_shell,
		    "EATT channel connected. CID 0x%04X, MTU %d, MPS %d, Init credits %d",
		    info->tx->cid, info->tx->mtu, info->tx->mps, info->tx->init_credits);
}

static void eatt_disconnected(const struct bt_eatt_chan_info *info)
{
	shell_print(ctx_shell, "EATT channel disconnected. CID 0x%04X", info->tx->cid);
}

static struct bt_eatt_cb eatt_cb = {
	.chan_connected = eatt_connected,
	.chan_disconnected = eatt_disconnected,
};

static int cmd_init(const struct shell *sh, size_t argc, char *argv[])
{
	bt_eatt_cb_register(&eatt_cb);

	return 0;
}

static int cmd_eatt_connect(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	size_t num_channels = strtol(argv[1], NULL, 0);

	err = bt_eatt_connect(default_conn, num_channels);
	if (err) {
		shell_error(sh, "EATT connection failed (err %d)", err);
		return err;
	} else {
		shell_print(sh, "EATT connection request sent");
	}

	return 0;
}

static uint16_t csf_handle, ssf_handle, db_handle, sc_handle;

static uint8_t discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   struct bt_gatt_discover_params *params)
{
	if (!attr) {
		return BT_GATT_ITER_STOP;
	}

	const struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;

	if (!bt_uuid_cmp(BT_UUID_GATT_CLIENT_FEATURES, chrc->uuid)) {
		csf_handle = chrc->value_handle;
		shell_print(ctx_shell, "CSF handle %04X", csf_handle);
		return BT_GATT_ITER_STOP;
	} else if (!bt_uuid_cmp(BT_UUID_GATT_SERVER_FEATURES, chrc->uuid)) {
		ssf_handle = chrc->value_handle;
		shell_print(ctx_shell, "SSF handle %04X", csf_handle);
		return BT_GATT_ITER_STOP;
	} else if (!bt_uuid_cmp(BT_UUID_GATT_DB_HASH, chrc->uuid)) {
		db_handle = chrc->value_handle;
		shell_print(ctx_shell, "DB Hash handle %04X", db_handle);
		return BT_GATT_ITER_STOP;
	} else if (!bt_uuid_cmp(BT_UUID_GATT_SC, chrc->uuid)) {
		sc_handle = chrc->value_handle;
		shell_print(ctx_shell, "SC handle %04X", csf_handle);
		return BT_GATT_ITER_STOP;
	}

	return BT_GATT_ITER_CONTINUE;
}

static int cmd_discover(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	const char *characteristic = argv[1];
	static struct bt_uuid_16 uuid;
	static struct bt_gatt_discover_params params = {
		.func = discover_cb,
		.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE,
		.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE,
		.type = BT_GATT_DISCOVER_CHARACTERISTIC,
		.uuid = &uuid.uuid,
	};

	if (!strcmp(characteristic, "csf")) {
		(void)memcpy(&uuid, BT_UUID_GATT_CLIENT_FEATURES, sizeof(uuid));
	} else if (!strcmp(characteristic, "ssf")) {
		(void)memcpy(&uuid, BT_UUID_GATT_SERVER_FEATURES, sizeof(uuid));
	} else if (!strcmp(characteristic, "db")) {
		(void)memcpy(&uuid, BT_UUID_GATT_DB_HASH, sizeof(uuid));
	} else if (!strcmp(characteristic, "sc")) {
		(void)memcpy(&uuid, BT_UUID_GATT_SC, sizeof(uuid));
	} else {
		shell_error(sh, "Invalid characteristic choice (csf, ssf, db, sc)");
		return 1;
	}

	err = bt_gatt_discover(default_conn, &params);
	if (err) {
		shell_error(sh, "Discovery failed (err %d)", err);
		return err;
	} else {
		shell_print(sh, "Discovery request sent");
	}

	return 0;
}

uint8_t read_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
		const void *data, uint16_t length)
{
	if (err) {
		shell_error(ctx_shell, "Read request failed (err %d)", err);
		return BT_GATT_ITER_STOP;
	}


	shell_print(ctx_shell, "Read response");
	shell_hexdump(ctx_shell, data, length);

	return BT_GATT_ITER_STOP;

}

// static void set_params(struct bt_gatt_read_params *params, struct bt_uuid_16 *uuid_dst,
// 		       struct bt_uuid *uuid_src, uint16_t handle)
// {
// 	if (handle) {
// 		params->handle_count = 1;
// 		params->single.handle = handle;
// 		params->single.offset = 0;
// 	} else {
// 		params->handle_count = 0;
// 		params->by_uuid.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
// 		params->by_uuid.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
// 		(void)memcpy(&uuid, uuid, sizeof(*uuid));
// 		params->by_uuid.uuid = &uuid->uuid;
// 	}
// }

static int cmd_read(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	const char *characteristic = argv[1];
	static struct bt_uuid_16 uuid = BT_UUID_INIT_16(0);
	static struct bt_gatt_read_params params = {
		.func = read_cb,
	};

	if (!strcmp(characteristic, "csf")) {
		if (csf_handle) {
			params.handle_count = 1;
			params.single.handle = csf_handle;
			params.single.offset = 0;
		} else {
			params.handle_count = 0;
			params.by_uuid.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
			params.by_uuid.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
			(void)memcpy(&uuid, BT_UUID_GATT_CLIENT_FEATURES, sizeof(uuid));
			params.by_uuid.uuid = &uuid.uuid;
		}
	} else if (!strcmp(characteristic, "ssf")) {
		if (ssf_handle) {
			params.handle_count = 1;
			params.single.handle = ssf_handle;
			params.single.offset = 0;
		} else {
			params.handle_count = 0;
			params.by_uuid.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
			params.by_uuid.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
			(void)memcpy(&uuid, BT_UUID_GATT_SERVER_FEATURES, sizeof(uuid));
			params.by_uuid.uuid = &uuid.uuid;
		}
	} else if (!strcmp(characteristic, "db")) {
		if (db_handle) {
			params.handle_count = 1;
			params.single.handle = db_handle;
			params.single.offset = 0;
		} else {
			params.handle_count = 0;
			params.by_uuid.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
			params.by_uuid.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
			(void)memcpy(&uuid, BT_UUID_GATT_DB_HASH, sizeof(uuid));
			params.by_uuid.uuid = &uuid.uuid;
		}
	} else if (!strcmp(characteristic, "sc")) {
		if (sc_handle) {
			params.handle_count = 1;
			params.single.handle = sc_handle;
			params.single.offset = 0;
		} else {
			params.handle_count = 0;
			params.by_uuid.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
			params.by_uuid.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
			(void)memcpy(&uuid, BT_UUID_GATT_SC, sizeof(uuid));
			params.by_uuid.uuid = &uuid.uuid;
		}
	} else {
		shell_error(sh, "Invalid characteristic choice (csf, ssf, db, sc)");
		return 1;
	}

	err = bt_gatt_read(default_conn, &params);
	if (err) {
		shell_error(sh, "Read failed (err %d)", err);
		return err;
	} else {
		shell_print(sh, "Read request sent");
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	upf_cmds, SHELL_CMD_ARG(connect_name, NULL, "<name>", cmd_connect_name, 2, 0),
	SHELL_CMD_ARG(init, NULL, "", cmd_init, 1, 0),
	SHELL_CMD_ARG(eatt_connect, NULL, "<num_channels>", cmd_eatt_connect, 2, 0),
	SHELL_CMD_ARG(discover, NULL, "<char>", cmd_discover, 2, 0),
	SHELL_CMD_ARG(read, NULL, "<char>", cmd_read, 2, 0),
	SHELL_SUBCMD_SET_END);

static int cmd_upf(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 1) {
		shell_help(sh);
		/* shell returns 1 when help is printed */
		return 1;
	}

	shell_error(sh, "%s unknown parameter: %s", argv[0], argv[1]);

	return -EINVAL;
}

SHELL_CMD_ARG_REGISTER(upf, &upf_cmds, "Bluetooth UPF shell commands", cmd_upf, 1, 1);
