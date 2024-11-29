/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   trade.h
 * Author: LAM-HOANG
 *
 * Created on June 4, 2021, 11:34 AM
 */

#ifndef _RTLORA_TRADE_H
#define _RTLORA_TRADE_H

#include <stdint.h>     /* C99 types */
#include <stdbool.h>    /* bool type */
#include <stdio.h>      /* printf fprintf sprintf fopen fputs */
#include <pthread.h>

#include <time.h>       /* time clock_gettime strftime gmtime clock_nanosleep*/

#define DEBUG_RTLORA_MAC        1
#define DEBUG_DEVICE_MNGT       0
#define DEBUG_PKT_QUEUE         0
#define DEBUG_SCH_MNGT          0
#define DEBUG_LOG               1
 
#define MSG(args...) printf(args) /* message that is destined to the user */
#define MSG_DEBUG(FLAG, fmt, ...)                                                                         \
            do  {                                                                                         \
                if (FLAG)                                                                                 \
                    fprintf(stdout, "%s:%d:%s(): " fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
            } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

_Bool open_log(void);

#endif /* _RTLORA_TRADE_H */

