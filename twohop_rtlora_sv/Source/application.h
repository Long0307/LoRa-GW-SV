/******************************************************************************
* Filename     : application.c                                                *
* Program      : Two-hop RT-LoRa application program                          *
* Copyright    : Copyright (C) 2021                                           *
* Authors      : Quy Lam Hoang (quylam925@gmail.com)                          *
* Description  : Two-hop RT-LoRa Application Handling Routine                 *
* Created at   :                                                              *
* Modified by  :                                                              *
* Modified at  :                                                              *
******************************************************************************/

#ifndef __APPLICATION_H_
#define __APPLICATION_H_

#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "forwarder.h"


#define DBHOST "106.252.240.216"
#define DBUSER "etri"
#define DBPASS "etri_db"
#define DBNAME "LoRa"
#define DPORT  13306
#define USEDB  0


#define HHDBHOST "192.168.0.169"
#define HHDBUSER "root"
#define HHDBPASS "ubimicro"
#define HHDBNAME "vm"
#define HHDPORT  3306
#define USEHHDB  0

#define WEATHER_MONITORING_APP

/******************************************************************************
* Function Name        : InitApplication
* Input Parameters     : None
* Return Value         : None
* Function Description : Application Initialization Code
******************************************************************************/
void InitApplication(void);

/******************************************************************************
* Function Name        : DeInitApplication
* Input Parameters     : None
* Return Value         : None
* Function Description : Application DeInitialization Code
******************************************************************************/
void DeInitApplication(void);

void AppInputDataHandle(uint16_t sender_id, uint8_t *data_buff, uint8_t data_len );

void AppParseData(void);

void AppPushDataToAppServer(void);

void AppDisplayData(void);

/******************************************************************************
* Function Name        : GetAppNonce
* Input Parameters     : Nonce
* Return Value         : Application Nonce Value
* Function Description : Application Nonce Value return
******************************************************************************/
void GetAppNonce(uint8_t *nonce);

/******************************************************************************
* Function Name        : GetAppKey
* Input Parameters     : Nonce
* Return Value         : Application Key Value
* Function Description : Application Key Value return
******************************************************************************/
void GetAppKey(uint8_t *key);

/******************************************************************************
* Function Name        : ApplicationRxHandler
* Input Parameters     : uint8_t *rx   - Received Application Data
*                      : uint32_t size - Received Application Payload Size
*                      : uint32_t address - End Device Address
* Return Value         : None
* Function Description : Receive Data via LoRa Communication
******************************************************************************/
//void ApplicationRxHandler(uint8_t *rx, uint32_t size, uint32_t address, uint32_t seq, int16_t rssi, int8_t snr, uint32_t channel, uint8_t sf);
void ApplicationRxHandler(uint8_t *rx, uint32_t size, uint32_t address, uint32_t seq, LoRaRxFrameInfo_t *rxFrame);

void SendApplicationData(void);

/******************************************************************************
* Function Name        : ApplicationRxHandlerUWB
* Input Parameters     : uint8_t *rx   - Received Application Data
*                      : uint32_t size - Received Application Payload Size
*                      : uint32_t address - End Device Address
* Return Value         : None
* Function Description : Receive Data via UWB communication interface
******************************************************************************/
void ApplicationRxHandlerUWB(uint8_t *rx, uint32_t size, uint32_t id);

/*
Data Transmission (Msg ID = 1)
  1  2  3   4  5  6  7  8  9 10  11 12 13 14 15 16 17 18 19 20
+---------+-----+---+-------+---+--------------+-----+--------+
| start   | seq |msg| ppp ID|ws |  PPE Data    |reser|  end   |
|         |     |ID |       |ID | 1| 2| 3| 4| 5|ved  |        |
+---------+-----+---+-------+---+--------------+-----+--------+
| m  i  c |00-99| 1 |000-999|1-4|     0 - 3    | 0 0 | fin    |
+---------+-----+---+-------+---+--------------+-----+--------+

Rule Control (Msg ID = 2)
  1  2  3   4  5  6  7  8  9 10  11 12 13 14 15 16 17 18 19 20
+---------+-----+---+-------+---+--------------+-----+--------+
| start   | seq |msg| ppp ID|ws |  PPE Rule    |reser|  end   |
|         |     |ID |       |ID | 1| 2| 3| 4| 5|ved  |        |
+---------+-----+---+-------+---+--------------+-----+--------+
| m  i  c |00-99| 1 |000-999|1-4|     0 - 3    | 0 0 | fin    |
+---------+-----+---+-------+---+--------------+-----+--------+

Interval Setup (Msg ID = 3)
  1  2  3   4  5  6  7  8  9 10  11 12 13 14 15 16 17 18 19 20
+---------+-----+---+-------+---+--------------+-----+--------+
| start   | seq |msg| ppp ID|ws |  Data Send   |reser|  end   |
|         |     |ID |       |ID |  Interval    |ved  |        |
+---------+-----+---+-------+---+--------------+-----+--------+
| m  i  c |00-99| 1 |000-999|1-4|  0 - 99999   | 0 0 | fin    |
+---------+-----+---+-------+---+--------------+-----+--------+

LED, Buzzer (Msg ID = 4)
  1  2  3   4  5  6  7  8  9 10  11 12 13 14 15 16 17 18 19 20
+---------+-----+---+-------+---+--+--+--------------+--------+
| start   | seq |msg| ppp ID|ws | L| B| reserved     |  end   |
|         |     |ID |       |ID |  |  |              |        |
+---------+-----+---+-------+---+--+--+--------------+--------+
| m  i  c |00-99| 1 |000-999|1-4| 0-1 | 0  0  0  0 0 | fin    |
+---------+-----+---+-------+---+-----+--------------+--------+

ACK (Msg ID = 9)
  1  2  3   4  5  6  7  8  9 10  11 12 13 14 15 16 17 18 19 20
+---------+-----+---+-------+---+--------------+-----+--------+
| start   | seq |msg| ppp ID|ws |    ACK       |reser|  end   |
|         |     |ID |       |ID | seq |retrycnt|ved  |        |
+---------+-----+---+-------+---+--------------+-----+--------+
| m  i  c |00-99| 1 |000-999|1-4|0-99 | 1 - 2  | 0 0 | fin    |
+---------+-----+---+-------+---+--------------+-----+--------+

*/

