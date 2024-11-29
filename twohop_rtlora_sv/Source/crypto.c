/******************************************************************************
* Filename     : crypto.c                                                     *
* Program      : Private LoRa End Device class X MAC                          *
* Copyright    : Copyright (C) 2017-2017, ETRI                                *
*              : URL << http://www.etri.re.kr >>                              *
* Authors      : Woo-Sung Jung (woosung@etri.re.kr)                           *
*              : Tae Hyun Yoon (thyoon0820@etri.re.kr)                        *
*              : Dae Seung Yoo (ooseyds@etri.re.kr)                           *
* Description  : Private LoRa End Device class X MAC cryptography routine     *
* Created at   : Fri Jul 28 2017.                                             *
* Modified by  :                                                              *
* Modified at  :                                                              *
******************************************************************************/

#include <string.h>
#include <unistd.h>
#include "crypto.h"
#include "aes.h"
#include "aes_cmac.h"
#include "debug.h"

/******************************************************************************
* Function Name        : EncryptPayload
* Input Parameters     : uint8_t *dest             - Encrypted payload
*                      : uint8_t *src              - Original payload
*                      : uint16_t size             - Size of payload
*                      : uint8_t direction         - UPLINK/DOWNLINK
*                      : LoRaFrameHeader_t fHeader - DevAddress / FrameCounter
*                      : uint8_t port              - Port
*                      : uint8_t *NwkSKey          - NWKSKEY for port = 0
*                      : uint8_t *AppSKey          - APPSKEY for port 1..255
* Return Value         : None
* Function Description : Encrypt LoRa payload
******************************************************************************/
void EncryptPayload (uint8_t *dest, uint8_t *src, uint16_t size, uint8_t direction, LoRaFrameHeader_t fHeader, uint8_t port, uint8_t *nwkSKey, uint8_t *appSKey)
{
	uint8_t aBlocki[16], sBlocki[16];
	uint8_t blockCnt = 1;
	uint8_t len = 0;
	uint8_t loopCnt;
	uint8_t *key;

	// Get key
	if(port == 0)
	{
		// MAC command only port
		key = nwkSKey;
	}
	else
	{
		// Application port
		key = appSKey;
	}

	// Initialization aBlock
	// 0x01
	aBlocki[0] = 0x01;
	// 4 x 0x00
	aBlocki[1] = 0x00;
	aBlocki[2] = 0x00;
	aBlocki[3] = 0x00;
	aBlocki[4] = 0x00;
	// Direction
	aBlocki[5] = direction;
	// Dev Address
	memcpy(&aBlocki[6], &fHeader.DevAddr.Address, 4);
	// Frame Counter
	memcpy(&aBlocki[10], &fHeader.FrameCounter, 2);
	aBlocki[12] = 0x00;
	aBlocki[13] = 0x00;
	// 0x00
	aBlocki[14] = 0x00;

	// Generate encrypted frame
	while(size >= 16)
	{
		aBlocki[15] = blockCnt++;

		// AES Encrypt Block
		AESEncryptBlock(aBlocki, sBlocki, key);

		for(loopCnt = 0; loopCnt < 16; loopCnt++)
		{
			dest[len + loopCnt] = src[len + loopCnt] ^ sBlocki[loopCnt];
		}

		// update parameters
		size -= 16;
		len +=16;
	}
	// Remaining frame
	if(size > 0)
	{
		aBlocki[15] = blockCnt++;

		// AES Encrypt Block - same as ECB
		AESEncryptBlock(aBlocki, sBlocki, key);

		for(loopCnt = 0; loopCnt < size; loopCnt++)
		{
			dest[len + loopCnt] = src[len + loopCnt] ^ sBlocki[loopCnt];
		}
	}
}

/******************************************************************************
* Function Name        : DecryptPayload
* Input Parameters     : uint8_t *dest             - Encrypted payload
*                      : uint8_t *src              - Original payload
*                      : uint16_t size             - Size of payload
*                      : uint8_t direction         - UPLINK/DOWNLINK
*                      : LoRaFrameHeader_t fHeader - DevAddress / FrameCounter
*                      : uint8_t port              - Port
*                      : uint8_t *NwkSKey          - NWKSKEY for port = 0
*                      : uint8_t *AppSKey          - APPSKEY for port 1..255
* Return Value         : None
* Function Description : Decrypt LoRa payload -> same as Encryption
******************************************************************************/
void DecryptPayload (uint8_t *dest, uint8_t *src, uint16_t size, uint8_t direction, LoRaFrameHeader_t fHeader, uint8_t port, uint8_t *nwkSKey, uint8_t *appSKey)
{
	EncryptPayload (dest, src, size, direction, fHeader, port, nwkSKey, appSKey);
}

