/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   packet_queue.h
 * Author: LAM-HOANG
 *
 * Created on June 4, 2021, 10:32 AM
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "packet_queue.h"
#include "trade.h"

void pktQueueInit(struct PktQueue *queue){
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_mutex_lock(&queue->mutex);
    queue->size = 0;
    queue->head = NULL;
    queue->tail = NULL;
    pthread_mutex_unlock(&queue->mutex);
}

bool isPktQueueFull(struct PktQueue *queue){
    bool result;

    pthread_mutex_lock(&queue->mutex);

    result = (queue->size == PKT_QUEUE_MAX) ? true : false;

    pthread_mutex_unlock(&queue->mutex);

    return result;
}

bool isPktQueueEmpty(struct PktQueue *queue) {
    bool result;

    pthread_mutex_lock(&queue->mutex);

    result = (queue->size == 0) ? true : false;

    pthread_mutex_unlock(&queue->mutex);

    return result;
}

PktError_e pktEnqueue(struct PktQueue *queue, struct MsgInfo_ *packet){
    struct PktNode *newPktNode;
    
    if(packet == NULL){
        MSG("ERROR: cannot enqueue packet, packet is invalid\n");
        return PKT_ERROR_INVALID;
    }
    
    if (isPktQueueFull(queue)) {
        MSG("ERROR: cannot enqueue packet, packet queue is full\n");
        return PKT_ERROR_FULL;
    }
    
    pthread_mutex_lock(&queue->mutex);
    newPktNode = (struct PktNode *)malloc(sizeof(struct PktNode));
    memcpy(&(newPktNode->pkt), packet, sizeof(struct MsgInfo_));
    
    if(queue->head == NULL){
        /* Queue is empty. Insert packet at the head of the queue */
        queue->head = newPktNode;
        queue->tail = newPktNode;
        newPktNode->next = NULL;
        newPktNode->prev = NULL;
    } else {
        /* Insert packet at the end of the queue */
        queue->tail->next = newPktNode;
        newPktNode->next = NULL;
        newPktNode->prev = queue->tail;
        queue->tail = newPktNode;
    }
    queue->size++;
    pthread_mutex_unlock(&queue->mutex);

    // printPktQueue(queue, false, DEBUG_PKT_QUEUE);

    MSG_DEBUG(DEBUG_PKT_QUEUE, "Enqueued packet succeeded. Queue size %hu\n", queue->size);

    return PKT_ERROR_OK;
}

PktError_e pktDequeue(struct PktQueue *queue, struct MsgInfo_ *packet){
    struct PktNode *tmpPktNode;
    
    if (packet == NULL) {
        MSG_DEBUG(DEBUG_PKT_QUEUE, "ERROR: invalid parameter\n");
        return PKT_ERROR_INVALID;
    }
    
    if (isPktQueueEmpty(queue)) {
        return PKT_ERROR_EMPTY;
    }
    
    pthread_mutex_lock(&queue->mutex);
    tmpPktNode = queue->head;
    queue->head = tmpPktNode->next;
    if(queue->head != NULL)
        queue->head->prev = NULL;
    
    queue->size--;
    
    memcpy(packet, &(tmpPktNode->pkt), sizeof(struct MsgInfo_));
    
    free(tmpPktNode);
     
    pthread_mutex_unlock(&queue->mutex);

    // printPktQueue(queue, false, DEBUG_PKT_QUEUE);

    MSG_DEBUG(DEBUG_PKT_QUEUE, "Dequeued packet succeed. Queue size %hu\n", queue->size);

    return PKT_ERROR_OK;
}

void printPktQueue(struct PktQueue *queue, bool show_all, int debug_level){
    MSG("TODO: implement printPktQueue\n");
//    struct PktNode_s *tmPktNode;
//    if(isPktQueueEmpty(queue) == true){
//        MSG_DEBUG(DEBUG_PKT_QUEUE, "Empty queue\n");
//        return;
//    }
//    
//    tmPktNode = queue->head;
//    while(tmPktNode != NULL){
//        MSG_DEBUG(DEBUG_PKT_QUEUE, "Node %d\n", tmPktNode->pkt.status);
//        tmPktNode = tmPktNode->next;
//    }
    return; 
}