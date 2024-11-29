/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   schedule_mngt.h
 * Author: LAM-HOANG
 *
 * Created on June 11, 2021, 8:53 AM
 */

#ifndef SCHEDULE_MNGT_H
#define SCHEDULE_MNGT_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "rtlora_mac_conf.h"

typedef enum SchErr_{
    SCH_SUCCEEDED,
    SCH_FAILED,
}SchErr_t;

typedef struct SchNode_{
    unsigned short addr;
    unsigned short class;
    unsigned short startLSI;
    unsigned short slotDemand;
    unsigned short nboSchDist;   // Count the number of schedule distribution
                                // nboUpdate = 0: no more schedule distribution needed
                                // nboUpdate = x, x > 0: the schedule for this node
                                // must be distributed x times more
//    _Bool distFlag;     // indicate the schedule for this node has been distributed or not
//                        // false: not distributed yet
//                        // true: already distributed
    struct SchNode_ *next;
}SchNode_t;

typedef struct SchList_{
    SchNode_t *head;
    unsigned int nboNode;  // Number of one hop members
    unsigned int nboTotSlots;   // Number of total slots
    unsigned int nboAsgSlots;   // Number of assigned slots
    unsigned int nboRmnSlots;   // Number of remaining slots
    unsigned int nboDistReq;    // Number of schedule node need to be distributed
}SchList_t;

void smInitSchedule(SchList_t *list, unsigned int nboTotSlots);

// Schedule one hop node to one of the schedule groups
SchErr_t smScheduleOneNode(SchList_t *list, SchNode_t newNode);

void smRemoveOneNode(SchList_t *list, unsigned short addr);

void smClearSchedule(SchList_t *list);

SchNode_t* smGetRefToNode(SchList_t *list, unsigned short addr);

SchNode_t* smGetHeadNodeRef(SchList_t *list);

SchNode_t* smGetNextNodeRef(SchNode_t* curNode);

//SchNode_t* smGetRefToFirstNodeForSchDist(SchList_t *list);

void smNodeSetNboSchDist(SchList_t *list, unsigned short addr, uint8_t nboSchDist);

unsigned short smGetLastAsgLsi(SchList_t *list);

void smPrintSchedule(SchList_t *list);

#endif /* SCHEDULE_MNGT_H */