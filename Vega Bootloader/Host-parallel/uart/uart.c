
#include <stdint.h>
#include <msp430.h>

#include "bsl.h"

#define TIMEOUT_COUNT   1500000


// #define UCOSx UCOS16
// #define UCBRx 8
// #define UCBRFx UCBRF_10
// #define UCBRSx 0xF7

void BSL_Comm_Init(uint8_t baudcode) {

	// default values for 57600 baud @ 8MHz
	uint16_t UCOSx = UCOS16;
	uint16_t UCBRx = 8;
	uint16_t UCBRFx = UCBRF_10;
	uint8_t UCBRSx = 0xF7;

    switch (baudcode) {
        case 0x60: // 9600
            UCBRx = 52; UCBRFx = UCBRF_1; UCBRSx = 0x49; UCOSx = UCOS16;
            break;
        case 0x61: // 19200
            UCBRx = 26; UCBRFx = UCBRF_0; UCBRSx = 0xD6; UCOSx = UCOS16;
            break;
		case 0x62: // 38400
			UCBRx = 13; UCBRFx = UCBRF_0; UCBRSx = 0x45; UCOSx = UCOS16;
			break;
        case 0x63: // 57600
            UCBRx = 8; UCBRFx = UCBRF_10; UCBRSx = 0xF7; UCOSx = UCOS16;
            break;
        case 0x64: // 115200
            UCBRx = 4; UCBRFx = UCBRF_5; UCBRSx = 0x55; UCOSx = UCOS16;
            break;
        case 0x65: // 230400
            UCBRx = 2; UCBRFx = UCBRF_2; UCBRSx = 0xBB; UCOSx = UCOS16;
            break;
        case 0x66: // 460800
            UCBRx = 17; UCBRFx = UCBRF_0; UCBRSx = 0x4A; UCOSx = 0;
            break;
        case 0x67: // 921600
            UCBRx = 8; UCBRFx = UCBRF_0; UCBRSx = 0xD6; UCOSx = 0;
			// No oversampling
            break;
        default: // Default to 57600
            break;
    }

	/* Initializing UART Module */
	UCA1CTLW0 = UCSWRST | UCSSEL_2;   // USCI held in reset and use SMCLK = 8MHz
	P2SEL1 |= BIT5 | BIT6;             // P2.5 = RXD, P2.6 = TXD
	P2SEL0 &= ~(BIT5 | BIT6);
	UCA1BRW = UCBRx;
	UCA1MCTLW = UCOSx | (UCBRSx << 1) | UCBRFx;
	UCA1CTL1 &= ~UCSWRST;              // **Initialize USCI state machine**
}

/* Reads one byte from BSL target */
uint8_t BSL_getResponse(void) {
	uint32_t timeout = TIMEOUT_COUNT;

	while ((!(UCA1IFG & UCRXIFG)) && (timeout-- > 0))
		;

	if (!timeout)
		return 0xFF;
	else
		return UCA1RXBUF;

}

/* Checks if a slave is responding */
uint8_t BSL_slavePresent(void) {
	// NO ACK in UART
	return 1;
}

/* Sends single UART Byte (start and stop included) */
void BSL_sendSingleByte(uint8_t ui8Byte) {
	uint32_t timeout = TIMEOUT_COUNT;
	while ((!(UCA1IFG & UCTXIFG)) && (timeout-- > 0))
		;

	if (!timeout)
		return;
	else
		UCA1TXBUF = ui8Byte;
}

/* Sends application image to BSL target (start and stop included)
 * This is a polling function and is blocking. */
void BSL_sendPacket(tBSLPacket tPacket, uint8_t target_select) {
	uint16_t ii;
	volatile uint8_t crch, crcl;

	BSL_sendSingleByte(0x80);
	BSL_sendSingleByte(target_select); // send the list of intended targets
	BSL_sendSingleByte(tPacket.ui8Length);
	BSL_sendSingleByte(tPacket.tPayload.ui8Command);

	if (tPacket.ui8Length > 1)  {
		// Standard format for all commands: Address (3 bytes) + Data (variable)
		BSL_sendSingleByte(tPacket.tPayload.ui8Addr_L);
		BSL_sendSingleByte(tPacket.tPayload.ui8Addr_M);
		BSL_sendSingleByte(tPacket.tPayload.ui8Addr_H);
		for (ii = 0; ii < (tPacket.ui8Length - 4); ii++) {
			BSL_sendSingleByte(tPacket.tPayload.ui8pData[ii]);
		}
	}

	crcl = (uint8_t) (tPacket.ui16Checksum & 0xFF);
	BSL_sendSingleByte(crcl);

	crch = (uint8_t) (tPacket.ui16Checksum >> 8);
	BSL_sendSingleByte(crch);

}

void BSL_flush(void) {
	UCA1IFG &= ~(UCRXIFG);
}