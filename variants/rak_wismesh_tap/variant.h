#ifndef _VARIANT_RAK_WISMESH_TAP_
#define _VARIANT_RAK_WISMESH_TAP_

#define RAK4630
#define RAK_WISMESH_TAP

/** Master clock frequency */
#define VARIANT_MCK (64000000ul)

#define USE_LFXO // Board uses 32khz crystal for LF

#include "WVariant.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * WisBlock Base GPIO definitions
 */
static const uint8_t WB_IO1 = 17;     // SLOT_A SLOT_B / Touch Reset
static const uint8_t WB_IO2 = 34;     // SLOT_A SLOT_B / 3V3_S Power Switch
static const uint8_t WB_IO3 = 21;     // SLOT_C / Display Backlight
static const uint8_t WB_IO4 = 4;      // SLOT_C / Display DC
static const uint8_t WB_IO5 = 9;      // SLOT_D / Display Reset
static const uint8_t WB_IO6 = 10;     // SLOT_D / Touch INT
static const uint8_t WB_SW1 = 33;     // IO_SLOT
static const uint8_t WB_A0 = 5;       // IO_SLOT
static const uint8_t WB_A1 = 31;      // IO_SLOT
static const uint8_t WB_I2C1_SDA = 13;// SENSOR_SLOT IO_SLOT
static const uint8_t WB_I2C1_SCL = 14;// SENSOR_SLOT IO_SLOT
static const uint8_t WB_I2C2_SDA = 24;// IO_SLOT
static const uint8_t WB_I2C2_SCL = 25;// IO_SLOT
static const uint8_t WB_SPI_CS = 26;  // IO_SLOT / Display CS
static const uint8_t WB_SPI_CLK = 3;  // IO_SLOT / Display SCK
static const uint8_t WB_SPI_MISO = 29;// IO_SLOT / Display MISO
static const uint8_t WB_SPI_MOSI = 30;// IO_SLOT / Display MOSI

// Number of pins defined in PinDescription array
#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (6)
#define NUM_ANALOG_OUTPUTS (0)

// LEDs
#define PIN_LED1 (35)
#define PIN_LED2 (36)
#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED2
#define LED_GREEN PIN_LED1
#define LED_BLUE PIN_LED2
#define LED_STATE_ON 1

// Analog pins
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

// Power management boot protection threshold (millivolts)
#define PWRMGT_VOLTAGE_BOOTLOCK 3300
#define PWRMGT_LPCOMP_AIN 3
#define PWRMGT_LPCOMP_REFSEL 4

#define PIN_AREF (2)
static const uint8_t AREF = PIN_AREF;

// Serial interfaces (GPS on Serial1)
#define PIN_SERIAL1_RX (15)
#define PIN_SERIAL1_TX (16)
#define PIN_SERIAL2_RX (19)
#define PIN_SERIAL2_TX (20)

// WisBlock SPI Interface (ST7789 display)
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO (29)
#define PIN_SPI_MOSI (30)
#define PIN_SPI_SCK (3)

static const uint8_t SS = 26;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

// LoRa radio module pins for RAK4631
#define P_LORA_DIO_1 (47)
#define P_LORA_NSS (42)
#define P_LORA_RESET (38)
#define P_LORA_BUSY (46)
#define P_LORA_SCLK (43)
#define P_LORA_MISO (45)
#define P_LORA_MOSI (44)
#define SX126X_POWER_EN (37)
#define SX126X_DIO2_AS_RF_SWITCH true
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// Wire Interfaces (I2C for FT6336 touch & sensors)
#define WIRE_INTERFACES_COUNT 2
#define PIN_WIRE_SDA (13)
#define PIN_WIRE_SCL (14)
#define PIN_WIRE1_SDA (24)
#define PIN_WIRE1_SCL (25)

// Display Hardware Configuration
#define PIN_TFT_CS (26)
#define PIN_TFT_DC (4)
#define PIN_TFT_RST (9)
#define PIN_TFT_LEDA_CTL (21)
#define PIN_TFT_BL (21)

// Touch Hardware Configuration
#define PIN_TOUCH_INT (10)
#define PIN_TOUCH_RST (17)

#define PIN_GPS_1PPS (17)
#define GPS_BAUD_RATE 9600
#define GPS_ADDRESS 0x42

#ifdef __cplusplus
}
#endif

#endif
