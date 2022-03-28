/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bstests.h"

extern struct bst_test_list *test_server_install(struct bst_test_list *tests);
extern struct bst_test_list *test_client_install(struct bst_test_list *tests);

bst_test_install_t test_installers[] = {
	test_server_install,
	test_client_install,
	NULL
};

void main(void)
{
	bst_main();
}
