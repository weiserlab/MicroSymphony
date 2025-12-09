/*
 * \file   TI_MSPBoot_MI_FRAM.c
 *
 * \brief  Driver for memory interface using FRAM in FR59xx
 */
/* --COPYRIGHT--,BSD-3-Clause
 * Copyright (c) 2017, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * --/COPYRIGHT--*/

//
// Include files
//
#include "msp430.h"
#include "TI_MSPBoot_Common.h"
#include "TI_MSPBoot_MI.h"


//
//  Functions declarations
//
/******************************************************************************
*
 * @brief   Erase a FRAM Sector
 *          FRAM doesn't have sectors or an "erase" state but this function 
 *          is added for compatibility with Flash and erases as 0xFF
 *          in order to calculate CRC
 *
 * @param  addr    Address in the sector being erased (emulated sector is 512B)
 *
 * @return  RET_OK when sucessful,
 *          RET_PARAM_ERROR if address is outside of Application area
 *****************************************************************************/
uint8_t TI_MSPBoot_MI_EraseSector(uint32_t addr)
{
    uint16_t i;
    uint32_t currentAddr;
#ifdef CONFIG_MI_MEMORY_RANGE_CHECK
    // Check address to be within Application range
    if (((addr < APP_START_ADDR) || (addr > APP_END_ADDR)) && ((addr < FLEX_START_ADDR)||(addr > FLEX_END_ADDR)))
        return RET_PARAM_ERROR;
#endif
    // Erase is not necessary for FRAM but we do it in order to calculate CRC
    // properly
    //ptr = (uint8_t *) (addr & ~0x1FF);
    currentAddr = addr & ~0x1FF;
    for (i = 0; i < 512; i++)
        //*ptr++ = 0xFF;
    	__data20_write_char(currentAddr, 0xFF);

    return RET_OK;
}

/******************************************************************************
 *
 * @brief   Erase the application area (address obtained from linker file)
 *          FRAM doesn't have an "erased" state but this function is added
 *          for compatibility with Flash and in order to calculate CRC
 *
 * @return  none
 *****************************************************************************/
void TI_MSPBoot_MI_EraseApp(void)
{
    uint32_t addr;

    __no_operation();

    for (addr = APP_END_ADDR; addr >=APP_START_ADDR; addr--)
    {
        //*(uint8_t *) (addr) = 0xFF;
    	__data20_write_char(addr, 0xFF);
        __no_operation();
    }
    //erase the upper app area too
    for (addr = FLEX_END_ADDR; addr >= FLEX_START_ADDR; addr--)
    {
        //*(uint8_t *) (addr) = 0xFF;
    	__data20_write_char(addr, 0xFF);
        __no_operation();
    }
}

/******************************************************************************
 *
 * @brief   Write a Byte to FRAM memory
 *          The bootloader is protected using MPU but all interrupts (except for
 *          Vector) can be reprogrammed 
 *
 * @param  addr     Address of the Byte being written
 * @param  data     Byte being written
 *
 * @return  RET_OK when sucessful,
 *          RET_PARAM_ERROR if address is outside of Application area
 *****************************************************************************/
uint8_t TI_MSPBoot_MI_WriteByte(uint32_t addr, uint8_t data)
{

#ifdef CONFIG_MI_MEMORY_RANGE_CHECK
    if (((addr < APP_START_ADDR) || (addr > APP_END_ADDR)) && ((addr < FLEX_START_ADDR)||(addr > FLEX_END_ADDR)))
        return RET_PARAM_ERROR;
#endif

    //*(uint8_t *) addr = data;
    __data20_write_char(addr, data);
    if ((addr >= APP_VECTOR_TABLE) && (addr < APP_RESET_VECTOR_ADDR))
    {
        // If address is an interrupt vector, copy directly to interrupt table
        addr = (addr - APP_VECTOR_TABLE) + BOOT_VECTOR_TABLE;
        
        MPUCTL0 = MPUPW | MPUENA;   // Enable access to MPU registers
        MPUSAM |= MPUSEG2WE;        // Enable Write access
        *(uint8_t *) addr = data;   // Write the vector
        MPUSAM &= ~MPUSEG2WE;       // Disable Write access
        MPUCTL0_H = 0x00;           // Disable access to MPU registers
        
    }

    return RET_OK;
}
