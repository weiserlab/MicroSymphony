
//
// Include files
//
#include "msp430.h"
#include "TI_MSPBoot_Common.h"
#include "TI_MSPBoot_CI.h"
#include "TI_MSPBoot_MI.h"
#include "crc.h"
#include "stdbool.h"


//
//  Configuration checks
//
#if (MCLK==1000000)
#   warning "It's recommended to use MCLK>=4Mhz with BSL-type protocol"
#endif
#ifndef MSPBoot_CI_UART
#   warning "This file was tested on UART interface"
#endif


//
// Macros and definitions - Network Layer
//
#ifndef __IAR_SYSTEMS_ICC__
// This keyword doesn't exist in CCS and is only used with IAR.
#   define __no_init
#endif
/*! Maximum size of data payload =  16 data + 1 CMD + 3 ADDR */
#define PAYLOAD_MAX_SIZE            (16+1+3)
/*! MSPBoot Packet header */
#define HEADER_CHAR                 (0x80)

// MSPBoot Network layer responses 
#define RESPONSE_NWK_HEADER_ERROR       (0x51)  /*! Incorrect header        */
#define RESPONSE_NWK_CHECKSUM_ERROR     (0x52)  /*! Packet checksum error   */
#define RESPONSE_NWK_PACKETZERO_ERROR   (0x53)  /*! Packet size is zero     */
#define RESPONSE_NWK_PACKETSIZE_ERROR   (0x54)  /*! Packet size is invalid  */
#define RESPONSE_NWK_UNKNOWN_ERROR      (0x55)  /*! Error in protocol       */
#define RESPONSE_NWK_SLAVE_IGNORED      (0x56)  /*! Slave ignored           */

/*! MSPBoot version sent as response of COMMAND_TX_VERSION command  */
#define MSPBoot_VERSION                 (0xA1)

//
// Macros and definitions - Application layer
//
// Supported commands in BSL-based protocol
/*! Erase a segment :
    0x80    LEN=0x03    0x12    ADDRL   ADDRH   CHK_L   CHK_H   */
#define COMMAND_ERASE_SEGMENT           (0x12)
/*! Erase application area:
    0x80    LEN=0x01    0x15    CHK_L   CHK_H   */
#define COMMAND_ERASE_APP               (0x15)
/*! Receive data block :
    0x80    LEN=3+datapayload   0x10    ADDRL   ADDRH   DATA0...DATAn   CHK_L   CHK_H   */
#define COMMAND_RX_DATA_BLOCK           (0x10)
/*! Transmit MSPBoot version :
    0x80    LEN=0x01    0x19    CHK_L   CHK_H   */
#define COMMAND_TX_VERSION              (0x19)
/*! Change baudrate (standardized format):
    0x80    LEN=0x05    0x17    ADDR_L  ADDR_M  ADDR_H  baudcode   CHK_L   CHK_H   */
#define COMMAND_CHANGE_BAUD             (0x17)
/*! Jump to application:
    0x80    LEN=0x01    0x1C    CHK_L   CHK_H   */
#define COMMAND_JUMP2APP                (0x1C)

//  MSPBoot Application layer responses
#define RESPONSE_APP_OK                 (0x00)  /*! Command processed OK    */
#define RESPONSE_APP_INVALID_PARAMS     (0xC5)  /*! Invalid parameters      */
#define RESPONSE_APP_INCORRECT_COMMAND  (0xC6)  /*! Invalid command         */


//
//  Global variables
//
/*! Communication Status byte:
 *  BIT1 = COMM_PACKET_RX = Packet received
 *  BIT3 = COMM_ERROR = Error in communication protocol
 */
__no_init uint8_t CommStatus;
__no_init uint8_t TxByte;                       /*! Byte sent as response to Master */
__no_init uint8_t RxPacket[PAYLOAD_MAX_SIZE];   /*! Data received from Master */
__no_init static uint8_t counter;               /*! Data counter */
__no_init static uint8_t actual_counter;        /*! Data counter */
__no_init static uint8_t Len;                   /*! Data lenght */
__no_init static bool respond;                   /*! Data respond flag */

//
//  Local function prototypes
//
extern uint8_t CI_CMD_Intepreter(uint8_t *RxData, uint8_t RxLen, uint8_t *TxData);
static void CI_NWK_Rx_Callback(uint8_t data);
static uint8_t CI_CMD_Rx_Data_Block(uint32_t addr, uint8_t *data, uint8_t len);


/*! Callback structure used for UART BSL-based
 *   RX callback function is implemented
 */
