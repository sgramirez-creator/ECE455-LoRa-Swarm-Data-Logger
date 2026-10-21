/*
  Copyright (c) 2014-2015 Arduino LLC.  All right reserved.
  Copyright (c) 2016 Sandeep Mistry All right reserved.
  Copyright (c) 2018, Adafruit Industries (adafruit.com)

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU Lesser General Public License for more details.
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _VARIANT_RAK4630_
#define _VARIANT_RAK4630_

#define RAK4630

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF
// define USE_LFRC    // Board uses RC for LF

/*----------------------------------------------------------------------------
 *        Headers
 *----------------------------------------------------------------------------*/

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (6)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
#define PIN_LED1 (35)
#define LED_BLUE (36)

#define LED_GREEN PIN_LED1
#define LED_NOTIFICATION LED_BLUE

#define LED_STATE_ON 1 // State when LED is litted

// No button: stock PIN_BUTTON1 (P0.09 = IO5) is the RTC CLKOUT line in Slot D on the RAK19001.

/*
 * Analog pins
 */
#define PIN_A0 (5)
#define PIN_A1 (31)
#define PIN_A2 (28)
#define PIN_A3 (29)
#define PIN_A4 (30)
#define PIN_A5 (31)
#define PIN_A6 (0xff)
#define PIN_A7 (0xff)

static const uint8_t A0 = PIN_A0;
static const uint8_t A1 = PIN_A1;
static const uint8_t A2 = PIN_A2;
static const uint8_t A3 = PIN_A3;
static const uint8_t A4 = PIN_A4;
static const uint8_t A5 = PIN_A5;
static const uint8_t A6 = PIN_A6;
static const uint8_t A7 = PIN_A7;
#define ADC_RESOLUTION 14

// Other pins
#define WB_I2C1_SDA (13) // SENSOR_SLOT IO_SLOT
#define WB_I2C1_SCL (14) // SENSOR_SLOT IO_SLOT

#define PIN_AREF (2)
#define PIN_NFC1 (9)
#define WB_IO5 PIN_NFC1
#define WB_IO4 (4)
#define PIN_NFC2 (10)

static const uint8_t AREF = PIN_AREF;

/*
 * Serial interfaces
 */
// Serial1 = base TXD1/RXD1 (IO slot pins 33/34) = RAK5802 RS485. Reserved for the sonde only.
#define PIN_SERIAL1_RX (15)
#define PIN_SERIAL1_TX (16)

// Connected to Jlink CDC
#define PIN_SERIAL2_RX (8)
#define PIN_SERIAL2_TX (6)

/*
 * SPI Interfaces
 */
#define SPI_INTERFACES_COUNT 2

#define PIN_SPI_MISO (45)
#define PIN_SPI_MOSI (44)
#define PIN_SPI_SCK (43)

#define PIN_SPI1_MISO (29) // (0 + 29)
#define PIN_SPI1_MOSI (30) // (0 + 30)
#define PIN_SPI1_SCK (3)   // (0 + 3)

static const uint8_t SS = 42;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

/*
 * eink display pins
 */

#define PIN_EINK_CS (0 + 26)
#define PIN_EINK_BUSY (0 + 4)
#define PIN_EINK_DC (0 + 17)
#define PIN_EINK_RES (-1)
#define PIN_EINK_SCLK (0 + 3)
#define PIN_EINK_MOSI (0 + 30) // also called SDI

// #define USE_EINK

// RAKRGB
#define HAS_NCP5623

/*
 * Wire Interfaces
 */
#define WIRE_INTERFACES_COUNT 1

#define PIN_WIRE_SDA (WB_I2C1_SDA)
#define PIN_WIRE_SCL (WB_I2C1_SCL)

// QSPI Pins
#define PIN_QSPI_SCK 3
#define PIN_QSPI_CS 26
#define PIN_QSPI_IO0 30
#define PIN_QSPI_IO1 29
#define PIN_QSPI_IO2 28
#define PIN_QSPI_IO3 2

// On-board QSPI Flash
#define EXTERNAL_FLASH_DEVICES IS25LP080D
#define EXTERNAL_FLASH_USE_QSPI

/* @note RAK5005-O GPIO mapping to RAK4631 GPIO ports
   RAK5005-O <->  nRF52840
   IO1       <->  P0.17 (Arduino GPIO number 17)
   IO2       <->  P1.02 (Arduino GPIO number 34)
   IO3       <->  P0.21 (Arduino GPIO number 21)
   IO4       <->  P0.04 (Arduino GPIO number 4)
   IO5       <->  P0.09 (Arduino GPIO number 9)
   IO6       <->  P0.10 (Arduino GPIO number 10)
   IO7       <->  P0.28 (Arduino GPIO number 28)
   SW1       <->  P0.01 (Arduino GPIO number 1)
   A0        <->  P0.04/AIN2 (Arduino Analog A2
   A1        <->  P0.31/AIN7 (Arduino Analog A7
   SPI_CS    <->  P0.26 (Arduino GPIO number 26)
 */

// RAK4630 LoRa module

