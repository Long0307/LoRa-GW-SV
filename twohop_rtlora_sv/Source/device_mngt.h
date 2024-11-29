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
 * Created on May 25, 2021, 3:56 PM
 */

#ifndef DEVICE_MNGT_H
#define DEVICE_MNGT_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
//#include <arpa/inet.h>
//#include <sys/socket.h>

#include "rtlora_mac_conf.h"

//#define TCP_STREAM_BUFFER_SIZE	8192

//typedef struct GatewayInfo{
//	struct GatewayInfo *next;
//	int socket;
//	struct sockaddr_in sockaddr;
//	uint8_t rxBuffer[TCP_STREAM_BUFFER_SIZE];
//	uint32_t currentRxBufferSize;
//}GatewayInfo_t;

typedef enum NodeType_ {
    Node_Type_Onehop,
    Node_Type_Twohop,
} NodeType_e;

typedef struct Child_{
    uint16_t addr;
    uint8_t class;
    uint8_t slotDemand;
}Child_t;

typedef struct NodeGenInfo_{
    uint16_t addr;       // Node address
    uint8_t class;
    uint8_t slotDmn;
    NodeType_e type;
}NodeGenInfo_t;

// Single end-node information
typedef struct MngtNode_{
    NodeGenInfo_t genInfo;      // general information
    bool isConnected;           // true: connected, false: disconnected
    _Bool schFlag;          // Indicate this node has been schedule or not
                            // true: scheduled
                            // false: not scheduled yet
    uint16_t parrentAddr;   // only for two hop node
    uint8_t nboChildren;    // only for relay node
    Child_t children[TWOHOP_MAX_NBO_CHILDREN];
    
    uint16_t dataMissCount;  // Count the number of consecutive frame periods that data is missed
    uint16_t latestSeqNo;   // Latest data sequence number
    uint16_t dataCount;     // Total data count (both main link and support link (if 2 hop type))
    uint16_t dataCountMainLink;     // Total data count (both main link and support link (if 2 hop type))
    uint16_t dataCountDirectLink;     // Total data count (both main link and support link (if 2 hop type))
    uint16_t prevSeqNo; // Data count of the previous frame period

    struct MngtNode_ *next;
    struct MngtNode_ *prev;
}MngtNode_t;

typedef struct MngtNodeList_{
//    char name[20];
    MngtNode_t *head;
    MngtNode_t *tail;
    unsigned int size;
    unsigned int nboSchReq;    // number of node waiting for (or requesting) scheduling
    _Bool sort; // if true, the list is always be sorted in the order of increasing slot demand
}MngtNodeList_t;

void initNodeList(MngtNodeList_t *lst, _Bool sort);

void deinitNodeList(MngtNodeList_t *lst);

MngtNode_t * createNewNode(NodeGenInfo_t genInfo, uint16_t parrent);
/*
 * Push node (*node) the list (*lst) in the order of increasing address value
 * Return 1 if push done 
 * Return 0 if node existed
 */
int pushNode(MngtNodeList_t *lst, MngtNode_t *node);

/*
 * Pop the head node from the list (*lst) 
 */
MngtNode_t* popHeadNode(MngtNodeList_t *lst);

/*
 * Pop the tail node from the list (*lst)
 */
MngtNode_t* popTailNode(MngtNodeList_t *lst);

/*
 * Pop the node by address (addr) from the list (*lst) 
 */
MngtNode_t* popNodeByAddress(MngtNodeList_t *lst, uint16_t addr);

/*
 * Get pointer to the node by address (addr). Return NULL if node does not exist in the list
 */
MngtNode_t* getNodeReferenceByAddress(MngtNodeList_t *lst, uint16_t addr);

MngtNode_t* dmGetRefToNextNode(MngtNode_t* curNode);

MngtNode_t* dmGetHeadNodeRef(MngtNodeList_t *lst);

uint8_t dmGetNboRelays(MngtNodeList_t *lst);

void dmNodeSetSchFlag(MngtNode_t *node, _Bool val);

void dmSetSchFlagAll(MngtNodeList_t *lst, _Bool val);

_Bool dmIsRelayNode(MngtNodeList_t *lst, uint16_t addr);

uint8_t dmNodeGetClass(MngtNodeList_t *lst, uint16_t addr);

Child_t* dmNodeGetChildList(MngtNodeList_t *lst, uint16_t addr, uint8_t* nboChild);

void dmSetChildConnStatus(MngtNodeList_t *lst, MngtNode_t *node, _Bool status);

void dmNodeUpdateDataInfo(MngtNodeList_t *lst, uint16_t addr, uint16_t seq, bool isDataRelay);

uint8_t dmCheckDataMissed(MngtNodeList_t *lst);

_Bool isNodeExisted(MngtNodeList_t *lst, uint16_t addr);
        
/*
 * Update node information. Return true if succeed, false if failed
 */
_Bool dmNodeUpdateGenInfo(MngtNodeList_t *lst, MngtNode_t *node);

void dmRemoveChildOfNode(MngtNode_t* parrent);

_Bool dmAddChildToNode(MngtNode_t* parent, MngtNode_t* child);
        
void printNodeList(MngtNodeList_t *lst);

#endif /* DEVICE_MNGT_H */

