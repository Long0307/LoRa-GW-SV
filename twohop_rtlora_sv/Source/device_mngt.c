/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
/*
 * Description: To store Two-hop RT-LoRa Gateways and End Nodes profile as 
 *              linked-lists. 
 * Author: Quy Lam Hoang
 * Email: quylam925@gmail.com
 * Created: 2021/05/25
 */
#include <string.h>

#include "device_mngt.h"
#include "debug.h"
#include "trade.h"
#include "rtlora_mac.h"

#define POW2(n)             (1 << n)

extern FILE * log_file;

void initNodeList(MngtNodeList_t *lst, _Bool sort) {
//    dprintf("Init node list %s\n", name);
//    strcpy(lst->name, name);
    lst->head = NULL;
    lst->tail = NULL;
    lst->size = 0;
    lst->nboSchReq = 0;
    lst->sort = sort;
}

void deinitNodeList(MngtNodeList_t *lst) {
    MngtNode_t *node;

    while (true) {
        node = lst->head;
        if(node == NULL)
            break;
        lst->head = node->next;
        free(node);
    }
    lst->head = NULL;
    lst->tail = NULL;
    lst->size = 0;
}

MngtNode_t * createNewNode(NodeGenInfo_t genInfo, uint16_t parrent){
    MngtNode_t *newNode = (MngtNode_t *) malloc(sizeof(MngtNode_t));
    
    if(newNode == NULL){
        MSG_DEBUG(DEBUG_DEVICE_MNGT, "Failed to create a new node\n");
        return NULL;
    }
    
    newNode->genInfo.addr = genInfo.addr;
    newNode->genInfo.class = genInfo.class;
    newNode->genInfo.slotDmn = genInfo.slotDmn;
    newNode->genInfo.type = genInfo.type;
    
    newNode->isConnected = false;
    newNode->schFlag = false;    // indicate that the node has already scheduled or not
    newNode->parrentAddr = parrent;
    newNode->nboChildren = 0;
    memset(&newNode->children, 0 ,sizeof(Child_t) * TWOHOP_MAX_NBO_CHILDREN);

    newNode->dataMissCount = 0;
    newNode->dataCount = 0;
    newNode->dataCountDirectLink = 0;
    newNode->dataCountMainLink = 0;
    newNode->prevSeqNo = 0;
    newNode->latestSeqNo = 0;
    
    newNode->next = NULL;
    newNode->prev = NULL;
    return newNode;
}

// if push done: return 1
// if node existed: return 0
int pushNode(MngtNodeList_t *lst, MngtNode_t *node){
    MngtNode_t *curNode = lst->head;
    if(isNodeExisted(lst, node->genInfo.addr)){
        dmNodeUpdateGenInfo(lst, node);
        free(node);
        return 0;
    }
    
    if(curNode == NULL) { // list is null currently
        lst->tail = node;
        lst->head = node;
        node->next = NULL;
        node->prev = NULL;
        goto succeeded;
    }
    
    if(lst->sort == false){
        /* Insert packet at the end of the list */
        lst->tail->next = node;
        node->next = NULL;
        node->prev = lst->tail;
        lst->tail = node;
        goto succeeded;
    }
    
    while(1) {
        if(curNode->genInfo.slotDmn < node->genInfo.slotDmn) { 
            if(curNode == lst->head) {
                // node becomes list's head
                lst->head = node;
                node->prev = NULL;
            } else {
                //push in the middle of list
                MngtNode_t *prev = curNode->prev;
                node->prev = prev;
                prev->next = node;
            }
            curNode->prev = node;
            node->next = curNode;
            goto succeeded;
        } else {
            if(curNode->next == NULL) { 
                // node becomes list's tail
                lst->tail = node;
                curNode->next = node;
                node->prev = curNode;
                node->next = NULL;
                goto succeeded;
            }
            curNode = curNode->next;
        }
    }
succeeded:
    if(node->schFlag == false)
        lst->nboSchReq++;
    lst->size ++;
    MSG_DEBUG(DEBUG_DEVICE_MNGT, "Add node %d to list done\n", node->genInfo.addr);
    return 1;
}

