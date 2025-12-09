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
#include <stdint.h>
#include "bsl.h"
#include "Crc.h"

#define CRC8_POLY   0x07
#define CRC16_POLY   0x1021

//
//  Function declarations
//
/******************************************************************************
 *
 * @brief   CRC_CCITT as implemented in slaa221
 *          This implementation is slower but smaller than table method
 *
 * @param pmsg  Pointer to data being calculated
 *
 * @return  16-bit CRC_CCITT result
 *****************************************************************************/
uint16_t crc16MakeBitwise(tBSLPacket tPacket)
{
    uint16_t i;

    CRCINIRES = 0xFFFF;

    uint8_t* pmsg = (uint8_t*)&tPacket.tPayload;

    uint16_t msg_size = tPacket.ui8Length;

    for(i = 0 ; i < msg_size ; i ++)
    {
        if(i < 4)
        	CRCDIRB_L = pmsg[i];
        else if(i == 4)
        {
            pmsg = (uint8_t*)tPacket.tPayload.ui8pData;
            CRCDIRB_L = pmsg[i-4];
        }
        else
        {
        	CRCDIRB_L = pmsg[i-4];
        }

    }

    return CRCINIRES;
}
