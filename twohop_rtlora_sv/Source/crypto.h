/******************************************************************************
* Filename     : crypto.h                                                     *
* Program      : Private LoRa End Device class X MAC                          *
* Copyright    : Copyright (C) 2017-2017, ETRI                                *
*              : URL << http://www.etri.re.kr >>                              *
* Authors      : Woo-Sung Jung (woosung@etri.re.kr)                           *
*              : Tae Hyun Yoon (thyoon0820@etri.re.kr)                        *
*              : Dae Seung Yoo (ooseyds@etri.re.kr)                           *
* Description  : Private LoRa End Device class X MAC cryptography header file *
* Created at   : Fri Jul 28 2017.                                             *
* Modified by  :                                                              *
* Modified at  :                                                              *
******************************************************************************/

#ifndef __CRYPTO_H_
#define __CRYPTO_H_

#include "lora_mac.h"


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
void EncryptPayload (uint8_t *dest, uint8_t *src, uint16_t size, uint8_t direction, LoRaFrameHeader_t fHeader, uint8_t port, uint8_t *NwkSKey, uint8_t *AppSKey);

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
void DecryptPayload (uint8_t *dest, uint8_t *src, uint16_t size, uint8_t direction, LoRaFrameHeader_t fHeader, uint8_t port, uint8_t *nwkSKey, uint8_t *appSKey);

/******************************************************************************
* Function Name        : EncryptJoinAccept
* Input Parameters     : uint8_t *dest         - Encrypted Join Accept Message
*                      : uint8_t *src          - Original Join Accept Message
*                      : uint16_t size         - Size of Join Accept Message
*                      : uint8_t *key          - APPKEY
* Return Value         : None
* Function Description : Encrypt LoRa Join Accept Message using AppKey (It is not APPSKEY)
******************************************************************************/
void EncryptJoinAccept(uint8_t *dest, uint8_t *src, uint16_t size, uint8_t *key);

/******************************************************************************
* Function Name        : DecryptJoinAccept
* Input Parameters     : uint8_t *dest         - Encrypted Join Accept Message
*                      : uint8_t *src          - Original Join Accept Message
*                      : uint16_t size         - Size of Join Accept Message
*                      : uint8_t *key          - APPKEY 
* Return Value         : None
* Function Description : Decrypt LoRa Join Accept Message using AppKey (It is not APPSKEY)
******************************************************************************/
void DecryptJoinAccept(uint8_t *dest, uint8_t *src, uint16_t size, uint8_t *key);


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
void GenerateMIC (uint8_t *msg, uint16_t size, uint8_t direction, LoRaFrameHeader_t fHeader, uint8_t *nwkSKey, uint8_t *mic);

/******************************************************************************
* Function Name        : GenerateMICforJoinRequest
* Input Parameters     : uint8_t *msg              - MHDR | AppEUI | DevEUI | DevNounce
*                      : uint8_t *appKey           - Application Key
*                      : uint8_t *mic              - Message Integrity Code (Return Value)
* Return Value         : None
* Function Description : Calculate MIC
******************************************************************************/
void GenerateMICforJoinRequest (uint8_t *msg, uint8_t *appKey, uint8_t *mic);

/******************************************************************************
* Function Name        : GenerateMICforJoinRequest
* Input Parameters     : uint8_t *msg              - MHDR | AppEUI | DevEUI | DevNounce
*                      : uint8_t *appKey           - Application Key
*                      : uint8_t *mic              - Message Integrity Code (Return Value)
* Return Value         : None
* Function Description : Calculate MIC
******************************************************************************/
void GenerateMICforJoinResponse (uint8_t *msg, uint8_t *appKey, uint8_t *mic);

#endif // __CRYPTO_H_

