/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
  (C)2013 Semtech-Cycleo

Description:
    LoRa concentrator : TX scheduling queue

License: Revised BSD License, see LICENSE.TXT file include in the project
Maintainer: Michael Coracin
*/

/* -------------------------------------------------------------------------- */
/* --- DEPENDANCIES --------------------------------------------------------- */

#define _GNU_SOURCE     /* needed for qsort_r to be defined */
#include <stdlib.h>     /* qsort_r */
#include <stdio.h>      /* printf, fprintf, snprintf, fopen, fputs */
#include <string.h>     /* memset, memcpy */
#include <pthread.h>
#include <assert.h>
#include <math.h>

#include "trace.h"
#include "jitqueue.h"
#include "time_conversion.h"

/* -------------------------------------------------------------------------- */
/* --- PRIVATE MACROS ------------------------------------------------------- */

/* -------------------------------------------------------------------------- */
/* --- PRIVATE CONSTANTS & TYPES -------------------------------------------- */
#define TX_MARGIN_DELAY_2       2000    /* in microsecond */

#define TX_START_DELAY          1500    /* microseconds */
                                        /* TODO: get this value from HAL? */
#define TX_MARGIN_DELAY         1000    /* Packet overlap margin in microseconds */
                                        /* TODO: How much margin should we take? */
#define TX_JIT_DELAY            10000   /* Pre-delay to program packet for TX in microseconds */
#define TX_MAX_ADVANCE_DELAY    (6 * 1E6) /* Maximum advance delay accepted for a TX packet, compared to current time */

#define BEACON_GUARD            3000000 /* Interval where no ping slot can be placed,
                                            to ensure beacon can be sent */
#define BEACON_RESERVED         2120000 /* Time on air of the beacon, with some margin */

#define TX_DEVIATION_THRESHOLD    500 /* Threshold to peek a packet out of queue */

/* -------------------------------------------------------------------------- */
/* --- PRIVATE VARIABLES (GLOBAL) ------------------------------------------- */
static pthread_mutex_t mx_jit_queue = PTHREAD_MUTEX_INITIALIZER; /* control access to JIT queue */

/* -------------------------------------------------------------------------- */
/* --- PRIVATE FUNCTIONS DEFINITION ----------------------------------------- */

/* -------------------------------------------------------------------------- */
/* --- PUBLIC FUNCTIONS DEFINITION ----------------------------------------- */

bool jit_queue_is_full(struct jit_queue_s *queue) {
    bool result;

    pthread_mutex_lock(&mx_jit_queue);

    result = (queue->num_pkt == JIT_QUEUE_MAX)?true:false;

    pthread_mutex_unlock(&mx_jit_queue);

    return result;
}

bool jit_queue_is_empty(struct jit_queue_s *queue) {
    bool result;

    pthread_mutex_lock(&mx_jit_queue);

    result = (queue->num_pkt == 0)?true:false;

    pthread_mutex_unlock(&mx_jit_queue);

    return result;
}

void jit_queue_init(struct jit_queue_s *queue) {
    int i;

    pthread_mutex_lock(&mx_jit_queue);

    memset(queue, 0, sizeof(*queue));
    for (i=0; i<JIT_QUEUE_MAX; i++) {
        queue->nodes[i].tx_timestamp.tv_sec = 0;
        queue->nodes[i].tx_timestamp.tv_usec = 0;
        queue->nodes[i].pre_delay = 0;
        queue->nodes[i].post_delay = 0;
    }

    pthread_mutex_unlock(&mx_jit_queue);
}

int compare(const void *a, const void *b, void *arg)
{
    struct jit_node_s *p = (struct jit_node_s *)a;
    struct jit_node_s *q = (struct jit_node_s *)b;
    int *counter = (int *)arg;
    struct timeval p_time, q_time;
    int return_val;
    
    p_time.tv_sec = p->tx_timestamp.tv_sec;
    p_time.tv_usec = p->tx_timestamp.tv_usec;

    q_time.tv_sec = q->tx_timestamp.tv_sec;
    q_time.tv_usec = q->tx_timestamp.tv_usec;

    if(timercmp(&p_time, &q_time, >))
        *counter = *counter + 1;
    
    return_val = -1;
    if(p_time.tv_sec >= q_time.tv_sec){
        if (p_time.tv_sec == q_time.tv_sec){
            if(p_time.tv_usec > q_time.tv_sec)
                return_val = 1;
            else
              return_val = -1;  
        } else {
            return_val = 1;
        }
    } else {
        return_val = -1;
    } 
   
    return return_val;
}

