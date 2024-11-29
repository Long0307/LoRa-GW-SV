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
#include <stdint.h>  
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "application.h"
#include "debug.h"
#include "lora_mac.h"
#include <mysql/mysql.h>
#include <time.h>

#ifdef WEATHER_MONITORING_APP
#include "weather_device.h"
#endif

static uint32_t seqNum = 0;
extern uint8_t AppKey[16];
extern uint8_t AppNonce[3];

#if defined (LOG)
int logfdapp;										// LOG File Descriptor
char logmsgapp[512];
#endif

extern int guwbsocket;
MYSQL*connector;

/******************************************************************************
* Function Name        : InitApplication
* Input Parameters     : None
* Return Value         : None
* Function Description : Application Initialization Code
******************************************************************************/
void InitApplication(void)
{
#ifdef WEATHER_MONITORING_APP
    ws_app_init();
#endif
    // for other apps
}

/******************************************************************************
* Function Name        : DeInitApplication
* Input Parameters     : None
* Return Value         : None
* Function Description : Application DeInitialization Code
******************************************************************************/
void DeInitApplication(void)
{
#ifdef WEATHER_MONITORING_APP
    ws_app_deinit();
#endif
}

void AppInputDataHandle(uint16_t sender_id, uint8_t *data_buff, uint8_t data_len ){
#ifdef WEATHER_MONITORING_APP
//    uint8_t buff[WS_MAX_DATA_SIZE];
//    memcpy (buff, data_buff, data_len); 
    ws_update_raw_data(sender_id, data_buff, data_len);
#endif
}

void AppParseData(void){
#ifdef WEATHER_MONITORING_APP
//    struct timeval cur_time;
//    gettimeofday(&cur_time, NULL);
//    printf("START PARSE: %ld.%03ld\n", cur_time.tv_sec, cur_time.tv_usec / 1000);
    time_t current_time;
    time(&current_time);
    // parse raw data to meta data
    ws_parse_data(current_time);
//    gettimeofday(&cur_time, NULL);
//    printf("END PARSE: %ld.%03ld\n", cur_time.tv_sec, cur_time.tv_usec / 1000);
#endif
}

void AppPushDataToAppServer(void){
#ifdef WEATHER_MONITORING_APP
    ws_push_data_to_server();
#endif
}

void AppDisplayData(void){
#ifdef WEATHER_MONITORING_APP
    ws_print_meta_data();
#endif
}

/******************************************************************************
* Function Name        : GetAppNonce
* Input Parameters     : Nonce
* Return Value         : Application Nonce Value
* Function Description : Application Nonce Value return
******************************************************************************/
void GetAppNonce(uint8_t *nonce)
{
	memcpy(nonce, AppNonce, 3);
}

/******************************************************************************
* Function Name        : GetAppKey
* Input Parameters     : Nonce
* Return Value         : Application Key Value
* Function Description : Application Key Value return
******************************************************************************/
void GetAppKey(uint8_t *key)
{
	memcpy(key, AppKey, 16);
}

