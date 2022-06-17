// Copyright 2015-2019 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*******************************************************************************
 * NOTICE
 * The hal is not public api, don't use in application code.
 * See readme.md in soc/include/hal/readme.md
 ******************************************************************************/

// The HAL layer for UHCI.
// There is no parameter check in the hal layer, so the caller must ensure the correctness of the parameters.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "uhci_ll.h"

/**
 * Context that should be maintained by both the driver and the HAL
 */
typedef struct {
	uhci_dev_t *dev;
	uint32_t version;
} uhci_hal_context_t;

void uhci_hal_attach_uart_port(uhci_hal_context_t *hal, int uart_num);
void uhci_hal_set_seper_chr(uhci_hal_context_t *hal, uhci_seper_chr_t *seper_char);
void uhci_hal_get_seper_chr(uhci_hal_context_t *hal, uhci_seper_chr_t *seper_chr);
void uhci_hal_set_swflow_ctrl_sub_chr(uhci_hal_context_t *hal, uhci_swflow_ctrl_sub_chr_t *sub_ctr);
void uhci_hal_dma_in_reset(uhci_hal_context_t *hal);
void uhci_hal_dma_out_reset(uhci_hal_context_t *hal);
void uhci_hal_enable_intr(uhci_hal_context_t *hal, uint32_t intr_mask);
void uhci_hal_disable_intr(uhci_hal_context_t *hal, uint32_t intr_mask);
void uhci_hal_clear_intr(uhci_hal_context_t *hal, uint32_t intr_mask);
uint32_t uhci_hal_get_intr(uhci_hal_context_t *hal);
void uhci_hal_set_rx_dma(uhci_hal_context_t *hal, uint32_t addr);
void uhci_hal_set_tx_dma(uhci_hal_context_t *hal, uint32_t addr);
void uhci_hal_rx_dma_start(uhci_hal_context_t *hal);
void uhci_hal_tx_dma_start(uhci_hal_context_t *hal);
void uhci_hal_rx_dma_stop(uhci_hal_context_t *hal);
void uhci_hal_tx_dma_stop(uhci_hal_context_t *hal);
void uhci_hal_set_eof_mode(uhci_hal_context_t *hal, uint32_t eof_mode);
void uhci_hal_init(uhci_hal_context_t *hal, int uhci_num);

#ifdef __cplusplus
}
#endif