void jit_sort_queue(struct jit_queue_s *queue) {
    int counter = 0;

    if (queue->num_pkt == 0) {
        return;
    }

    MSG_DEBUG(DEBUG_JIT, "sorting queue in ascending order packet timestamp - queue size:%u\n", queue->num_pkt);
    qsort_r(queue->nodes, queue->num_pkt, sizeof(queue->nodes[0]), compare, &counter);
    MSG_DEBUG(DEBUG_JIT, "sorting queue done - swapped:%d\n", counter);
}

bool jit_collision_test(struct timeval p1_timestamp, uint32_t p1_pre_delay, uint32_t p1_post_delay, struct timeval p2_timestamp, uint32_t p2_pre_delay, uint32_t p2_post_delay) {
    struct timeval temp_time;
    if(timercmp(&p1_timestamp, &p2_timestamp, <= )){
        timersub(&p2_timestamp, &p1_timestamp, &temp_time);
        if ((uint32_t)(temp_time.tv_sec*1000000 + temp_time.tv_usec) <= (p2_pre_delay + p1_post_delay + TX_MARGIN_DELAY)) {
            return true;
        } else {
            return false;
        }
    } else {
        timersub(&p1_timestamp, &p2_timestamp, &temp_time);
        if ((uint32_t)(temp_time.tv_sec*1000000 + temp_time.tv_usec) <= (p1_pre_delay + p2_post_delay + TX_MARGIN_DELAY)) {
            return true;
        } else {
            return false;
        }
    }
}

enum jit_error_e jit_peek(struct jit_queue_s *queue, struct timeval *time, int *pkt_idx) {
    /* Return index of node containing a packet inline with given time */
    int i = 0;
    struct timeval current_time, delta_time;

    if (pkt_idx == NULL) {
        MSG("ERROR: invalid parameter\n");
        return JIT_ERROR_INVALID;
    }

    if (jit_queue_is_empty(queue)) {
        return JIT_ERROR_EMPTY;
    }

    pthread_mutex_lock(&mx_jit_queue);

    /* Search for highest priority packet to be sent */
    for (i=0; i<queue->num_pkt; i++) {
        /* First check if that packet is outdated:
         *  If a packet seems too much in advance, and was not rejected at enqueue time,
         *  it means that we missed it for peeking, we need to drop it
         *
         *  Warning: unsigned arithmetic
         *      t_packet > t_current + TX_MAX_ADVANCE_DELAY
         */
        gettimeofday(&current_time, NULL);
        if(timercmp(&current_time, &(queue->nodes[i].tx_timestamp), >)){
            /* We drop the packet to avoid lock-up */
            queue->num_pkt--;
            MSG("WARNING: --- Packet dropped (tx_timestamp=%ld.%06ld)\n",queue->nodes[i].tx_timestamp.tv_sec, \
                    queue->nodes[i].tx_timestamp.tv_sec);

            /* Replace dropped packet with last packet of the queue */
            memcpy(&(queue->nodes[i]), &(queue->nodes[queue->num_pkt]), sizeof(struct jit_node_s));
            memset(&(queue->nodes[queue->num_pkt]), 0, sizeof(struct jit_node_s));

            /* Sort queue in ascending order of packet timestamp */
            jit_sort_queue(queue);

            /* restart loop  after purge to find packet to be sent */
            i = 0;
            continue;
        }
    }
    gettimeofday(&current_time, NULL);
    timersub(&(queue->nodes[0].tx_timestamp), &current_time, &delta_time);
    if((delta_time.tv_sec*1000000 + delta_time.tv_usec) < TX_DEVIATION_THRESHOLD)
        *pkt_idx = 0;
    else
        *pkt_idx = -1;
    MSG_DEBUG(DEBUG_JIT, "peek packet with tx_timestamp=%ld.%06ld at index %d\n",\
            queue->nodes[0].tx_timestamp.tv_sec, queue->nodes[0].tx_timestamp.tv_usec, 0);

    pthread_mutex_unlock(&mx_jit_queue);

    return JIT_ERROR_OK;
}

enum jit_error_e jit_enqueue(struct jit_queue_s *queue, struct timeval time, struct lgw_pkt_tx_s *packet, enum jit_pkt_type_e pkt_type) {
    int i = 0;
    uint32_t packet_post_delay = 0;
    uint32_t packet_pre_delay = 0;
    uint32_t target_pre_delay = 0;
    enum jit_error_e err_collision;
    struct timeval tx_timestamp;
    struct timeval temp_time;
    bool insert_ok = true;

    if (packet == NULL) {
        MSG_DEBUG(DEBUG_JIT_ERROR, "ERROR: invalid parameter\n");
        return JIT_ERROR_INVALID;
    }