/******************************************************************************
* Function Name        : ApplicationRxHandler
* Input Parameters     : uint8_t *rx   - Received Application Data
*                      : uint32_t size - Received Application Payload Size
*                      : uint32_t address - End Device Address
* Return Value         : None
* Function Description : Receive Data via LoRa Communication
******************************************************************************/
//void ApplicationRxHandler(uint8_t *rx, uint32_t size, uint32_t address, uint32_t seq, int16_t rssi, int8_t snr, uint32_t channel, uint8_t sf)
void ApplicationRxHandler(uint8_t *rx, uint32_t size, uint32_t address, uint32_t seq, LoRaRxFrameInfo_t *rxFrame)
//void ApplicationRxHandler(uint8_t *rx, uint32_t size, uint8_t* address, uint32_t seq, LoRaRxFrameInfo_t *rxFrame)
{
	uint8_t data[BUFFER_SIZE];
	
	// Generate Rx Infomation to Application Client
	
	memcpy(data, rxFrame, RX_TUNNELING_OVERHEAD + size + 9 + 4 - 1);	// without EOF
	memcpy(&data[RX_TUNNELING_OVERHEAD + 9 - 1], rx, size);
	data[size+RX_TUNNELING_OVERHEAD+9+4-1] = END_OF_FRAME;
		
	SendtoAppClient(data, size+RX_TUNNELING_OVERHEAD+9+4);
	
#if 0	
	char query[1024];
	char timebuf[1024];
	char temp_address[8];
	char dev_address[12];
	struct tm *psttime;
	time_t ctime;
	
	// gas detector
	char O2[8];
	char H2S[8];
	char CO[8];
	char H2[8];
	char CH4[8];
	char temperature[8];
	char humidity[8];

	double O2_d;
	double H2S_d;
	double CO_d;
	double H2_d;
	double CH4_d;
	double temperature_d;
	double humidity_d;
	int comma_count = 0;
	int copy_count = 0;
	int k = 0;
	int sftrans = 0;
	
	if(USEDB == 1){
		// ETRI DB
		time(&ctime);
		psttime = localtime(&ctime);
		strftime(timebuf, 1024, "'%Y-%m-%d %H:%M:%S'", psttime);
		
		memset(dev_address,0,12);
		temp_address[0] = (address & 0xFF000000) >> 24;
		temp_address[1] = (address & 0x00FF0000) >> 16;
		temp_address[2] = (address & 0x0000FF00) >> 8;
		temp_address[3] = (address & 0x000000FF);

		char_to_hex(dev_address, temp_address, 4);

		// Store DB for Local test
		connector = mysql_init(NULL);
		if(!mysql_real_connect(connector,DBHOST,DBUSER,DBPASS,DBNAME,DPORT,NULL,0))
		{
				dprintf("%s\n", mysql_error(connector));
		}
		else{
			
			memset(query, 0, 1024);

			rx[size] = 0;

			//sprintf(query, "INSERT INTO Hanwha (address, seqnum, rssi, snr, freq, data, time) VALUES (%x, %d, %d, %d, %d, \"%s\", %s)", address, seq, rssi, snr, channel, rx, timebuf);
			sprintf(query, "INSERT INTO Hanwha (address, seqnum, rssi, snr, freq, data, time) VALUES ('%s', %d, %d, %d, %d, \"%s\", %s)", dev_address, seq, rssi, snr, channel, rx, timebuf);

			dprintf("%s", query);
			if(mysql_query(connector,query))
			{
					dprintf("%s\n",mysql_error(connector));
					dprintf("Write DB error\n");
			}
			else
			{
					dprintf("Write OK\n");
			}
			mysql_close(connector);
			
			// Send Data to connected Application Client
			SendtoAppClient(rx, size);		
		}
	}
	
	if(USEHHDB == 1){
		// HANHWA DB
		memset(O2,0,8);
		memset(H2S,0,8);
		memset(CO,0,8);
		memset(CH4,0,8);
		memset(H2,0,8);
		memset(temperature,0,8);
		memset(humidity,0,8);

		comma_count = 0;
		copy_count = 0;
		for(k = 0; k < size; k++){
			if(rx[k] == '|'){
				comma_count = rx[k+1] - 0x30;
				copy_count=0;
				k = k+2;								
			}
			
			if(comma_count == 1){
				O2[copy_count++] = rx[k];
			}
			else if(comma_count == 2){
				H2S[copy_count++] = rx[k];
			}
			else if(comma_count == 3){
				CO[copy_count++] = rx[k];
			}
			else if(comma_count == 4){
				CH4[copy_count++] = rx[k];
			}
			else if(comma_count == 5){
				H2[copy_count++] = rx[k];
			}
			else if(comma_count == 6){
				temperature[copy_count++] = rx[k];
			}
			else if(comma_count == 7){
				humidity[copy_count++] = rx[k];
			}

		}

		O2_d = atof(O2);
		H2S_d = atof(H2S);
		CO_d = atof(CO);
		H2_d = atof(CH4);
		CH4_d = atof(H2);
		temperature_d = atof(temperature)/10;
		humidity_d = atof(humidity);

/*
		switch (sf) {
			case 0x02:   sftrans = 7; break;
			case 0x04:   sftrans = 8; break;
			case 0x08:   sftrans = 9; break;
			case 0x10:  sftrans = 10; break;
			case 0x20:  sftrans = 11; break;
			case 0x40:  sftrans = 12; break;
			default:            sftrans = 7;
		}
*/		
		switch (sf) {
			case 2:   sftrans = 7; break;
			case 4:   sftrans = 8; break;
			case 8:   sftrans = 9; break;
			case 16:  sftrans = 10; break;
			case 32:  sftrans = 11; break;
			case 64:  sftrans = 12; break;
			default:            sftrans = 7;
		}
		

		//dprintf("'%.1f', '%.1f', '%.1f', '%.1f', '%.1f', '%.1f', '%.1f'\n",O2_d,H2S_d,CO_d,H2_d,CH4_d,temperature_d,humidity_d);
		//dprintf("'%+.0f', '%d', '%10u', '%s', '%+5.1f', '%s'\n",(double)rssi,7,channel,"OK", (double)snr,dev_address);

		memset(query,0,1024);
		connector = mysql_init(NULL);
		if(!mysql_real_connect(connector,HHDBHOST,HHDBUSER,HHDBPASS,HHDBNAME,HHDPORT,NULL,0))
		{
			dprintf("%s\n", mysql_error(connector));
		}
		else{

			//dprintf("DB_connect\n");
			sprintf(query,"CALL sp_insert_gas_log('%.1f', '%.1f', '%.1f', '%.1f', '%.1f', '%.1f', '%.1f', '%+.0f', '%d', '%10u', '%s', '%+5.1f', '%s')",O2_d,H2S_d,CO_d,H2_d,CH4_d,temperature_d,humidity_d,(double)rssi,sftrans,channel,"OK", (double)snr,dev_address);
			dprintf("%s\n", query);
			if(mysql_query(connector,query))
			{
				dprintf("%s\n",mysql_error(connector));
				dprintf("Write DB error\n");
			}
			else{
				dprintf("Write OK\n");
			}

			mysql_close(connector);
		}	
	}
#endif
}

