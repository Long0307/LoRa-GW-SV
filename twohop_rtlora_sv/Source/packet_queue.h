/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   packet_queue.h
 * Author: LAM-HOANG
 * Description: 
 *          TX and RX packet queue
 * Created on June 4, 2021, 10:32 AM
 */

#ifndef PACKET_QUEUE_H
#define PACKET_QUEUE_H

/* --- DEPENDANCIES --------------------------------------------------------- */

#include <stdint.h>     /* C99 types */
#include <stdbool.h>    /* bool type */
#include <pthread.h>
#include <sys/time.h>

/* --- PUBLIC CONSTANTS ----------------------------------------------------- */
#define PKT_QUEUE_MAX           16  /* Maximum number of packets to be stored in queue */

/* --- PUBLIC TYPES --------------------------------------------------------- */

typedef enum PktError_ {
    PKT_ERROR_OK,           /* Packet is ok to be transmit/receive */
    PKT_ERROR_FULL,         /* Packet queue is full */
    PKT_ERROR_EMPTY,        /* Packet queue is empty */
    PKT_ERROR_INVALID       /* Packet is invalid */
}PktError_e;

// Include packet metadata, packet payload and payload size (size)
typedef struct MsgInfo_{
    /* Downlink packet only*/
    uint8_t     tx_mode;        /*!> select on what event/time the TX is triggered */
    int8_t      rf_power;       /*!> TX power, in dBm */
    bool        invert_pol;     /*!> invert signal polarity, for orthogonal downlinks (LoRa only) */
    uint16_t    preamble;       /*!> set the preamble length, 0 for default */
    struct timeval msg_tx_time; /*!> absolute time at which the GW has to transmit the msg */
    
    /* Uplink packet only*/
    uint8_t     if_chain;       /*!> by which IF chain was packet received or will be transmitted */
    uint8_t     status;         /*!> status of the received packet */
    uint32_t    count_us;       /*!> internal concentrator counter for timestamping, 1 microsecond resolution */
    int         sock;           /*!> socket in which the packet is received */
    float       rssi;           /*!> average packet RSSI in dB */
    float       snr;            /*!> average packet SNR, in dB (LoRa only) */
    uint16_t    crc;            /*!> CRC that was received in the payload */
    
    /* Common fields */
    uint32_t    freq;           /*!> center frequency of TX */
    uint8_t     rf_chain;       /*!> through which RF chain the packet was received or will be transmitted */
    uint8_t     modulation;     /*!> modulation LoRa or FSK */
    uint8_t     bandwidth;      /*!> modulation bandwidth (LoRa only) */
    uint32_t    datarate;       /*!> TX datarate (baudrate for FSK, SF for LoRa) */
    uint8_t     coderate;       /*!> error-correcting code of the packet (LoRa only) */
    uint16_t    size;           /*!> payload size in bytes */
    uint8_t     payload[256];   /*!> buffer containing the payload */
}MsgInfo_s;

struct PktNode{
    struct MsgInfo_ pkt;
    struct PktNode *next;
    struct PktNode *prev;
};

struct PktQueue{
    uint8_t size;
    pthread_mutex_t mutex; /* control access to packet queue */
    struct PktNode *head;
    struct PktNode *tail;
};

/* --- PUBLIC FUNCTIONS PROTOTYPES ------------------------------------------ */

/**
@brief Initialize a packet queue.
@param queue[in] Packet queue to be initialized. Memory should have been allocated already.
This function is used to reset every elements in the allocated queue.
*/
void pktQueueInit(struct PktQueue *queue);

/**
@brief Check if a packet queue is full.
@param queue[in] Packet queue to be checked.
@return true if queue is full, false otherwise.
*/
bool isPktQueueFull(struct PktQueue *queue);

/**
@brief Check if a packet queue is empty.
@param queue[in] Packet queue to be checked.
@return true if queue is empty, false otherwise.
*/
bool isPktQueueEmpty(struct PktQueue *queue);

/**
@brief Add a packet in a packet queue (at the END of the queue)
@param queue[in/out] Packet queue in which the packet should be inserted
@param packet[in] Packet to be queued in TAIL position of the queue
@return success if the function was able to queue the packet
*/
PktError_e pktEnqueue(struct PktQueue *queue, struct MsgInfo_ *packet);

/**
@brief Dequeue a packet from a packet queue at the HEAD position
@param queue[in/out] Packet queue from which the packet should be removed
@param packet[out] that at the head of the queue
@return success if the function was able to dequeue the packet
*/
PktError_e pktDequeue(struct PktQueue *queue, struct MsgInfo_ *packet);

/**
@brief Debug function to print the queue's content on console
@param queue[in] Packet queue to be displayed
@param show_all[in] Indicates if empty nodes have to be displayed or not
*/
void printPktQueue(struct PktQueue *queue, bool show_all, int debug_level);

#endif /* PACKET_QUEUE_H */