    if (jit_queue_is_full(queue)) {
        MSG_DEBUG(DEBUG_JIT_ERROR, "ERROR: cannot enqueue packet, JIT queue is full\n");
        return JIT_ERROR_FULL;
    }

    tx_timestamp.tv_sec = time.tv_sec;
    tx_timestamp.tv_usec = time.tv_usec;
           
    /* Compute packet pre/post delays depending on packet's type */
    packet_pre_delay = TX_START_DELAY + TX_JIT_DELAY;
    packet_post_delay = lgw_time_on_air(packet) * 1000UL; /* in us */

    pthread_mutex_lock(&mx_jit_queue);

    /* An immediate downlink becomes a timestamped downlink "ASAP" */
    /* Set the packet count_us to the first available slot */
//    if (packet->tx_mode == IMMEDIATE){
//        /* change tx_mode to timestamped */
//        temp_time.tv_sec = 0;
//        temp_time.tv_usec = TX_MARGIN_DELAY_2;
//        if(temp_time.tv_usec >= 1000000){
//            temp_time.tv_sec += temp_time.tv_usec/1000000;
//            temp_time.tv_usec -= temp_time.tv_sec * 1000000;
//        }
//        /* Search for the ASAP timestamp to be given to the packet */
//        TIMEVAL_ADD(tx_timestamp, temp_time);
//    }
    /* Check criteria_1: is it already too late to send this packet ?
     *  The packet should arrive at least at (tmst - TX_START_DELAY) to be programmed into concentrator
     *  Note: - Also add some margin, to be checked how much is needed, if needed
     *        - Valid for both Downlinks and Beacon packets
     *
     *  Warning: unsigned arithmetic (handle roll-over)
     *      t_packet < t_current + TX_START_DELAY + MARGIN
     */
    gettimeofday(&temp_time, NULL);
    temp_time.tv_usec += TX_MARGIN_DELAY + TX_JIT_DELAY;
    if (temp_time.tv_usec > 1000000){
        temp_time.tv_sec += (uint32_t) temp_time.tv_usec/1000000;
        temp_time.tv_usec = temp_time.tv_usec % 1000000;
    }
    
    if(timercmp(&tx_timestamp, &temp_time, <=)){
        MSG_DEBUG(DEBUG_JIT_ERROR, "ERROR: Packet REJECTED, already too late to send it\n");
        pthread_mutex_unlock(&mx_jit_queue);
        return JIT_ERROR_TOO_LATE;
    }
    
    gettimeofday(&temp_time, NULL);
    temp_time.tv_usec += TX_MAX_ADVANCE_DELAY;
    if (temp_time.tv_usec > 1000000){
        temp_time.tv_sec += (uint32_t) temp_time.tv_usec/1000000;
        temp_time.tv_usec = temp_time.tv_usec % 1000000;
    }
//    printf("tx_timestamp: %ld.%06ld\n", tx_timestamp.tv_sec, tx_timestamp.tv_usec);
//    printf("temp_time: %ld.%06ld\n", temp_time.tv_sec, temp_time.tv_usec);
    if(timercmp(&tx_timestamp, &temp_time, >=)){
        MSG_DEBUG(DEBUG_JIT_ERROR, "ERROR: Packet REJECTED, timestamp seems wrong, too much in advance\n");
        pthread_mutex_unlock(&mx_jit_queue);
        return JIT_ERROR_TOO_EARLY;
    }
    
    insert_ok = true;
    if (queue->num_pkt == 0) {
        /* If the jit queue is empty, we can insert this packet */
        MSG_DEBUG(DEBUG_JIT, "DEBUG: insert downlink, first in JiT queue (at %ld.%ld)\n", tx_timestamp.tv_sec, tx_timestamp.tv_usec);
    } else {
        /* The packet can be inserted to the queue if it does not collide with other packets */
        for (i = 0; i < queue->num_pkt; i++) {
            if (jit_collision_test(tx_timestamp, packet_pre_delay, packet_post_delay, queue->nodes[i].tx_timestamp,\
                queue->nodes[i].pre_delay, queue->nodes[i].post_delay) == true) {
                MSG_DEBUG(DEBUG_JIT, "DEBUG: cannot insert downlink at %ld.%ld, collides with %ld.%ld (index=%d)\n",\
                        tx_timestamp.tv_sec, tx_timestamp.tv_usec, queue->nodes[i].tx_timestamp.tv_sec, \
                        queue->nodes[i].tx_timestamp.tv_usec, i);
                
                break;
            }
        }

        if (i == queue->num_pkt) {
            /* No collision with ASAP time, we can insert it */
            MSG_DEBUG(DEBUG_JIT, "DEBUG: insert downlink at %ld.%ld (no collision)\n", tx_timestamp.tv_sec, tx_timestamp.tv_usec);
            insert_ok = true;
        } else {
            /* No collision with ASAP time, we can insert it */
            insert_ok = false;
        }
    }
        
