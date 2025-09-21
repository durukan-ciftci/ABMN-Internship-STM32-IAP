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

#define FLASH_APP2_ADDRESS 0x08140000 // APP2 256K flash in BANK1. Also start of new app's the header

#define META_DATA_ADDRESS 0x08100000
#define META_DATA_CRC_START_ADRESS 0x08100060 //first 96 byte is for file_name:file_size:optional_bytes:padding
#define META_DATA_IS_LOADED_ADDRESS 0x08101FF0 // 16:10 1000010

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

#define ACK 0x06
#define EOT 0x04
#define C 0x43
#define CAN 0x18
#define NAK 0x15
#define NUM_OF_NAK_BEFORE_ABORT 5

uint8_t RxData[1];

uint16_t Ymodem_data_block_counter = 0;
uint16_t Ymodem_info_block_counter = 0;
uint16_t Ymodem_name_counter = 0;
uint16_t Ymodem_size_counter = 0;
uint16_t Ymodem_optional_counter = 0;
uint16_t Ymodem_padding_counter = 0;
uint8_t Ymodem_NAK_counter = 0;

uint8_t file_name[100];
uint32_t file_size_val = 0;
uint16_t last_block_size_check_index_val = 0; // last data block's 3 bytes to be checked for end of the data :file size % 1024
uint8_t file_size[100];
uint8_t file_size_check[3];// size check for file_size-1: file_size: file_size+1
uint8_t optional_bytes[100];
uint8_t padding[100];

uint8_t received_block0[128]; // should be all zeros since the info is extracted
uint8_t received_block[1024];
uint8_t received_end_block[128];
uint8_t header_block[640];
uint8_t received_block_deneme[50][1024];
uint8_t received_block_deneme_index = 0;

uint8_t Ymodem_is_data_block_started = 0;
uint8_t ymodem_is_name_started = 0;
uint8_t ymodem_is_size_started = 0;
uint8_t ymodem_is_optional_bytes_started = 0;
uint8_t ymodem_is_padding_started = 0;
uint8_t ymodem_is_info_ended = 0;
volatile uint8_t ymodem_is_start_request = 0;
uint8_t ymodem_is_end_of_transmission = 0;
uint8_t ymodem_is_last_block = 0;
uint8_t Ymodem_is_abort = 0; // size error sets to 1 and crc_check function checks

//uint8_t is_jump_to_app2 = 0;

uint8_t CRC_H = 0;
uint8_t CRC_L = 0;
uint8_t SOH_STX = 0;
uint8_t Block_num = 0;
uint8_t Block_num_inverted = 0;
uint16_t Block_num_counter = 0;

uint8_t check_index = 0;
uint8_t check_values[512];
uint16_t check_value1;
uint16_t check_value2;

uint8_t IS_LOADED_BYTE[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};

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

void go2App2(void)
{
	uint32_t JumpAdress;
	pFunction Jump_To_Application;  // function pointer
	printf("Jumping to App2	\r\n");
	/* Check if there is smt installed in the app Flash region*/
	if(((*(uint32_t*) FLASH_APP2_ADDRESS) & 0x2FFE0000) == 0x24100000){ // originally ==
		HAL_Delay(100);//checks the first code
		printf("APP2 START...	\r\n");


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
		JumpAdress = *(uint32_t *) (FLASH_APP2_ADDRESS + 4); // gets the applcation's reset handler adress
		Jump_To_Application = (pFunction) JumpAdress; // the adress function pointer becomes the adress of the reset
		/* Initialize the stack pointer of the application*/
		__set_MSP(*(uint32_t*)FLASH_APP2_ADDRESS);
		Jump_To_Application();
	}
	else{
		// no application is installed
		printf("No app2 found!	\r\n");

	}
}

