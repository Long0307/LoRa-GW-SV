/*
 * Description: Two-hop RT-LoRa MAC protocol source file
 * Author: Quy Lam Hoang
 * Email: quylam925@gmail.com
 * Created: 2021/05/24
 */


/* -------------------------------------------------------------------------- */
/* --- DEPENDANCIES --------------------------------------------------------- */

/* fix an issue between POSIX and C99 */
#if __STDC_VERSION__ >= 199901L
#define _XOPEN_SOURCE 600
#else
#define _XOPEN_SOURCE 500
#endif

#include <stdint.h>     /* C99 types */
#include <stdbool.h>    /* bool type */
#include <stdio.h>      /* printf fprintf sprintf fopen fputs */

#include <string.h>     /* memset */
#include <signal.h>     /* sigaction */
#include <unistd.h>     /* getopt access */
#include <stdlib.h>     /* exit codes */
#include <getopt.h>     /* getopt_long */
#include <math.h>

#include <pthread.h>
#include <sys/time.h>
//#include <time.h>

#include "trade.h"
#include "parson.h"

#include "rtlora_mac.h"   /* contain MAC parameters configuration */
#include "rtlora_mac_conf.h"
#include "device_mngt.h"
#include "packet_queue.h"
#include "time_conversion.h"
#include "schedule_mngt.h"

#include "application.h"


/* -------------------------------------------------------------------------- */
/* --- PRIVATE MACROS ------------------------------------------------------- */

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* -------------------------------------------------------------------------- */
/* --- PRIVATE CONSTANTS ---------------------------------------------------- */

/* -------------------------------------------------------------------------- */

typedef enum OperationPhase_{
    TWOHOP_NETWORK_INIT_PHASE,
    TWOHOP_SCHEDULE_DIST_PHASE,
    TWOHOP_DATA_COLL_PHASE,
}OperationPhase_e;

/* MESSAGE STRUCTURE */
/* Packet type definition */
typedef enum TwohopMsgType_ {
	/* Downlink */
    Twohop_MsgType_DL_RNL,  /* Registration Node List */
	Twohop_MsgType_DL_SM,   /* Scheduling message */
	Twohop_MsgType_DL_CM,   /* Command message*/
    
	/* Uplink */
    Twohop_MsgType_UL_RR,   /* registration request */
	Twohop_MsgType_UL_DATA, /* up-link data */
            
    /* Between 1-hop and 2-hop nodes */
    Twohop_MsgType_RRACK,   /* registration confirmation */
}TwohopMsgType_e;

typedef union TwohopNodeAddrFormat_{   // Node addressing format
    uint16_t value;
    struct AddrFormat_{
        uint16_t address    : 13;
        uint16_t class      : 3;
    }bits;
}TwohopNodeAddrFormat_u;

typedef union TwohopMacHeader_ {
	uint8_t value;
	struct MacHeaderValue {
		uint8_t RFU         :4;
		uint8_t pktType     :4;
	} bits;
}TwohopMacHeader_u;

typedef union TwohopMsgRnlCtrl_{
    uint8_t value;
    struct RnlCtrl_{
        uint8_t netReadyFlag    : 1; // bit 0
        uint8_t nboAddedNodes   : 7; // bit 1 ~ 7
    }bits;
}TwohopMsgRnlCtrl_u;

typedef union TwohopMsgRrCtrl_{
    uint8_t value;
    struct RrCtrl_{
        uint8_t rrType1         : 1; // bit 0
        uint8_t rrType2         : 1; // bit 0
        uint8_t nboChild        : 6; // bit 1 ~ 7
    }bits;
}TwohopMsgRrCtrl_u;

typedef union TwohopMsgSmCtrl_{
    uint8_t value;
    struct SmCtrl_{
        uint8_t smCount        : 4;
        uint8_t sch1Size       : 4;
    }bits;
}TwohopMsgSmCtrl_u;

typedef union TwohopMsgCmCtrl_{
    uint8_t value;
    struct CmCtrl_{
        uint8_t usiFlag         : 1;
        uint8_t nboNodesInUsi   : 4;
        uint8_t unuse           : 3;
    }bits;
}TwohopMsgCmCtrl_u;

typedef union TwohopMsgDataCtrl_{
    uint8_t value;
    struct DataCtrl_{
        uint8_t ctrl0           : 1;
        uint8_t ctrl1           : 1;
        uint8_t ctrl2           : 1;
        uint8_t unuse           : 5;
    }bits;
}TwohopMsgDataCtrl_u;

typedef union TwohopMacParams_{   // Mac configuration parameters
    uint16_t value;
    struct Params_{
        uint16_t frameFactor        : 3;
        uint16_t uplinkSlotSize     : 5;
        uint16_t downlinkSlotSize   : 5;
        uint16_t nboChannels        : 3;
    }bits;
}TwohopMacParams_u;

typedef struct TwohopFrameHeader_ {
    uint16_t srcAddr;
    uint16_t destAddr;
    uint16_t seqNumber;
    TwohopMacParams_u macParams;
    TwohopMsgRnlCtrl_u rnlCtrl;
    TwohopMsgRrCtrl_u rrCtrl;
    TwohopMsgSmCtrl_u smCtrl;
    TwohopMsgCmCtrl_u cmCtrl;
    TwohopMsgDataCtrl_u dataCtrl;
}TwohopFrameHeader_s;

/* --- GLOBAL VARIABLES ----------------------------------------------------- */
/* MAC remotely configurable parameters */
int mac_frame_factor = 6;       // N = 6 by default
int mac_ul_slot_size_ms = 100;     // 100 ms by default
int mac_dl_slot_size_ms = 200;     // 200 ms by default
int mac_nbo_channels = 1;       // = 1 by default

int mac_nbo_sch_groups; // determined after the number of channels is confirmed via input command options

/* --- PRIVATE SHARED VARIABLES (GLOBAL) ------------------------------------ */
// Condition variable, mutex, and flag for packet exchange between lora server threads and other threads
pthread_cond_t condTxMsg = PTHREAD_COND_INITIALIZER;
pthread_cond_t condRxMsg = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutexTxMsg = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutexRxMsg = PTHREAD_MUTEX_INITIALIZER;
_Bool flagTxMsg;   // flag indicates it has a packet to transmit
_Bool flagRxMsg;   // flag indicates it receives a packet

struct PktQueue inboundMsgQueue;
struct PktQueue outboundMsgQueue;

pthread_mutex_t mutexPhaseTrans = PTHREAD_MUTEX_INITIALIZER;
_Bool phaseTransRequest;     // true indicates the network will move to anther phase 

extern bool exit_sig;
extern bool quit_sig;

extern FILE * log_file;

/* --- PRIVATE VARIABLES (GLOBAL) ------------------------------------------- */
static pthread_t moThId; // MAC operation thread ID