    if(insert_ok == false){
        pthread_mutex_unlock(&mx_jit_queue);
        return JIT_ERROR_COLLISION_PACKET;
    }
  
    /* Finally enqueue it */
    /* Insert packet at the end of the queue */
    memcpy(&(queue->nodes[queue->num_pkt].pkt), packet, sizeof(struct lgw_pkt_tx_s));
    queue->nodes[queue->num_pkt].pre_delay = packet_pre_delay;
    queue->nodes[queue->num_pkt].post_delay = packet_post_delay;
    queue->nodes[queue->num_pkt].pkt_type = pkt_type;
    queue->nodes[queue->num_pkt].tx_timestamp.tv_sec = tx_timestamp.tv_sec;
    queue->nodes[queue->num_pkt].tx_timestamp.tv_usec = tx_timestamp.tv_usec;
    queue->num_pkt++;
    /* Sort the queue in ascending order of packet timestamp */
    jit_sort_queue(queue);

    /* Done */
    pthread_mutex_unlock(&mx_jit_queue);

    jit_print_queue(queue, false, DEBUG_JIT);

    MSG_DEBUG(DEBUG_JIT, "enqueued packet with timestamp=%ld.%06ld, (size=%u bytes, toa=%u us, type=%u)\n", \
            tx_timestamp.tv_sec, tx_timestamp.tv_usec, packet->size, packet_post_delay, pkt_type);

    return JIT_ERROR_OK;
}

enum jit_error_e jit_dequeue(struct jit_queue_s *queue, int index, struct lgw_pkt_tx_s *packet, enum jit_pkt_type_e *pkt_type) {
    if (packet == NULL) {
        MSG("ERROR: invalid parameter\n");
        return JIT_ERROR_INVALID;
    }

    if ((index < 0) || (index >= JIT_QUEUE_MAX)) {
        MSG("ERROR: invalid parameter\n");
        return JIT_ERROR_INVALID;
    }

    if (jit_queue_is_empty(queue)) {
        MSG("ERROR: cannot dequeue packet, JIT queue is empty\n");
        return JIT_ERROR_EMPTY;
    }

    pthread_mutex_lock(&mx_jit_queue);
    
    MSG_DEBUG(DEBUG_JIT, "dequeued packet with tx_timestamp=%ld.%06ld from index %d\n", \
            queue->nodes[index].tx_timestamp.tv_sec, queue->nodes[index].tx_timestamp.tv_usec, index);
            
    /* Dequeue requested packet */
    memcpy(packet, &(queue->nodes[index].pkt), sizeof(struct lgw_pkt_tx_s));
    queue->num_pkt--;
    *pkt_type = queue->nodes[index].pkt_type;

    /* Replace dequeued packet with last packet of the queue */
    memcpy(&(queue->nodes[index]), &(queue->nodes[queue->num_pkt]), sizeof(struct jit_node_s));
    memset(&(queue->nodes[queue->num_pkt]), 0, sizeof(struct jit_node_s));

    /* Sort queue in ascending order of packet timestamp */
    jit_sort_queue(queue);

    /* Done */
    pthread_mutex_unlock(&mx_jit_queue);

    jit_print_queue(queue, false, DEBUG_JIT);

    
    return JIT_ERROR_OK;
}

void jit_print_queue(struct jit_queue_s *queue, bool show_all, int debug_level) {
    int i = 0;
    int loop_end;

    if (jit_queue_is_empty(queue)) {
        MSG_DEBUG(debug_level, "INFO: [jit] queue is empty\n");
    } else {
        pthread_mutex_lock(&mx_jit_queue);

        MSG_DEBUG(debug_level, "INFO: [jit] queue contains %d packets:\n", queue->num_pkt);
        loop_end = (show_all == true) ? JIT_QUEUE_MAX : queue->num_pkt;
        for (i=0; i<loop_end; i++) {
            MSG_DEBUG(debug_level, " - node[%d]: timestamp=%ld.%06ld - type=%d\n",
                        i, queue->nodes[i].tx_timestamp.tv_sec, queue->nodes[i].tx_timestamp.tv_usec, queue->nodes[i].pkt_type);
        }

        pthread_mutex_unlock(&mx_jit_queue);
    }
}