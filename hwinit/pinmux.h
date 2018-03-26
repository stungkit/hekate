/*
* Copyright (c) 2018 naehrwert
*
* This program is free software; you can redistribute it and/or modify it
* under the terms and conditions of the GNU General Public License,
* version 2, as published by the Free Software Foundation.
*
* This program is distributed in the hope it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _PINMUX_H_
#define _PINMUX_H_

#include "types.h"

/*! Pinmux registers. */
#define PINMUX_AUX_UART2_TX 0xF4
#define PINMUX_AUX_UART3_TX 0x104
#define PINMUX_AUX_GPIO_PE6 0x248
#define PINMUX_AUX_GPIO_PH6 0x250
/*! 0:UART-A, 1:UART-B, 3:UART-C, 3:UART-D */
#define PINMUX_AUX_UARTX_TX(x) (0xE4 + 0x10 * (x))
#define PINMUX_AUX_UARTX_RX(x) (0xE8 + 0x10 * (x))
#define PINMUX_AUX_UARTX_RTS(x) (0xEC + 0x10 * (x))
#define PINMUX_AUX_UARTX_CTS(x) (0xF0 + 0x10 * (x))
/*! 0:GEN1, 1:GEN2, 2:GEN3, 3:CAM, 4:PWR */
#define PINMUX_AUX_X_I2C_SCL(x) (0xBC + 8 * (x))
#define PINMUX_AUX_X_I2C_SDA(x) (0xC0 + 8 * (x))

void pinmux_config_uart(u32 idx);
void pinmux_config_i2c(u32 idx);

#endif
