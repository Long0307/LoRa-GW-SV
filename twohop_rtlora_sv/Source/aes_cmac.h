/******************************************************************************
* Filename     : aes_cmac.h                                                   *
* Program      : Private LoRa End Device class X MAC                          *
* Copyright    : Copyright (C) 2017-2017, ETRI                                *
*              : URL << http://www.etri.re.kr >>                              *
* Authors      : Woo-Sung Jung (woosung@etri.re.kr)                           *
*              : Tae Hyun Yoon (thyoon0820@etri.re.kr)                        *
*              : Dae Seung Yoo (ooseyds@etri.re.kr)                           *
* Description  : Private LoRa AES-CMAC Implementation for MIC                 *
* Created at   : Wed Aug 02 2017.                                             *
* Modified by  :                                                              *
* Modified at  :                                                              *
******************************************************************************/

#ifndef __AES_CMAC_H
#define __AES_CMAC_H

/******************************************************************************
* Function Name        : SubkeyGeneration
* Input Parameters     : uint8_t *key             - Primary Key
*                      : uint8_t *KeyOne          - Sub Key 1
*                      : uint8_t *KeyTwo          - Sub Key 2
* Return Value         : None
* Function Description : Generate two sub key for CMAC
******************************************************************************/
void SubkeyGeneration(uint8_t *key, uint8_t *KeyOne, uint8_t *KeyTwo);

/******************************************************************************
* Function Name        : SubkeyGeneration
* Input Parameters     : uint8_t *key             - Primary Key
*                      : uint8_t *msg             - msg = MHDR|FHDR|FPORT|FRMPayload
*                      : uint8_t size             - msg size
*                      : uint8_t *mic             - MIC code (return value)
* Return Value         : None
* Function Description : Generate two sub key for CMAC
******************************************************************************/
void AES128_CMAC(uint8_t *key, uint8_t *msg, uint8_t size, uint8_t *mic);

#endif


