/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "common.h"

#define ARRAY_ITEM(i, _) i
uint8_t short_chrc_data[SHORT_CHRC_SIZE] = { LISTIFY(SHORT_CHRC_SIZE, ARRAY_ITEM, (,)) };
uint8_t long_chrc_data[LONG_CHRC_SIZE] = { LISTIFY(LONG_CHRC_SIZE, ARRAY_ITEM, (,)) };

void test_tick(bs_time_t HW_device_time)
{
	if (bst_result != Passed) {
		FAIL("test failed (not passed after %i seconds)\n", WAIT_TIME);
	}
}

void test_init(void)
{
	bst_ticker_set_next_tick_absolute(WAIT_TIME);
	bst_result = In_progress;
}
