/******************************************************************************
* Filename     : debug.h                                                      *
* Program      : Private LoRa Debug Print Macro                               *
* Copyright    : Copyright (C) 2017-2017, ETRI                                *
*              : URL << http://www.etri.re.kr >>                              *
* Authors      : Woo-Sung Jung (woosung@etri.re.kr)                           *
*              : Tae Hyun Yoon (thyoon0820@etri.re.kr)                        *
*              : Dae Seung Yoo (ooseyds@etri.re.kr)                           *
* Description  : Debug printf Macro Definition                                *
* Created at   : Mon Jul. 10. 2017.                                           *
* Modified by  :                                                              *
* Modified at  :                                                              *
******************************************************************************/

#ifndef debug_h
#define debug_h

#include <stdio.h>

#ifdef DEBUG 
#define dprintf(fmt, args...) fprintf(stderr, "DEBUG: %s:%d:%s(): " fmt, \
    __FILE__, __LINE__, __func__, ##args)
#define dprintfc(fmt, args...) fprintf(stderr, " " fmt, ##args)

#else
#define dprintf(fmt, args...) /* Don't do anything in release builds */
#define dprintfc(fmt, args...) /* None */

#endif


#endif // End of #ifndef debug_h