MngtNode_t* popHeadNode(MngtNodeList_t *lst){
    MngtNode_t* retNode = lst->head;
    if(retNode == NULL)
        return NULL;
    
    lst->head = retNode->next;
    if(lst->head != NULL)
        lst->head->prev = NULL;
    
    retNode->next = NULL;
    retNode->prev = NULL;
    lst->size --;
    if(retNode->schFlag == false)
        lst->nboSchReq--;
    return retNode; 
}

MngtNode_t* popTailNode(MngtNodeList_t *lst) {
    MngtNode_t* retNode = lst->tail;
    if(lst->size == 0)
        return NULL;
    
    lst->tail = retNode->prev;
    lst->tail->next = NULL;
    retNode->next = NULL;
    retNode->prev = NULL;
    lst->size --;
    if(retNode->schFlag == false)
        lst->nboSchReq--;
    return retNode;
}

MngtNode_t* popNodeByAddress(MngtNodeList_t *lst, uint16_t addr){
    MngtNode_t* retNode = NULL;
    
    MngtNode_t *curNode = lst->head;
    while(1) {
        if(curNode == NULL)
            break;
        
        if(curNode->genInfo.addr == addr){
            if(curNode == lst->head)
                lst->head = curNode->next;
            
            if(curNode == lst->tail)
                lst->tail = curNode->prev;
            
            retNode = curNode;
            
            if(retNode->prev != NULL)
                retNode->prev->next = retNode->next;         
            
            if(retNode->next != NULL)
                retNode->next->prev = retNode->prev;
            
            retNode->next = NULL;
            retNode->prev = NULL;
            if(retNode->schFlag == false)
                lst->nboSchReq--;
            
            lst->size --;
            break;
        }
        curNode = curNode->next;
    }
    return retNode;
}

MngtNode_t* dmGetHeadNodeRef(MngtNodeList_t *lst){
    return lst->head;
}

MngtNode_t* getNodeReferenceByAddress(MngtNodeList_t *lst, uint16_t addr){
    MngtNode_t *curNode = lst->head;
    while(curNode != NULL){
        if(curNode->genInfo.addr == addr)
            return curNode;
        curNode = curNode->next;
    }
    return NULL;
}

// Return the reference to the next node of curNode
MngtNode_t* dmGetRefToNextNode(MngtNode_t* curNode){
    if(curNode != NULL)
        return curNode->next;
    return NULL;
}

_Bool dmIsRelayNode(MngtNodeList_t *lst, uint16_t addr){
    MngtNode_t *curNode = lst->head;
    while(curNode != NULL){
        if(curNode->genInfo.addr == addr){
            if(curNode->nboChildren > 0)
                return true;
            else
                return false;
        }
        curNode = curNode->next;
    }
    return false;
}

uint8_t dmGetNboRelays(MngtNodeList_t *lst){
    MngtNode_t *curNode = lst->head;
    uint16_t nboRelays = 0;
    while(curNode != NULL){
        if(curNode->nboChildren > 0)
            nboRelays++;
        curNode = curNode->next;
    }
    return nboRelays;
}

uint8_t dmNodeGetClass(MngtNodeList_t *lst, uint16_t addr){
    MngtNode_t *curNode = lst->head;
    while(curNode != NULL){
        if(curNode->genInfo.addr == addr)
            return curNode->genInfo.class;
        curNode = curNode->next;
    }
    return 0;
}

Child_t* dmNodeGetChildList(MngtNodeList_t *lst, uint16_t addr, uint8_t* nboChild){
    MngtNode_t *curNode = lst->head;
    while(curNode != NULL){
        if(curNode->genInfo.addr == addr){
            *nboChild = curNode->nboChildren;
            return curNode->children;
        }
        curNode = curNode->next;
    }
    return NULL;
}

void dmSetChildConnStatus(MngtNodeList_t *lst, MngtNode_t *node, _Bool status){
    MngtNode_t *child;
    uint8_t nboChild = node->nboChildren;
    for(int i = 0; i < TWOHOP_MAX_NBO_CHILDREN; i++){
        if(node->children[i].addr != 0){
            child = getNodeReferenceByAddress(lst, node->children[i].addr);
            child->isConnected = status;
            nboChild--;
        }
        if(nboChild == 0)
            break;
    }
}

