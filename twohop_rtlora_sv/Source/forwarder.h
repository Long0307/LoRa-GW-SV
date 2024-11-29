/******************************************************************************
* Filename     : forwarder.h                                                  *
* Program      : Private LoRa Gateway Program                                 *
* Copyright    : Copyright (C) 2017-2017, ETRI                                *
*              : URL << http://www.etri.re.kr >>                              *
* Description  : Private LoRa Gateway Forwarding Routine                      *
* Created at   : Mon Jul. 24. 2017.                                           *
* Modified by  :                                                              *
* Modified at  :                                                              *
******************************************************************************/

#ifndef __FORWARDER_H_
#define __FORWARDER_H_

#include <stdint.h>

#define START_OF_FRAME			0xDE
#define END_OF_FRAME			0xCA

#define RX_TUNNELING_OVERHEAD		41	// 1(SOF) + 1(SIZE) + 17(TIME) + 1 (padding) + 20(NET INFO) + 1(EOF)
#define TX_TUNNELING_OVERHEAD		12	// 1(SOF) + 1(SIZE) + 17(TIME) + 1 (padding) + 20(NET INFO) + 1(EOF)


// When Network server send packt to App server, gatewayID will use DevEUI.

typedef struct NetworkInfo{
	uint8_t gatewayID[8];		//   8 bytes - ex) AA555A0000000001
	uint32_t freq;				//   4 bytes - ex) 921100000 (921.1MHz)
	int16_t rssi;				//   2 bytes
	int16_t snr;				//   2 bytes - ex) 0.8 --> 8 (10x)
	uint8_t cr;					//   1 byte  - ex) 4/5 ~ 4/8
	uint8_t sf;					//   1 byte  - ex) SF7 ~ SF12
	uint8_t bw;					//   1 byte  - ex) 125Khz, 250Khz, 500Khz
	uint8_t padding[1];			//   1 bytes padding
}NetworkInfo_t;					//  20 bytes

typedef struct LoRaRxFrameInfo{
	uint8_t	startOfFrame;				//   1 byte
	uint8_t size;				//   1 bytes - ex) Only LoRaFrame Size
	uint8_t timestamp[17];		//  17 bytes - ex) 20190301 09:10:00
	uint8_t padding;
	NetworkInfo_t netInfo;		//  20 bytes
	uint8_t loraframe[256];		// 256 bytes - Maxium 256 bytes..
	uint8_t endOfFrame;
}LoRaRxFrameInfo_t;

typedef struct LoRaTxFrameInfo{
	uint8_t startOfFrame;
	uint8_t frameSize;
	uint8_t cr;
	uint8_t sf;
	uint32_t ch;
	uint8_t bw;
	uint8_t appPayloadSize;
	uint8_t startOfAppPayload;
	uint8_t payload[256];
	uint8_t endOfFrame;
}LoRaTxFrameInfo_t;


#endif
