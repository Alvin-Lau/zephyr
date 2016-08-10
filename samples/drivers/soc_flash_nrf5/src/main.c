/*
 * Copyright (c) 2016 Linaro Limited
 *               2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <zephyr.h>
#include <flash.h>
#include <device.h>

#define SYS_LOG_LEVEL SYS_LOG_LEVEL_INFO
#include <misc/sys_log.h>

/* Offset between pages */
#define FLASH_TEST_OFFSET 0x40000
#define FLASH_PAGE_SIZE   4096
#define TEST_DATA_WORD_0  0x1122
#define TEST_DATA_WORD_1  0xaabb
#define TEST_DATA_WORD_2  0xabcd
#define TEST_DATA_WORD_3  0x1234
#define TEST_DATA_LEN     4

void main(void)
{
	struct device *flash_dev;
	uint32_t buf_array_1[4] = { TEST_DATA_WORD_0, TEST_DATA_WORD_1,
				TEST_DATA_WORD_2, TEST_DATA_WORD_3 };
	uint32_t buf_array_2[4] = { TEST_DATA_WORD_3, TEST_DATA_WORD_1,
				TEST_DATA_WORD_2, TEST_DATA_WORD_0 };
	uint32_t buf_word = 0;
	uint32_t i, offset;

	SYS_LOG_INF("\nNordic nRF5 Flash Testing");
	SYS_LOG_INF("=========================");

	flash_dev = device_get_binding("NRF5_FLASH");

	if (!flash_dev) {
		SYS_LOG_ERR("Nordic nRF5 flash driver was not found!");
		return;
	}

	SYS_LOG_INF("\nTest 1: Flash erase page at 0x%x", FLASH_TEST_OFFSET);
	if (flash_erase(flash_dev, FLASH_TEST_OFFSET, FLASH_PAGE_SIZE) != 0) {
		SYS_LOG_INF("   Flash erase failed!");
	} else {
		SYS_LOG_INF("   Flash erase succeeded!");
	}

	SYS_LOG_INF("\nTest 2: Flash write (word array 1)");
	flash_write_protection_set(flash_dev, false);
	for (i = 0; i < TEST_DATA_LEN; i++) {
		offset = FLASH_TEST_OFFSET + (i << 2);
		SYS_LOG_INF("   Attempted to write %x at 0x%x",
			    buf_array_1[i], offset);
		if (flash_write(flash_dev, offset, &buf_array_1[i],
					TEST_DATA_LEN) != 0) {
			SYS_LOG_INF("   Flash write failed!");
			return;
		}
		SYS_LOG_INF("   Attempted to read 0x%x", offset);
		if (flash_read(flash_dev, offset, &buf_word,
					TEST_DATA_LEN) != 0) {
			SYS_LOG_INF("   Flash read failed!");
			return;
		}
		SYS_LOG_INF("   Data read: %x", buf_word);
		if (buf_array_1[i] == buf_word) {
			SYS_LOG_INF("   Data read matches data written. "
				    "Good!");
		} else {
			SYS_LOG_INF("   Data read does not match data "
				    "written!");
		}
	}
	flash_write_protection_set(flash_dev, true);

	offset = FLASH_TEST_OFFSET - FLASH_PAGE_SIZE * 2;
	SYS_LOG_INF("\nTest 3: Flash erase (4 pages at 0x%x)", offset);
	if (flash_erase(flash_dev, offset, FLASH_PAGE_SIZE * 4) != 0) {
		SYS_LOG_INF("   Flash erase failed!");
	} else {
		SYS_LOG_INF("   Flash erase succeeded!");
	}

	SYS_LOG_INF("\nTest 4: Flash write (word array 2)");
	flash_write_protection_set(flash_dev, false);
	for (i = 0; i < TEST_DATA_LEN; i++) {
		offset = FLASH_TEST_OFFSET + (i << 2);
		SYS_LOG_INF("   Attempted to write %x at 0x%x",
			    buf_array_2[i], offset);
		if (flash_write(flash_dev, offset, &buf_array_2[i],
					TEST_DATA_LEN) != 0) {
			SYS_LOG_INF("   Flash write failed!");
			return;
		}
		SYS_LOG_INF("   Attempted to read 0x%x", offset);
		if (flash_read(flash_dev, offset, &buf_word,
					TEST_DATA_LEN) != 0) {
			SYS_LOG_INF("   Flash read failed!");
			return;
		}
		SYS_LOG_INF("   Data read: %x", buf_word);
		if (buf_array_2[i] == buf_word) {
			SYS_LOG_INF("   Data read matches data written. "
				    "Good!");
		} else {
			SYS_LOG_INF("   Data read does not match data "
				    "written!");
		}
	}
	flash_write_protection_set(flash_dev, true);
}