typedef struct RWSMS{
	uint8_t seqNum;		// 2 digits
	uint8_t msgID;		// 1 digit
	uint16_t pppID;		// 3 digits
	uint8_t wsID;		// 1 digit
	uint8_t ppeData[5];	// 5 digits only if msgID = 1
	uint8_t ppeRule[5];	// 5 digits only if msgID = 2
	uint32_t interval;	// 5 digits only if msgID = 3
	uint8_t led;		// 1 digit  only if msgID = 4
	uint8_t buzzer;		// 1 digit  only if msgID = 4
	uint8_t ackSeq;		// 2 digits only if msgID = 5
	uint16_t ackRetryCnt;	// 3 digits only if msgID = 5
}RWSMS_t;

typedef enum message_type {
	DATA_MSG = 1,
	RULE_MSG = 2,
	INTERVAL_MSG = 3,
	LED_BUZZER_MSG = 4,
	ACK_MSG = 9,
}message_type_t;

/*
Gas Detection Device Message Format
  [GAS:VALUE|GAS2:VALUE2|...]
  Value Range = -99.9 ~ 99.9

Example
  [O2: 20.9|CO: 00.3|H2: 00.2|H2S: 00.7|CH4: -84.3]

 */
typedef struct GDD {
	float oxygen;		// O2
	float carbonOxygen;	// CO
	float hydrogen;		// H2
	float hydrogenSulfide;	// H2S
	float methane;		// CH4
}GDD_t;



typedef struct AppClientInfo{
	struct AppClientInfo *next;
	int socket;
	struct sockaddr_in sockaddr;
}AppClientInfo_t;

/******************************************************************************
* Function Name        : InitGateWayInfo
* Input Parameters     : None
* Return Value         : None
* Function Description : AppClient Management Linked-List Structure Initialization..
******************************************************************************/
void InitAppClientInfo(void);

/******************************************************************************
* Function Name        : InitGateWayInfo
* Input Parameters     : None
* Return Value         : None
* Function Description : AppClient Management Linked-List Structure Deinitialization..
******************************************************************************/
void DeinitAppClientInfo(void);

/******************************************************************************
* Function Name        : SendtoAllApplication
* Input Parameters     : None
* Return Value         : None
* Function Description : Send LoRa Pkt to All Application Client
******************************************************************************/
void SendtoAppClient(uint8_t *data, uint32_t size);

/******************************************************************************
* Function Name        : AddAppClient
* Input Parameters     : int clntsocket                 - AppClient Socket Number
*                      : struct sockaddr_in clntAddress - AppClient Address Info
* Return Value         : None
* Function Description : New AppClient registration..
******************************************************************************/
void AddAppClient(int clntsocket, struct sockaddr_in clntAddress);

/******************************************************************************
* Function Name        : FindGateWay
* Input Parameters     : int socket      - AppClient Socket Number
* Return Value         : GateWayInfo_t * - AppClient Information Structure pointer
* Function Description : Find AppClient Information structure..
******************************************************************************/
AppClientInfo_t *FindAppClient(int socket);

/******************************************************************************
* Function Name        : RemoveAppClient
* Input Parameters     : int socket      - Application Clinet Socket Number
* Return Value         : None
* Function Description : Remove Application Clinet Information structure..
******************************************************************************/
void RemoveAppClient(int socket);

void char_to_hex(char * dst, char * src, int size);

void hex_to_char(char * dst, char * src, int size);
#endif