void SendApplicationData(void)
{

	RWSMS_t appRWSMS;
	appRWSMS.seqNum = seqNum++;;
	appRWSMS.msgID = RULE_MSG;
	appRWSMS.pppID = 1;
	appRWSMS.wsID = 1;
	appRWSMS.ppeRule[0] = 0;
	appRWSMS.ppeRule[1] = 1;
	appRWSMS.ppeRule[2] = 2;
	appRWSMS.ppeRule[3] = 3;
	appRWSMS.ppeRule[4] = 4;

	uint8_t buffer[256];
	// Start of Frame
	buffer[0] = 'm';
	buffer[1] = 'i';
	buffer[2] = 'c';

	// Sequence Number
	buffer[3] = ((appRWSMS.seqNum/10) % 10) + '0';
	buffer[4] = (appRWSMS.seqNum % 10) + '0';

	// Msg ID
	buffer[5] = appRWSMS.msgID + '0';

	// PPP ID
	buffer[6] = ((appRWSMS.pppID/100) % 10) + '0';
	buffer[7] = ((appRWSMS.pppID/10) % 10) + '0';
	buffer[8] = (appRWSMS.pppID % 10) + '0';

	// wsID
	buffer[9] = appRWSMS.wsID + '0';

	// PPE Data
	buffer[10] = appRWSMS.ppeRule[0] + '0';
	buffer[11] = appRWSMS.ppeRule[1] + '0';
	buffer[12] = appRWSMS.ppeRule[2] + '0';
	buffer[13] = appRWSMS.ppeRule[3] + '0';
	buffer[14] = appRWSMS.ppeRule[4] + '0';

	// Reserved..
	buffer[15] = '0';
	buffer[16] = '0';

	// End of String
	buffer[17] = 'f';
	buffer[18] = 'i';
	buffer[19] = 'n';
	buffer[20] = 0;

	dprintf("Down Link Test..\n");
	dprintf("Send Payload: %s\n", buffer);

	// SendFrame(uint8_t *buffer, uint8_t size, uint8_t port, uint8_t ack_req, uint8_t direction, uint32_t nwkAddr, uint32_t nwkID)
	SendFrame(buffer, 20, 1, 0, 1, 0x0101, 0x0001);
}