/* Setup of the SX1262 LoRa module ( https://docs.rakwireless.com/Product-Categories/WisBlock/RAK4631/Datasheet/ )

P1.10   NSS     SPI NSS (Arduino GPIO number 42)
P1.11   SCK     SPI CLK (Arduino GPIO number 43)
P1.12   MOSI    SPI MOSI (Arduino GPIO number 44)
P1.13   MISO    SPI MISO (Arduino GPIO number 45)
P1.14   BUSY    BUSY signal (Arduino GPIO number 46)
P1.15   DIO1    DIO1 event interrupt (Arduino GPIO number 47)
P1.06   NRESET  NRESET manual reset of the SX1262 (Arduino GPIO number 38)

Important for successful SX1262 initialization:

* Setup DIO2 to control the antenna switch
* Setup DIO3 to control the TCXO power supply
* Setup the SX1262 to use it's DCDC regulator and not the LDO
* RAK4630 schematics show GPIO P1.07 connected to the antenna switch, but it should not be initialized, as DIO2 will do the
control of the antenna switch

SO GPIO 39/TXEN MAY NOT BE DEFINED FOR SUCCESSFUL OPERATION OF THE SX1262 - TG

*/

// PMSA003I_ENABLE_PIN / AQ_SET_PIN (IO6) and DETECTION_SENSOR_EN (IO4) intentionally omitted:
// IO6 is the RTC INT line in Slot D and IO4 is the GNSS 1PPS output in Slot E.

#define USE_SX1262
#define SX126X_CS (42)
#define SX126X_DIO1 (47)
#define SX126X_BUSY (46)
#define SX126X_RESET (38)
// #define SX126X_TXEN (39)
// #define SX126X_RXEN (37)
#define SX126X_POWER_EN (37)
// DIO2 controlls an antenna switch and the TCXO voltage is controlled by DIO3
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// Testing USB detection
#define NRF_APM
// If using a power chip like the INA3221 you can override the default battery voltage channel below
// and comment out NRF_APM to use the INA3221 instead of the USB detection for charging
// #define INA3221_BAT_CH INA3221_CH2
// #define INA3221_ENV_CH INA3221_CH1

// enables 3.3V periphery like GPS or IO Module
// Do not toggle this for GPS power savings
#define PIN_3V3_EN (34)
#define WB_IO2 PIN_3V3_EN

// No UART GPS: GPS_RX_PIN/GPS_TX_PIN are left undefined so Meshtastic's GPS driver never opens a UART.
// The RAK12500 is read over I2C by GnssI2CModule, which works in any RAK19001 slot.
// In Slot E its RESET_N lands on IO3 and 1PPS on IO4; neither is driven by firmware.
#define HAS_GNSS_I2C 1
#define GNSS_I2C_ADDR 0x42

// Yosemitech Y4000 sonde over the RAK5802 (auto-direction RS485, no DE pin needed).
#define HAS_SONDE 1
#define SONDE_SERIAL Serial1
#define SONDE_BAUD 9600
#define SONDE_MODBUS_ADDR 1
// Y4000 12 V load switch enable, active high. P0.28 = header pin "IO7" on the RAK19001. Set -1 if always powered.
#define SONDE_POWER_PIN 28

// RAK12002 RTC Module
#define RV3028_RTC (uint8_t)0b1010010

// No PIN_BUZZER: stock IO3 buzzer would toggle the GNSS RESET_N line in Slot E.

// RAK4631 custom ringtone
#undef USERPREFS_RINGTONE_RTTTL
#define USERPREFS_RINGTONE_RTTTL "Rak:d=32,o=5,b=200:b7,p,b7,4p,p"

// Battery
// The battery sense is hooked to pin A0 (5)
#define BATTERY_PIN PIN_A0
// and has 12 bit resolution
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define BATTERY_SENSE_RESOLUTION 4096.0
#undef AREF_VOLTAGE
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
#define ADC_MULTIPLIER 1.73

// RAK4630 AIN0 = nrf52840 AIN3 = Pin 5
#define BATTERY_LPCOMP_INPUT NRF_LPCOMP_INPUT_3

// We have AIN3 with a VBAT divider so AIN3 = VBAT * (1.5/2.5)
// We have the device going deep sleep under 3.1V, which is AIN3 = 1.86V
// So we can wake up when VBAT>=VDD is restored to 3.3V, where AIN3 = 1.98V
// 1.98/3.3 = 6/10, but that's close to the VBAT divider, so we
// pick 6/8VDD, which means VBAT=4.1V.
// Reference:
// VDD=3.3V AIN3=5/8*VDD=2.06V VBAT=1.66*AIN3=3.41V
// VDD=3.3V AIN3=11/16*VDD=2.26V VBAT=1.66*AIN3=3.76V
// VDD=3.3V AIN3=6/8*VDD=2.47V VBAT=1.66*AIN3=4.1V
#define BATTERY_LPCOMP_THRESHOLD NRF_LPCOMP_REF_SUPPLY_11_16

// No Ethernet: the RAK13800 reset line is IO3, which is the GNSS RESET_N in Slot E.
#define RAK_4631 1

#ifdef __cplusplus
}
#endif

/*----------------------------------------------------------------------------
 *        Arduino objects - C++ only
 *----------------------------------------------------------------------------*/

#endif
