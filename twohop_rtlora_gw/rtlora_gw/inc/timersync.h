/*
 * Description: Two-hop RT-LoRa time synchronization with server
 * Author: Quy Lam Hoang
 * Email: quylam925@gmail.com
 * Created: 2021/05/24
 */


#ifndef _LORA_PKTFWD_TIMERSYNC_H
#define _LORA_PKTFWD_TIMERSYNC_H

/* -------------------------------------------------------------------------- */
/* --- DEPENDANCIES --------------------------------------------------------- */

#include <sys/time.h>    /* timeval */

/* -------------------------------------------------------------------------- */
/* --- PUBLIC FUNCTIONS PROTOTYPES ------------------------------------------ */

int get_concentrator_time(struct timeval *concent_time, struct timeval unix_time);

void thread_timersync(void);

#endif
