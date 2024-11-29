/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   schedule_mngt.c
 * Author: LAM-HOANG
 *
 * Created on June 11, 2021, 8:53 AM
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "schedule_mngt.h"
#include "trade.h"

static unsigned short smAssignLsiToNode(SchList_t *list, unsigned short demandSlot);

static SchNode_t* smAddNodeToScheduleList(SchList_t *list, SchNode_t node);

extern FILE * log_file;

void smInitSchedule(SchList_t *list, unsigned int nboTotSlots){
    list->head = NULL;
    list->nboNode = 0;
    list->nboTotSlots = nboTotSlots;
    list->nboRmnSlots = 0;
    list->nboAsgSlots = 0;
    list->nboDistReq = 0;
}

SchErr_t smScheduleOneNode(SchList_t *list, SchNode_t node){
    SchNode_t *tmpNode;
    
    node.startLSI = smAssignLsiToNode(list, node.slotDemand);
    if(node.startLSI == 0){
        return SCH_FAILED;
    }
    
    tmpNode = smAddNodeToScheduleList(list, node);
            
    if(tmpNode != NULL){
        MSG_DEBUG(DEBUG_SCH_MNGT,"NODE %u: Scheduled to LSI=%u\n", tmpNode->addr, tmpNode->startLSI);
        fprintf(log_file, "NODE %u: Scheduled to LSI=%u\n", tmpNode->addr, tmpNode->startLSI);
        return SCH_SUCCEEDED;
    } else
        return SCH_FAILED;
}

void smRemoveOneNode(SchList_t *list, unsigned short addr){
    SchNode_t *curNode, *prevNode;
    
    prevNode = NULL;
    curNode = list->head;
    while(true){
        if(curNode == NULL)
            break;
        
        if(curNode->addr == addr){ // curNode will be removed
            if(prevNode == NULL){
                list->head = curNode->next;
            } else{
                prevNode->next = curNode->next;
                curNode->next = NULL;
            }
            
            list->nboNode--;
            list->nboRmnSlots += curNode->slotDemand;
            list->nboAsgSlots -= curNode->slotDemand;
            if(curNode->nboSchDist > 0)
                list->nboDistReq--;
            
            free(curNode);
            break;
        } else {
            prevNode = curNode;
            curNode = curNode->next;
        }
    }
}

void smClearSchedule(SchList_t *list){
    SchNode_t *curNode;
    while(true){
        if(list->head == NULL)
            break;
        curNode = list->head;
        list->head = curNode->next;
        free(curNode);
    }
    list->nboNode = 0;
    list->nboAsgSlots = 0;
    list->nboRmnSlots = 0;
    list->nboDistReq = 0;
}

//SchNode_t * smGetRefToFirstNodeForSchDist(SchList_t *list){
//    SchNode_t *curNode;
//    curNode = list->head;
//    while(true){
//        if(curNode == NULL)
//            break;
//        
//        if(curNode->nboSchDist > 0){
//            break;
//        }
//        curNode = curNode->next;
//    }
//    return curNode;
//}

SchNode_t* smGetHeadNodeRef(SchList_t *list){
    return list->head;
}

// Return the reference to the next node of curNode
SchNode_t* smGetNextNodeRef(SchNode_t* curNode){
    if(curNode != NULL){
        return curNode->next;
    }
    return NULL;
}

void smNodeSetNboSchDist(SchList_t *list, unsigned short addr, uint8_t nboSchDist){
    SchNode_t *curNode;
    curNode = list->head;
    while(true){
        if(curNode == NULL){
            break;
        }
        if(curNode->addr == addr){
            if(curNode->nboSchDist > 0 && nboSchDist == 0){
                // No more schedule update needed
                list->nboDistReq--;
            }
            if (curNode->nboSchDist == 0 && nboSchDist > 0) {
                // Need to update schedule
                list->nboDistReq++;
            }
            curNode->nboSchDist = nboSchDist;
            break;
        }
        curNode = curNode->next;
    }
}