//static pthread_mutex_t mutexPhase;
static OperationPhase_e phase;

/* Node management */
MngtNodeList_t RNL;         // Registration node list
pthread_mutex_t mutexRNL = PTHREAD_MUTEX_INITIALIZER;
MngtNodeList_t NODES;       // Node list after registration is confirmed
pthread_mutex_t mutexNODES = PTHREAD_MUTEX_INITIALIZER;

/* Schedule management */
SchList_t SCHEDULES[TWOHOP_MAX_NBO_CHANNELS];
//pthread_mutex_t mutexSCHEDULES = PTHREAD_MUTEX_INITIALIZER;

/* --- PRIVATE FUNCTIONS ---------------------------------------------------- */
static void* macMainThread(void);

static void *phaseHandlerThread(void *arg);

static void initNetworkPhase(void);

static void scheduleDistributionPhase(void);

static void dataCollectionPhase(void);

static void checkDataMissed(void);

static void prepareDownlinkMsgMetaData(struct MsgInfo_ *packet, struct timeval TxTimestamp);

static void prepareDownlinkMsgPayload(MsgInfo_s *msg, TwohopMsgType_e type, uint16_t seq, void *addInfo);

static void *inputMsgHandlerThread(void *args);

static _Bool addNodeToNodeList(MngtNodeList_t *lst, MngtNode_t *node);

static unsigned short schedule(OperationPhase_e phase);

static void removeNodeFromSchedule(uint16_t addr);

static void clearSchedule(void);

static int ipow(int base, int exp);

static unsigned short slotDemandCalculation(unsigned short class);

static uint64_t getTimeOffetTimeval(struct timeval start, struct timeval end){
    uint64_t offset;
    // TODO: check condition start < end
    TIMEVAL_SUB(end, start); // end = end - start
    TIMEVAL_TO_MICROSECONDS(end, offset);
    return offset;
}

void twohopLoRaMacInit(void) {
    int i;
    unsigned short maxLsi;
    
    /* Init MAC parameters */
    pktQueueInit(&inboundMsgQueue);
    pktQueueInit(&outboundMsgQueue);
    
    pthread_mutex_lock(&mutexRNL);
    initNodeList(&RNL, false); // nodes in the RNL list are not sorted
    pthread_mutex_unlock(&mutexRNL);
    
    pthread_mutex_lock(&mutexNODES);
    initNodeList(&NODES, true);
    pthread_mutex_unlock(&mutexNODES);
    
    // The number of scheduling groups is equal to the number of channels
    mac_nbo_sch_groups = mac_nbo_channels;
    
    maxLsi = (unsigned short)ipow(2, mac_frame_factor);
//    pthread_mutex_lock(&mutexSCHEDULES);
    for(i = 0; i < mac_nbo_sch_groups; i++){
        smInitSchedule(&SCHEDULES[i], maxLsi);
    }
//    pthread_mutex_unlock(&mutexSCHEDULES);
    
    phaseTransRequest = false;
    
    //phase = TWOHOP_NETWORK_INIT_PHASE;
    
    // for testing, go directly to DC
    phase = TWOHOP_DATA_COLL_PHASE;
    
    i = pthread_create(&moThId, NULL, (void * (*)(void *))macMainThread, NULL);
    if (i != 0) {
        MSG("[MAC]: failed to create MAC operation thread\n");
        exit(EXIT_FAILURE);
    }
}

void twohopLoRaMacDeInit(void){
    pthread_join(moThId, NULL);
}

static void* macMainThread(void){ // create from MacInit function
    int i;
    pthread_t phThId;           // phase handler thread ID
    pthread_t imhThId;           // input message handler thread ID
    pthread_t testThId;           // test thread ID

    sleep(5);
    MSG("[MAC] TWO-HOP RT-LORA MAC START\n");
    fputs("[MAC] TWO-HOP RT-LORA MAC START\n", log_file);
    
    i = pthread_create(&phThId, NULL, (void * (*)(void *))phaseHandlerThread, NULL);
    if (i != 0) {
        MSG("[MAC]: failed to create NI thread\n");
        exit(EXIT_FAILURE);
    }
    i = pthread_create(&imhThId, NULL, (void * (*)(void *))inputMsgHandlerThread, NULL);
    if (i != 0) {
        MSG("[MAC]: failed to create NI thread\n");
        exit(EXIT_FAILURE);
    }
    
	while(!exit_sig && !quit_sig){
		// check command from upper layer (include phase end condition)
		// show statistic
        sleep(10);
	}
    pthread_cancel(phThId);
    pthread_cancel(imhThId);
    fprintf(log_file, "\n[MAC] PROGRAM STOP!\n");
	// Print exiting message
}

static void *phaseHandlerThread(void *arg){
    while(1){
        switch(phase){
            case TWOHOP_NETWORK_INIT_PHASE:
                
                initNetworkPhase();
                
                phase = TWOHOP_SCHEDULE_DIST_PHASE;
                break;
            case TWOHOP_SCHEDULE_DIST_PHASE:
                
                scheduleDistributionPhase();
                
                phase = TWOHOP_DATA_COLL_PHASE;
                break;
            case TWOHOP_DATA_COLL_PHASE:
                
                dataCollectionPhase();
                
                phase = TWOHOP_SCHEDULE_DIST_PHASE;
                break;
            default:
                break;
        }
    }
}

struct timeval getDownlinkTxTimestamp(struct timeval fromTime){
    struct timeval tx_timestamp, temp_time;
    // convert MAC_SHIFT_DELAY from millisecond to timeval
    temp_time.tv_sec = 0;
    temp_time.tv_usec =  MAC_SHIFT_DELAY_MS*1000;
    if(temp_time.tv_usec >= 1000000){
        temp_time.tv_sec += temp_time.tv_usec/1000000;
        temp_time.tv_usec -= temp_time.tv_sec * 1000000;
    }
    tx_timestamp.tv_sec = fromTime.tv_sec;
    tx_timestamp.tv_usec = fromTime.tv_usec;
    TIMEVAL_ADD(tx_timestamp, temp_time);
    return tx_timestamp;
}

