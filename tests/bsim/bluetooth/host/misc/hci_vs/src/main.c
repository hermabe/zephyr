/*
 * Copyright (c) 2025 Embeint Pty Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/controller.h>
#include <zephyr/bluetooth/hci_vs.h>

#include <testlib/conn.h>
#include "babblekit/testcase.h"
#include "babblekit/flags.h"

static void test_write_bdaddr(void)
{
	char addr_str[BT_ADDR_LE_STR_LEN];
	bt_addr_le_t bt_addr;
	bt_addr_le_t bdaddr = {
		.type = BT_ADDR_LE_PUBLIC,
		.a.val = {0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8},
	};
	size_t count;
	int err;

#if 0
	bt_ctlr_set_public_addr(bdaddr.val);
#endif

	err = bt_id_create(&bdaddr, NULL);
	TEST_ASSERT(err == 0, "Failed to create public identity (err %d)", err);

	err = bt_enable(NULL);
	if (err != 0) {
		TEST_FAIL("Bluetooth init failed (err %d)\n", err);
		return;
	}

	printk("Bluetooth initialized\n");

#if 0
	/* Set the address on the controller */
	err = hci_vs_write_bd_addr(bdaddr);
	if (err != 0) {
		TEST_FAIL("Bluetooth device address write failed (err %d)\n", err);
		return;
	}
#endif

	/* Pull the address back out of the host */
	count = 1;
	bt_id_get(&bt_addr, &count);

	bt_addr_le_to_str(&bt_addr, addr_str, sizeof(addr_str));
	printk("Bluetooth controller address: %s\n", addr_str);

	TEST_ASSERT(bt_addr_le_eq(&bt_addr, &bdaddr));

	TEST_PASS("Write BDADDR passed\n");
}

static const struct bst_test_instance test_def[] = {{.test_id = "write_bdaddr",
						     .test_descr = "Write Public Bluetooth address",
						     .test_main_f = test_write_bdaddr},
						    BSTEST_END_MARKER};

struct bst_test_list *test_unregister_conn_cb_install(struct bst_test_list *tests)
{
	return bst_add_tests(tests, test_def);
}

extern struct bst_test_list *test_unregister_conn_cb_install(struct bst_test_list *tests);

bst_test_install_t test_installers[] = {test_unregister_conn_cb_install, NULL};

int main(void)
{
	bst_main();
	return 0;
}