unsigned short smGetLastAsgLsi(SchList_t *list){
    SchNode_t *curNode = list->head;
    
    if(curNode == NULL)
        return 0;
    
    while(curNode){
        if(curNode->next == NULL)
            return (unsigned short)(curNode->startLSI + curNode->slotDemand - 1);
        curNode = curNode->next;
    }
    return 0;
}

void smPrintSchedule(SchList_t *list){
    SchNode_t *node;
    node = list->head;
    printf("\t\t======== SCHEDULE =======\n");
    printf("\tNode\tAsgLsi\tDemand\tNboSchDist\n");
    
    fprintf(log_file, "\t\t======== SCHEDULE =======\n");
    fprintf(log_file, "\tNode\tAsgLsi\tDemand\tNboSchDist\n");
    while(true){
        if(node == NULL)
            return;
        printf("\t%hu\t%hu\t%hu", node->addr, node->startLSI, node->slotDemand);
        printf("\t%hu\n", node->nboSchDist);
        
        fprintf(log_file, "\t%hu\t%hu\t%hu", node->addr, node->startLSI, node->slotDemand);
        fprintf(log_file, "\t%hu\n", node->nboSchDist);
        node = node->next;
    }
}

/************************* PRIVATE FUNCTION DEFINITION *************************/

static unsigned short smAssignLsiToNode(SchList_t *list, unsigned short demandSlot){
    SchNode_t *curNode;
    unsigned short asgLsi, lastLsi;

    curNode = list->head;
    
    if(curNode == NULL){
        asgLsi = 1;
        return asgLsi;
    }
 
    if(curNode->startLSI > demandSlot){
        asgLsi = 1;
        return asgLsi;
    }
    
    asgLsi = 0;
    while(true){
        if(curNode == NULL) // Cannot find the schedule
           break;
        
        // Calculate the last LSI assigned to curNode
        lastLsi = curNode->startLSI + curNode->slotDemand - 1;

        if(curNode->next == NULL){ // This node is the last one in the list
            // If the number of free slots in this group is enough to schedule the node
            if(list->nboTotSlots - lastLsi >= demandSlot)
                asgLsi = lastLsi + 1;
            break;
        } else { // This node is in the middle of the list
            if(curNode->next->startLSI - lastLsi > demandSlot){
                asgLsi = lastLsi + 1;
                break;
            }
        }
        curNode = curNode->next;
    }
    return asgLsi;
}

// Insert node to the list in the order of ascending LSI
static SchNode_t* smAddNodeToScheduleList(SchList_t *list, SchNode_t node){
    SchNode_t *newNode;
    SchNode_t *curNode, *prevNode;
        
    newNode = (SchNode_t *)malloc(sizeof(SchNode_t));
    if(newNode != NULL){
        newNode->addr = node.addr;
        newNode->class = node.class;
        newNode->slotDemand = node.slotDemand;
        newNode->startLSI = node.startLSI;
        newNode->nboSchDist = node.nboSchDist;
        newNode->next = NULL;
    } else {
        return NULL;
    }
    
    if(list->head == NULL){
        list->head = newNode;
        goto succeeded;
    }
    
    prevNode = NULL;
    curNode = list->head;

    while(true){
        if(curNode == NULL){
            free(newNode);
            return NULL;
        }
        
        if(curNode->startLSI > newNode->startLSI){ // found the right position to add the node
            if(prevNode == NULL){ // node will be the list's head
                newNode->next = list->head;
                list->head = newNode;
            } else {
                prevNode->next = newNode;
                newNode->next = curNode;
            }
            goto succeeded;
        } else {
            if(curNode->next == NULL){
                // node becomes list's tail
                curNode->next = newNode;
                newNode->next = NULL;
                goto succeeded;
            } 
        }
        prevNode = curNode;
        curNode = curNode->next;
    }
    
succeeded:
    list->nboNode++;
    list->nboAsgSlots += newNode->slotDemand;
    list->nboRmnSlots -= newNode->slotDemand;
    list->nboDistReq++;
    return newNode;
}