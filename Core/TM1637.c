/**
 * @file TM1637.c
 */

#include "TM1637.h"

//extern TIM_HandleTypeDef htim6;

// 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, DP
const uint8_t TM1637_Digits[] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x80};

typedef enum
{
	TM1637_IDLE,
	TM1637_START,
	TM1637_WRITE_BIT,
	TM1637_READ_READY,
	TM1637_STOP,
	TM1637_DONE
} TM1637_State;


static volatile TM1637_Buffer tx_buffer;
static volatile uint8_t slave_respond;
static volatile TM1637_State current_state = TM1637_IDLE;
static uint8_t current_brightness = BRIGHTNESS_5;

static void processState();

static bool writeBuffer(volatile TM1637_Buffer* buffer, TM1637_Tx* data);
static bool readBuffer(volatile TM1637_Buffer* buffer,volatile TM1637_Tx* data);
static bool isBufferEmpty(volatile TM1637_Buffer* buffer);

static bool sendData(volatile TM1637_Buffer* buffer, TM1637_Tx* data);
static void writeBit(uint8_t bit);

TM1637_Config config = {0};

/**
 * @brief Initialize the TM1637 display and clear segments.
 * @param clk_port: GPIO port for the Clock line.
 * @param clk_pin: GPIO pin for the Clock line.
 * @param dio_port: GPIO port for the Data line.
 * @param dio_pin: GPIO pin for the Data line.
 * @param htim: Pointer to the timer handle used for bit-banging.
 */
void TM1637_Init(GPIO_TypeDef* clk_port, uint16_t clk_pin,
					GPIO_TypeDef* dio_port, uint16_t dio_pin,
					TIM_HandleTypeDef* htim) 
{
	// 1. Save hardware configuration to the global structure
	config.clk_port = clk_port;
	config.clk_pin = clk_pin;
	config.dio_port = dio_port;
	config.dio_pin = dio_pin;
	config.htim = htim;
	
	// 2. Clear all 4 digits (data command is already sent inside TM1637_WriteDisplay)
	uint8_t digits[4] = {0};
	TM1637_WriteDisplay(ADDRESS_00H, digits, 4);

	// 3. Enable display with initial brightness
	TM1637_WriteByte((BRIGHTNESS_ON | current_brightness));
}

void TM1637_DisplayOn(void)
{
	TM1637_WriteByte((WRITE_DATA | ADDRESS_AI | NORMAL_MODE));
	TM1637_WriteByte((BRIGHTNESS_ON | current_brightness));
}

void TM1637_DisplayOff(void)
{
	TM1637_WriteByte((WRITE_DATA | ADDRESS_AI | NORMAL_MODE));
	TM1637_WriteByte((BRIGHTNESS_OFF | current_brightness));
}

void TM1637_SetBrightness(uint8_t brightness) {
	current_brightness = brightness;
	TM1637_WriteByte((WRITE_DATA | ADDRESS_AI | NORMAL_MODE));
	TM1637_WriteByte((BRIGHTNESS_ON | brightness));
}

/**
 * @brief Returns and resets the slave response.
 * @return True if communication was successful.
 */
bool TM1637_GetAndClearResponse(void) {
	bool response = !slave_respond;
	slave_respond = 0;
	return response;
}

/**
 * @brief High-level public function to display whole sequences of segments.
 * @param address: The register address (position) where segments will start being displayed.
 * @param segments: Pointer to an array of segment bitmasks.
 * @param segments_length Number of segments to be displayed.
 * @return True if bytes were accepted for transmission, false if the buffer is full.
 */
bool TM1637_WriteDisplay(uint8_t address, uint8_t* segments, uint8_t segments_length)
{
	if (segments_length >= TX_SIZE) return false;

	TM1637_WriteByte((WRITE_DATA | ADDRESS_AI | NORMAL_MODE));

	TM1637_Tx data = {0};
	data.tx_frame[0] = address;

	for (int i = 0; i < segments_length; ++i) {
		data.tx_frame[i + 1] = segments[i];
	}

	data.tx_len = (segments_length + 1);

	return sendData(&tx_buffer, &data);
}