void dmNodeUpdateDataInfo(MngtNodeList_t *lst, uint16_t addr, uint16_t seq, bool isDataRelay){
    MngtNode_t *curNode = lst->head;
    bool duplicated = false;
    while(curNode != NULL){
        if(curNode->genInfo.addr == addr){
            if(curNode->latestSeqNo < seq){
                curNode->dataCount++;
                curNode->latestSeqNo = seq;
                curNode->dataMissCount = 0;
            } else if (curNode->latestSeqNo > seq){
                // reset statistic
                curNode->dataCount = 1;
                curNode->dataCountMainLink = 0;
                curNode->dataCountDirectLink = 0;
                curNode->latestSeqNo = seq;
                curNode->dataMissCount = 0;
                curNode->prevSeqNo = seq - 1;
            } else {
                // Receive duplicate data, do nothing 
                duplicated = true;
            }
            
            // Check whether the DATA is receive via main link or support link
            if(curNode->genInfo.type == Node_Type_Onehop){
                if(isDataRelay == false){
                    curNode->dataCountMainLink++;
                    curNode->dataCountDirectLink++;
                }
            }

            if((curNode->genInfo.type == Node_Type_Twohop) ){
                if(isDataRelay == true){
                    curNode->dataCountMainLink++;
                } else {
                    curNode->dataCountDirectLink++;
                }
            }
            
            return;
        }
        curNode = curNode->next;
    }
    return;
}

uint8_t dmCheckDataMissed(MngtNodeList_t *lst){
    MngtNode_t *curNode = lst->head;
    uint8_t disCount = 0;
    
    while(curNode != NULL){
        if(curNode->isConnected == true){
            if(curNode->prevSeqNo == curNode->latestSeqNo)
                if(curNode->dataMissCount < TWOHOP_MAX_MISS_DATA_ALLOWED)
                    curNode->dataMissCount++;
            else
                curNode->dataMissCount = 0;

            curNode->prevSeqNo = curNode->latestSeqNo;
        }
        
        curNode = curNode->next;
    }
    return disCount;
}

void dmNodeSetSchFlag(MngtNode_t *node, _Bool val){
    node->schFlag = val;
}

void dmSetSchFlagAll(MngtNodeList_t *lst, _Bool val){
    MngtNode_t *curNode = lst->head;
    while(curNode != NULL){
        dmNodeSetSchFlag(curNode, val);
        curNode = curNode->next;
    }
}

_Bool isNodeExisted(MngtNodeList_t *lst, uint16_t addr){
    MngtNode_t *curNode = lst->head;
    while(curNode != NULL){
        if(curNode->genInfo.addr == addr)
            return true;
        curNode = curNode->next;
    }
    return false;
}

_Bool dmNodeUpdateGenInfo(MngtNodeList_t *lst, MngtNode_t *node) {
    MngtNode_t *tmpNode;
    
    tmpNode = getNodeReferenceByAddress(lst, node->genInfo.addr);
    if(tmpNode == NULL){
        MSG_DEBUG(DEBUG_DEVICE_MNGT, "Update node info FAILED. Node %u does not exist in the list\n", node->genInfo.addr);
        return false;
    } else {
        MSG_DEBUG(DEBUG_DEVICE_MNGT, "NODE %u: Update info\n", tmpNode->genInfo.addr);
    }
    
    tmpNode->genInfo.addr = node->genInfo.addr;
    tmpNode->genInfo.class = node->genInfo.class;
    tmpNode->genInfo.type = node->genInfo.type;
    tmpNode->genInfo.slotDmn = node->genInfo.slotDmn;
    tmpNode->schFlag = node->schFlag;
    tmpNode->parrentAddr = node->parrentAddr;
    tmpNode->nboChildren = node->nboChildren;
    tmpNode->isConnected = node->isConnected;
    
    for(unsigned int i = 0; i < TWOHOP_MAX_NBO_CHILDREN; i++){
        memcpy(&tmpNode->children[i], &node->children[i], sizeof(Child_t));
    }
    
    // Update node information
//    tmpNode->dataCount = node->dataCount;
//    tmpNode->latestSeqNo = node->latestSeqNo;
    return true;
}

