/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdio.h"
#include "stm32h7xx.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
typedef void (*pFunction)(void);
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

COM_InitTypeDef BspCOMInit;
__IO uint32_t BspButtonState = BUTTON_RELEASED;

SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_rx;
DMA_HandleTypeDef hdma_spi1_tx;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
#define FLASH_APP1_ADDRESS 0x08040000 // end of 256K Boot, APP1 256K flash in BANK1.
#define FLASH_APP2_ADDRESS 0x08140000 // APP2 256K flash in BANK2.

#define META_DATA_ADDRESS 0x08100000
#define META_DATA_IS_LOADED_ADDRESS 0x08101FF0
#define META_DATA_CRC_START_ADDRESS 0x08100060 //first 96 byte is for file_name:file_size:optional_bytes:padding


uint8_t data_read[1024];

uint8_t file_name_read[30];
uint32_t file_size_value_read = 0;
uint8_t file_size_read[20];
uint8_t optional_bytes_read[100];
uint8_t padding_read[100];
uint8_t is_loaded_check_byte[16];
uint8_t NOT_LOADED_BYTE[16] = {0}; // check

uint8_t check_value[50];
uint8_t TX_Buffer[50];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */



void Update_TX_Buffer_concatinate(uint8_t *array1, uint8_t *array2, size_t len_array1, size_t len_array2){
	for(int i = 0; i < 50; i++){
		if((array1[i] != 0x00) & (i < len_array1)){
			TX_Buffer[i] = array1[i];
		}
		else if(i < len_array1){
			TX_Buffer[i] = 0x1a;
		}
		else if((array2[i - len_array1] != 0x00)){
			TX_Buffer[i] = array2[i - len_array1];
		}
		else {
			TX_Buffer[i] = 0x1a;
		}
	}
}

HAL_StatusTypeDef Single_Write_Is_Loaded_MetaData(uint32_t address, const uint8_t* data){
	if (address % 16 != 0){ // check the adress alignment
		return HAL_ERROR;
	}
	if ((FLASH_CR_LOCK & FLASH->CR2)){ //check if the CR is unlocked
		FLASH->KEYR2 = 0x45670123; // magic numbers for unlocking
		FLASH->KEYR2 = 0xCDEF89AB; // magic numbers for unlocking

	}
	FLASH->CR2 |= FLASH_CR_PG; // enter programming mode

    __IO volatile uint64_t *dst = (__IO uint64_t *) address;

    uint64_t first_qword  = *((uint64_t*)&data[0]);
    uint64_t second_qword;
    memcpy(&second_qword, data + 8, sizeof(second_qword)); // more robust

    //printf("%016" PRIX64 "\n", second_qword);
    //printf("%016" PRIX64 "\n", first_qword);


    dst[0] = first_qword;
    __DSB();
    //printf("Writing to flash: Address = 0x%08lX, Value = 0x%016llX\r\n", (uint32_t)&dst[0], first_qword);
    dst[1] = second_qword;
    __DSB();
    //printf("Writing to flash: Address = 0x%08lX, Value = 0x%016llX \r\n", (uint32_t)&dst[1], second_qword);

    while (FLASH->SR2 & FLASH_SR_QW) {
        // wait
    }

    // Wait for BSY flag to clear (programming is completed)
    while (FLASH->SR2 & FLASH_SR_BSY) {
        // wait
    }

    FLASH->CR2 &= ~FLASH_CR_PG;

    if(FLASH->SR2 & FLASH_SR_WRPERR){
    	printf("ERROR: An illegal erase/program operation is attempted,	WRPERR");
    	return HAL_ERROR;
    }
    else if(FLASH->SR2 & FLASH_SR_PGSERR){
    	printf("ERROR: Programming sequence is incorrect,	PGSERR");
    	return HAL_ERROR;
    }
    else if(FLASH->SR2 & FLASH_SR_STRBERR){
    	printf("ERROR:  Software has written several times to the same byte,	STRBERR");
    	return HAL_ERROR;
    }
    else if(FLASH->SR2 & FLASH_SR_INCERR){
    	printf("ERROR:  A programming inconsistency is detected,	INCERR");
    	return HAL_ERROR;
    }
    FLASH->CR2 |= FLASH_CR_LOCK; //lock flash
    return HAL_OK;
}