/**
 * @brief High-level public function to display segments at a specific address.
 * @param address: The register address (position) where segments will be displayed.
 * @param segments: Bitmask of segments to be displayed.
 * @return True if the byte was accepted for transmission, false if the buffer is full.
 */
bool TM1637_WriteSegments(uint8_t address, uint8_t segments)
{
	TM1637_WriteByte((WRITE_DATA | FIXED_ADDRESS | NORMAL_MODE));

	TM1637_Tx data = {0};
	data.tx_frame[0] = address;
	data.tx_frame[1] = segments;
	data.tx_len = 2;

	return sendData(&tx_buffer, &data);
}

/**
 * @brief High-level public function to send a single byte.
 * @param data: Byte to be transmitted.
 * @return True if the byte was accepted for transmission, false if the buffer is full.
 */
bool TM1637_WriteByte(uint8_t data)
{
	TM1637_Tx tx_data;
	tx_data.tx_frame[0] = data;
	tx_data.tx_len = 1;

	return sendData(&tx_buffer, &tx_data);;
}

/**
 * @brief Timer period elapsed callback (ISR context).
 * @note Overrides the weak HAL_TIM_PeriodElapsedCallback to drive the TM1637 state machine.
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	if (htim == config.htim) {
		processState();
	}
}

/**
 * @brief High-level helper function to send data.
 * @param buffer: Pointer to the destination circular buffer.
 * @param data: Pointer to the source structure containing data which will be saved.
 * @return true if data is successfully buffered, and false otherwise.
 */
static bool sendData(volatile TM1637_Buffer* buffer, TM1637_Tx* data)
{
	bool respond = writeBuffer(buffer, data);

	if (current_state == TM1637_IDLE) {
		current_state = TM1637_START;
		HAL_TIM_Base_Start_IT(config.htim);
	}

	return respond;
}

/**
 * @brief Write data to the circular buffer.
 * @param buffer: Pointer to the destination circular buffer.
 * @param data: Pointer to the source structure containing data which will be saved.
 * @return True if data was successfully saved, false if the buffer is full.
 */
static bool writeBuffer(volatile TM1637_Buffer* buffer, TM1637_Tx* data)
{
	uint8_t temp_head = (buffer->head + 1) % BUFFER_SIZE;

	if (temp_head == buffer->tail) {
		return false;
	}

	buffer->buffer[buffer->head] = *data;
	buffer->head = temp_head;

	return true;
}

/**
 * @brief Read the next available frame from circular buffer.
 * @param buffer: Pointer to the source circular buffer .
 * @param data: Pointer to the destination structure where data will copied.
 * @return true if data is successfully read, and false if buffer is empty.
 */
static bool readBuffer(volatile TM1637_Buffer* buffer,volatile TM1637_Tx* data)
{
	if (buffer->tail == buffer->head) {
		return false;
	}

	*data = buffer->buffer[buffer->tail];

	buffer->tail++;
	buffer->tail %= BUFFER_SIZE;

	return true;
}

static bool isBufferEmpty(volatile TM1637_Buffer* buffer)
{
	return buffer->tail == buffer->head;
}

/**
 * @brief Write a single bit to TM1637.
 * @note Leaves CLK in SET state.
 */
static void writeBit(uint8_t bit)
{
	HAL_GPIO_WritePin(config.clk_port, config.clk_pin, GPIO_PIN_RESET);

	if (bit) HAL_GPIO_WritePin(config.dio_port, config.dio_pin, GPIO_PIN_SET);
	else HAL_GPIO_WritePin(config.dio_port, config.dio_pin, GPIO_PIN_RESET);

	HAL_GPIO_WritePin(config.clk_port, config.clk_pin, GPIO_PIN_SET);
}

