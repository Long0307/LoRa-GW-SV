/******************************************************************************
* Filename     : aes_cmac.c                                                   *
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

#include <stdint.h>
#include <string.h>
#include "aes_cmac.h"
#include "aes.h"
#include "debug.h"

/*
 * RFC 4493
   +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   +                   Algorithm AES-CMAC                              +
   +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   +                                                                   +
   +   Input    : K    ( 128-bit key )                                 +
   +            : M    ( message to be authenticated )                 +
   +            : len  ( length of the message in octets )             +
   +   Output   : T    ( message authentication code )                 +
   +                                                                   +
   +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   +   Constants: const_Zero is 0x00000000000000000000000000000000     +
   +              const_Bsize is 16                                    +
   +                                                                   +
   +   Variables: K1, K2 for 128-bit subkeys                           +
   +              M_i is the i-th block (i=1..ceil(len/const_Bsize))   +
   +              M_last is the last block xor-ed with K1 or K2        +
   +              n      for number of blocks to be processed          +
   +              r      for number of octets of last block            +
   +              flag   for denoting if last block is complete or not +
   +                                                                   +
   +   Step 1.  (K1,K2) := Generate_Subkey(K);                         +
   +   Step 2.  n := ceil(len/const_Bsize);                            +
   +   Step 3.  if n = 0                                               +
   +            then                                                   +
   +                 n := 1;                                           +
   +                 flag := false;                                    +
   +            else                                                   +
   +                 if len mod const_Bsize is 0                       +
   +                 then flag := true;                                +
   +                 else flag := false;                               +
   +                                                                   +
   +   Step 4.  if flag is true                                        +
   +            then M_last := M_n XOR K1;                             +
   +            else M_last := padding(M_n) XOR K2;                    +
   +   Step 5.  X := const_Zero;                                       +
   +   Step 6.  for i := 1 to n-1 do                                   +
   +                begin                                              +
   +                  Y := X XOR M_i;                                  +
   +                  X := AES-128(K,Y);                               +
   +                end                                                +
   +            Y := M_last XOR X;                                     +
   +            T := AES-128(K,Y);                                     +
   +   Step 7.  return T;                                              +
   +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
   */

/* For CMAC Calculation */
uint8_t const_Rb[16] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x87
};


/******************************************************************************
* Function Name        : SubkeyGeneration
* Input Parameters     : uint8_t *key             - Primary Key
*                      : uint8_t *KeyOne          - Sub Key 1
*                      : uint8_t *KeyTwo          - Sub Key 2
* Return Value         : None
* Function Description : Generate two sub key for CMAC
******************************************************************************/
void SubkeyGeneration(uint8_t *key, uint8_t *KeyOne, uint8_t *KeyTwo)
{
	uint8_t L[16];
	uint8_t zero[16];
	uint8_t keyTmp[16];
	int8_t loopCnt;
	uint8_t overflow;

    memset(zero, 0, 16);
    AESEncryptBlock(zero, L, key);

	if ( (L[0] & 0x80) == 0 ) {
    	// If MSB(L) = 0, then K1 = L << 1
		overflow = 0;
		for ( loopCnt = 15; loopCnt >= 0; loopCnt-- ) {
			KeyOne[loopCnt] = L[loopCnt] << 1;
			KeyOne[loopCnt] |= overflow;
			overflow = (L[loopCnt] & 0x80)?1:0;
		}
	} else {
		// Else K1 = ( L << 1 ) ^ Rb
		overflow = 0;
		for ( loopCnt = 15; loopCnt >= 0; loopCnt-- ) {
			keyTmp[loopCnt] = L[loopCnt] << 1;
			keyTmp[loopCnt] |= overflow;
			overflow = (L[loopCnt] & 0x80)?1:0;
		}
		for(loopCnt = 0; loopCnt < 16; loopCnt++)
		{
			KeyOne[loopCnt] = keyTmp[loopCnt] ^ const_Rb[loopCnt];
		}
	}

	if ( (KeyOne[0] & 0x80) == 0 ) {
		// If MSB(L) = 0, then K1 = L << 1
    	overflow = 0;
		for ( loopCnt = 15; loopCnt >= 0; loopCnt-- ) {
			KeyTwo[loopCnt] = KeyOne[loopCnt] << 1;
			KeyTwo[loopCnt] |= overflow;
			overflow = (KeyOne[loopCnt] & 0x80)?1:0;
		}
	} else {
		// Else K1 = ( L << 1 ) ^ Rb
		overflow = 0;
		for ( loopCnt = 15; loopCnt >= 0; loopCnt-- ) {
			keyTmp[loopCnt] = KeyOne[loopCnt] << 1;
			keyTmp[loopCnt] |= overflow;
			overflow = (KeyOne[loopCnt] & 0x80)?1:0;
		}
		for(loopCnt = 0; loopCnt < 16; loopCnt++)
		{
			KeyTwo[loopCnt] = keyTmp[loopCnt] ^ const_Rb[loopCnt];
		}
	}
	return;
}

