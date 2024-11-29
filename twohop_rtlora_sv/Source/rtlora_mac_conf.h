/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   rtlora_mac_conf.h
 * Author: LAM-HOANG
 *
 * Created on June 2, 2021, 4:33 PM
 */

#ifndef RTLORA_MAC_CONF_H
#define RTLORA_MAC_CONF_H

#define TWOHOP_SERVER_ADDR              0xAEBE
#define TWOHOP_BROADCAST_ADDR           0xCAFE // put it in the destination address field

#define MAC_SHIFT_DELAY_MS              100 // The shift time between mac and lora server program (in milisecond)
                                            // The downlink packet generated from mac will be transmitted after MAC_SHIFT_DELAY

#define TWOHOP_MAX_NBO_CHILDREN         2

#define TWOHOP_NBO_PHASE_TRANS_PERIOD   6
#define TWOHOP_MAX_NBO_NODES_IN_RNL     20  // maximum must be 127
#define TWOHOP_MAX_NBO_NODES_IN_SM      31  // maximum must be 31
#define TWOHOP_MAX_NBO_RELAYS_IN_USI    15  // maximum must be 15

#define TWOHOP_NBO_SLOTS_IN_SCH1        15  // maximum must be 15

#define TWOHOP_RNL_INTERVAL_US          5000000      // RNL interval in milliseconds
#define TWOHOP_SCH1_SLOT_SIZE_US        200000       // SCH1 interval in milliseconds
#define TWOHOP_SCH2_SLOT_SIZE_US        100000      // 200 ms

//#define TWOHOP_UL_DATA_SLOT_SIZE_US     100000
//#define TWOHOP_DL_DATA_SLOT_SIZE_US     200000

//#define TWOHOP_NBO_CHANNELS             1
#define TWOHOP_MAX_NBO_CHANNELS         7

/* Channel 1 of GL, chan_multiSF_4 in global_conf.json file */
#define TWOHOP_CHANNEL_1                ((uint32_t)(922.1*1e6))    
/* Channel 2 of GL, chan_multiSF_5 in global_conf.json file */
//#define TWOHOP_CHANNEL_2                ((uint32_t)(919.1*1e6))    
/* Channel 1 of GH, chan_multiSF_6 in global_conf.json file */
//#define TWOHOP_CHANNEL_3                ((uint32_t)(918.9*1e6))  

#define TWOHOP_DOWNLINK_CHANNEL         TWOHOP_CHANNEL_1
#define TWOHOP_DL_POWER                 23
//#define TWOHOP_DL_FREQ_NB               1
//#define TWOHOP_DL_FREQ_STEP             0
//#define RTLORA_DL_DATARATE              7
//#define RTLORA_DL_BW_HZ                 125000

//#define RTLORA_DL_INFODESC                 0


#endif /* RTLORA_MAC_CONF_H */