static void initNetworkPhase(void){
    struct timeval mac_start_rnl_int_time, current_time;
    uint64_t delayUsec;
    uint16_t rnlIntCount;
    uint16_t phaseTransCount = TWOHOP_NBO_PHASE_TRANS_PERIOD;
    MsgInfo_s dlMsg;
    rnlIntCount = 0;
    MSG("\n[MAC] NETWORK INIT PHASE START!\n");
    fprintf(log_file, "\n[MAC] NETWORK INIT PHASE START!\n");
    while(true){
        gettimeofday(&mac_start_rnl_int_time, NULL);
        /* periodically  transmit RNLint */
        pthread_mutex_lock(&mutexPhaseTrans);
            
        if(phaseTransCount == 0){ // End of init phase
            phaseTransRequest = false;
            pthread_mutex_unlock(&mutexPhaseTrans);
            break;
        }
        
        // Determine whether to terminate the current phase
        if(phaseTransRequest == true){
            rnlIntCount = phaseTransCount;
            phaseTransCount--;
        } else {
            rnlIntCount++;
        }
        
        prepareDownlinkMsgPayload(&dlMsg, Twohop_MsgType_DL_RNL, rnlIntCount, (void*)&phaseTransRequest);
        pthread_mutex_unlock(&mutexPhaseTrans);
        
        if(dlMsg.size != 0){
            struct timeval msg_tx_time = getDownlinkTxTimestamp(mac_start_rnl_int_time);
            prepareDownlinkMsgMetaData(&dlMsg, msg_tx_time);
            // Add packet to the queue and trigger signal to the waiting threads
            pktEnqueue(&outboundMsgQueue, &dlMsg);

            // Notify lora server thread that the packet is ready to transmit
            pthread_mutex_lock(&mutexTxMsg);
            flagTxMsg = true;
            pthread_cond_signal(&condTxMsg);
            pthread_mutex_unlock(&mutexTxMsg);

            MSG("\n[MAC] NetReady = %hu. Transmit RNLint %hu\n", (phaseTransRequest ? 1 : 0), rnlIntCount);
            fprintf(log_file, "\n[MAC] NetReady = %hu. Transmit RNLint %hu\n", (phaseTransRequest ? 1 : 0), rnlIntCount);
        }
        
        if(rnlIntCount %3 == 0){
            pthread_mutex_lock(&mutexNODES);
            printNodeList(&NODES);
            pthread_mutex_unlock(&mutexNODES);
        }
        
        gettimeofday(&current_time, NULL);
        delayUsec = (uint64_t) TWOHOP_RNL_INTERVAL_US - getTimeOffetTimeval(mac_start_rnl_int_time, current_time);
        usleep((__useconds_t)delayUsec);
    }
    MSG("\n[MAC] NETWORK INIT PHASE DONE!\n");
    fprintf(log_file, "\n[MAC] NETWORK INIT PHASE DONE!\n");
    
    pthread_mutex_lock(&mutexNODES);
    printNodeList(&NODES);
    pthread_mutex_unlock(&mutexNODES);
}

static void scheduleDistributionPhase(void){
    int sch1Cnt;
    struct timeval mac_start_sd_slot_time;
    struct timeval current_time;
    uint64_t delayUsec;
    MsgInfo_s dlMsg;
    uint8_t sch2Sslot = 1;   // SCH2 start slot
    
    MSG("\n[MAC] SCHEDULE DISTRIBUTION PHASE START!\n");
    fprintf(log_file, "\n[MAC] SCHEDULE DISTRIBUTION PHASE START!\n");
    // Must reset schedule flag to false before generate a new schedule
    dmSetSchFlagAll(&NODES, false);
    // Remove old schedule
    clearSchedule();

    unsigned short nboSchNodes = schedule(TWOHOP_SCHEDULE_DIST_PHASE);
    MSG_DEBUG(DEBUG_RTLORA_MAC, "Generate schedule for %u nodes\n", nboSchNodes);
    fprintf(log_file, "Generate schedule for %u nodes\n", nboSchNodes);
    for(unsigned short i = 0; i < mac_nbo_sch_groups; i++){
//        pthread_mutex_lock(&mutexSCHEDULES);
        printf("SCHEDULE GROUP %hu, NBO SCH DIST REQUEST %u\n", i, SCHEDULES[i].nboDistReq);
        fprintf(log_file, "SCHEDULE GROUP %hu, NBO SCH DIST REQUEST %u\n", i, SCHEDULES[i].nboDistReq);
        smPrintSchedule(&SCHEDULES[i]);
//        pthread_mutex_unlock(&mutexSCHEDULES);
    }
    
    MSG("\n[MAC] Start SCH1 (%hu slots of %u ms)\n", TWOHOP_NBO_SLOTS_IN_SCH1, (unsigned int)(TWOHOP_SCH1_SLOT_SIZE_US/1000));
    fprintf(log_file, "\n[MAC] Start SCH1 (%hu slots of %u ms)\n", TWOHOP_NBO_SLOTS_IN_SCH1, (unsigned int)(TWOHOP_SCH1_SLOT_SIZE_US/1000));
    // Distribute schedule to one hop nodes in SCH1
    for(sch1Cnt = 1; sch1Cnt <= TWOHOP_NBO_SLOTS_IN_SCH1; sch1Cnt++){
        gettimeofday(&mac_start_sd_slot_time, NULL);
        
        // Get number of node in the SM payload
        prepareDownlinkMsgPayload(&dlMsg, Twohop_MsgType_DL_SM, sch1Cnt, (void*)&sch2Sslot);
        
        if(dlMsg.size != 0){
            struct timeval msg_tx_time = getDownlinkTxTimestamp(mac_start_sd_slot_time);
            
            prepareDownlinkMsgMetaData(&dlMsg, msg_tx_time);
        
            // Add packet to the queue and trigger signal to the waiting threads
            pktEnqueue(&outboundMsgQueue, &dlMsg);

            // Notify lora server thread that the packet is ready to transmit
            pthread_mutex_lock(&mutexTxMsg);
            flagTxMsg = true;
            pthread_cond_signal(&condTxMsg);
            pthread_mutex_unlock(&mutexTxMsg);
            MSG("\n[MAC] Transmit SM_%hu\n", sch1Cnt);
            fprintf(log_file, "\n[MAC] Transmit SM_%hu\n", sch1Cnt);
        }
        gettimeofday(&current_time, NULL);
        delayUsec = (uint64_t) TWOHOP_SCH1_SLOT_SIZE_US - getTimeOffetTimeval(mac_start_sd_slot_time, current_time);
        usleep((__useconds_t)delayUsec);
    }
    
    // SCH2 phase
    uint8_t nboSch2Slot = sch2Sslot - 1;
    MSG("\n[MAC] Start SCH2 (%hu slots of %u ms)\n", nboSch2Slot, (unsigned int)(TWOHOP_SCH2_SLOT_SIZE_US/1000));
    fprintf(log_file, "\n[MAC] Start SCH2 (%hu slots of %u ms)\n", nboSch2Slot, (unsigned int)(TWOHOP_SCH2_SLOT_SIZE_US/1000));
    __useconds_t sch2Interval = (__useconds_t)(TWOHOP_SCH2_SLOT_SIZE_US * nboSch2Slot);
    usleep((__useconds_t)sch2Interval);
    MSG("\n[MAC] SCHEDULE DISTRIBUTION PHASE DONE!\n");
    fprintf(log_file, "\n[MAC] SCHEDULE DISTRIBUTION PHASE DONE!\n");
}