static const t_CI_Callback CI_Callback_s =
{
    CI_NWK_Rx_Callback,
    NULL,               // TX_Callback Not implemented for UART
    NULL,               // Error callback not implemented in this protocol
};

/******************************************************************************
 *
 * @brief   Initialize the Communication Interface
*  Lower layers will also be initialized
 *
 * @return none
 *****************************************************************************/
// void TI_MSPBoot_CI_Init(uint8_t br_zero, uint8_t br_one, uint8_t oversample, uint8_t brf, uint8_t brs)
void TI_MSPBoot_CI_Init(uint8_t baudcode)
{
    CommStatus = 0;
    counter = 0;
    actual_counter = 0;
    TxByte = RESPONSE_NWK_UNKNOWN_ERROR;
    respond = false;
    TI_MSPBoot_CI_PHYDL_Init((t_CI_Callback *) &CI_Callback_s, baudcode);
}

/******************************************************************************
 *
 * @brief   On packet reception, process the data
 *  BSL-based protocol expects:
 *  HEADER  = 0x80
 *  Lenght  = lenght of CMD + [ADDR] + [DATA]
 *  CMD     = 1 byte with the corresponding command
 *  ADDR    = optional address depending on command
 *  DATA    = optional data depending on command
 *  CHKSUM  = 2 bytes (L:H) with CRC checksum of CMD + [ADDR] + [DATA]
 *
 * @return RET_OK: Communication protocol in progress
 *         RET_JUMP_TO_APP: Last byte received, request jump to application
 *         RET_PARAM_ERROR: Incorrect command
 *****************************************************************************/
uint8_t TI_MSPBoot_CI_Process(void)
{
    uint8_t ret = RET_OK;

    if (CommStatus & COMM_PACKET_RX)    // On complete packet reception
    {
        ret = CI_CMD_Intepreter(RxPacket, Len, &TxByte);
        if (respond)
            TI_MSPBoot_CI_PHYDL_TXByte(TxByte);
        counter = 0;
        actual_counter = 0;
        CommStatus = 0; // Clear packet reception
        respond = false;
    }
    return ret;
}


/******************************************************************************
 *
 * @brief   Process a packet checking the command and sending a response
 *  New commands can be added in this switch statement
 *
 * @param RxData Pointer to buffer with received data including command
 * @param RxLen  Lenght of Received data
 * @param TxData Pointer to buffer which will be updated with a response
 *
 * @return RET_OK: Communication protocol in progress
 *         RET_JUMP_TO_APP: Last byte received, request jump to application
 *         RET_PARAM_ERROR: Incorrect command
 *****************************************************************************/
uint8_t CI_CMD_Intepreter(uint8_t *RxData, uint8_t RxLen, uint8_t *TxData)
{
    switch (RxData[0])
    {
        case COMMAND_ERASE_APP:
            // Erase the application area
            TI_MSPBoot_MI_EraseApp();
            *TxData = RESPONSE_APP_OK;
        break;
        case COMMAND_RX_DATA_BLOCK:
            // Receive and program a data block specified by an address
            *TxData = CI_CMD_Rx_Data_Block((uint32_t)RxData[1]+((uint32_t)RxData[2]<<8)+(((uint32_t)RxData[3] & 0x0F)<<16), &RxData[4], RxLen-4);
        break;
        case COMMAND_ERASE_SEGMENT:
            // Erase an application area sector as defined by the address
            if (TI_MSPBoot_MI_EraseSector((uint32_t)RxData[1]+((uint32_t)RxData[2]<<8)+(((uint32_t)RxData[3] & 0x0F)<<16)) == RET_OK)
            {
                *TxData = RESPONSE_APP_OK;
            }
            else
            {
                *TxData = RESPONSE_APP_INVALID_PARAMS;
            }
        break;
        case COMMAND_TX_VERSION:
            // Transmit MSPBoot version
            *TxData = MSPBoot_VERSION;
        break;
        case COMMAND_CHANGE_BAUD: // check functionality
            // Change baudrate - standardized packet format
            // RxData[0] = Command (0x17)
            // RxData[1] = Addr_L (dummy, ignored)  
            // RxData[2] = Addr_M (dummy, ignored)
            // RxData[3] = Addr_H (dummy, ignored)
            // RxData[4] = Baudcode (actual parameter)
            // TI_MSPBoot_CI_PHYDL_disable();
            __delay_cycles(8000); // Small delay for stability
            TI_MSPBoot_CI_Init(RxData[4]);
            // *TxData = RESPONSE_APP_OK;
        break;
        case COMMAND_JUMP2APP:
            // Jump to Application
            return RET_JUMP_TO_APP;
        //break;
        default:
            *TxData = RESPONSE_APP_INCORRECT_COMMAND;
            return RET_PARAM_ERROR;
        //break;
    }

    return RET_OK;
}


