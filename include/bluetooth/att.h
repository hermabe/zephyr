/** @file
 *  @brief Attribute Protocol handling.
 */

/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_BLUETOOTH_ATT_H_
#define ZEPHYR_INCLUDE_BLUETOOTH_ATT_H_

#include <sys/slist.h>
#include <bluetooth/l2cap.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes for Error response PDU */
#define BT_ATT_ERR_SUCCESS			0x00
#define BT_ATT_ERR_INVALID_HANDLE		0x01
#define BT_ATT_ERR_READ_NOT_PERMITTED		0x02
#define BT_ATT_ERR_WRITE_NOT_PERMITTED		0x03
#define BT_ATT_ERR_INVALID_PDU			0x04
#define BT_ATT_ERR_AUTHENTICATION		0x05
#define BT_ATT_ERR_NOT_SUPPORTED		0x06
#define BT_ATT_ERR_INVALID_OFFSET		0x07
#define BT_ATT_ERR_AUTHORIZATION		0x08
#define BT_ATT_ERR_PREPARE_QUEUE_FULL		0x09
#define BT_ATT_ERR_ATTRIBUTE_NOT_FOUND		0x0a
#define BT_ATT_ERR_ATTRIBUTE_NOT_LONG		0x0b
#define BT_ATT_ERR_ENCRYPTION_KEY_SIZE		0x0c
#define BT_ATT_ERR_INVALID_ATTRIBUTE_LEN	0x0d
#define BT_ATT_ERR_UNLIKELY			0x0e
#define BT_ATT_ERR_INSUFFICIENT_ENCRYPTION	0x0f
#define BT_ATT_ERR_UNSUPPORTED_GROUP_TYPE	0x10
#define BT_ATT_ERR_INSUFFICIENT_RESOURCES	0x11
#define BT_ATT_ERR_DB_OUT_OF_SYNC		0x12
#define BT_ATT_ERR_VALUE_NOT_ALLOWED		0x13

/* Common Profile Error Codes (from CSS) */
#define BT_ATT_ERR_WRITE_REQ_REJECTED		0xfc
#define BT_ATT_ERR_CCC_IMPROPER_CONF		0xfd
#define BT_ATT_ERR_PROCEDURE_IN_PROGRESS	0xfe
#define BT_ATT_ERR_OUT_OF_RANGE			0xff

/* Version 5.2, Vol 3, Part F, 3.2.9 defines maximum attribute length to 512 */
#define BT_ATT_MAX_ATTRIBUTE_LEN		512

/* Handle 0x0000 is reserved for future use */
#define BT_ATT_FIRST_ATTRIBUTE_HANDLE           0x0001
#define BT_ATT_FIRST_ATTTRIBUTE_HANDLE __DEPRECATED_MACRO BT_ATT_FIRST_ATTRIBUTE_HANDLE
/* 0xffff is defined as the maximum, and thus last, valid attribute handle */
#define BT_ATT_LAST_ATTRIBUTE_HANDLE            0xffff
#define BT_ATT_LAST_ATTTRIBUTE_HANDLE __DEPRECATED_MACRO BT_ATT_LAST_ATTRIBUTE_HANDLE

#if defined(CONFIG_BT_TESTING)
int bt_eatt_disconnect_one(struct bt_conn *conn);
#endif /* CONFIG_BT_TESTING */

typedef void (*bt_att_func_t)(struct bt_conn *conn, uint8_t err, const void *pdu, uint16_t length,
			      void *user_data);

typedef int (*bt_att_encode_t)(struct net_buf *buf, size_t len, void *user_data);

/* ATT request context */
struct bt_att_req {
	sys_snode_t node;
	bt_att_func_t func;
	struct net_buf *buf;
#if defined(CONFIG_BT_SMP)
	bt_att_encode_t encode;
	uint8_t retrying : 1;
	uint8_t att_op;
	size_t len;
#endif /* CONFIG_BT_SMP */
	void *user_data;
};

enum {
	ATT_PENDING_RSP,
	ATT_PENDING_CFM,
	ATT_DISCONNECTED,
	ATT_ENHANCED,
	ATT_PENDING_SENT,

	/* Total number of flags - must be at the end of the enum */
	ATT_NUM_FLAGS,
};

/* ATT channel specific data */
struct bt_att_chan {
	/* Connection this channel is associated with */
	struct bt_att *att;
	struct bt_l2cap_le_chan chan;
	ATOMIC_DEFINE(flags, ATT_NUM_FLAGS);
	struct bt_att_req *req;
	struct k_fifo tx_queue;
	struct k_work_delayable timeout_work;
	void (*sent)(struct bt_att_chan *chan);
	sys_snode_t node;
};

int bt_att_chan_req_send(struct bt_att_chan *chan, struct bt_att_req *req);

/* Connect EATT channels */
int bt_eatt_connect(struct bt_conn *conn, uint8_t num_channels);

/* Disconnect EATT channels */
int bt_eatt_disconnect(struct bt_conn *conn);

struct bt_att_chan *bt_eatt_get_channel(struct bt_conn *conn);
struct bt_att_chan *att_get_fixed_chan(struct bt_conn *conn);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_BLUETOOTH_ATT_H_ */