void Flash_Clear_Write_Errors_Bank1(void)
{
    FLASH->SR1 = FLASH_SR_WRPERR  |
                 FLASH_SR_PGSERR  |
                 FLASH_SR_STRBERR  |
                 FLASH_SR_INCERR  |
                 FLASH_SR_EOP;
}

void Flash_Clear_Read_Errors_Bank1(void)
{
    FLASH->SR1 = FLASH_SR_SNECCERR |
                 FLASH_SR_RDSERR   |
                 FLASH_SR_RDPERR  |
                 FLASH_SR_EOP;
}

void go2App1(void)
{
	uint32_t JumpAddress;
	pFunction Jump_To_Application;  // function pointer
	/* Check if there is smt installed in the app Flash region*/
	if(((*(uint32_t*) FLASH_APP1_ADDRESS) & 0x2FFE0000) == 0x24100000){ //
		printf("Jumping to application...	\r\n");
		HAL_Delay(100);//checks the first code

		// 1. Disable SysTick
		SysTick->CTRL = 0;
		SysTick->LOAD = 0;
		SysTick->VAL  = 0;

		// 2. Disable all interrupts (optional but recommended)
		__disable_irq();

		// 3. Clear all pending interrupts (especially SysTick)
		for (int i = 0; i < 8; i++) {
		    NVIC->ICER[i] = 0xFFFFFFFF;
		    NVIC->ICPR[i] = 0xFFFFFFFF;
		}

		//jump to the application
		JumpAddress = *(uint32_t *) (FLASH_APP1_ADDRESS + 4); // gets the applcation's reset handler adress
		Jump_To_Application = (pFunction) JumpAddress; // the adress function pointer becomes the adress of the reset
		/* Initialize the stack pointer of the application*/
		__set_MSP(*(uint32_t*)FLASH_APP1_ADDRESS);
		Jump_To_Application();
	}
	else{
		// no application is installed
		printf("No app1 found!	\r\n");

	}
}

void Erase_App1_Flash(void)
{
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef eraseInit;
    uint32_t SectorError;
    HAL_StatusTypeDef status;

    eraseInit.TypeErase    = FLASH_TYPEERASE_SECTORS; //h7a3 has sectors
    eraseInit.VoltageRange = 0;  // Adjusted according to Vdd, safe
    eraseInit.Sector       = FLASH_SECTOR_32; //
    eraseInit.NbSectors    = 96;
    eraseInit.Banks        = FLASH_BANK_1; //h7a3 has 2

    status = HAL_FLASHEx_Erase(&eraseInit, &SectorError);

    HAL_FLASH_Lock();

    if (status != HAL_OK) {
        // error handler might go there
        while (1);
    }

    printf("APP1 is erased!	\n\r");
}


uint16_t CRC16Compute(uint8_t *data, uint32_t length, uint16_t init_value, uint16_t key){
	uint16_t crc = init_value;
	for(uint32_t i = 0; i < length; i++){
		crc ^= (uint16_t) (data[i] << 8);// initial shift
		for(uint8_t j = 0; j < 8; j++){
			if(crc & 0x8000){//most significant is 1
				crc = (uint16_t) (crc << 1) ^ key; //xored with the key
			}
			else{
				crc = (uint16_t) crc << 1; // shifted once
			}
		}
	}
	return crc;
}

HAL_StatusTypeDef Single_Block_Read_App2(uint32_t address, uint8_t* data, uint32_t length){
	uint8_t CRC_H = *(uint8_t*)(((address - FLASH_APP2_ADDRESS) / 0x400) * 2 + META_DATA_CRC_START_ADDRESS); // 0x08100000 + padding(60)
	uint8_t CRC_L = *(uint8_t*)(((address - FLASH_APP2_ADDRESS) / 0x400) * 2 + META_DATA_CRC_START_ADDRESS + 1); // 0x08100000 + padding(60) + 1

	for(int i = 0; i < length; i++){
		data[i] =  *(uint8_t*) (address + i);
	}

	if(((CRC_L | (CRC_H << 8)) == CRC16Compute(data,  length, 0x0000, 0x1021))){	 // old: if( (CRC_L | (CRC_H << 8)) == CRC16Compute(received_block, 1024, 0x0000, 0x1021))
		return HAL_OK;
	}
	else{
		return HAL_ERROR;
	}
}