/******************************************************************************
* Function Name        : ApplicationRxHandlerUWB
* Input Parameters     : uint8_t *rx   - Received Application Data
*                      : uint32_t size - Received Application Payload Size
*                      : uint32_t address - End Device Address
* Return Value         : None
* Function Description : Receive Data via UWB communication interface
******************************************************************************/
void ApplicationRxHandlerUWB(uint8_t *rx, uint32_t size, uint32_t id)
{
	int loopCnt;
	dprintf("Rx from UWB Master Node..(size=%d) to socket %d:\n", size, guwbsocket);
	dprintf(" %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
	 		rx[0], rx[1], rx[2], rx[3], rx[4], 
	 		rx[5], rx[6], rx[7], rx[8], rx[9]);
	write(guwbsocket, rx, size);

	if(id == 0x2000101)
		printf(" Rx from UWB (A%c-T%c from A2-3) :", rx[4], '0'+rx[5]);
	else if(id == 0x2000102)
		printf(" Rx from UWB (A%c-T%c from A2-4) :", rx[4], '0'+rx[5]);
	else if(id == 0x2000103)
		printf(" Rx from UWB (A%c-T%c from A1-3) :", rx[4], '0'+rx[5]);
	else if(id == 0x2000104)
		printf(" Rx from UWB (A%c-T%c from A1-4) :", rx[4], '0'+rx[5]);

	for(loopCnt = 0; loopCnt < size; loopCnt ++)
	{
		printf("%02x ", rx[loopCnt]);
	}
	printf("\n");
}

/*
*********************************************************************************************************
*  Application Client Related Codes are here..
*********************************************************************************************************
*/

AppClientInfo_t AC_HEAD;

/******************************************************************************
* Function Name        : InitGateWayInfo
* Input Parameters     : None
* Return Value         : None
* Function Description : AppClient Management Linked-List Structure Initialization..
******************************************************************************/
void InitAppClientInfo(void)
{
	AC_HEAD.next = NULL;
}

/******************************************************************************
* Function Name        : InitGateWayInfo
* Input Parameters     : None
* Return Value         : None
* Function Description : AppClient Management Linked-List Structure Deinitialization..
******************************************************************************/
void DeinitAppClientInfo(void)
{
	AppClientInfo_t *acInfo;
	while(AC_HEAD.next != NULL)
	{
		acInfo = AC_HEAD.next;
		AC_HEAD.next = acInfo->next;
		//close socket all remaining client..
		close(acInfo->socket);
		free(acInfo);
	}
}

/******************************************************************************
* Function Name        : SendtoAllApplication
* Input Parameters     : None
* Return Value         : None
* Function Description : Send LoRa Pkt to All Application Client
******************************************************************************/
void SendtoAppClient(uint8_t *data, uint32_t size)
{
	AppClientInfo_t *acInfo;
	
	acInfo = &AC_HEAD;
	while(acInfo->next != NULL)
	{
		acInfo = acInfo->next;
		// Send LoRa pkt to Application Client
		write(acInfo->socket, data, size);
	}
}

