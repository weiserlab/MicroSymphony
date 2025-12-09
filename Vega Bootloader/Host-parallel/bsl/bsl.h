
#ifndef __BSL_H__
#define __BSL_H__


typedef struct sBSLPayload
{
    uint8_t ui8Command;
    uint8_t ui8Addr_L;
    uint8_t ui8Addr_M;
    uint8_t ui8Addr_H;
    const uint8_t* ui8pData;
} tBSLPayload;

typedef struct sBSLPacket
{
    uint8_t ui8Header;
    uint8_t ui8Length;
    tBSLPayload tPayload;
    uint16_t ui16Checksum;
} tBSLPacket;

/* Prototypes */
// Generic BSL
void BSL_Init(uint8_t baudcode);
uint8_t BSL_sendCommand(uint8_t cmd, uint8_t target_select);
uint8_t BSL_changeBaudRate(uint8_t target_select, uint8_t baudcode);
// uint8_t BSL_changeBaudRate(uint8_t target_select, uint8_t br_zero, uint8_t br_one, uint8_t oversample, uint8_t brf, uint8_t brs);
uint8_t BSL_programMemorySegment(uint32_t addr, const uint8_t* data,
        uint32_t len, uint8_t target_select);
// Communication dependent
void BSL_Comm_Init(uint8_t baudcode);
uint8_t BSL_getResponse(void);
uint8_t BSL_slavePresent(void);
void BSL_sendSingleByte(uint8_t ui8Byte);
void BSL_sendPacket(tBSLPacket tPacket, uint8_t target_select);
void BSL_flush(void);

// commands to Target BSL
#define VBOOT_ENTRY_CMD     0xAA
#define VBOOT_VERSION       0xA1

#define BSL_VERSION_CMD     0x19
#define BSL_ERASE_APP_CMD   0x15
#define BSL_RX_APP_CMD      0x10
#define BSL_JMP_APP_CMD     0x1C
#define BSL_CHANGE_BAUD_CMD 0x17

#define BSL_OK_RES          0x00
#define BSL_NULL_RES        0x01

#define BSL_HEADER          0x80

#endif /* __BSL_H__ */