/******************************************************************************
* Function Name        : SubkeyGeneration
* Input Parameters     : uint8_t *key             - Primary Key
*                      : uint8_t *msg             - msg = MHDR|FHDR|FPORT|FRMPayload
*                      : uint8_t size             - msg size
*                      : uint8_t *mic             - MIC code (return value)
* Return Value         : None
* Function Description : Generate two sub key for CMAC
******************************************************************************/
void AES128_CMAC(uint8_t *key, uint8_t *msg, uint8_t size, uint8_t *mic)
{
	uint8_t keyOne[16];
	uint8_t keyTwo[16];
	uint8_t nBlock;			// Number of Block;
	uint8_t completeFlag;	// Last block is complete block (order of 16)
	uint8_t mLast[16];
	uint8_t remainingSize;
	uint8_t X[16];
	uint8_t Y[16];
	uint8_t loopCnt;
	uint8_t blockCnt;


	// Step 1 : Generate Sub Key
	SubkeyGeneration(key, keyOne, keyTwo);

	// Step 2 : Get number of Blocks using "n := ceil(len/const_Bsize):
	// n is always more than 1 because we handle bBlock + msg
	nBlock = (uint8_t)((size+15)/16);	// ceil function
	remainingSize = size % 16;

	// Step 3
	if(nBlock == 0)
	{
		// We will never enter here... b Block Size is 16
		nBlock = 1;
		completeFlag = 0;
	}
	else
	{
		if(remainingSize == 0)
		{
			completeFlag = 1;
		}
		else
		{
			completeFlag = 0;
		}
	}

	// Step 4
	// Last block is complete block   - M_last := M_n XOR K1;
	// Last block is incomplete block - M_last := padding(M_n) XOR K2;

	if (completeFlag == 1)
	{
		for(loopCnt = 0; loopCnt < 16; loopCnt++)
		{
			mLast[loopCnt] = msg[16*(nBlock-1) + loopCnt] ^ keyOne[loopCnt];
		}
	}
	else
	{
		// Padding..
		for(loopCnt = 0; loopCnt < 16; loopCnt++)
		{
			if(loopCnt < remainingSize)
			{
				mLast[loopCnt] = msg[16*(nBlock-1) + loopCnt];	// Copy block
			}
			else if (loopCnt == remainingSize)
			{
				mLast[loopCnt] = 0x80;
			}
			else
			{
				mLast[loopCnt] = 0x0;
			}
			// XOR
			mLast[loopCnt] = mLast[loopCnt] ^ keyTwo[loopCnt];
		}
	}

	// Step 5 : X := const_Zero;
	memset(X, 0, 16);

	// Step 6 :  for i := 1 to n-1 do ->  Y := X XOR M_i; ,  X := AES-128(K,Y)
	// M_0...M_i-1
	for(blockCnt = 0; blockCnt < (nBlock - 1); blockCnt++)
	{
		dprintf("Step 6 [blockCnt=%d] - ", blockCnt);
		for(loopCnt = 0; loopCnt < 16; loopCnt++)
		{
			Y[loopCnt] = msg[16*blockCnt + loopCnt] ^ X[loopCnt];
			dprintfc("%02x ", Y[loopCnt]);
		}
		dprintfc("\n");
		AESEncryptBlock(Y, X, key);
	}

	//M_Last - Y := M_last XOR X;
	dprintf("Step 6 [blockCnt=%d] - ", blockCnt);
	for(loopCnt = 0; loopCnt < 16; loopCnt++)
	{
		Y[loopCnt] = mLast[loopCnt] ^ X[loopCnt];
		dprintfc("%02x ", Y[loopCnt]);
	}
	dprintfc("\n");

	//T := AES-128(K,Y);
	AESEncryptBlock(Y, X, key);

	// Step 7 : Calculate MIC
	for(loopCnt = 0; loopCnt < 4; loopCnt++)
	{
		mic[loopCnt] = X[loopCnt];
	}
}