/******************************************************************************
* Function Name        : AddAppClient
* Input Parameters     : int clntsocket                 - AppClient Socket Number
*                      : struct sockaddr_in clntAddress - AppClient Address Info
* Return Value         : None
* Function Description : New AppClient registration..
******************************************************************************/
void AddAppClient(int clntsocket, struct sockaddr_in clntAddress)
{
	AppClientInfo_t *acInfo;
	// Find exist AppClient Information using socket number..
	acInfo = FindAppClient(clntsocket);

	if(acInfo == NULL)
	{
		// New AppClient connects to Network Server..
		acInfo = malloc(sizeof(AppClientInfo_t));
		if(acInfo == NULL)
		{
			dprintf("Memory allocation fail!!\n");
			return;
		}
		dprintf("Add New Application Client Information - socket number = %d, IP = %s\n", clntsocket, inet_ntoa(clntAddress.sin_addr));
		acInfo->socket = clntsocket;
		acInfo->sockaddr = clntAddress;
		acInfo->next = AC_HEAD.next;
		AC_HEAD.next = acInfo;
	}
	else
	{
		// Never enter here..
		dprintf("New connection socket is already used for other Application Server\n");
		acInfo->socket = clntsocket;
		acInfo->sockaddr = clntAddress;
		return;
	}
		
}

/******************************************************************************
* Function Name        : FindGateWay
* Input Parameters     : int socket      - AppClient Socket Number
* Return Value         : GateWayInfo_t * - AppClient Information Structure pointer
* Function Description : Find AppClient Information structure..
******************************************************************************/
AppClientInfo_t *FindAppClient(int socket)
{
	AppClientInfo_t *acInfo;
	acInfo = AC_HEAD.next;

	while(acInfo != NULL)
	{
		if(acInfo->socket == socket)
		{
			dprintf("Find Application Client Information\n");
			return acInfo;
		}
		acInfo = acInfo->next;
	}
	return NULL;
}

/******************************************************************************
* Function Name        : RemoveAppClient
* Input Parameters     : int socket      - Application Clinet Socket Number
* Return Value         : None
* Function Description : Remove Application Clinet Information structure..
******************************************************************************/
void RemoveAppClient(int socket)
{
	AppClientInfo_t *acInfo;
	AppClientInfo_t *acInfo_prev;
	acInfo = AC_HEAD.next;
	acInfo_prev = &AC_HEAD;

	while(acInfo != NULL)
	{
		if(acInfo->socket == socket)
		{
			dprintf("Gateway(socket = %d, IP = %s) Information will be removed..\n", acInfo->socket, inet_ntoa(acInfo->sockaddr.sin_addr));
			acInfo_prev->next = acInfo->next;
			free(acInfo);
			
			return;
		}
		acInfo_prev = acInfo;
		acInfo = acInfo->next;
	}
	
	dprintf("We cannot find Gateway information..\n");
}

void char_to_hex(char * dst, char * src, int size){
	int i;
	
	for( i = 0; i < size ; i++){
		if((src[i] >> 4) > 9){
			dst[(i*2)] = ((src[i] >> 4) & 0x0F) + 0x37;
		}
		else{
			dst[(i*2)] = ((src[i] >> 4) & 0x0F) + 0x30;
		}
		
		if((src[i] & 0x0F) > 9){
			dst[(i*2)+1] = (src[i] & 0x0F) + 0x37;
		}
		else{
			dst[(i*2)+1] = (src[i] & 0x0F) + 0x30;
		}
	}
	
}

void hex_to_char(char * dst, char * src, int size){
	int i;
	
	for( i = 0; i < size ; i++){
		
		if(src[i*2] > 0x39){
			dst[i] = ((src[i*2] - 0x37) << 4) & 0xF0;
		} 
		else{
			dst[i] = ((src[i*2] - 0x30) << 4) & 0xF0;
		}
		
		if(src[(i*2)+1] > 0x39){
			dst[i] = dst[i] | ((src[(i*2)+1] - 0x37) & 0x0F);
		} 
		else{
			dst[i] = dst[i] | ((src[(i*2)+1] - 0x30) & 0x0F);
		}					
	}
}