/******************************************************************************
* Function Name        : EncryptJoinAccept
* Input Parameters     : uint8_t *dest         - Encrypted Join Accept Message
*                      : uint8_t *src          - Original Join Accept Message
*                      : uint16_t size         - Size of Join Accept Message
*                      : uint8_t *key          - APPKEY 
* Return Value         : None
* Function Description : Encrypt LoRa Join Accept Message using AppKey (It is not APPSKEY)
******************************************************************************/
void EncryptJoinAccept(uint8_t *dest, uint8_t *src, uint16_t size, uint8_t *key)
{
	// The network server uses an AES decrypt operation in ECB mode to encrypt the
	// join-accept message so that the end-device can use an AES encrypt operation
	// to decrypt the message. This way an end-device only has to implement AES encrypt but not AES decrypt.
	AES_ECB_decrypt(src, key, dest, size);
}

/******************************************************************************
* Function Name        : DecryptJoinAccept
* Input Parameters     : uint8_t *dest         - Encrypted Join Accept Message
*                      : uint8_t *src          - Original Join Accept Message
*                      : uint16_t size         - Size of Join Accept Message
*                      : uint8_t *key          - APPKEY 
* Return Value         : None
* Function Description : Decrypt LoRa Join Accept Message using AppKey (It is not APPSKEY)
******************************************************************************/
void DecryptJoinAccept(uint8_t *dest, uint8_t *src, uint16_t size, uint8_t *key)
{
	// The network server uses an AES decrypt operation in ECB mode to encrypt the
	// join-accept message so that the end-device can use an AES encrypt operation
	// to decrypt the message. This way an end-device only has to implement AES encrypt but not AES decrypt.
	AES_ECB_encrypt(src, key, dest, size);
}

/******************************************************************************
* Function Name        : GenerateMIC
* Input Parameters     : uint8_t *msg              - MHDR | FHDR | FPORT | FRMPayload
*                      : uint8_t size              - size og msg
*                      : uint8_t direction         - UPLINK/DOWNLINK
*                      : LoRaFrameHeader_t fHeader - DevAddress / FrameCounter
*                      : uint8_t *NwkSKey          - NWKSKEY for port = 0
*                      : uint8_t *mic              - Message Integrity Code (Return Value)
* Return Value         : None
* Function Description : Calculate MIC
******************************************************************************/
void GenerateMIC (uint8_t *msg, uint16_t size, uint8_t direction, LoRaFrameHeader_t fHeader, uint8_t *nwkSKey, uint8_t *mic)
{
	uint8_t bBlocki[256+16];

	// Initialization bBlock
	// 0x49
	bBlocki[0] = 0x49;
	// 4 x 0x00
	bBlocki[1] = 0x00;
	bBlocki[2] = 0x00;
	bBlocki[3] = 0x00;
	bBlocki[4] = 0x00;
	// Direction
	bBlocki[5] = direction;
	// Dev Address
	memcpy(&bBlocki[6], &fHeader.DevAddr.Address, 4);
	// Frame Counter
	memcpy(&bBlocki[10], &fHeader.FrameCounter, 2);
	bBlocki[12] = 0x00;
	bBlocki[13] = 0x00;
	// 0x00
	bBlocki[14] = 0x00;
	// len(msg)
	bBlocki[15] = size;

	// b Block = b0 | msg
	memcpy(&bBlocki[16], msg, size);

	AES128_CMAC(nwkSKey, bBlocki, size+16, mic);
}

/******************************************************************************
* Function Name        : GenerateMICforJoinRequest
* Input Parameters     : uint8_t *msg              - MHDR | AppEUI | DevEUI | DevNounce
*                      : uint8_t *appKey          - Application Key
*                      : uint8_t *mic              - Message Integrity Code (Return Value)
* Return Value         : None
* Function Description : Calculate MIC
******************************************************************************/
void GenerateMICforJoinRequest (uint8_t *msg, uint8_t *appKey, uint8_t *mic)
{
	AES128_CMAC(appKey, msg, 19, mic);
}

/******************************************************************************
* Function Name        : GenerateMICforJoinRequest
* Input Parameters     : uint8_t *msg              - MHDR | AppEUI | DevEUI | DevNounce
*                      : uint8_t *appKey           - Application Key
*                      : uint8_t *mic              - Message Integrity Code (Return Value)
* Return Value         : None
* Function Description : Calculate MIC
******************************************************************************/
void GenerateMICforJoinResponse (uint8_t *msg, uint8_t *appKey, uint8_t *mic)
{
	AES128_CMAC(appKey, msg, 13, mic);
}