uint64_t calculateFrameLength(void){
    uint64_t nboUlSlot;
    uint64_t framePeriod;
    
    nboUlSlot = ipow(2, mac_frame_factor);
    framePeriod = nboUlSlot * mac_ul_slot_size_ms*1000 + mac_dl_slot_size_ms*1000 * 2;
    return framePeriod;
}

static void dataCollectionPhase(void){
    uint64_t delayUsec;
    uint16_t phaseTransCount = TWOHOP_NBO_PHASE_TRANS_PERIOD;
    MsgInfo_s dlMsg;
    uint16_t framePeriodCnt = 0;
    uint64_t frameLength;
    
    struct timeval mac_start_fp_time;
    struct timeval current_time;
    time_t local_current_time;
    struct tm* ptime;
    
    MSG("\n[MAC] DATA COLLECTION PHASE START!\n");
    fprintf(log_file, "\n[MAC] DATA COLLECTION PHASE START!\n");
    frameLength = calculateFrameLength();
    MSG("[MAC] Frame period: %u ms\n", (unsigned int)frameLength/1000);
    fprintf(log_file, "[MAC] Frame period: %u ms\n", (unsigned int)frameLength/1000);
    
    while(true){      
        gettimeofday(&mac_start_fp_time, NULL);
        time(&local_current_time);
        ptime = localtime(&local_current_time);
        // Determine whether to terminate the current phase
        pthread_mutex_lock(&mutexPhaseTrans);
        if(phaseTransCount == 0){ // End of data collection phase
            phaseTransRequest = false;
            pthread_mutex_unlock(&mutexPhaseTrans);
            break;
        }
        // Determine whether to terminate the current phase
        if(phaseTransRequest == true){
            framePeriodCnt = phaseTransCount;
            phaseTransCount--;
        } else {
            framePeriodCnt++;
        }
        pthread_mutex_unlock(&mutexPhaseTrans);
        MSG("\n\n ----------- FRAME PERIOD %hu ----------\n", framePeriodCnt);
        MSG("TIMESTAMP: %ld.%06ld", mac_start_fp_time.tv_sec, mac_start_fp_time.tv_usec);
        MSG(" (%d-%02d-%02d %2d:%02d:%02d)\n",ptime->tm_year + 1900, ptime->tm_mon + 1, ptime->tm_mday, \
                (ptime->tm_hour) % 24, ptime->tm_min, ptime->tm_sec);
        
        fprintf(log_file, "\n\n ----------- FRAME PERIOD %hu ----------\n", framePeriodCnt);
        fprintf(log_file, "TIMESTAMP: %ld.%06ld", mac_start_fp_time.tv_sec, mac_start_fp_time.tv_usec);
        fprintf(log_file, " (%d-%02d-%02d %2d:%02d:%02d)\n",ptime->tm_year + 1900, ptime->tm_mon + 1, ptime->tm_mday, \
                (ptime->tm_hour) % 24, ptime->tm_min, ptime->tm_sec);
        
        checkDataMissed();
        
        unsigned short nboNodes;
        // Update node in RNL list to NODES list
        MngtNode_t *tempNode;
        pthread_mutex_lock(&mutexRNL);
        tempNode = popHeadNode(&RNL);
        pthread_mutex_unlock(&mutexRNL);
        nboNodes = 0;
        while(tempNode != NULL){
            pthread_mutex_lock(&mutexNODES);
            addNodeToNodeList(&NODES, tempNode);
            pthread_mutex_unlock(&mutexNODES);
            
            pthread_mutex_lock(&mutexRNL);
            tempNode = popHeadNode(&RNL);
            pthread_mutex_unlock(&mutexRNL);
            nboNodes++;
        }
        
        // Schedule for unscheduled nodes in NODES
        nboNodes = schedule(TWOHOP_DATA_COLL_PHASE);
        
        if(nboNodes > 0){
            for(unsigned short i = 0; i < mac_nbo_sch_groups; i++){
//                pthread_mutex_lock(&mutexSCHEDULES);
                smPrintSchedule(&SCHEDULES[i]);
//                pthread_mutex_unlock(&mutexSCHEDULES);
            }
        }
          
        // Get number of node in the CM payload
        prepareDownlinkMsgPayload(&dlMsg, Twohop_MsgType_DL_CM, framePeriodCnt, NULL);
        if(dlMsg.size != 0){
            struct timeval msg_tx_time = getDownlinkTxTimestamp(mac_start_fp_time);
//            printf("DL Tx time: %ld.%06ld\n", msg_tx_time.tv_sec, msg_tx_time.tv_usec);
//            fprintf(log_file, "DL Tx time: %ld.%06ld\n", msg_tx_time.tv_sec, msg_tx_time.tv_usec);
            
            prepareDownlinkMsgMetaData(&dlMsg, msg_tx_time);

            // Add packet to the queue and trigger signal to the waiting threads
            pktEnqueue(&outboundMsgQueue, &dlMsg);

            // Notify lora server thread that the packet is ready to transmit
            pthread_mutex_lock(&mutexTxMsg);
            flagTxMsg = true;
            pthread_cond_signal(&condTxMsg);
            pthread_mutex_unlock(&mutexTxMsg);
        }
        
        // parse data every frame period
        AppParseData();
        
        //AppPushDataToAppServer();
        
        if(framePeriodCnt %5 == 0){
            pthread_mutex_lock(&mutexNODES);
            printNodeList(&NODES);
            pthread_mutex_unlock(&mutexNODES);
            
            //AppDisplayData();
        }
        
        gettimeofday(&current_time, NULL);
        delayUsec = (uint64_t) frameLength - getTimeOffetTimeval(mac_start_fp_time, current_time);
        usleep((__useconds_t)delayUsec);
    }
    MSG("\n[MAC] DATA COLLECTION PHASE END!\n");
    fprintf(log_file, "\n[MAC] DATA COLLECTION PHASE END!\n");
}

static void checkDataMissed(void){
    uint8_t disCount;
    disCount = dmCheckDataMissed(&NODES);

    /* TODO: Temporally disable this feature
    if(disCount > 0){
        // Remove disconnected node from the schedule
        MngtNode_t *mngtNode;
        mngtNode = dmGetHeadNodeRef(&NODES);
        while(mngtNode != NULL){
            if(mngtNode->genInfo.type == Node_Type_Onehop && mngtNode->isConnected == false){
                removeNodeFromSchedule(mngtNode->genInfo.addr);
                MSG("NODE %hu: Remove from schedule\n", mngtNode->genInfo.addr);
                fprintf(log_file, "NODE %hu: Remove from schedule\n", mngtNode->genInfo.addr);
            }
            mngtNode = dmGetRefToNextNode(mngtNode);
        }
    }
    */
}