void First_Block_Read_App2(uint32_t address, uint32_t length){
	uint32_t file_size_read_value_temp = 0;
	uint8_t file_size_read_temp[20];
	memset(file_size_read_temp, 0, 20);
	uint8_t name_index_counter = 0;
	uint8_t optional_bytes_read_counter = 0;
	uint8_t padding_read_counter = 0;
	uint8_t fsm_step = 0;
	for(int i = 0; i < length; i++){
		uint8_t first_block_data = *(uint8_t*) (address + i);
		switch (fsm_step){
			case 0:
				if(first_block_data != 0x00){
					file_name_read[i] = first_block_data;
				}
				else{
					fsm_step++;

				}
				break;

			case 1:
				if(first_block_data != 0x20){
					file_size_read_value_temp = file_size_read_value_temp*10 + first_block_data - 48;
					file_size_read_temp[name_index_counter++] = first_block_data;

				}
				else{
					file_size_value_read = file_size_read_value_temp;
					memcpy(file_size_read, file_size_read_temp,20);
					fsm_step++;
				}
				break;

			case 2:
				if(first_block_data != 0x20){
					optional_bytes_read[optional_bytes_read_counter] = first_block_data;
					optional_bytes_read_counter++;
				}
				else{
					fsm_step++;
				}
				break;

			case 3:
				if(first_block_data != 0x00){
					padding_read[padding_read_counter] = first_block_data;
					padding_read_counter++;
				}
				else{
					fsm_step++;
				}
				break;

			default:
				break;
		}
	}

}

HAL_StatusTypeDef Last_Block_Read_App2(uint32_t address, uint8_t* data, uint32_t length, uint16_t last_data_index_read){
	uint8_t CRC_H = *(uint8_t*)(((address - FLASH_APP2_ADDRESS) / 0x400) * 2 + META_DATA_CRC_START_ADDRESS);
	uint8_t CRC_L = *(uint8_t*)(((address - FLASH_APP2_ADDRESS) / 0x400) * 2 + META_DATA_CRC_START_ADDRESS + 1);

	for(int i = 0; i < length; i++){
		if(i < last_data_index_read - 1){
			data[i] =  *(uint8_t*) (address + i);
		}
		else if (i == last_data_index_read - 1){ // check if the last byte is not 0x1a
			if(((*(uint8_t*) (address + i)) == 26)){
				printf("File size problem during read (file size information is larger than actual size)	\r\n");
				return HAL_ERROR;
			}
			data[i] =  *(uint8_t*) (address + i);
		}
		else{
			if((*(uint8_t*) (address + i) != 0x1a)){
				printf("File size problem during read (file size information is smaller than actual size) \r\n");
				return HAL_ERROR;
			}
			data[i] =  *(uint8_t*) (address + i);
		}
	}

	if(((CRC_L | (CRC_H << 8)) == CRC16Compute(data,  length, 0x0000, 0x1021))){	 // old: if( (CRC_L | (CRC_H << 8)) == CRC16Compute(received_block, 1024, 0x0000, 0x1021))
		return HAL_OK;
	}
	else{
		printf("CRC problem during last read.	\r\n");
		return HAL_ERROR;
	}
}

HAL_StatusTypeDef Multiple_Read_App2_Flash(uint32_t address, uint8_t* data, uint32_t length){
	First_Block_Read_App2(META_DATA_ADDRESS, 128);
	uint16_t num_of_blocks = file_size_value_read / 1024 + 1;
	uint16_t last_data_index_read = file_size_value_read % 1024;
	for(int i = 0; i < num_of_blocks; i++){
		if(i == num_of_blocks - 1){

			if(Last_Block_Read_App2(address + i*1024, data, length, last_data_index_read) == HAL_ERROR){
				printf("File size ERROR !\n\r");
				return HAL_ERROR;
			}
		}
		else if((Single_Block_Read_App2(address + i*1024, data, length) == HAL_ERROR)){
			printf("CRC check ERROR !\n\r");
			//Erase_App2_Flash(); erase after the check in
			return HAL_ERROR;
		}
	}

	printf("CRC check OK !\n\r");
	return HAL_OK;
}