void Erase_App2_Flash(void)
{
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef eraseInit;
    uint32_t SectorError;
    HAL_StatusTypeDef status;

    eraseInit.TypeErase    = FLASH_TYPEERASE_SECTORS; //h7a3 has sectors
    eraseInit.VoltageRange = 0;  // Adjusted according to Vdd, safe
    eraseInit.Sector       = FLASH_SECTOR_32; //
    eraseInit.NbSectors    = 32;
    eraseInit.Banks        = FLASH_BANK_2; //h7a3 has 2

    status = HAL_FLASHEx_Erase(&eraseInit, &SectorError);

    HAL_FLASH_Lock();

    if (status != HAL_OK) {
        // error handler might go there
        while (1);
    }

    printf("APP2 is erased!	\n\r");

    HAL_FLASH_Unlock();

    eraseInit.TypeErase    = FLASH_TYPEERASE_SECTORS; //h7a3 has sectors
    eraseInit.VoltageRange = 0;  // Adjusted according to Vdd, safe
    eraseInit.Sector       = FLASH_SECTOR_0; //erase metadata
    eraseInit.NbSectors    = 1;
    eraseInit.Banks        = FLASH_BANK_2; //h7a3 has 2

    status = HAL_FLASHEx_Erase(&eraseInit, &SectorError);

    HAL_FLASH_Lock();

    if (status != HAL_OK) {
        // error handler might go there
        while (1);
    }
    printf("Metadata is erased!	\n\r");
}

void Flash_Clear_Write_Errors_Bank2(void)
{
    FLASH->SR2 = FLASH_SR_WRPERR  |
                 FLASH_SR_PGSERR  |
                 FLASH_SR_STRBERR  |
                 FLASH_SR_INCERR  |
                 FLASH_SR_EOP;
}