/**
 * @brief Handler for TM1637 communication state machine.
 * @note This function should be called periodically (e.g. Timer Interrupt).
 * @note Recommended calling interval should be set 5-10us for stable bit-banging.
 */
static void processState()
{
	static uint8_t sub_state = 0;

	// Storage for the currently processed data frame
	volatile static TM1637_Tx tx_data = {0};
	volatile static uint8_t* tx_ptr = tx_data.tx_frame;

	// Manages the bit-banging sequence: START, WRITE, ACK and STOP
	switch (current_state) {
		// Check if new data is available in the buffer
		case TM1637_IDLE:
			if (readBuffer(&tx_buffer, &tx_data)) {
				tx_ptr = tx_data.tx_frame;
				sub_state = 0;
				current_state = TM1637_START;
			}
			break;
		// Execute TM1637 start condition sequence
		case TM1637_START:
			if (sub_state == 0) {
				HAL_GPIO_WritePin(config.dio_port, config.dio_pin, GPIO_PIN_SET);
				HAL_GPIO_WritePin(config.clk_port, config.clk_pin, GPIO_PIN_SET);
				sub_state++;
			} else if (sub_state == 1) {
				HAL_GPIO_WritePin(config.dio_port, config.dio_pin, GPIO_PIN_RESET);
				sub_state++;
			} else {
				HAL_GPIO_WritePin(config.clk_port, config.clk_pin, GPIO_PIN_RESET);
				sub_state = 0;
				current_state = TM1637_WRITE_BIT;
			}

			break;
		// Transmit data byte bit-by-bit
		case TM1637_WRITE_BIT:
			 if (sub_state < 8) {
				writeBit((*tx_ptr >> sub_state++) & 1);
			} else if (sub_state == 8) {
				HAL_GPIO_WritePin(config.clk_port, config.clk_pin, GPIO_PIN_RESET);
				HAL_GPIO_WritePin(config.dio_port, config.dio_pin, GPIO_PIN_SET);

				sub_state++;
			} else {
				HAL_GPIO_WritePin(config.clk_port, config.clk_pin, GPIO_PIN_SET);
				sub_state = 0;
				current_state = TM1637_READ_READY;
			}

			break;
		// Read the answer (ACK) of TM1637
		case TM1637_READ_READY:
			if (sub_state == 0) {
				slave_respond += HAL_GPIO_ReadPin(config.dio_port, config.dio_pin);
				sub_state++;
			} else {
				HAL_GPIO_WritePin(config.clk_port, config.clk_pin, GPIO_PIN_RESET);
				sub_state = 0;

				// Check if more bytes remain in the current frame
				if (tx_data.tx_len > 1) {
					tx_data.tx_len--;
					tx_ptr++;

					current_state = TM1637_WRITE_BIT;
				} else {
					current_state = TM1637_STOP;
				}
			}
			break;
		// Execute TM1637 stop condition sequence
		case TM1637_STOP:
			if (sub_state == 0) {
				HAL_GPIO_WritePin(config.clk_port, config.clk_pin, GPIO_PIN_RESET);
				sub_state++;
			}
			else if (sub_state == 1) {
				HAL_GPIO_WritePin(config.dio_port, config.dio_pin, GPIO_PIN_RESET);
				sub_state++;
			}
			else if (sub_state == 2) {
				HAL_GPIO_WritePin(config.clk_port, config.clk_pin, GPIO_PIN_SET);
				sub_state++;
			}
			else {
				HAL_GPIO_WritePin(config.dio_port, config.dio_pin, GPIO_PIN_SET);
				sub_state = 0;
				current_state = TM1637_DONE;
			}

			break;
		// End transmission and disable timer if buffer is empty
		case TM1637_DONE:
			sub_state = 0;
			current_state = TM1637_IDLE;

			if (isBufferEmpty(&tx_buffer)) {
				HAL_TIM_Base_Stop_IT(config.htim);
			}

			break;
	}
}