/******************************************************************************
 *
 * @brief   Programs a block of data to memory
 *
 * @param addr  Start address (16-bit) of area being programmed
 * @param data  Pointer to data being written
 * @param len   Lenght of data being programmed
 *
 * @return  RESPONSE_APP_OK: Result OK
 *          RESPONSE_APP_INVALID_PARAMS: Error writing the data
 *****************************************************************************/
static uint8_t CI_CMD_Rx_Data_Block(uint32_t addr, uint8_t *data, uint8_t len)
{
    uint8_t i;
    for (i=0; i < len; i++)
    {
        if ( TI_MSPBoot_MI_WriteByte(addr++, data[i]) != RET_OK)
        {
            return RESPONSE_APP_INVALID_PARAMS;
        }
    }

    return RESPONSE_APP_OK;
}

/******************************************************************************
 *
 * @brief   RX Callback for BSL-based protocol
 *
 * @param data  Byte received from Master
 *
 * @return  none
 *****************************************************************************/
void CI_NWK_Rx_Callback(uint8_t data)
{
    __no_init static uint16_t Checksum;

    if (counter == 0) // receive the first byte
    {
        // Byte 0 = Header
        TxByte = RESPONSE_NWK_UNKNOWN_ERROR;    // Initial response if packet is incomplete
        // Check header
        if (data !=HEADER_CHAR)
        {
            // Incorrect header
            CommStatus |= COMM_ERROR;
            TxByte =  RESPONSE_NWK_HEADER_ERROR;    // Send as response to Master
            if(data != 0xAA) //for debug
            	__no_operation();
        }
    }
    else if (counter == 1) // receive the slave address
    {
        uint8_t SlaveAddress = data;
        // the address is a 8-bit number, intended for 7 slaves.
        // all the slave addresses are power of 2
        // if the bitwise AND of the address and the slave address is 0,
        // then the packet is not for this slave
        // the 7th bit (0-indexing) is used to indicate if the slave should respond
        respond = false;
        // respond = (SlaveAddress & BIT7) ? true : false;

        // Check if address is valid - change for different addressing scheme
        if ((SlaveAddress & SLAVE_ADDRESS_MASK) == 0 || (SlaveAddress & BOARD_ADDRESS_MASK) == 0)
        {
            // Packet is not for this slave - reset state immediately
            CommStatus = 0;
            counter = 0;
            actual_counter = 0;
            respond = false;
            return; // Do not process further
        }

        if (SlaveAddress & BIT7)  
            respond = true;

    }
    else if (counter == 2) // receive the length 
    {
        // Byte 1 = Len (max 255)
        Len = data;
        if (data == 0)
        {
            // Size = 0
            CommStatus |= COMM_ERROR;
            TxByte =  RESPONSE_NWK_PACKETZERO_ERROR;    // Send as response to Master
        }
        else if(data > PAYLOAD_MAX_SIZE)
        {
            // Size too big
            CommStatus |= COMM_ERROR;
            TxByte =  RESPONSE_NWK_PACKETSIZE_ERROR;    // Send as response to Master
        }
        actual_counter = counter-1;
    }
    else{ 
        if (actual_counter < (Len+2))
        {
            // Payload (optional address + data)
            RxPacket[actual_counter-2] = data;
        }
        else if (actual_counter == (Len+2))
        {
            // Byte n-1 = Expected checksum_Low
            Checksum = data;
        }
        else if (actual_counter == (Len+3))
        {
            // Byte n = Expected checksum_High
            Checksum += (data<<8);
            // Calculate checksum and compare with received
            if (crc16MakeBitwise(RxPacket, Len, 0xFFFF) != Checksum)
            {
                // Incorrect checksum
                CommStatus |= COMM_ERROR;
                TxByte =  RESPONSE_NWK_CHECKSUM_ERROR;  // Send as response to Master
            }
            else
            {
                CommStatus |= COMM_PACKET_RX;
            }
        }
    }
    if (CommStatus & COMM_ERROR)
    {
        // if (respond)
            // TI_MSPBoot_CI_PHYDL_TXByte(TxByte);
        CommStatus = 0;
        counter = 0;
        actual_counter = 0;
        respond = false; // Reset respond flag
    }
    else
    {
        counter++;
        actual_counter++;
    }
}