static unsigned int getIndexOfLowestLoadSch(void){
    int i, index = 0;
    unsigned int minLoad;
    
    minLoad = SCHEDULES[0].nboAsgSlots;
    for(i = 0; i < mac_nbo_sch_groups; i++){
        if(SCHEDULES[i].nboAsgSlots < minLoad){
            minLoad = SCHEDULES[i].nboAsgSlots;
            index = i;
        }
    }
    return index;
}

static unsigned short schedule(OperationPhase_e phase){
    unsigned int group;
    unsigned short count;
    MngtNode_t *mngtNode;
    SchNode_t schNode;
    
    pthread_mutex_lock(&mutexNODES);
    mngtNode = dmGetHeadNodeRef(&NODES);
    
    count = 0;
    while(true){
        // Schedule all unscheduled nodes (scheduleFlag = false) in NODES
        if(mngtNode == NULL)
            break;
        
        if(mngtNode->genInfo.type == Node_Type_Onehop && mngtNode->schFlag == false){
            // Remove schedule of this node first
//            pthread_mutex_lock(&mutexSCHEDULES);
            removeNodeFromSchedule(mngtNode->genInfo.addr);
            group = getIndexOfLowestLoadSch();
//            pthread_mutex_unlock(&mutexSCHEDULES);
            
            schNode.addr = mngtNode->genInfo.addr;
            schNode.class = mngtNode->genInfo.class;
            schNode.slotDemand = mngtNode->genInfo.slotDmn;

            if(phase == TWOHOP_SCHEDULE_DIST_PHASE)
                schNode.nboSchDist = TWOHOP_NBO_SCH_UPDATE_SD;
            else if (phase == TWOHOP_DATA_COLL_PHASE)
                schNode.nboSchDist = TWOHOP_NBO_SCH_UPDATE_DC;
            else
               schNode.nboSchDist = TWOHOP_NBO_SCH_UPDATE_DEFAULT;
            
//            pthread_mutex_lock(&mutexSCHEDULES);
            SchErr_t schCheck = smScheduleOneNode(&SCHEDULES[group], schNode);
//            pthread_mutex_unlock(&mutexSCHEDULES);
            
            if(schCheck == SCH_SUCCEEDED){
                mngtNode->schFlag = true;
                mngtNode->isConnected = true;
                // Mark all children as connected
                dmSetChildConnStatus(&NODES, mngtNode, true);
//                MSG("NODE %u: Scheduled to GROUP %u\n", mngtNode->genInfo.addr, group);
//                fprintf(log_file, "NODE %u: Scheduled to GROUP %u\n", mngtNode->genInfo.addr, group);
            }
            count++;
        }
        mngtNode = dmGetRefToNextNode(mngtNode);
    }
    pthread_mutex_unlock(&mutexNODES);
    return count;
}

static void removeNodeFromSchedule(uint16_t addr){
    for(unsigned int i = 0; i < mac_nbo_sch_groups; i++){
        smRemoveOneNode(&SCHEDULES[i], (unsigned short)addr);
    }
    return;
}

static void clearSchedule(void){
    int i;
//    pthread_mutex_lock(&mutexSCHEDULES);
    for(i = 0; i < mac_nbo_sch_groups; i++){
        smClearSchedule(&SCHEDULES[i]);
    }
//    pthread_mutex_unlock(&mutexSCHEDULES);
}

static void prepareDownlinkMsgMetaData(struct MsgInfo_ *packet, struct timeval TxTimestamp){
    packet->tx_mode = IMMEDIATE;
    packet->rf_power = TWOHOP_DL_POWER;
    packet->invert_pol = false;
    packet->preamble = 8;
    
    packet->freq = TWOHOP_DOWNLINK_CHANNEL;
    packet->rf_chain = 0;
//    packet.if_chain;
    packet->modulation = MOD_LORA;
    packet->bandwidth = BW_125KHZ;
    packet->datarate = DR_LORA_SF7;
    packet->coderate = CR_LORA_4_5;
    packet->msg_tx_time.tv_sec = TxTimestamp.tv_sec;
    packet->msg_tx_time.tv_usec = TxTimestamp.tv_usec;
}