HAL_StatusTypeDef Single_Write_App1_Flash(uint32_t address, const uint8_t* data){
	if (address % 16 != 0){ // check the adress alignment
		return HAL_ERROR;
	}
	if ((FLASH_CR_LOCK & FLASH->CR1)){ //check if the CR is unlocked
		FLASH->KEYR1 = 0x45670123; // magic numbers for unlocking
		FLASH->KEYR1 = 0xCDEF89AB; // magic numbers for unlocking

	}
	FLASH->CR1 |= FLASH_CR_PG; // enter programming mode

    __IO volatile uint64_t *dst = (__IO uint64_t *) address;

    uint64_t first_qword  = *((uint64_t*)&data[0]);
    uint64_t second_qword;
    memcpy(&second_qword, data + 8, sizeof(second_qword)); // more robust

    //printf("%016" PRIX64 "\n", second_qword);
    //printf("%016" PRIX64 "\n", first_qword);


    dst[0] = first_qword;
    __DSB();
    //printf("Writing to flash: Address = 0x%08lX, Value = 0x%016llX\r\n", (uint32_t)&dst[0], first_qword);
    dst[1] = second_qword;
    __DSB();
    //printf("Writing to flash: Address = 0x%08lX, Value = 0x%016llX \r\n", (uint32_t)&dst[1], second_qword);

    while (FLASH->SR1 & FLASH_SR_QW) {
        // wait
    }

    // Wait for BSY flag to clear (programming is completed)
    while (FLASH->SR1 & FLASH_SR_BSY) {
        // wait
    }

    FLASH->CR1 &= ~FLASH_CR_PG;

    if(FLASH->SR1 & FLASH_SR_WRPERR){
    	printf("ERROR: An illegal erase/program operation is attempted,	WRPERR");
    	return HAL_ERROR;
    }
    else if(FLASH->SR1 & FLASH_SR_PGSERR){
    	printf("ERROR: Programming sequence is incorrect,	PGSERR");
    	return HAL_ERROR;
    }
    else if(FLASH->SR1 & FLASH_SR_STRBERR){
    	printf("ERROR:  Software has written several times to the same byte,	STRBERR");
    	return HAL_ERROR;
    }
    else if(FLASH->SR1 & FLASH_SR_INCERR){
    	printf("ERROR:  A programming inconsistency is detected,	INCERR");
    	return HAL_ERROR;
    }
    FLASH->CR1 |= FLASH_CR_LOCK; //lock flash
    return HAL_OK;
}

HAL_StatusTypeDef Multiple_Write_App1_Flash(uint32_t address, const uint8_t* data16, uint16_t block_size){

	Flash_Clear_Write_Errors_Bank1(); // clear all the error flags
	for(int i = 0; i < block_size; i += 16){
		HAL_StatusTypeDef res = Single_Write_App1_Flash((address + i), &data16[i]);
	    if (res != HAL_OK) {
	    	FLASH->CR1 |= FLASH_CR_LOCK;
	        return HAL_ERROR;
	    }
	}

	return HAL_OK;
	//printf("Block writing has finished	\r\n");
}

HAL_StatusTypeDef Single_Block_Copy_App2(uint32_t write_address, uint32_t read_address, uint8_t* read_data, uint32_t read_length){
	uint8_t CRC_H = *(uint8_t*)(((read_address - FLASH_APP2_ADDRESS) / 0x400) * 2 + META_DATA_CRC_START_ADDRESS); // 0x08100000 + padding(60)
	uint8_t CRC_L = *(uint8_t*)(((read_address - FLASH_APP2_ADDRESS) / 0x400) * 2 + META_DATA_CRC_START_ADDRESS + 1); // 0x08100000 + padding(60) + 1

	for(int i = 0; i < read_length; i++){
		read_data[i] =  *(uint8_t*) (read_address + i);
	}

	if(((CRC_L | (CRC_H << 8)) == CRC16Compute(read_data,  read_length, 0x0000, 0x1021))){	 // old: if( (CRC_L | (CRC_H << 8)) == CRC16Compute(received_block, 1024, 0x0000, 0x1021))
		if(Multiple_Write_App1_Flash(write_address, read_data, read_length) == HAL_ERROR){
			return HAL_ERROR;
		}
	}
	else{
		return HAL_ERROR;
	}
	return HAL_OK;
}