void dmRemoveChildOfNode(MngtNode_t* destNode){
    if(destNode != NULL){
        memset(&destNode->children, 0, sizeof(Child_t) * TWOHOP_MAX_NBO_CHILDREN);
        destNode->genInfo.slotDmn = POW2(destNode->genInfo.class);
        destNode->nboChildren = 0;
    }
}

_Bool dmAddChildToNode(MngtNode_t* parent, MngtNode_t* child){
    int i;
    _Bool existed = false;
    // First, determine whether this child existed
    for(i = 0; i < TWOHOP_MAX_NBO_CHILDREN; i++){
        if(parent->children[i].addr == child->genInfo.addr){
            existed = true;
            break;
        }
    }
    
    // If child has not existed yet, find a empty slot to insert the child
    if(existed == false){
        for(i = 0; i < TWOHOP_MAX_NBO_CHILDREN; i++){
            if(parent->children[i].addr == 0)
                break;
        }
    }
    
    if(i < TWOHOP_MAX_NBO_CHILDREN){
        // Update infor for parent node
        if(existed == true){
            parent->genInfo.slotDmn -= parent->children[i].slotDemand * 2;
        } else {
            parent->nboChildren++;
        }
        
        parent->genInfo.slotDmn += child->genInfo.slotDmn * 2; // take slot demand of children into account
        
        // Update infor for child
        parent->children[i].addr = child->genInfo.addr;
        parent->children[i].class = child->genInfo.class;
        parent->children[i].slotDemand = child->genInfo.slotDmn;
        
        return true;
    } else {
        MSG("[MAC] Parent %hu of node %hu is full of children.\n", parent->genInfo.addr, child->genInfo.addr);
        return false;
    }
}

