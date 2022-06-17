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

// The HAL layer for UHCI (common part)
#include "uhci/uhci_hal.h"

void uhci_hal_attach_uart_port(uhci_hal_context_t *hal, int uart_num)
{
	uhci_ll_attach_uart_port(hal->dev, uart_num);
}

void uhci_hal_set_seper_chr(uhci_hal_context_t *hal, uhci_seper_chr_t *seper_char)
{
	uhci_ll_set_seper_chr(hal->dev, seper_char);
}

void uhci_hal_get_seper_chr(uhci_hal_context_t *hal, uhci_seper_chr_t *seper_chr)
{
	uhci_ll_get_seper_chr(hal->dev, seper_chr);
}

void uhci_hal_set_swflow_ctrl_sub_chr(uhci_hal_context_t *hal, uhci_swflow_ctrl_sub_chr_t *sub_ctr)
{
	uhci_ll_set_swflow_ctrl_sub_chr(hal->dev, sub_ctr);
}

void uhci_hal_dma_in_reset(uhci_hal_context_t *hal)
{
	uhci_ll_dma_in_reset(hal->dev);
}

void uhci_hal_dma_out_reset(uhci_hal_context_t *hal)
{
	uhci_ll_dma_out_reset(hal->dev);
}

void uhci_hal_enable_intr(uhci_hal_context_t *hal, uint32_t intr_mask)
{
	uhci_ll_enable_intr(hal->dev, intr_mask);
}

void uhci_hal_disable_intr(uhci_hal_context_t *hal, uint32_t intr_mask)
{
	uhci_ll_disable_intr(hal->dev, intr_mask);
}

void uhci_hal_clear_intr(uhci_hal_context_t *hal, uint32_t intr_mask)
{
	uhci_ll_clear_intr(hal->dev, intr_mask);
}

uint32_t uhci_hal_get_intr(uhci_hal_context_t *hal)
{
	return uhci_ll_get_intr(hal->dev);
}

void uhci_hal_set_rx_dma(uhci_hal_context_t *hal, uint32_t addr)
{
	uhci_ll_set_rx_dma(hal->dev, addr);
}

void uhci_hal_set_tx_dma(uhci_hal_context_t *hal, uint32_t addr)
{
	uhci_ll_set_tx_dma(hal->dev, addr);
}

void uhci_hal_rx_dma_start(uhci_hal_context_t *hal)
{
	uhci_ll_rx_dma_start(hal->dev);
}

void uhci_hal_tx_dma_start(uhci_hal_context_t *hal)
{
	uhci_ll_tx_dma_start(hal->dev);
}

void uhci_hal_rx_dma_stop(uhci_hal_context_t *hal)
{
	uhci_ll_rx_dma_stop(hal->dev);
}

void uhci_hal_tx_dma_stop(uhci_hal_context_t *hal)
{
	uhci_ll_tx_dma_stop(hal->dev);
}

void uhci_hal_set_eof_mode(uhci_hal_context_t *hal, uint32_t eof_mode)
{
	uhci_ll_set_eof_mode(hal->dev, eof_mode);
}

void uhci_hal_init(uhci_hal_context_t *hal, int uhci_num)
{
	hal->dev = UHCI_LL_GET_HW(uhci_num);
	hal->version = hal->dev->date;
	uhci_ll_init(hal->dev);
}