static void prepareDownlinkMsgPayload(MsgInfo_s *msg, TwohopMsgType_e type, uint16_t seq, void *addInfo){
    TwohopMacHeader_u macHeader;
    TwohopFrameHeader_s frmHeader;
    TwohopNodeAddrFormat_u addrFormat;
    SchNode_t *schNode;
    uint8_t grpIndex;
    uint8_t payloadLen;
    
    payloadLen = 0;
    macHeader.bits.pktType = type;
    macHeader.bits.RFU = 0;
    memcpy(&msg->payload[payloadLen], &macHeader.value, 1);
    payloadLen += 1;
    
    frmHeader.srcAddr = TWOHOP_SERVER_ADDR;
    memcpy(&msg->payload[payloadLen], &frmHeader.srcAddr, 2);
    payloadLen += 2;
    
    frmHeader.destAddr = TWOHOP_BROADCAST_ADDR;
    memcpy(&msg->payload[payloadLen], &frmHeader.destAddr, 2);
    payloadLen += 2;
    
    frmHeader.macParams.bits.frameFactor = mac_frame_factor;
    frmHeader.macParams.bits.uplinkSlotSize = mac_ul_slot_size_ms/10;
    frmHeader.macParams.bits.downlinkSlotSize = mac_dl_slot_size_ms/10;
    frmHeader.macParams.bits.nboChannels = mac_nbo_channels;
    
    memcpy(&msg->payload[payloadLen], &frmHeader.macParams, 2);
    payloadLen += 2;
    
//    printf("Mac params: %d %d %d %d\n", frmHeader.macParams.bits.frameFactor, frmHeader.macParams.bits.uplinkSlotSize, \
//         frmHeader.macParams.bits.downlinkSlotSize, frmHeader.macParams.bits.nboChannels);

    switch(type){
        case Twohop_MsgType_DL_RNL:
            frmHeader.seqNumber = seq;
            memcpy(&msg->payload[payloadLen], &frmHeader.seqNumber, 2);
            payloadLen += 2;
            
            // Insert RNLCtrl field
            frmHeader.rnlCtrl.bits.netReadyFlag = *((_Bool *)addInfo) ? 1 : 0;
            pthread_mutex_lock(&mutexRNL);
            frmHeader.rnlCtrl.bits.nboAddedNodes = (RNL.size > TWOHOP_MAX_NBO_NODES_IN_RNL) \
                                                    ? TWOHOP_MAX_NBO_NODES_IN_RNL : RNL.size;
            pthread_mutex_unlock(&mutexRNL);
            memcpy(&msg->payload[payloadLen], &frmHeader.rnlCtrl, 1);
            payloadLen += 1;
            
            // Insert RNL field
            MngtNode_t *tempNode;
            for(int i = 0; i < frmHeader.rnlCtrl.bits.nboAddedNodes; i++){
                pthread_mutex_lock(&mutexRNL);
                tempNode = popHeadNode(&RNL);
                pthread_mutex_unlock(&mutexRNL);
                
                if(tempNode != NULL){
                    pthread_mutex_lock(&mutexNODES);
                    addrFormat.bits.address = (uint16_t) tempNode->genInfo.addr;
                    addrFormat.bits.class = (uint16_t) tempNode->genInfo.class;
                    addNodeToNodeList(&NODES, tempNode);
                    pthread_mutex_unlock(&mutexNODES);
                } else {
                    addrFormat.bits.address = 0;
                    addrFormat.bits.class = 0;
                }
                memcpy(&msg->payload[payloadLen], &addrFormat, 2);
                payloadLen += 2;
                
                MSG("NODE %u: Added to RNL msg\n", addrFormat.bits.address);
                fprintf(log_file, "NODE %u: Added to RNL msg\n", addrFormat.bits.address);
            }
            msg->size = payloadLen;
            break;

        case Twohop_MsgType_DL_SM:
            frmHeader.smCtrl.bits.smCount = seq;
            frmHeader.smCtrl.bits.sch1Size = TWOHOP_NBO_SLOTS_IN_SCH1;
            memcpy(&msg->payload[payloadLen], &frmHeader.smCtrl, 1);
            payloadLen += 1;
            
            uint8_t sch2Sslot = *(uint8_t *)addInfo; // SCH2 start slot
            memcpy(&msg->payload[payloadLen], &sch2Sslot, 1);
            payloadLen += 1;
            
            uint8_t nboRelaysSm = dmGetNboRelays(&NODES);
            memcpy(&msg->payload[payloadLen], &nboRelaysSm, 1);
            payloadLen++;
            
            // Get group's schedule to be distributed
//            pthread_mutex_lock(&mutexSCHEDULES);
            for(grpIndex = 0; grpIndex < mac_nbo_sch_groups; grpIndex++){
                if(SCHEDULES[grpIndex].nboDistReq > 0)
                    break;
            }
            
            bool add_node_to_sm_done = false;
            
            if(grpIndex < mac_nbo_sch_groups){
                union smPlCtrl_{
                    uint8_t value;
                    struct plCtrl{
                        uint8_t groupId     : 3;
                        uint8_t nboNodes    : 5;
                    }bits;
                }smPlCtrl;
                
                // Add scheduling information header to the packet
                smPlCtrl.bits.groupId = grpIndex;
                smPlCtrl.bits.nboNodes = (SCHEDULES[grpIndex].nboDistReq > TWOHOP_MAX_NBO_NODES_IN_SM) \
                                        ? TWOHOP_MAX_NBO_NODES_IN_SM : SCHEDULES[grpIndex].nboDistReq;
                memcpy(&msg->payload[payloadLen], &smPlCtrl.value, 1);
                payloadLen += 1;
                
                if(smPlCtrl.bits.nboNodes > 0){
                    MSG("GROUP %hu: %hu nodes will be added to SM\n", grpIndex, smPlCtrl.bits.nboNodes);
                    fprintf(log_file, "GROUP %hu: %hu nodes will be added to SM\n", grpIndex, smPlCtrl.bits.nboNodes);
                }
                
                // To add nodes in the schedule to SM, first, find the first node that has schedule need to be distributed
                schNode = smGetHeadNodeRef(&SCHEDULES[grpIndex]);

                while(schNode != NULL && schNode->nboSchDist == 0){ // This implementation is dangerous, refine later
                    schNode = smGetNextNodeRef(schNode);
                }
    
                if(schNode != NULL){

                    // Up to here, schNode is the first node that has schedule need to be distributed
                    // Add start LSI information
                    uint8_t startLSI = schNode->startLSI;
                    memcpy(&msg->payload[payloadLen], &startLSI, 1);
                    payloadLen += 1;

                    MSG("START LSI: %u\n", startLSI);
                    fprintf(log_file, "START LSI: %u\n", startLSI);

                    while(smPlCtrl.bits.nboNodes--){
                        addrFormat.bits.address = (uint16_t) schNode->addr;
                        addrFormat.bits.class = (uint16_t) schNode->class;
                        memcpy(&msg->payload[payloadLen], &addrFormat, 2);
                        payloadLen += 2;

                        uint8_t slotDemand = (uint8_t)schNode->slotDemand;
                        memcpy(&msg->payload[payloadLen], &slotDemand, 1);
                        payloadLen += 1;

                        uint8_t nboSchDist = schNode->nboSchDist;

                        if(nboSchDist != 0)
                            nboSchDist--;

                        smNodeSetNboSchDist(&SCHEDULES[grpIndex], schNode->addr, nboSchDist);

                        // increase nbo slot 
                        pthread_mutex_lock(&mutexNODES);
                        if(dmIsRelayNode(&NODES, schNode->addr) == true)
                            *(uint8_t*)addInfo += 1;
                        pthread_mutex_unlock(&mutexNODES);

                        MSG("NODE %u: Added to SM\n", schNode->addr);
                        fprintf(log_file, "NODE %u: Added to SM\n", schNode->addr);

                        schNode = smGetNextNodeRef(schNode);
                    }
                    add_node_to_sm_done = true;
                }
            }
            
            if(!add_node_to_sm_done) {
                memset(&msg->payload, 0, sizeof(msg->payload));
                payloadLen = 0;
            }
//            pthread_mutex_unlock(&mutexSCHEDULES);
            msg->size = payloadLen;
            break;
        
        case Twohop_MsgType_DL_CM:
            frmHeader.seqNumber = seq;
            memcpy(&msg->payload[payloadLen], &frmHeader.seqNumber, 2);
            payloadLen += 2;
            
            //pthread_mutex_lock(&mutexSCHEDULES);
            uint8_t lastLSI;
            for(grpIndex = 0; grpIndex < mac_nbo_sch_groups; grpIndex++){
                lastLSI = smGetLastAsgLsi(&SCHEDULES[grpIndex]);
                memcpy(&msg->payload[payloadLen], &lastLSI, 1);
                payloadLen += 1;
            }
            
            // Determine number of relays will be added in uSI
            uint8_t nboRelaysUsi = 0;
            for(grpIndex = 0; grpIndex < mac_nbo_sch_groups; grpIndex++){
                if (nboRelaysUsi + SCHEDULES[grpIndex].nboDistReq > TWOHOP_MAX_NBO_RELAYS_IN_USI) {
                    nboRelaysUsi = TWOHOP_MAX_NBO_RELAYS_IN_USI;
                    break;
                }
                nboRelaysUsi += SCHEDULES[grpIndex].nboDistReq;
            }
            
            if(nboRelaysUsi > 0){
                frmHeader.cmCtrl.bits.usiFlag = 1;
                frmHeader.cmCtrl.bits.nboNodesInUsi = nboRelaysUsi;
                frmHeader.cmCtrl.bits.unuse = 0;
                memcpy(&msg->payload[payloadLen], &frmHeader.cmCtrl.value, 1);
                payloadLen += 1;
                
                union cmUsiCtrl_{
                    uint8_t value;
                    struct usiCtrl{
                        uint8_t groupId     : 3;
                        uint8_t nboChild    : 5;
                    }bits;
                }cmUsiCtrl;
                
                // Add uSI for relays in each group
                for(grpIndex = 0; grpIndex < mac_nbo_sch_groups; grpIndex++){
                    schNode = smGetHeadNodeRef(&SCHEDULES[grpIndex]);
                    while(nboRelaysUsi){
                        if(schNode == NULL)
                            break;
                        
                        if(schNode->nboSchDist > 0){
                            nboRelaysUsi--;

                            MSG("NODE %u: Added to USI\n", schNode->addr);
                            fprintf(log_file, "NODE %u: Added to USI\n", schNode->addr);
                            
                            uint8_t nboChild;
                            pthread_mutex_lock(&mutexNODES);
                            Child_t* child = dmNodeGetChildList(&NODES, (uint16_t) schNode->addr, &nboChild);
                            pthread_mutex_unlock(&mutexNODES);
                            cmUsiCtrl.bits.groupId = grpIndex;
                            cmUsiCtrl.bits.nboChild = nboChild;

                            memcpy(&msg->payload[payloadLen], &cmUsiCtrl.value, 1);
                            payloadLen += 1;

                            uint8_t startLSI = (uint8_t) schNode->startLSI;
                            memcpy(&msg->payload[payloadLen], &startLSI, 1);
                            payloadLen += 1;

                            // Add parent info
                            addrFormat.bits.address = (uint16_t) schNode->addr;
                            addrFormat.bits.class = (uint16_t) schNode->class;
                            memcpy(&msg->payload[payloadLen], &addrFormat, 2);
                            payloadLen += 2; 

                            // Add child
                            pthread_mutex_lock(&mutexNODES);
                            for(uint8_t childIndex = 0; childIndex < TWOHOP_MAX_NBO_CHILDREN; childIndex++){
                                if(child[childIndex].addr != 0){
                                    addrFormat.bits.address = (uint16_t) child[childIndex].addr;
                                    addrFormat.bits.class = (uint16_t) child[childIndex].class;
                                    memcpy(&msg->payload[payloadLen], &addrFormat, 2);
                                    payloadLen += 2;
                                    MSG("CHILD %u: Added to USI\n", child[childIndex].addr);
                                    fprintf(log_file, "CHILD %u: Added to USI\n", child[childIndex].addr);
                                }
                            }
                            pthread_mutex_unlock(&mutexNODES);

                            uint8_t nboSchDist = schNode->nboSchDist;

                            if(nboSchDist != 0)
                                nboSchDist--;

                            smNodeSetNboSchDist(&SCHEDULES[grpIndex], schNode->addr, nboSchDist);
                        }
                        
                        schNode = smGetNextNodeRef(schNode);
                    }
                }
            } else {
                frmHeader.cmCtrl.bits.usiFlag = 0;
                frmHeader.cmCtrl.bits.nboNodesInUsi = 0;
                frmHeader.cmCtrl.bits.unuse = 0;
                memcpy(&msg->payload[payloadLen], &frmHeader.cmCtrl.value, 1);
                payloadLen += 1;
            }
            
            //pthread_mutex_unlock(&mutexSCHEDULES);
            
            msg->size = payloadLen;
            break;
        
        default:
            break;
    }
}

