/*
 * @file TM1637.h
 *
 * Created on: Jan 19, 2026
 * Author: Marek Godlowicz https://github.com/MarekGodlo
 * @file tm1637_driver.c
 * @brief Non-blocking driver for TM1637 display using state machine and timer.
 */

#ifndef SRC_TM1637_TM1637_H_
#define SRC_TM1637_TM1637_H_
#include "main.h"
#include <stdbool.h>

//#define CMD 0x40

//#define DIO_PIN DIO_Pin
//#define DIO_PORT DIO_GPIO_Port

//#define CLK_PIN CLK_Pin
//#define CLK_PORT CLK_GPIO_Port

// Configuration
#define BUFFER_SIZE 50
#define TX_SIZE 7

// TM1637 Command Definitions
#define WRITE_DATA 0x40
#define READ_DATA 0x42
#define ADDRESS_AI 0x40
#define FIXED_ADDRESS 0x44
#define NORMAL_MODE 0x40
#define TEST_MODE 0x48

// Display Address Map
#define ADDRESS_00H 0xC0
#define ADDRESS_01H 0xC1
#define ADDRESS_02H 0xC2
#define ADDRESS_03H 0xC3
#define ADDRESS_04H 0xC4
#define ADDRESS_05H 0xC5

// Brightness Controls
#define BRIGHTNESS_ON 0x88
#define BRIGHTNESS_OFF 0x80

#define BRIGHTNESS_0 0x80
#define BRIGHTNESS_1 0x81
#define BRIGHTNESS_2 0x82
#define BRIGHTNESS_3 0x83
#define BRIGHTNESS_4 0x84
#define BRIGHTNESS_5 0x85
#define BRIGHTNESS_6 0x86
#define BRIGHTNESS_7 0x87
#define BRIGHTNESS_MAX BRIGHTNESS_7
#define BRIGHTNESS_MIN BRIGHTNESS_0

extern const uint8_t TM1637_Digits[];

typedef struct {
    GPIO_TypeDef* dio_port;
    uint16_t      dio_pin;
    GPIO_TypeDef* clk_port;
    uint16_t      clk_pin;
    TIM_HandleTypeDef* htim;
} TM1637_Config;

/**
 * @struct TM1637_Tx
 * @brief Structure representing a single data frame for transmission.
 */
typedef struct {
	uint8_t tx_frame[TX_SIZE]; /**< Array containing the raw segments data. */
	uint8_t tx_len; /**< Number of bytes to be transmitted in this frame. */
} TM1637_Tx;

/**
 * @struct TM1637_Buffer
 * @brief Circular buffer structure for transmission frame.
 */
typedef struct {
	TM1637_Tx buffer[BUFFER_SIZE]; /**< Array containing data frames to be sent. */
	uint8_t head; /**< Index where the next frame will be saved. */
	uint8_t tail; /**< Index of the frame currently being processed*/
} TM1637_Buffer;

void TM1637_Init(GPIO_TypeDef* clk_port, uint16_t clk_pin, GPIO_TypeDef* dio_port, uint16_t dio_pin, TIM_HandleTypeDef* htim);

void TM1637_DisplayOn(void);
void TM1637_DisplayOff(void);
void TM1637_SetBrightness(uint8_t brightness);

bool TM1637_WriteDisplay(uint8_t address, uint8_t* segments, uint8_t segments_length);
bool TM1637_WriteSegments(uint8_t address, uint8_t segments);
bool TM1637_WriteByte(uint8_t data);

bool TM1637_GetAndClearResponse(void);

#endif /* SRC_TM1637_TM1637_H_ */
