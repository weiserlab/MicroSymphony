
#include <stdint.h>
#include <msp430.h>


#include "bsl.h"
#include "Crc.h"

void BSL_Init(uint8_t baudcode)
{
  BSL_Comm_Init(baudcode);
}

uint8_t BSL_sendCommand(uint8_t cmd, uint8_t target_select)
{
    tBSLPacket tPacket;
    tPacket.ui8Header = BSL_HEADER;
    tPacket.tPayload.ui8Command = cmd;
    tPacket.ui8Length = 1;
    tPacket.ui16Checksum = crc16MakeBitwise(tPacket);
    BSL_sendPacket(tPacket, target_select);

    if(cmd == BSL_JMP_APP_CMD) return 0;

    uint8_t resp = BSL_getResponse(); // flush out
    return (target_select & BIT7) ? resp : BSL_OK_RES;
}

uint8_t BSL_changeBaudRate(uint8_t target_select, uint8_t baudcode)
// uint8_t BSL_changeBaudRate(uint8_t target_select, uint8_t br_zero, uint8_t br_one, uint8_t oversample, uint8_t brf, uint8_t brs)
{
    // uint8_t baud_data[5] = { br_zero, br_one, oversample, brf, brs };

    tBSLPacket tPacket;
    tPacket.ui8Header = BSL_HEADER;
    tPacket.tPayload.ui8Command = BSL_CHANGE_BAUD_CMD;
    
    // Use standard packet format: Command + 3 dummy address bytes + 1 data byte
    // This allows the existing CRC function to work unchanged
    tPacket.tPayload.ui8Addr_L = 0x00;  // Dummy address (not used for baud rate)
    tPacket.tPayload.ui8Addr_M = 0x00;  // Dummy address (not used for baud rate)  
    tPacket.tPayload.ui8Addr_H = 0x00;  // Dummy address (not used for baud rate)
    tPacket.tPayload.ui8pData = &baudcode;
    tPacket.ui8Length = 5;  // Command (1) + Address (3) + Data (1) = 5 bytes total

    tPacket.ui16Checksum = crc16MakeBitwise(tPacket);
    BSL_sendPacket(tPacket, target_select);

    uint8_t resp = BSL_getResponse(); // flush out
    return BSL_OK_RES;
}


uint8_t BSL_programMemorySegment(uint32_t addr, const uint8_t* data,
        uint32_t len, uint8_t target_select)
{
    uint16_t xferLen;
    uint8_t res;

    while (len > 0)
    {
        if (len > 16)
        {
            xferLen = 16;
        } else
        {
            xferLen = len;
        }

        tBSLPacket tPacket;
        tPacket.tPayload.ui8Command = BSL_RX_APP_CMD;
        tPacket.tPayload.ui8pData = data;
        tPacket.ui8Length = xferLen + 4;
        tPacket.tPayload.ui8Addr_H = ((uint8_t) (addr >> 16));
        tPacket.tPayload.ui8Addr_M = ((uint8_t) (addr >> 8));
        tPacket.tPayload.ui8Addr_L = ((uint8_t) (addr & 0xFF));
        tPacket.ui16Checksum = crc16MakeBitwise(tPacket);
        BSL_sendPacket(tPacket, target_select);
        //TEST
        if(addr >= 0x23F00)
        	__no_operation();
        //END TEST
        if(target_select & BIT7)
            res = BSL_getResponse();
        else res = BSL_NULL_RES;

        // if (target_select & BIT7) res == BSL_NULL_RES; // check if the target has to respond
        // else {
        //     res = BSL_NULL_RES;
        //     volatile uint8_t _ = UCA1RXBUF; // clear the buffer incase
        // }

        if (res != BSL_OK_RES && res != BSL_NULL_RES)
            break;

        len -= xferLen;
        addr += xferLen;
        data += xferLen;
    }

    return res;
}