/* pow() function for integer parameter */
static int ipow(int base, int exp) {
	int result = 1;
	while (exp) {
		if (exp & 1)
			result *= base;
		exp >>= 1;
		base *= base;
	}

	return result;
}

static unsigned short slotDemandCalculation(unsigned short class){
    return (unsigned short)ipow(2, (int) class);
}

static void *inputMsgHandlerThread(void *args){
    MsgInfo_s msg;
    TwohopMacHeader_u rxMacHdr;
    TwohopFrameHeader_s rxFrmHdr;
    MngtNode_t *node;
    uint8_t pktLen;
    int i;
    
    while(true){
        pthread_mutex_lock(&mutexRxMsg);
        flagRxMsg = false;
        while(flagRxMsg == false)
            pthread_cond_wait(&condRxMsg, &mutexRxMsg);
        pthread_mutex_unlock(&mutexRxMsg);
        
        while(pktDequeue(&inboundMsgQueue, &msg) == PKT_ERROR_OK){
            // Process input message
            pktLen = 0;
            memcpy(&rxMacHdr, &msg.payload[pktLen], 1);
            pktLen += 1;

            memcpy(&rxFrmHdr.srcAddr, &msg.payload[pktLen], 2);
            pktLen += 2;

            memcpy(&rxFrmHdr.destAddr, &msg.payload[pktLen], 2);
            pktLen += 2;

            switch(rxMacHdr.bits.pktType){
                case Twohop_MsgType_UL_RR:
                    if(rxFrmHdr.destAddr != TWOHOP_SERVER_ADDR)
                        continue;

                    memcpy(&rxFrmHdr.rrCtrl.value, &msg.payload[pktLen], 1);
                    pktLen += 1;
                    // Add registered nodes to RNL
                    TwohopNodeAddrFormat_u addFmt;
                    NodeGenInfo_t nodeGenInfo;
                    if(rxFrmHdr.rrCtrl.bits.rrType1 == 0){  // registration itself
                        memcpy(&addFmt.value, &msg.payload[pktLen], 2);
                        pktLen += 2;
                        nodeGenInfo.addr = (uint16_t)addFmt.bits.address;
                        nodeGenInfo.class = (uint8_t)addFmt.bits.class;
                        nodeGenInfo.slotDmn = slotDemandCalculation((uint8_t)addFmt.bits.class);
                        nodeGenInfo.type = Node_Type_Onehop;
                        node = createNewNode(nodeGenInfo, 0);
                        
                        if(node != NULL){
                            pthread_mutex_lock(&mutexRNL);
                            addNodeToNodeList(&RNL, node);
                            pthread_mutex_unlock(&mutexRNL);
                            MSG("NODE %u: Receive RR (%.2f, %.2f)\n", node->genInfo.addr, msg.rssi, msg.snr);
                            fprintf(log_file, "NODE %u: Receive RR (%.2f, %.2f)\n", node->genInfo.addr, msg.rssi, msg.snr);
                        } 
                    } else { // registration for the node and its child
                        uint8_t nboAddedNode = rxFrmHdr.rrCtrl.bits.nboChild;

                        // Update information for two hop node
                        for(i = 0; i < nboAddedNode; i++){
                            memcpy(&addFmt.value, &msg.payload[pktLen], 2);
                            pktLen += 2;
                            nodeGenInfo.addr = (uint16_t)addFmt.bits.address;
                            nodeGenInfo.class = (uint8_t)addFmt.bits.class;
                            nodeGenInfo.slotDmn = slotDemandCalculation((uint8_t)addFmt.bits.class);
                            
                            if(nodeGenInfo.addr == rxFrmHdr.srcAddr){
                                nodeGenInfo.type = Node_Type_Onehop;
                                node = createNewNode(nodeGenInfo, 0);
                            } else {
                                nodeGenInfo.type = Node_Type_Twohop;
                                node = createNewNode(nodeGenInfo, rxFrmHdr.srcAddr);
                            }

                            if(node != NULL){
                                pthread_mutex_lock(&mutexRNL);
                                addNodeToNodeList(&RNL, node);
                                pthread_mutex_unlock(&mutexRNL);
                                MSG("NODE %u: Receive RR via NODE %u\n", node->genInfo.addr, rxFrmHdr.srcAddr);
                                fprintf(log_file, "NODE %u: Receive RR via NODE %u\n", node->genInfo.addr, rxFrmHdr.srcAddr);
                            }
                        }
                    }
                    break;
                case Twohop_MsgType_UL_DATA:
                    memcpy(&rxFrmHdr.seqNumber, &msg.payload[pktLen], 2);
                    pktLen += 2;

                    memcpy(&rxFrmHdr.dataCtrl.value, &msg.payload[pktLen], 1);
                    pktLen += 1;

                    if(rxFrmHdr.dataCtrl.bits.ctrl1 == 1)
                        pktLen += 1; // skip Jslot field

                    uint16_t dataSrcAddr;
                    if(rxFrmHdr.dataCtrl.bits.ctrl0 == 1){
                        memcpy(&dataSrcAddr, &msg.payload[pktLen], 2);
                        pktLen += 2; 
                        MSG("NODE %hu: Receive DATA %hu (via NODE %hu)\n", dataSrcAddr, \
                                rxFrmHdr.seqNumber, rxFrmHdr.srcAddr);
                        fprintf(log_file, "NODE %hu: Receive DATA %hu (via NODE %hu)\n", dataSrcAddr, \
                                rxFrmHdr.seqNumber, rxFrmHdr.srcAddr);
                    } else {
                        int8_t snr = 0.0;
                        int16_t rssi = 0.0;
                        if(rxFrmHdr.dataCtrl.bits.ctrl2 == 1){
                            memcpy(&rssi, &msg.payload[pktLen], 2);
                            pktLen += 2; 
                            memcpy(&snr, &msg.payload[pktLen], 1);
                            pktLen += 1; 
                        }
                        dataSrcAddr = rxFrmHdr.srcAddr;
                        MSG("NODE %hu: Rx DATA %hu - DL(%d,%d) UL(%.2f,%.2f)\n", dataSrcAddr, rxFrmHdr.seqNumber, rssi, snr, msg.rssi, msg.snr);
                        fprintf(log_file, "NODE %hu: Rx DATA %hu - DL(%d,%d) UL(%.2f,%.2f)\n", dataSrcAddr, rxFrmHdr.seqNumber, rssi, snr, msg.rssi, msg.snr);
                    }

                    uint8_t payloadSize;
                    memcpy(&payloadSize, &msg.payload[pktLen], 1);
                    pktLen++;
                    
                    // process app data
                    AppInputDataHandle(dataSrcAddr, &msg.payload[pktLen], payloadSize);
                    
                    bool isRelayData = false;
                    if(rxFrmHdr.dataCtrl.bits.ctrl0 == 1){
                        isRelayData = true;
                    }
                    pthread_mutex_lock(&mutexNODES);
                    dmNodeUpdateDataInfo(&NODES, dataSrcAddr, rxFrmHdr.seqNumber, isRelayData);
                    pthread_mutex_unlock(&mutexNODES);
                    break;
                default:
                    break;
            }
        }
    }
}

