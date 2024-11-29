/******************************************************************************
* Filename     : conf.h                                                       *
* Program      : Private LoRa                                                 *
* Copyright    : Copyright (C) 2017-2017, ETRI                                *
*              : URL << http://www.etri.re.kr >>                              *
* Authors      : Woo-Sung Jung (woosung@etri.re.kr)                           *
*              : Tae Hyun Yoon (thyoon0820@etri.re.kr)                        *
*              : Dae Seung Yoo (ooseyds@etri.re.kr)                           *
* Description  : Private LoRa Configuration Setting                           *
* Created at   : Mon Mar 27 2017.                                             *
* Modified by  : Woo-Sung, Jung,                                              *
* Modified at  : Mon Feb. 25, 2019                                            *
******************************************************************************/

#ifndef __CONF_H_
#define __CONF_H_

// Server related ..
// Defines..
//#define UWB_SERVICE
//#define HHI_DEMO

#define LORA_NETWORK_WELCOME_SERVER_PORT                        8000		// Default LoRa Gateway Welcome Server Port
//#define APPLICATION_WELCOME_SERVER_PORT			8001		// Default Application Welcome Server Port
#define LORA_NETWORK_SERVER_VERSION				"v1.3"

#define PROTOCOL_VERSION        2  /* v1.3 */

//#define LOG
#define MONITORING_INTERVAL	30

#define Nwk_ID									0x0001	// [31..25]

#define	RECEIVE_DELAY1							1000		// 1 sec

#define UWBDEVICE "/dev/ttyUSB0"
#define BAUDRATE B115200

// Application Port lists.. move to user application
#define APP_PORT							2		// 1..223
#define APP_PORT_GDD						3
#define APP_PORT_UWB						223


#endif // end of  __CONF_H_

