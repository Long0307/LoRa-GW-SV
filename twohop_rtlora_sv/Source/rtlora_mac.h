/*
 * Description: Two-hop RT-LoRa MAC protocol source file
 * Author: Quy Lam Hoang
 * Email: quylam925@gmail.com
 * Created: 2021/05/24
 */

#ifndef RTLORA_MAC_H
#define RTLORA_MAC_H
   
#include <stdbool.h>    /* bool type */
#include <time.h>       /* time clock_gettime strftime gmtime clock_nanosleep*/
    
#ifndef VERSION_STRING
#define VERSION_STRING "undefined"
#endif

#define TWOHOP_NBO_SCH_UPDATE_SD        1 // number of schedule update during Schedule Distribution phase

#define TWOHOP_NBO_SCH_UPDATE_DC        3 // number of schedule update during Data Collection phase

#define TWOHOP_NBO_SCH_UPDATE_DEFAULT   TWOHOP_NBO_SCH_UPDATE_SD

#define TWOHOP_MAX_MISS_DATA_ALLOWED    65535

/* values available for the 'modulation' parameters */
/* NOTE: arbitrary values */
#define MOD_UNDEFINED   0
#define MOD_LORA        0x10
#define MOD_FSK         0x20
    
/* values available for the 'bandwidth' parameters (LoRa & FSK) */
/* NOTE: directly encode FSK RX bandwidth, do not change */
#define BW_UNDEFINED    0
#define BW_500KHZ       0x01
#define BW_250KHZ       0x02
#define BW_125KHZ       0x03    

/* values available for the 'datarate' parameters */
/* NOTE: LoRa values used directly to code SF bitmask in 'multi' modem, do not change */
#define DR_UNDEFINED    0
#define DR_LORA_SF7     0x02
#define DR_LORA_SF8     0x04
#define DR_LORA_SF9     0x08
#define DR_LORA_SF10    0x10
#define DR_LORA_SF11    0x20
#define DR_LORA_SF12    0x40
#define DR_LORA_MULTI   0x7E

/* values available for the 'coderate' parameters (LoRa only) */
/* NOTE: arbitrary values */
#define CR_UNDEFINED    0
#define CR_LORA_4_5     0x01
#define CR_LORA_4_6     0x02
#define CR_LORA_4_7     0x03
#define CR_LORA_4_8     0x04

/* values available for the 'status' parameter */
/* NOTE: values according to hardware specification */
#define STAT_UNDEFINED  0x00
#define STAT_NO_CRC     0x01
#define STAT_CRC_BAD    0x11
#define STAT_CRC_OK     0x10

/* values available for the 'tx_mode' parameter */
#define IMMEDIATE       0
#define TIMESTAMPED     1
#define ON_GPS          2

void twohopLoRaMacInit(void);

void twohopLoRaMacDeInit(void);

//int RtLoRaGetReadyDownlinkPacket(struct pkt_dl_s *pkt);
//
//void rtlora_receive_frame_handle(struct pkt_ul_s *p);

#endif /* RTLORA_MAC_H */