void Flash_Clear_Read_Errors_Bank2(void)
{
    FLASH->SR2 = FLASH_SR_SNECCERR |
                 FLASH_SR_RDSERR   |
                 FLASH_SR_RDPERR  |
                 FLASH_SR_EOP;
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

HAL_StatusTypeDef Single_Write_App2_Flash(uint32_t address, const uint8_t* data){
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

HAL_StatusTypeDef Multiple_Write_App2_Flash(uint32_t address, const uint8_t* data16, uint16_t block_size){

	Flash_Clear_Write_Errors_Bank2(); // clear all the error flags
	for(uint32_t i = 0; i < block_size; i += 16){
		HAL_StatusTypeDef res = Single_Write_App2_Flash((address + i), &data16[i]);
	    if (res != HAL_OK) {
	    	FLASH->CR2 |= FLASH_CR_LOCK;
	        return HAL_ERROR;
	    }
	}

	return HAL_OK;
	//printf("Block writing has finished	\r\n");
}

void create_and_write_header(uint32_t address, uint8_t* data, uint8_t CRC_H, uint8_t CRC_L){
	if(ymodem_is_end_of_transmission){
		return;
	}
	else if(Block_num_counter != 0){ 					//copy CRC_H and CRC_L value of each data byte 0x08100060 to 0x08100260: CRC, 0x08100260: to :0x08100280 padding
		header_block[94 + Block_num_counter*2] = CRC_H;
		header_block[95 + Block_num_counter*2] = CRC_L;
		if(ymodem_is_last_block){
			Multiple_Write_App2_Flash(address, header_block, 640);
		}
	}
	else{ 									// when first block received copy file_name, file_size, optional_bytes and padding
		memcpy(header_block, received_block0, 96);
	}
}

void Ymodem_send(uint8_t signal){
	uint8_t TxData[1] = {signal};
	HAL_UART_Transmit(&huart2, TxData, sizeof(TxData), 20);
	HAL_UARTEx_ReceiveToIdle_IT(&huart2, RxData, 1);
}

void Ymodem_reset(){
	Ymodem_is_data_block_started = 0;
	Block_num_counter = 0;
	ymodem_is_end_of_transmission = 0;
	ymodem_is_info_ended = 0;
	ymodem_is_last_block = 0;
	BSP_LED_Off(LED_GREEN);
	BSP_LED_Off(LED_YELLOW);
}

void Ymodem_ABORT(){
	Ymodem_send(CAN);
	Ymodem_send(CAN); //ABORT
	Ymodem_NAK_counter = 0;
	Ymodem_is_data_block_started = 0;
	Block_num_counter = 0;
	ymodem_is_end_of_transmission = 0;
	ymodem_is_info_ended = 0;
	ymodem_is_last_block = 0;
	ymodem_is_name_started = 0;
	ymodem_is_size_started = 0;
	ymodem_is_optional_bytes_started = 0;
	ymodem_is_padding_started = 0;
	ymodem_is_start_request = 0;
	BSP_LED_Off(LED_GREEN);
	BSP_LED_Off(LED_YELLOW);
	printf("ABORTING!	\r\n");
}

void Ymodem_file_size_check(){ //file_size_check == {1,1,1} means data is valid
	if(abs(last_block_size_check_index_val - Ymodem_data_block_counter + 3) <= 1){
		if(file_size_check[1]){ //
			if(RxData[0] != 0x1a){ // 1a is for padding
				file_size_check[2] = 1;
			}
		}
		else if(file_size_check[0]){
			if(RxData[0] != 0x1a){ // 1a is for padding
				file_size_check[1] = 1;
			}
		}
		else{
			if(RxData[0] != 0x1a){ // 1a is for padding
				file_size_check[0] = 1;
			}
		}
	}
}

void Ymodem_file_size_last_check(){
	if(file_size_check[0] & file_size_check[1] & !file_size_check[2] ){	 // old: if( (CRC_L | (CRC_H << 8)) == CRC16Compute(received_block, 1024, 0x0000, 0x1021))
		Ymodem_is_abort = 0;
	}
	else{
		Ymodem_is_abort = 1;
	}
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

void Ymodem_check_CRC(uint8_t *data, uint32_t length){
	if( ((CRC_L | (CRC_H << 8)) ==   CRC16Compute(data,  length, 0x0000, 0x1021)) & (!Ymodem_is_abort)){	 // old: if( (CRC_L | (CRC_H << 8)) == CRC16Compute(received_block, 1024, 0x0000, 0x1021))
		create_and_write_header(META_DATA_ADDRESS, header_block, CRC_H, CRC_L);
		Ymodem_send(ACK);
		Ymodem_send(C);

	}
	else if((Ymodem_NAK_counter < NUM_OF_NAK_BEFORE_ABORT) & (!Ymodem_is_abort)){
		Ymodem_send(NAK);
		Ymodem_NAK_counter++;
		printf("CRC FAILED!");
	}
	else{
		Ymodem_ABORT();
	}
}

void Ymodem_check_CRC_and_write(uint8_t *data, uint32_t length){
	if( ((CRC_L | (CRC_H << 8)) ==   CRC16Compute(data,  length, 0x0000, 0x1021)) & (!Ymodem_is_abort)){	 // old: if( (CRC_L | (CRC_H << 8)) == CRC16Compute(received_block, 1024, 0x0000, 0x1021))
		Multiple_Write_App2_Flash(FLASH_APP2_ADDRESS + 1024 * (Block_num_counter - 1), data, length);
		create_and_write_header(META_DATA_ADDRESS, header_block, CRC_H, CRC_L);
		Block_num_counter++;
		Ymodem_send(ACK);
		Ymodem_send(C);

	}
	else if((Ymodem_NAK_counter < NUM_OF_NAK_BEFORE_ABORT) & (!Ymodem_is_abort)){
		Ymodem_send(NAK);
		Ymodem_NAK_counter++;
		printf("CRC FAILED!");
	}
	else{
		Ymodem_ABORT();
	}
}



void receive_first_block(){
	received_block0[Ymodem_data_block_counter] = RxData[0];
	if((Ymodem_is_data_block_started == 0) & (ymodem_is_info_ended == 0)){ // check
		switch (Ymodem_info_block_counter){
		case 0:
			ymodem_is_start_request = 0;
			BSP_LED_Off(LED_GREEN);
			SOH_STX = RxData[0];
			Ymodem_info_block_counter++;
			break;

		case 1:
			Block_num = RxData[0];
			Ymodem_info_block_counter++;
			break;

		case 2:
			Block_num_inverted = RxData[0];
			Ymodem_info_block_counter++;
			ymodem_is_name_started = 1;
			//check consistency: block num and block num inverted
			break;

		case 3:
			Ymodem_data_block_counter++;
			if((ymodem_is_name_started == 1) & (RxData[0] != 0x00)){
				file_name[Ymodem_name_counter] = RxData[0];
				Ymodem_name_counter++;

			}
			else{
				ymodem_is_name_started = 0;
				ymodem_is_size_started = 1;
				Ymodem_name_counter = 0;
				Ymodem_info_block_counter++;

			}
			break;
		case 4:
			Ymodem_data_block_counter++;
			if((ymodem_is_size_started == 1) & (RxData[0] != 0x20)){
				file_size_val = file_size_val * 10 + (RxData[0] - 48);
				file_size[Ymodem_size_counter] = RxData[0];
				Ymodem_size_counter++;

			}
			else{
				last_block_size_check_index_val = file_size_val % 1024;
				ymodem_is_size_started = 0;
				Ymodem_size_counter = 0;
				Ymodem_info_block_counter++;
				ymodem_is_optional_bytes_started = 1;

			}
			break;
		case 5:
			Ymodem_data_block_counter++;
			if((ymodem_is_optional_bytes_started == 1) & (RxData[0] != 0x20)){
				optional_bytes[Ymodem_optional_counter] = RxData[0];
				Ymodem_optional_counter++;
			}
			else{

				ymodem_is_optional_bytes_started = 0;
				Ymodem_optional_counter = 0;
				Ymodem_info_block_counter++;
				ymodem_is_padding_started = 1;
			}
			break;
		case 6:
			Ymodem_data_block_counter++;
			if((ymodem_is_padding_started == 1) & (RxData[0] != 0x00)){
				padding[Ymodem_padding_counter] = RxData[0];
				Ymodem_padding_counter++;
			}
			else{
				ymodem_is_padding_started = 0;
				Ymodem_padding_counter = 0;
				Ymodem_info_block_counter = 0;
				ymodem_is_info_ended = 1;

			}
			break;
		default: // write rest of info block
			break;
		}

	}
	else if(Ymodem_data_block_counter <= 127){
		Ymodem_data_block_counter++;
	}
	else if(Ymodem_data_block_counter == 128){
		CRC_H = RxData[0];
		Ymodem_data_block_counter++;
	}
	else if(Ymodem_data_block_counter == 129){
		CRC_L = RxData[0];

		Ymodem_check_CRC(received_block0, sizeof(received_block0)); // check

		Ymodem_data_block_counter = 0;
		Block_num_counter++;
		Ymodem_is_data_block_started = 1;


		/*
		check_val[0] = (CRC_L | (CRC_H << 8));
		check_val[1] = CRC16Compute(received_block0, 128, 0x0000, 0x1021);
		check_val[2] = ( (CRC_L | (CRC_H << 8)) == CRC16Compute(received_block0, 128, 0x0000, 0x1021));
		check_val[3] = CRC_H;
		check_val[4] = CRC_L;
		//check crc there
		index1++;
		*/
	}
	else{
	}

}

void receive_data_block(){ // reduced to one function
	if(Ymodem_data_block_counter == 0){
		if(RxData[0] == EOT){
			ymodem_is_end_of_transmission = 1;
			Ymodem_send(ACK);
			Ymodem_send(C);
		}
		else
		{
			SOH_STX = RxData[0];
		}
		Ymodem_data_block_counter++;
		return;
	}
	if(ymodem_is_end_of_transmission){
		if(Ymodem_data_block_counter == 1){
			SOH_STX = RxData[0];
			Ymodem_data_block_counter++;
		}
		else if(Ymodem_data_block_counter == 2){
			Block_num = RxData[0];
			Ymodem_data_block_counter++;
		}
		else if(Ymodem_data_block_counter == 3){
			Block_num_inverted = RxData[0];
			Ymodem_data_block_counter++;
		}
		else if(Ymodem_data_block_counter < 132){
			received_end_block[Ymodem_data_block_counter - 4] = RxData[0];
			Ymodem_data_block_counter++;
		}
		else if(Ymodem_data_block_counter == 132){
			CRC_H = RxData[0];
			Ymodem_data_block_counter++;
		}
		else if(Ymodem_data_block_counter == 133){
			CRC_L = RxData[0];
			Ymodem_data_block_counter = 0;
			Ymodem_check_CRC(received_end_block, sizeof(received_end_block));
			Ymodem_reset();
			Single_Write_Is_Loaded_MetaData(META_DATA_IS_LOADED_ADDRESS, IS_LOADED_BYTE);
//			Multiple_Read_App2_Flash(FLASH_APP2_ADDRESS , data_read, sizeof(data_read));
//			is_jump_to_app2 = 1;

		}
		else{

		}
	}
	else{
		if(Ymodem_data_block_counter == 1){
			if(Block_num == (file_size_val / 1024 )){
				ymodem_is_last_block = 1;
			}
			Block_num = RxData[0];
			Ymodem_data_block_counter++;
		}
		else if(Ymodem_data_block_counter == 2){
			Block_num_inverted = RxData[0];
			Ymodem_data_block_counter++;
		}
		else if(Ymodem_data_block_counter < 1027){
			received_block[Ymodem_data_block_counter - 3] = RxData[0];
			Ymodem_data_block_counter++;
			if(ymodem_is_last_block){
				Ymodem_file_size_check();
			}
		}
		else if(Ymodem_data_block_counter == 1027){
			CRC_H = RxData[0];
			Ymodem_data_block_counter++;
		}
		else if(Ymodem_data_block_counter == 1028){
			CRC_L = RxData[0];
			Ymodem_data_block_counter = 0;
			// check crc
			if(ymodem_is_last_block){ // check size if last data block
				Ymodem_file_size_last_check();
			}
			Ymodem_check_CRC_and_write(received_block, 1024);

			/*
			for(int k = 0; k<1024; k++){
				received_block_deneme[received_block_deneme_index][k] = received_block[k];

			}
			received_block_deneme_index++;

			*/
		}
		else{

		}
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
  SCB->VTOR = 0x08040000;// offset for SysTick
  __enable_irq();
  HAL_UARTEx_ReceiveToIdle_IT(&huart2, RxData, 1);
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
  printf("Welcome to APP !\n\r");

  /* -- Sample board code to switch on leds ---- */

  /* USER CODE END BSP */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

	/* -- Sample board code for User push-button in interrupt mode ---- */
    if (BspButtonState == BUTTON_PRESSED)
    {
    	//go2App2();
    	Erase_App2_Flash();
      /* Update button state */
    	ymodem_is_start_request ^= 1; // xor to toggle between 0x01, 0x00
    	BSP_LED_Toggle(LED_GREEN); //Ready for receiving

    	BspButtonState = BUTTON_RELEASED;
      }
    HAL_Delay(100);
    if(ymodem_is_start_request){

    	Ymodem_send(C);
      }
      /* ..... Perform your action ..... */
    HAL_Delay(1000);
    printf("Old App is running!	\r\n");

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
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
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

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
	if(huart->Instance == USART2)
	{
		BSP_LED_Toggle(LED_YELLOW);
		switch(Block_num_counter){
		case 0:
			receive_first_block();
			break;
		default:
			receive_data_block();
			break;
		}
	HAL_UARTEx_ReceiveToIdle_IT(&huart2, RxData, 1);
	}
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART2) {
    	//HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
    }

}

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
