/**
 * @file      utilities.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2024-01-22
 *
 */
#pragma once

// Overridable from build flags. 7 MHz is the conservative LilyGo stock value
// (23.5 Hz refresh); the ST7701S glass itself is a 60 Hz part. Upstream LilyGo
// now ships 8 MHz.
//
// Espressif's ~22 MHz figure for octal PSRAM @80 MHz is a quiet-bus number and
// does not survive contact with a busy renderer. Measured on this board with
// the scan-out DMA reading PSRAM directly: 13.3 MHz is unreadable while the
// render task writes at 60 fps, and the same clock is mostly clean at 5 fps.
// The ceiling is set by how much PSRAM traffic competes with scan-out, not by
// the pclk alone -- see GM_LCD_BOUNCE_LINES.
#ifndef RGB_MAX_PIXEL_CLOCK_HZ
#define RGB_MAX_PIXEL_CLOCK_HZ (7000000UL)
#endif

// Scanlines per bounce buffer, or 0 to scan straight out of PSRAM. Two buffers
// of this size are allocated from internal DRAM, so the cost is
// 2 * lines * 480 * 2 bytes. Must divide the 480-line framebuffer exactly.
#ifndef GM_LCD_BOUNCE_LINES
#define GM_LCD_BOUNCE_LINES (0)
#endif
#if GM_LCD_BOUNCE_LINES && (480 % GM_LCD_BOUNCE_LINES)
#error "GM_LCD_BOUNCE_LINES must divide the 480-line framebuffer exactly"
#endif

#define BOARD_TFT_WIDTH (480)
#define BOARD_TFT_HEIGHT (480)

#define BOARD_TFT_BL (46)

#define BOARD_TFT_HSYNC (47)
#define BOARD_TFT_VSYNC (41)
#define BOARD_TFT_DE (45)
#define BOARD_TFT_PCLK (42)

// T-RGB physical connection is RGB666, but ESP only supports RGB565

// RGB data signal(
// DB0:BlUE LSB;DB5:BIUE MSB;
// DB6:GREEN LSB;DB11:GREEN,MSB;
// DB12:RED LSB;DB17:RED MSB)
#define BOARD_TFT_DATA0 (44) // B0
#define BOARD_TFT_DATA1 (21) // B1
#define BOARD_TFT_DATA2 (18) // B2
#define BOARD_TFT_DATA3 (17) // B3
#define BOARD_TFT_DATA4 (16) // B4
#define BOARD_TFT_DATA5 (15) // B5    MSB

#define BOARD_TFT_DATA6 (14)  // G0
#define BOARD_TFT_DATA7 (13)  // G1
#define BOARD_TFT_DATA8 (12)  // G2
#define BOARD_TFT_DATA9 (11)  // G3
#define BOARD_TFT_DATA10 (10) // G4
#define BOARD_TFT_DATA11 (9)  // G5    MSB

#define BOARD_TFT_DATA12 (43) // R0
#define BOARD_TFT_DATA13 (7)  // R1
#define BOARD_TFT_DATA14 (6)  // R2
#define BOARD_TFT_DATA15 (5)  // R3
#define BOARD_TFT_DATA16 (3)  // R4
#define BOARD_TFT_DATA17 (2)  // R5    MSB

#define BOARD_TFT_RST (6)
#define BOARD_TFT_CS (3)
#define BOARD_TFT_MOSI (4)
#define BOARD_TFT_SCLK (5)

#define BOARD_I2C_SDA (8)
#define BOARD_I2C_SCL (48)

#define BOARD_TOUCH_IRQ (1)
#define BOARD_TOUCH_RST (1)

#define BOARD_SDMMC_EN (7)
#define BOARD_SDMMC_SCK (39)
#define BOARD_SDMMC_CMD (40)
#define BOARD_SDMMC_DAT (38)

#define BOARD_ADC_DET (4)