// If it is one hop node, remove the node
// If it is two hop node, remove the node and update the information for its parent
//static _Bool removeNodeFromNodeList(MngtNodeList_t *lst, uint16_t addr){
//    MngtNode_t* tempNode;
//    
//    tempNode = popNodeByAddress(lst, addr);
//    if(tempNode != NULL){
//        if(tempNode->genInfo.type == Node_Type_Twohop){
//            MngtNode_t* parentNode = getNodeReferenceByAddress(lst, tempNode->parrentAddr);
//            if(parentNode != NULL)
//                dmRemoveChildOfNode(parentNode);
//        }
//        free(tempNode);
//    }
//    return true;
//}

// True: succeeded
// False: failed
static _Bool addNodeToNodeList(MngtNodeList_t *lst, MngtNode_t *node){
    if(node == NULL)
        return false;
    
    if(lst == &RNL){
        pushNode(lst, node);
        return true;
    }
    
    if(lst == &NODES){     
        if(node->genInfo.type == Node_Type_Onehop){
            // mark all old children as disconnected
            MngtNode_t *temp = getNodeReferenceByAddress(lst, node->genInfo.addr);
            if(temp != NULL){
                dmSetChildConnStatus(lst, temp, false);
                dmRemoveChildOfNode(temp);
            }
            
            pushNode(lst, node);
        } else {
            // If this node is 1-hop node previously,remove this node from schedule
            removeNodeFromSchedule(node->genInfo.addr);
            // Update its parent information  
            MngtNode_t *parent = popNodeByAddress(lst, node->parrentAddr);
            if(parent != NULL){
                if(dmAddChildToNode(parent, node) == true){
                    // If the child is added successfully, this whole subtree need to be scheduled again
                    // Then, set the schFlag of parent to False and add back to the list
                    dmNodeSetSchFlag(parent, false);
                    pushNode(lst, parent);
                } else {
                    // Add parent back to the list
                    pushNode(lst, parent);
                    free(node);
                    return false;
                }
                // Finally, add node to the list
                pushNode(lst, node);
            } else {
                MSG("[MAC] Parent of %hu not found.\n", node->genInfo.addr);
                fprintf(log_file, "[MAC] Parent of %hu not found.\n", node->genInfo.addr);
                free(node);
                return false;
            }
        }
        return true;
    }
    
    return false;
}
/* --- EOF ------------------------------------------------------------------ */