HAL_StatusTypeDef Last_Block_Copy_App2(uint32_t write_address, uint32_t read_address, uint8_t* read_data, uint32_t read_length, uint16_t last_data_index_read){
	uint8_t CRC_H = *(uint8_t*)(((read_address - FLASH_APP2_ADDRESS) / 0x400) * 2 + META_DATA_CRC_START_ADDRESS);
	uint8_t CRC_L = *(uint8_t*)(((read_address - FLASH_APP2_ADDRESS) / 0x400) * 2 + META_DATA_CRC_START_ADDRESS + 1);

	for(int i = 0; i < read_length; i++){
		if(i < last_data_index_read - 1){
			read_data[i] =  *(uint8_t*) (read_address + i);
		}
		else if (i == last_data_index_read - 1){ // check if the last byte is not 0x1a
			if(((*(uint8_t*) (read_address + i)) == 26)){
				printf("File size problem during read (file size information is larger than actual size)	\r\n");
				return HAL_ERROR;
			}
			read_data[i] =  *(uint8_t*) (read_address + i);
		}
		else{
			if((*(uint8_t*) (read_address + i) != 0x1a)){
				printf("File size problem during read (file size information is smaller than actual size) \r\n");
				return HAL_ERROR;
			}
			read_data[i] =  *(uint8_t*) (read_address + i);
		}
	}

	if(((CRC_L | (CRC_H << 8)) == CRC16Compute(read_data,  read_length, 0x0000, 0x1021))){	 // old: if( (CRC_L | (CRC_H << 8)) == CRC16Compute(received_block, 1024, 0x0000, 0x1021))
		if(Multiple_Write_App1_Flash(write_address, read_data, read_length) == HAL_ERROR){
			return HAL_ERROR;
		}
	}
	else{
		printf("CRC problem during last read.	\r\n");
		return HAL_ERROR;
	}
	return HAL_OK;
}

HAL_StatusTypeDef Multiple_Copy_App2(uint32_t write_address, uint32_t read_address, uint8_t* read_data, uint32_t read_length){
	uint16_t num_of_blocks = file_size_value_read / 1024 + 1;
	uint16_t last_data_index_read = file_size_value_read % 1024;
	for(int i = 0; i < num_of_blocks; i++){
		if(i == num_of_blocks - 1){
			if(Last_Block_Copy_App2(write_address + i*1024, read_address + i*1024, read_data, read_length, last_data_index_read) == HAL_ERROR){
				printf("File size ERROR !\n\r");
				return HAL_ERROR;
			}
		}
		else if((Single_Block_Copy_App2(write_address + i*1024, read_address + i*1024, read_data, read_length) == HAL_ERROR)){
			printf("CRC check ERROR !\n\r");
			//Erase_App2_Flash(); erase after the check in
			return HAL_ERROR;
		}
	}

	printf("CRC check OK !\n\r");
	return HAL_OK;
}

HAL_StatusTypeDef MetaData_Is_Loaded_Read(uint32_t address, uint8_t* data, uint32_t length){
	for(uint8_t i = 0; i < length; i++){
		if(i !=  *(uint8_t*) (address + i)){
			return HAL_ERROR;
		}
	}
	return HAL_OK;
}