void printNodeList(MngtNodeList_t *lst){
    MngtNode_t *curNode = lst->head;
    float PDR, PDRMainLink, PDRDirectLink;
    
    printf("\t\t\t\t--- NODE INFORMATION (%u NODES) ---\n", lst->size);
    fprintf(log_file, "\t\t\t\t--- NODE INFORMATION (%u NODES) ---\n", lst->size);
    if(curNode == NULL){
        printf("Empty ...\n\n");
        fprintf(log_file, "Empty ...\n\n");
        return;
    }
    printf("ONE-HOP NODES\n");
    fprintf(log_file, "ONE-HOP NODES\n");
    printf("\tNodeID\t\tSch?\t\tSlotDemand\tClass\t\tDataCount\tLatestSeqNo\tPRD\t\tMissedCnt\n");
    fprintf(log_file,"\tNodeID\tSch?\t\tSlotDemand\tClass\tDataCount\tLatestSeqNo\tPRD\t\tMissedCnt\n");
    while(1) {
        if(curNode == NULL){
            break;
        }
        if(curNode->genInfo.type == Node_Type_Onehop){
            printf("\t%hu", curNode->genInfo.addr);
            if(curNode->nboChildren > 0)
                printf(" (R)");
            printf("\t\t%s", curNode->schFlag ? "true" : "false");
            printf("\t\t%hhu\t\t%hhu",curNode->genInfo.slotDmn, curNode->genInfo.class);
            printf("\t\t%hu (%hu) (%hu)\t\t%hu",curNode->dataCount, curNode->dataCountDirectLink, \
                    curNode->dataCountMainLink, curNode->latestSeqNo);
            if(curNode->latestSeqNo != 0){
                PDR = (float)curNode->dataCount/(curNode->latestSeqNo * 1.0)*100;
                PDRDirectLink = (float)curNode->dataCountDirectLink/(curNode->latestSeqNo * 1.0)*100;
                PDRMainLink = (float)curNode->dataCountMainLink/(curNode->latestSeqNo * 1.0)*100;
            } else {
                PDR = 0.0;
                PDRDirectLink = 0.0;
                PDRMainLink = 0.0;
            }
            printf("\t\t%.1f (%.1f) (%.1f)", PDR, PDRDirectLink, PDRMainLink);
            
            printf("\t%u",curNode->dataMissCount);
            if(curNode->isConnected == false)
                printf("(DIST)");
            printf("\n");
            
            fprintf(log_file, "\t%hu", curNode->genInfo.addr);
            if(curNode->nboChildren > 0)
                fprintf(log_file, " (R)");
            fprintf(log_file, "\t\t%s", curNode->schFlag ? "true" : "false");
            fprintf(log_file, "\t\t%hhu\t\t\t%hhu",curNode->genInfo.slotDmn, curNode->genInfo.class);
            fprintf(log_file, "\t\t%hu (%hu) (%hu)\t\t%hu",curNode->dataCount, curNode->dataCountDirectLink, \
                    curNode->dataCountMainLink, curNode->latestSeqNo);
            fprintf(log_file, "\t\t\t%.1f (%.1f) (%.1f)", PDR, PDRDirectLink, PDRMainLink);
            fprintf(log_file, "\t%u",curNode->dataMissCount);
            if(curNode->isConnected == false)
                fprintf(log_file, "(DIST)");
            fprintf(log_file, "\n");
        }
        curNode = curNode->next;
    }
    
    curNode = lst->head;
    printf("TWO-HOP NODES\n");
    printf("\tNodeID\t\t\t\tSlotDemand\tClass\t\tDataCount\tLatestSeqNo\tPRD\t\tMissedCnt\n");
    
    fprintf(log_file, "TWO-HOP NODES\n");
    fprintf(log_file, "\tNodeID\t\t\tSlotDemand\tClass\t\tDataCount\tLatestSeqNo\tPRD\t\tMissedCnt\n");
    while(1) {
        if(curNode == NULL){
            break;
        }
        if(curNode->genInfo.type == Node_Type_Twohop){
            printf("\t%hu (%hu)", curNode->genInfo.addr, curNode->parrentAddr);
            printf("\t\t");
            printf("\t\t%hhu\t\t%hhu", curNode->genInfo.slotDmn, curNode->genInfo.class);
            printf("\t\t%hu (%hu) (%hu)\t\t%hu",curNode->dataCount, curNode->dataCountDirectLink, \
                    curNode->dataCountMainLink, curNode->latestSeqNo);
            if(curNode->latestSeqNo != 0){
                PDR = (float)curNode->dataCount/(curNode->latestSeqNo * 1.0)*100;
                PDRDirectLink = (float)curNode->dataCountDirectLink/(curNode->latestSeqNo * 1.0)*100;
                PDRMainLink = (float)curNode->dataCountMainLink/(curNode->latestSeqNo * 1.0)*100;
            } else {
                PDR = 0.0;
                PDRDirectLink = 0.0;
                PDRMainLink = 0.0;
            }
            printf("\t\t%.1f (%.1f) (%.1f)", PDR, PDRDirectLink, PDRMainLink);
            printf("\t%u",curNode->dataMissCount);
            if(curNode->isConnected == false)
                printf("(DIST)");
            printf("\n");
            
            fprintf(log_file, "\t%hu (%hu)", curNode->genInfo.addr, curNode->parrentAddr);
            fprintf(log_file, "\t\t");
            fprintf(log_file, "\t\t%hhu\t\t%hhu", curNode->genInfo.slotDmn, curNode->genInfo.class);
            fprintf(log_file, "\t\t%hu (%hu) (%hu)\t\t%hu",curNode->dataCount, curNode->dataCountDirectLink, \
                    curNode->dataCountMainLink, curNode->latestSeqNo);
            fprintf(log_file, "\t\t\t%.1f (%.1f) (%.1f)", PDR, PDRDirectLink, PDRMainLink);
            fprintf(log_file, "\t\t%u",curNode->dataMissCount);
            if(curNode->isConnected == false)
                fprintf(log_file, "(DIST)");
            fprintf(log_file, "\n");
        }
        curNode = curNode->next;
    }
    printf("\t\t\t\t-----------------------------------\n");
    printf("\n");
    fprintf(log_file, "\t\t\t\t-----------------------------------\n");
    fprintf(log_file, "\n");
}
