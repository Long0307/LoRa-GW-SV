/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   trace.h
 * Author: lam.hq
 *
 * Created on May 20, 2020, 3:45 PM
 */

#ifndef RTLORA_GW_TRACE_H
#define RTLORA_GW_TRACE_H

#define DEBUG_RTLORA_GW     1
#define DEBUG_JIT           0
#define DEBUG_JIT_ERROR     1
#define DEBUG_TIMERSYNC     1
#define DEBUG_BEACON        0
#define DEBUG_LOG           1


#define MSG(args...) printf(args) /* message that is destined to the user */
#define MSG_DEBUG(FLAG, fmt, ...)                                                                         \
            do  {                                                                                         \
                if (FLAG)                                                                                 \
                    fprintf(stdout, "%s:%d:%s(): " fmt, __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
            } while (0)

#endif /* TRACE_H */