HAL_StatusTypeDef APP2_Check_Read_Copy(uint32_t metadata_address, uint8_t* metadata_data, uint32_t metadata_data_length, uint32_t app_address, uint8_t* app_data,uint32_t app_data_length, uint32_t write_address){
	if(MetaData_Is_Loaded_Read(metadata_address , metadata_data, metadata_data_length) == HAL_OK){
		if(((*(uint32_t*) FLASH_APP2_ADDRESS) & 0x2FFE0000) == 0x24100000){
			if((Multiple_Read_App2_Flash(app_address , app_data, app_data_length) == HAL_OK)){
				printf("%s(%s byte) was read successfully	\r\n",(char*)file_name_read, (char*)file_size_read);
				printf("Press the blue button to load old programme in 5 seconds.	\r\n");
			    for(int i = 0; i < 20; i++){
			    	if (BspButtonState == BUTTON_PRESSED)
			    	{
			    		/* Update button state */
			    		Single_Write_Is_Loaded_MetaData(META_DATA_IS_LOADED_ADDRESS, NOT_LOADED_BYTE);
			    		return HAL_OK;

			    		BspButtonState = BUTTON_RELEASED;
			    		/* -- Sample board code to toggle leds ---- */
			    		/* ..... Perform your action ..... */
			    	}
			    	HAL_Delay(250);
			    	BSP_LED_Toggle(LED_RED);
			    }
				Erase_App1_Flash();
				//Single_Block_Copy_App2(write_address, app_address, app_data, app_data_length);
				Multiple_Copy_App2(write_address, app_address, app_data, app_data_length);
				Update_TX_Buffer_concatinate(file_name_read, file_size_read, sizeof(file_name_read), sizeof(file_size_read));
				HAL_SPI_Transmit_DMA(&hspi1, TX_Buffer, 50);
				return HAL_OK;

			}
			else{
				printf("CRC Read Error!	\r\n");
				return HAL_ERROR;
			}
		}
		else{
			printf("APP start code error	\r\n");
			return HAL_ERROR;
		}
	}
	else{
		printf("No new APP found!	\r\n");
		return HAL_ERROR;
	}
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Initialize leds */
  BSP_LED_Init(LED_GREEN);
  BSP_LED_Init(LED_YELLOW);
  BSP_LED_Init(LED_RED);

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Initialize COM1 port (115200, 8 bits (7-bit data + 1 stop bit), no parity */
  BspCOMInit.BaudRate   = 115200;
  BspCOMInit.WordLength = COM_WORDLENGTH_8B;
  BspCOMInit.StopBits   = COM_STOPBITS_1;
  BspCOMInit.Parity     = COM_PARITY_NONE;
  BspCOMInit.HwFlowCtl  = COM_HWCONTROL_NONE;
  if (BSP_COM_Init(COM1, &BspCOMInit) != BSP_ERROR_NONE)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN BSP */

  /* -- Sample board code to send message over COM1 port ---- */
  printf("IAP DENEMELERI GUN 17\n\r");;

  /* -- Sample board code to switch on leds ---- */
  BSP_LED_On(LED_GREEN);
  BSP_LED_On(LED_YELLOW);
  BSP_LED_On(LED_RED);

  /* USER CODE END BSP */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  printf("BOOTLOADER START!	\r\n");

  while (1)
  {

	APP2_Check_Read_Copy(META_DATA_IS_LOADED_ADDRESS, is_loaded_check_byte, sizeof(is_loaded_check_byte),FLASH_APP2_ADDRESS , data_read, sizeof(data_read), FLASH_APP1_ADDRESS);
    /* -- Sample board code for User push-button in interrupt mode ---- */
    if (BspButtonState == BUTTON_PRESSED)
    {
      /* Update button state */
      BspButtonState = BUTTON_RELEASED;
      /* -- Sample board code to toggle leds ---- */
      BSP_LED_Toggle(LED_GREEN);
      BSP_LED_Toggle(LED_YELLOW);
      BSP_LED_Toggle(LED_RED);

      /* ..... Perform your action ..... */
    }
    go2App1();

	HAL_Delay(15000);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /*AXI clock gating */
  RCC->CKGAENR = 0xE003FFFF;

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = 64;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 8;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 16;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV1;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_HARD_OUTPUT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 0x0;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  hspi1.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi1.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi1.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi1.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi1.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi1.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi1.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi1.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
  hspi1.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.Init.ClockPrescaler = UART_PRESCALER_DIV1;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetTxFifoThreshold(&huart2, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_SetRxFifoThreshold(&huart2, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_UARTEx_DisableFifoMode(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
  /* DMA1_Stream1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief BSP Push Button callback
  * @param Button Specifies the pressed button
  * @retval None
  */
void BSP_PB_Callback(Button_TypeDef Button)
{
  if (Button == BUTTON_USER)
  {
    BspButtonState = BUTTON_PRESSED;
  }
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
