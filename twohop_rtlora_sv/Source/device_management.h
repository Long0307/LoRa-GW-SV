/******************************************************************************
* Filename     : device_management.h                                          *
* Program      : Private LoRa Network Server Program                          *
* Copyright    : Copyright (C) 2017-2017, ETRI                                *
*              : URL << http://www.etri.re.kr >>                              *
* Authors      : Woo-Sung Jung (woosung@etri.re.kr)                           *
*              : Tae Hyun Yoon (thyoon0820@etri.re.kr)                        *
*              : Dae Seung Yoo (ooseyds@etri.re.kr)                           *
* Description  : Private LoRa End Device / GateWay management routine         *
* Created at   : Tue Aug 01 2017.                                             *
* Modified by  :                                                              *
* Modified at  :                                                              *
******************************************************************************/

#ifndef __DEVICE_MANAGEMENT_H
#define __DEVICE_MANAGEMENT_H

#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/types.h>
#include "lora_mac.h"

#define TCP_STREAM_BUFFER_SIZE	8192

typedef struct GateWayInfo{
	struct GateWayInfo *next;
	int socket;
	struct sockaddr_in sockaddr;
	uint8_t rxBuffer[TCP_STREAM_BUFFER_SIZE];
	uint32_t currentRxBufferSize;
}GateWayInfo_t;

typedef struct GatewayRxInfo{
	int socket;
	int16_t rssi;
	int8_t snr;
	uint32_t totalRxNum;
	uint32_t totalRxSize;
	time_t rxTime;
	uint8_t	GW_ID[8];
}GateWayRxInfo_t;

typedef struct EndDeviceInfo{
	struct EndDeviceInfo *next;
	uint32_t address;
	uint32_t totalRxNum;
	uint32_t totalRxSize;
	uint32_t seqNum;
	uint32_t totalRxNumPrev;
	uint32_t totalRxSizePrev;
	uint32_t seqNumPrev;
	uint8_t devcr;
	uint8_t devsf;
	uint32_t devch;	
	uint8_t devbw;	
	uint8_t	DevEUI[8];
	uint8_t AppEUI[8];
	uint8_t AppKEY[16];
	uint8_t NwkSKey[16];
	uint8_t AppSKey[16];
	uint8_t DevNonce[2];
	GateWayRxInfo_t gw[2];
}EndDeviceInfo_t;



//*********************************************************************************************************
//*  Gateway Related Codes are here..
//*********************************************************************************************************

/******************************************************************************
* Function Name        : InitGateWayInfo
* Input Parameters     : None
* Return Value         : None
* Function Description : GateWay Management Linked-List Structure Initialization..
******************************************************************************/
void InitGateWayInfo(void);

/******************************************************************************
* Function Name        : InitGateWayInfo
* Input Parameters     : None
* Return Value         : None
* Function Description : GateWay Management Linked-List Structure Deinitialization..
******************************************************************************/
void DeinitGateWayInfo(void);


/******************************************************************************
* Function Name        : AddGateWay
* Input Parameters     : int clntsocket                 - Gateway Socket Number
*                      : struct sockaddr_in clntAddress - Gateway Address Info
* Return Value         : None
* Function Description : New Gateway registration..
******************************************************************************/
void AddGateWay(int clntsocket, struct sockaddr_in clntAddress);

/******************************************************************************
* Function Name        : FindGateWay
* Input Parameters     : int socket      - Gateway Socket Number
* Return Value         : GateWayInfo_t * - Gateway Information Structure pointer
* Function Description : Find Gateway Information structure..
******************************************************************************/
GateWayInfo_t *FindGateWay(int socket);


/******************************************************************************
* Function Name        : RemoveGateWay
* Input Parameters     : int socket      - Gateway Socket Number
* Return Value         : None
* Function Description : Remove Gateway Information structure..
******************************************************************************/
void RemoveGateWay(int socket);


//*********************************************************************************************************
//*  End Device Related Codes are here..
//*********************************************************************************************************

/******************************************************************************
* Function Name        : InitEndDeviceInfo
* Input Parameters     : None
* Return Value         : None
* Function Description : EndDevice Management Linked-List Structure Initialization..
******************************************************************************/
void InitEndDeviceInfo(void);

/******************************************************************************
* Function Name        : DeinitEndDeviceInfo
* Input Parameters     : None
* Return Value         : None
* Function Description : EndDevice Management Linked-List Structure Deinitialization..
******************************************************************************/
void DeinitEndDeviceInfo(void);

/******************************************************************************
* Function Name        : AddEndDeviceInfo
* Input Parameters     : uint32_t address - End Device Address
*                      : int gwSocket     - Rx Gateway Socekt
*                      : LoRaJoinReqMsg_t - Received LoRaWAN Join Message Info
*                      : int16_t rssi     - Received Signal Strength
*                      : int8_t snr       - Signal to Noise Ratio
* Return Value         : End Device Info (ptr)
* Function Description : Add End Device Information
******************************************************************************/
EndDeviceInfo_t *AddEndDeviceInfo(uint32_t address, int gwSocket, LoRaJoinReqMsg_t rxMsg, int16_t rssi, int8_t snr, uint8_t cr, uint8_t sf, uint32_t ch, uint8_t bw, uint8_t* deui);

/******************************************************************************
* Function Name        : RxEndDeviceInfoUpdate
* Input Parameters     : uint32_t address - End Device Address
*                      : int gwSocket     - Rx Gateway Socekt
*                      : int16_t rssi     - RSSI
*                      : int8_t snr       - Signal to Noise Ratio
*                      : int8_t pktSize   - Received LoRaWAN Frame size
*                      : uint32_t seqNum  - End Device Seq. Number (UP_CNT)
* Return Value         : None
* Function Description : Add/Update End Device Information
******************************************************************************/
void RxEndDeviceInfoUpdate(uint32_t address, int gwSocket, int16_t rssi, int8_t snr, uint8_t pktSize, uint32_t seqNum, uint8_t cr, uint8_t sf, uint32_t ch, uint8_t bw);

/******************************************************************************
* Function Name        : FindEndDevice
* Input Parameters     : uint32_t address  - End Device Address
* Return Value         : EndDeviceInfo_t * - End Device Information Structure pointer
* Function Description : Find End Device Information structure..
******************************************************************************/
EndDeviceInfo_t *FindEndDevice(uint32_t address);

/******************************************************************************
* Function Name        : FindEndDevice
* Input Parameters     : uint32_t address - End Device Address
* Return Value         : int              - Gateway socket Number
* Function Description : Find Gateway socket number to send LoRa Pkt.
******************************************************************************/
int FindGWSocket(uint32_t address);

/******************************************************************************
* Function Name        : RemoveGateWayFromEndDevice
* Input Parameters     : int socket - Gateway socket
* Return Value         : None
* Function Description : Remove GW information from End Device Information 
******************************************************************************/
void RemoveGateWayFromEndDevice(int socket);

//*********************************************************************************************************
//*  Visualization Related Codes are here..
//*********************************************************************************************************
void ShowEndDevices(void);
void ShowGateWays(void);
void WriteEndDevices(int fd, int gwsocket);

void PreConfigNode(void);

int parse_Info_update(const char * conf_file, uint8_t* dev_eui, uint8_t* app_eui, uint32_t dev_addr, uint8_t* asky, uint8_t* nsky);
int parse_join_configuration(const char * conf_file, uint8_t* dev_eui, uint8_t* app_eui);
int parse_device_configuration(const char * conf_file);
void NodeExp(int gwcosket);

#endif

