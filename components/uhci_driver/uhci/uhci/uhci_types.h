// Copyright 2015-2019 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef struct {
	bool seper_en;
	bool head_en;
	bool crc_rec_en;
	bool encode_crc_en;
} uhci_pk_cfg_t;

typedef enum {
	UHCI_RX_BREAK_CHR_EOF = 0x1,
	UHCI_RX_IDLE_EOF = 0x2,
	UHCI_RX_LEN_EOF = 0x4,
	UHCI_RX_EOF_MAX = 0x7,
} uhci_rxeof_cfg_t;

typedef struct {
	uint8_t seper_chr;
	uint8_t sub_chr1;
	uint8_t sub_chr2;
	bool sub_chr_en;
} uhci_seper_chr_t;

typedef struct {
	uint8_t xon_chr;
	uint8_t xon_sub1;
	uint8_t xon_sub2;
	uint8_t xoff_chr;
	uint8_t xoff_sub1;
	uint8_t xoff_sub2;
	uint8_t flow_en;
} uhci_swflow_ctrl_sub_chr_t;

typedef enum {
	UHCI_INTR_RX_START = (0x1 << 0),
	UHCI_INTR_TX_START = (0x1 << 1),
	UHCI_INTR_RX_HUNG = (0x1 << 2),
	UHCI_INTR_TX_HUNG = (0x1 << 3),
	UHCI_INTR_IN_DONE = (0x1 << 4),
	UHCI_INTR_IN_SUC_EOF = (0x1 << 5),
	UHCI_INTR_IN_ERR_EOF = (0x1 << 6),
	UHCI_INTR_OUT_DONE = (0x1 << 7),
	UHCI_INTR_OUT_EOF = (0x1 << 8),
	UHCI_INTR_IN_DSCR_ERR = (0x1 << 9),
	UHCI_INTR_OUT_DSCR_ERR = (0x1 << 10),
	UHCI_INTR_IN_DSCR_EMPTY = (0x1 << 11),
	UHCI_INTR_OUTLINK_EOF_ERR = (0x1 << 12),
	UHCI_INTR_OUT_TOT_EOF = (0x1 << 13),
	UHCI_INTR_SEND_S_Q = (0x1 << 14),
	UHCI_INTR_SEND_A_Q = (0x1 << 15),
	UHCI_INTR_IN_FIFO_FULL = (0x1 << 16),
} uhci_intr_t;

#ifdef __cplusplus
}
#endif
