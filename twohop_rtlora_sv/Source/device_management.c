/******************************************************************************
 * Filename     : device_management.h                                          *
 * Program      : Private LoRa Network Server Program                          *
 * Copyright    : Copyright (C) 2017-2017, ETRI                                *
 *              : URL << http://www.etri.re.kr >>                              *
 * Authors      : Woo-Sung Jung (woosung@etri.re.kr)                           *
 *              : Tae Hyun Yoon (thyoon0820@etri.re.kr)                        *
 *              : Dae Seung Yoo (ooseyds@etri.re.kr)                           *
 * Description  : Private LoRa End Device / GateWay management routine         *
 * Created at   : Tue Aug 01 2017.                                             *
 * Modified by  :                                                              *
 * Modified at  :                                                              *
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include "debug.h"
#include "device_management.h"
#include "lora_mac.h"
#include "parson.h"
#include "application.h"

//Initial Information Ptr
GateWayInfo_t GW_HEAD;
EndDeviceInfo_t ED_HEAD;

extern uint8_t Network_ID;
extern uint8_t DevEUI[8];
extern uint8_t AppEUI[8];
extern uint8_t AppKey[16];

extern uint8_t NwkSKey[16];
extern uint8_t AppSKey[16];

extern char con_path[256];
extern char dev_path[256];

extern unsigned int GW_PORT;
extern unsigned int AS_PORT;


const char device_information_fname[] = "Device_Information.json"; /* contain global (typ. network-wide) configuration */

/*
 *********************************************************************************************************
 *  Gateway Related Codes are here..
 *********************************************************************************************************
 */

/******************************************************************************
 * Function Name        : InitGateWayInfo
 * Input Parameters     : None
 * Return Value         : None
 * Function Description : GateWay Management Linked-List Structure Initialization..
 ******************************************************************************/
void InitGateWayInfo(void) {
    GW_HEAD.next = NULL;
}

/******************************************************************************
 * Function Name        : InitGateWayInfo
 * Input Parameters     : None
 * Return Value         : None
 * Function Description : GateWay Management Linked-List Structure Deinitialization..
 ******************************************************************************/
void DeinitGateWayInfo(void) {
    GateWayInfo_t *gwInfo;
    while (GW_HEAD.next != NULL) {
        gwInfo = GW_HEAD.next;
        GW_HEAD.next = gwInfo->next;
        //close socket all remaining client..
        close(gwInfo->socket);
        free(gwInfo);
    }
}

/******************************************************************************
 * Function Name        : AddGateWay
 * Input Parameters     : int clntsocket                 - Gateway Socket Number
 *                      : struct sockaddr_in clntAddress - Gateway Address Info
 * Return Value         : None
 * Function Description : New Gateway registration..
 ******************************************************************************/
void AddGateWay(int clntsocket, struct sockaddr_in clntAddress) {
    GateWayInfo_t *gwInfo;
    // Find exist Gateway Information using socket number..
    gwInfo = FindGateWay(clntsocket);

    if (gwInfo == NULL) {
        // New Gateway connects to Network Server..
        gwInfo = malloc(sizeof (GateWayInfo_t));
        if (gwInfo == NULL) {
            dprintf("Memory allocation fail!!\n");
            return;
        }
        dprintf("Add New GW Information - socket number = %d, IP = %s\n", clntsocket, inet_ntoa(clntAddress.sin_addr));
        gwInfo->socket = clntsocket;
        gwInfo->sockaddr = clntAddress;
        memset(gwInfo->rxBuffer, 0, TCP_STREAM_BUFFER_SIZE);
        gwInfo->currentRxBufferSize = 0;
        gwInfo->next = GW_HEAD.next;
        GW_HEAD.next = gwInfo;
    } else {
        // Never enter here..
        dprintf("New connection socket is already used for other GW\n");
        gwInfo->socket = clntsocket;
        gwInfo->sockaddr = clntAddress;
        memset(gwInfo->rxBuffer, 0, TCP_STREAM_BUFFER_SIZE);
        gwInfo->currentRxBufferSize = 0;
        return;
    }

}

/******************************************************************************
 * Function Name        : FindGateWay
 * Input Parameters     : int socket      - Gateway Socket Number
 * Return Value         : GateWayInfo_t * - Gateway Information Structure pointer
 * Function Description : Find Gateway Information structure..
 ******************************************************************************/
GateWayInfo_t *FindGateWay(int socket) {
    GateWayInfo_t *gwInfo;
    gwInfo = GW_HEAD.next;

    while (gwInfo != NULL) {
        if (gwInfo->socket == socket) {
//            dprintf("Find Gateway Information\n");
            return gwInfo;
        }
        gwInfo = gwInfo->next;
    }
    return NULL;
}

/******************************************************************************
 * Function Name        : RemoveGateWay
 * Input Parameters     : int socket      - Gateway Socket Number
 * Return Value         : None
 * Function Description : Remove Gateway Information structure..
 ******************************************************************************/
void RemoveGateWay(int socket) {
    GateWayInfo_t *gwInfo;
    GateWayInfo_t *gwInfo_prev;
    gwInfo = GW_HEAD.next;
    gwInfo_prev = &GW_HEAD;

    while (gwInfo != NULL) {
        if (gwInfo->socket == socket) {
            dprintf("Gateway(socket = %d, IP = %s) Information will be removed..\n", gwInfo->socket, inet_ntoa(gwInfo->sockaddr.sin_addr));
            gwInfo_prev->next = gwInfo->next;
            free(gwInfo);

            // GW information will be removed in the End Device Information
            RemoveGateWayFromEndDevice(socket);

            return;
        }
        gwInfo_prev = gwInfo;
        gwInfo = gwInfo->next;
    }

    dprintf("We cannot find Gateway information..\n");
}



/*
 *********************************************************************************************************
 *  End Device Related Codes are here..
 *********************************************************************************************************
 */

/******************************************************************************
 * Function Name        : InitEndDeviceInfo
 * Input Parameters     : None
 * Return Value         : None
 * Function Description : EndDevice Management Linked-List Structure Initialization..
 ******************************************************************************/
void InitEndDeviceInfo(void) {
    ED_HEAD.next = NULL;
}

/******************************************************************************
 * Function Name        : DeinitEndDeviceInfo
 * Input Parameters     : None
 * Return Value         : None
 * Function Description : EndDevice Management Linked-List Structure Deinitialization..
 ******************************************************************************/
void DeinitEndDeviceInfo(void) {
    EndDeviceInfo_t *edInfo;
    while (ED_HEAD.next != NULL) {
        edInfo = ED_HEAD.next;
        ED_HEAD.next = edInfo->next;
        free(edInfo);
    }
}

/******************************************************************************
 * Function Name        : AddEndDeviceInfo
 * Input Parameters     : uint32_t address - End Device Address
 *                      : int gwSocket     - Rx Gateway Socekt
 *                      : LoRaJoinReqMsg_t - Received LoRaWAN Join Message Info
 *                      : int16_t rssi     - Received Signal Strength
 *                      : int8_t snr       - Signal to Noise Ratio
 * Return Value         : End Device Info (ptr)
 * Function Description : Add End Device Information
 ******************************************************************************/
EndDeviceInfo_t *AddEndDeviceInfo(uint32_t address, int gwSocket, LoRaJoinReqMsg_t rxMsg, int16_t rssi, int8_t snr, uint8_t cr, uint8_t sf, uint32_t ch, uint8_t bw, uint8_t* deui) {
    uint8_t loopcnt;
    EndDeviceInfo_t *edInfo;
    EndDeviceInfo_t *edInfoTmpPrev;
    EndDeviceInfo_t *edInfoTmpCur;
    edInfo = FindEndDevice(address);

    if (edInfo == NULL) {
        // New End Device -> Add New Information
        edInfo = malloc(sizeof (EndDeviceInfo_t));
        if (edInfo == NULL) {
            dprintf("Memory allocation fail!!\n");
            return NULL;
        }
        dprintf("Add New EndDevice Information - address = %d\n", address);
        edInfo->address = address;
        edInfo->totalRxNum = 0;
        edInfo->totalRxSize = 0;
        edInfo->seqNum = 0;

        edInfo->devcr = cr;
        edInfo->devsf = sf;
        edInfo->devch = ch;
        edInfo->devbw = bw;

        edInfo->totalRxNumPrev = 0;
        edInfo->totalRxSizePrev = 0;
        edInfo->seqNumPrev = 0;

        memcpy(edInfo->DevNonce, rxMsg.DevNonce, 2);
        memcpy(edInfo->DevEUI, deui, 8);

        // Primary GW
        edInfo->gw[0].socket = gwSocket;
        edInfo->gw[0].rssi = rssi;
        edInfo->gw[0].snr = snr;
        edInfo->gw[0].totalRxNum = 0;
        edInfo->gw[0].totalRxSize = 0;
        time(&(edInfo->gw[0].rxTime));
        // Alternative GW
        edInfo->gw[1].socket = -1;
        edInfo->gw[1].rssi = 0;
        edInfo->gw[1].snr = 0;
        edInfo->gw[1].totalRxNum = 0;
        edInfo->gw[1].totalRxSize = 0;
        edInfo->gw[1].rxTime = 0;

        // Sorting by Address 
        edInfoTmpPrev = &ED_HEAD;
        edInfoTmpCur = ED_HEAD.next;
        while (edInfoTmpCur != NULL) {
            if (edInfoTmpCur->address > address) {
                edInfoTmpPrev->next = edInfo;
                edInfo->next = edInfoTmpCur;
                return edInfo;
            }
            edInfoTmpPrev = edInfoTmpCur;
            edInfoTmpCur = edInfoTmpCur->next;
        }
        if (edInfoTmpCur == NULL) {
            edInfoTmpPrev->next = edInfo;
            edInfo->next = NULL; // edInfoTmpCur	
        }

        return edInfo;
    } else {
        if (edInfo->gw[0].socket == gwSocket) {
            edInfo->gw[0].rssi = rssi;
            edInfo->gw[0].snr = snr;
            time(&(edInfo->gw[0].rxTime));
        } else if (edInfo->gw[1].socket == gwSocket) {
            edInfo->gw[1].rssi = rssi;
            edInfo->gw[1].snr = snr;
            time(&(edInfo->gw[1].rxTime));
        } else if (edInfo->gw[1].socket == -1) {
            edInfo->gw[1].socket = gwSocket;
            edInfo->gw[1].rssi = rssi;
            edInfo->gw[1].snr = snr;
            edInfo->devcr = cr;
            edInfo->devsf = sf;
            edInfo->devch = ch;
            edInfo->devbw = bw;

            time(&(edInfo->gw[1].rxTime));

            // Which GW is better?
            if (edInfo->gw[0].rssi < edInfo->gw[1].rssi) {
                dprintf("Change primary and Alternative GW\n");
                // switch GW priority
                GateWayRxInfo_t tempGWRxInfo;
                tempGWRxInfo.socket = edInfo->gw[0].socket;
                tempGWRxInfo.rssi = edInfo->gw[0].rssi;
                tempGWRxInfo.snr = edInfo->gw[0].snr;
                tempGWRxInfo.totalRxNum = edInfo->gw[0].totalRxNum;
                tempGWRxInfo.totalRxSize = edInfo->gw[0].totalRxSize;
                tempGWRxInfo.rxTime = edInfo->gw[0].rxTime;

                edInfo->gw[0].socket = edInfo->gw[1].socket;
                edInfo->gw[0].rssi = edInfo->gw[1].rssi;
                edInfo->gw[0].snr = edInfo->gw[1].snr;
                edInfo->gw[0].totalRxNum = edInfo->gw[1].totalRxNum;
                edInfo->gw[0].totalRxSize = edInfo->gw[1].totalRxSize;
                edInfo->gw[0].rxTime = edInfo->gw[1].rxTime;

                edInfo->gw[1].socket = tempGWRxInfo.socket;
                edInfo->gw[1].rssi = tempGWRxInfo.rssi;
                edInfo->gw[1].snr = tempGWRxInfo.snr;
                edInfo->gw[1].totalRxNum = tempGWRxInfo.totalRxNum;
                edInfo->gw[1].totalRxSize = tempGWRxInfo.totalRxSize;
                edInfo->gw[1].rxTime = tempGWRxInfo.rxTime;
            }
        }

        return edInfo;
    }
}

/******************************************************************************
 * Function Name        : RxEndDeviceInfoUpdate
 * Input Parameters     : uint32_t address - End Device Address
 *                      : int gwSocket     - Rx Gateway Socekt
 *                      : int16_t rssi     - RSSI
 *                      : int8_t snr       - Signal to Noise Ratio
 *                      : int8_t pktSize   - Received LoRaWAN Frame size
 *                      : uint32_t seqNum  - End Device Seq. Number (UP_CNT)
 * Return Value         : None
 * Function Description : Add/Update End Device Information
 ******************************************************************************/
void RxEndDeviceInfoUpdate(uint32_t address, int gwSocket, int16_t rssi, int8_t snr, uint8_t pktSize, uint32_t seqNum, uint8_t cr, uint8_t sf, uint32_t ch, uint8_t bw) {
    EndDeviceInfo_t *edInfo;
    // find End Device Information Structure
    edInfo = FindEndDevice(address);


    if (edInfo == NULL) {
        // New End Device -> Add New Information
        edInfo = malloc(sizeof (EndDeviceInfo_t));
        if (edInfo == NULL) {
            dprintf("Memory allocation fail!!\n");
            return;
        }
        dprintf("Add New EndDevice Information - address = %d\n", address);
        edInfo->address = address;
        edInfo->totalRxNum = 1;
        edInfo->totalRxSize = pktSize;
        edInfo->seqNum = seqNum;
        edInfo->totalRxNumPrev = 0;
        edInfo->totalRxSizePrev = 0;
        edInfo->seqNumPrev = 0;

        edInfo->devcr = cr;
        edInfo->devsf = sf;
        edInfo->devch = ch;
        edInfo->devbw = bw;

        // Primary GW
        edInfo->gw[0].socket = gwSocket;
        edInfo->gw[0].rssi = rssi;
        edInfo->gw[0].snr = snr;
        edInfo->gw[0].totalRxNum = 0;
        edInfo->gw[0].totalRxSize = 0;
        time(&(edInfo->gw[0].rxTime));
        // Alternative GW
        edInfo->gw[1].socket = -1;
        edInfo->gw[1].rssi = 0;
        edInfo->gw[1].snr = 0;
        edInfo->gw[1].totalRxNum = 0;
        edInfo->gw[1].totalRxSize = 0;
        edInfo->gw[1].rxTime = 0;
        edInfo->next = ED_HEAD.next;
        ED_HEAD.next = edInfo;
    } else {
        // Existing End Device -> Update Information
        dprintf("Update EndDevice Information - address = %d\n", address);
        edInfo->totalRxNum++;
        edInfo->totalRxSize += pktSize;
        edInfo->seqNum = seqNum;
        edInfo->devcr = cr;
        edInfo->devsf = sf;
        edInfo->devch = ch;
        edInfo->devbw = bw;

        if (edInfo->gw[0].socket == gwSocket) {
            edInfo->gw[0].rssi = rssi;
            edInfo->gw[0].snr = snr;
            time(&(edInfo->gw[0].rxTime));
        } else if (edInfo->gw[1].socket == gwSocket) {
            edInfo->gw[1].rssi = rssi;
            edInfo->gw[1].snr = snr;
            time(&(edInfo->gw[1].rxTime));
        } else {
            // New GW..
            if (edInfo->gw[1].socket == -1) {
                // Add Info
                edInfo->gw[1].socket = gwSocket;
                edInfo->gw[1].rssi = rssi;
                edInfo->gw[1].snr = snr;
                time(&(edInfo->gw[1].rxTime));
            } else if (edInfo->gw[1].rssi < rssi) {
                edInfo->gw[1].socket = gwSocket;
                edInfo->gw[1].rssi = rssi;
                edInfo->gw[1].snr = snr;
                time(&(edInfo->gw[1].rxTime));
            } else {
                // New Gw Rx Signal is bad..
                dprintf("New GW RSSI is Bad..\n");
                return;
            }
        }

        // Only Primary GW information is existed.
        if (edInfo->gw[1].socket == -1) {
            return;
        }

        // Which GW is better?
        if (edInfo->gw[0].rssi < edInfo->gw[1].rssi) {
            dprintf("Change primary and Alternative GW\n");
            // switch GW priority
            GateWayRxInfo_t tempGWRxInfo;
            tempGWRxInfo.socket = edInfo->gw[0].socket;
            tempGWRxInfo.rssi = edInfo->gw[0].rssi;
            tempGWRxInfo.snr = edInfo->gw[0].snr;
            tempGWRxInfo.totalRxNum = edInfo->gw[0].totalRxNum;
            tempGWRxInfo.totalRxSize = edInfo->gw[0].totalRxSize;
            tempGWRxInfo.rxTime = edInfo->gw[0].rxTime;

            edInfo->gw[0].socket = edInfo->gw[1].socket;
            edInfo->gw[0].rssi = edInfo->gw[1].rssi;
            edInfo->gw[0].snr = edInfo->gw[1].snr;
            edInfo->gw[0].totalRxNum = edInfo->gw[1].totalRxNum;
            edInfo->gw[0].totalRxSize = edInfo->gw[1].totalRxSize;
            edInfo->gw[0].rxTime = edInfo->gw[1].rxTime;

            edInfo->gw[1].socket = tempGWRxInfo.socket;
            edInfo->gw[1].rssi = tempGWRxInfo.rssi;
            edInfo->gw[1].snr = tempGWRxInfo.snr;
            edInfo->gw[1].totalRxNum = tempGWRxInfo.totalRxNum;
            edInfo->gw[1].totalRxSize = tempGWRxInfo.totalRxSize;
            edInfo->gw[1].rxTime = tempGWRxInfo.rxTime;
        }
    }

}

/******************************************************************************
 * Function Name        : FindEndDevice
 * Input Parameters     : uint32_t address  - End Device Address
 * Return Value         : EndDeviceInfo_t * - End Device Information Structure pointer
 * Function Description : Find End Device Information structure..
 ******************************************************************************/
EndDeviceInfo_t *FindEndDevice(uint32_t address) {
    EndDeviceInfo_t *edInfo;
    edInfo = ED_HEAD.next;

    while (edInfo != NULL) {
        if (edInfo->address == address) {
            dprintf("Find Gateway Information\n");
            return edInfo;
        }
        edInfo = edInfo->next;
    }
    return NULL;
}

/******************************************************************************
 * Function Name        : FindEndDevice
 * Input Parameters     : uint32_t address - End Device Address
 * Return Value         : int              - Gateway socket Number
 * Function Description : Find Gateway socket number to send LoRa Pkt.
 ******************************************************************************/
int FindGWSocket(uint32_t address) {
    EndDeviceInfo_t *edInfo;
    // find End Device Information Structure
    edInfo = FindEndDevice(address);

    dprintf("Find End Device Information .. address is %08x\n", address);

    if (edInfo == NULL) {
        // New End Device Address.. We have no information..
        dprintf("Target End Device cannot be reached.. No route..\n");
        return -1;
    }

    // Return promary GW socket
    return edInfo->gw[0].socket;
}

/******************************************************************************
 * Function Name        : RemoveGateWayFromEndDevice
 * Input Parameters     : int socket - Gateway socket
 * Return Value         : None
 * Function Description : Remove GW information from End Device Information 
 ******************************************************************************/
void RemoveGateWayFromEndDevice(int gwSocket) {
    EndDeviceInfo_t *edInfo;
    edInfo = ED_HEAD.next;

    while (edInfo != NULL) {
        if (edInfo->gw[0].socket == gwSocket) {
            edInfo->gw[0].socket = edInfo->gw[1].socket;
            edInfo->gw[0].rssi = edInfo->gw[1].rssi;
            edInfo->gw[0].snr = edInfo->gw[1].snr;
            edInfo->gw[0].totalRxNum = edInfo->gw[1].totalRxNum;
            edInfo->gw[0].totalRxSize = edInfo->gw[1].totalRxSize;

            edInfo->gw[0].rxTime = edInfo->gw[1].rxTime;

            edInfo->gw[1].socket = -1;
            edInfo->gw[1].rssi = 0;
            edInfo->gw[1].snr = 0;
        } else if (edInfo->gw[1].socket == gwSocket) {
            edInfo->gw[1].socket = -1;
            edInfo->gw[1].rssi = 0;
            edInfo->gw[1].snr = 0;
        }

        edInfo = edInfo->next;
    }
}

//*********************************************************************************************************
//*  Visualization Related Codes are here..
//*********************************************************************************************************

void ShowEndDevices() {
    EndDeviceInfo_t *edInfo;
    edInfo = ED_HEAD.next;
    GateWayInfo_t *tmpGateWayInfo;
    struct tm *tmPtr;
    uint8_t loopcnt;

    printf("Device Information\n");
    if (edInfo == NULL) {
        printf(" - Empty..\n\n");
        return;
    }

    while (edInfo != NULL) {
        // Print End Device Information Here..
        printf("- Device Address : 0x%08x\n", edInfo->address);
        printf("  Total Received Number of Packet(s) : %d <- %d\n", edInfo->totalRxNum, edInfo->totalRxNumPrev);
        printf("  Total Received Size of Packet(s)   : %d <- %d\n", edInfo->totalRxSize, edInfo->totalRxSizePrev);
        printf("  sequence Number : %d <- %d\n", edInfo->seqNum, edInfo->seqNumPrev);
        edInfo->totalRxNumPrev = edInfo->totalRxNum;
        edInfo->totalRxSizePrev = edInfo->totalRxSize;
        edInfo->seqNumPrev = edInfo->seqNum;

        printf("  ch %d\n", edInfo->devch);
        printf("  cr %d\n", edInfo->devcr);
        printf("  sf %d\n", edInfo->devsf);
        printf("  bw %d\n ", edInfo->devbw);
        printf("  Application EUI : ");

        for (loopcnt = 0; loopcnt < 8; loopcnt++) {
            printf("%02x ", edInfo->AppEUI[loopcnt]);
        }
        printf("\n");
        printf("  Network S Key   : ");
        for (loopcnt = 0; loopcnt < 16; loopcnt++) {
            printf("%02x ", edInfo->NwkSKey[loopcnt]);
        }
        printf("\n");
        printf("  App S Key       : ");
        for (loopcnt = 0; loopcnt < 16; loopcnt++) {
            printf("%02x ", edInfo->AppSKey[loopcnt]);
        }
        printf("\n");
        printf("  device Nonce    : %02x %02x\n", edInfo->DevNonce[0], edInfo->DevNonce[1]);

        if (edInfo->gw[0].socket != -1) {
            tmPtr = localtime(&(edInfo->gw[0].rxTime));
            tmpGateWayInfo = FindGateWay(edInfo->gw[0].socket);
            printf("  Primary GW   - Socket : %d\n", edInfo->gw[0].socket);
            if (tmpGateWayInfo != NULL)
                printf("               - IP ADDR: %s\n", inet_ntoa(tmpGateWayInfo->sockaddr.sin_addr));
            printf("               - RSSI   : %d\n", edInfo->gw[0].rssi);
            printf("               - SNR    : %d\n", edInfo->gw[0].snr);
            printf("               - Time   : %d-%d-%d %d:%d:%d\n",
                    tmPtr->tm_year + 1900,
                    tmPtr->tm_mon + 1,
                    tmPtr->tm_mday,
                    tmPtr->tm_hour,
                    tmPtr->tm_min,
                    tmPtr->tm_sec);
        }
        if (edInfo->gw[1].socket != -1) {
            tmPtr = localtime(&(edInfo->gw[1].rxTime));
            tmpGateWayInfo = FindGateWay(edInfo->gw[1].socket);
            printf("  Secondary GW - Socket : %d\n", edInfo->gw[1].socket);
            if (tmpGateWayInfo != NULL)
                printf("               - IP ADDR: %s\n", inet_ntoa(tmpGateWayInfo->sockaddr.sin_addr));
            printf("               - RSSI   : %d\n", edInfo->gw[1].rssi);
            printf("               - SNR    : %d\n", edInfo->gw[1].snr);
            printf("               - Time   : %d-%d-%d %d:%d:%d\n",
                    tmPtr->tm_year + 1900,
                    tmPtr->tm_mon + 1,
                    tmPtr->tm_mday,
                    tmPtr->tm_hour,
                    tmPtr->tm_min,
                    tmPtr->tm_sec);
        }

        edInfo = edInfo->next;
    }
}

void WriteEndDevices(int fd, int gwsocket) {
    EndDeviceInfo_t *edInfo;
    edInfo = ED_HEAD.next;
    GateWayInfo_t *tmpGateWayInfo;
    struct tm *tmPtr;
    uint8_t loopcnt;
    char tempmsg[512];

    write(fd, "\nRx Info,", 9);
    if (edInfo == NULL) {
        return;
    }

    while (edInfo != NULL) {
        sprintf(tempmsg, "Address=0x%08x, RxNum=%d,RxSize=%d,Seq=%d,", edInfo->address, edInfo->totalRxNum, edInfo->totalRxSize, edInfo->seqNum);
        write(fd, tempmsg, strlen(tempmsg));
        /*		
                        if(edInfo->gw[0].socket == gwsocket)
                        {
                                write(fd, "Rx via GW1,", 11);
                        }
                        else if(edInfo->gw[1].socket == gwsocket)
                        {
                                write(fd, "Rx via GW2,", 11);
                        }

                        if(edInfo->gw[0].socket != -1)
                        {
                                tmPtr = localtime(&(edInfo->gw[0].rxTime));
                                tmpGateWayInfo = FindGateWay(edInfo->gw[0].socket);
                                sprintf(tempmsg, "GW1,IP=%s, RSSI=%d, SNR=%d, RxTime=%d-%d-%d %d:%d:%d,", 
                                                inet_ntoa(tmpGateWayInfo->sockaddr.sin_addr), edInfo->gw[0].rssi, edInfo->gw[0].snr,
                                                tmPtr->tm_year+1900,
                                                tmPtr->tm_mon+1,
                                                tmPtr->tm_mday,
                                                tmPtr->tm_hour,
                                                tmPtr->tm_min,
                                                tmPtr->tm_sec);
                                write(fd, tempmsg, strlen(tempmsg));
                        }
                        if(edInfo->gw[1].socket != -1)
                        {
                                tmPtr = localtime(&(edInfo->gw[1].rxTime));
                                tmpGateWayInfo = FindGateWay(edInfo->gw[1].socket);
                                sprintf(tempmsg, "GW2,IP=%s, RSSI=%d, SNR=%d, RxTime=%d-%d-%d %d:%d:%d,", 
                                                inet_ntoa(tmpGateWayInfo->sockaddr.sin_addr), edInfo->gw[1].rssi, edInfo->gw[1].snr,
                                                tmPtr->tm_year+1900,
                                                tmPtr->tm_mon+1,
                                                tmPtr->tm_mday,
                                                tmPtr->tm_hour,
                                                tmPtr->tm_min,
                                                tmPtr->tm_sec);
                                write(fd, tempmsg, strlen(tempmsg));
                        }
         */
        edInfo = edInfo->next;
    }
}

void ShowGateWays() {
    GateWayInfo_t *gwInfo;
    gwInfo = GW_HEAD.next;
    int i;

    printf("Gateway Information\n");

    if (gwInfo == NULL) {
        printf(" - Empty..\n\n");
        return;
    }
    while (gwInfo != NULL) {
        printf("GW Socket Number : %d\n", gwInfo->socket);
        printf("GW IP Address    : %s\n", inet_ntoa(gwInfo->sockaddr.sin_addr));
        printf("GW Rx Buffer Size: %d\n", gwInfo->currentRxBufferSize);
        if (gwInfo->currentRxBufferSize > 0) {
            printf("   00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F");
            for (i = 0; i < gwInfo->currentRxBufferSize; i++) {
                if ((i % 16) == 0) {
                    printf("\n%01X0 ", (int) (i / 16));
                }
                printf("%02X ", gwInfo->rxBuffer[i]);
            }
            printf("\n");
        }

        gwInfo = gwInfo->next;
    }
}

void PreConfigNode(void) {

    int i, j;

    for (i = 0; i < 5; i++) {

        EndDeviceInfo_t *edInfo;
        EndDeviceInfo_t *edInfoTmpPrev;
        EndDeviceInfo_t *edInfoTmpCur;

        edInfo = malloc(sizeof (EndDeviceInfo_t));
        if (edInfo == NULL) {
            dprintf("Memory allocation fail!!\n");
        }
        dprintf("Add New EndDevice Information - address = %d\n", i);

        edInfo->address = 0x01110001 + i;
        edInfo->totalRxNum = 0;
        edInfo->totalRxSize = 0;
        edInfo->seqNum = 0;
        edInfo->totalRxNumPrev = 0;
        edInfo->totalRxSizePrev = 0;
        edInfo->seqNumPrev = 0;
        memset(edInfo->AppEUI, 0, 8);

        for (j = 0; j < 8; j++) {
            edInfo->AppEUI[j] = AppEUI[j];
        }

        edInfo->DevNonce[0] = 0;
        edInfo->DevNonce[1] = i;

        for (j = 0; j < 16; j++) {
            edInfo->AppSKey[j] = AppSKey[j];
            edInfo->NwkSKey[j] = NwkSKey[j];
        }

        edInfo->devcr = 1;
        edInfo->devsf = 7;
        edInfo->devch = 923300000;

        // Primary GW
        edInfo->gw[0].socket = -1;
        edInfo->gw[0].rssi = 0;
        edInfo->gw[0].snr = 0;
        edInfo->gw[0].totalRxNum = 0;
        edInfo->gw[0].totalRxSize = 0;
        edInfo->gw[0].rxTime = 0;
        // Alternative GW
        edInfo->gw[1].socket = -1;
        edInfo->gw[1].rssi = 0;
        edInfo->gw[1].snr = 0;
        edInfo->gw[1].totalRxNum = 0;
        edInfo->gw[1].totalRxSize = 0;
        edInfo->gw[1].rxTime = 0;

        // Sorting by Address
        edInfoTmpPrev = &ED_HEAD;
        edInfoTmpCur = ED_HEAD.next;
        while (edInfoTmpCur != NULL) {
            if (edInfoTmpCur->address > edInfo->address) {
                edInfoTmpPrev->next = edInfo;
                edInfo->next = edInfoTmpCur;
            }
            edInfoTmpPrev = edInfoTmpCur;
            edInfoTmpCur = edInfoTmpCur->next;
        }
        if (edInfoTmpCur == NULL) {
            edInfoTmpPrev->next = edInfo;
            edInfo->next = NULL; // edInfoTmpCur	
        }
    }
}

//
//int parse_Info_update(const char * conf_file, uint8_t* dev_eui, uint8_t* app_eui, uint32_t dev_addr, uint8_t* asky, uint8_t* nsky) {
//    int i,j;
//    const char conf_obj[] = "LoRa_Network_Server";
//    char param_name[64]; /* used to generate variable parameter names */
//    const char *str; /* used to store string value from JSON object */
//    uint32_t device_num = 0;
//    const char *DEVEUI;
//    const char *APPEUI;
//    const char *APPKEY;
//    char str_buff[40];
//    char dev_buff[40];
//    char app_buff[40];
//    
//    uint8_t dev_check = 0;
//    uint8_t app_check = 0;
//    
//    char *serialized_string = NULL;
//    
//    JSON_Value *root_val;
//    JSON_Object *root = NULL;
//    JSON_Object *conf = NULL;
//    JSON_Value *val;
//    JSON_Status result;
//    
//    unsigned long long ull = 0;
//	
//
//    /* try to parse JSON */
//    root_val = json_parse_file_with_comments(conf_file);
//    //printf(" %s \n", root_val);
//    
//    root = json_value_get_object(root_val);
//    if (root == NULL) {
//        dprintf("ERROR: %s id not a valid JSON file\n", conf_file);
//        //exit(EXIT_FAILURE);
//    }
//    
//    conf = json_object_get_object(root, conf_obj);
//    if (conf == NULL) {
//        dprintf("INFO: %s does not contain a JSON object named %s\n", conf_file, conf_obj);
//        return -1;
//    } else {
//        dprintf("INFO: %s does contain a JSON object named %s\n", conf_file, conf_obj);
//        //exit(EXIT_FAILURE);
//    }
//
//    /* read Device_Number configuration */
//    val = json_object_get_value(conf, "Device_Number"); /* fetch value (if possible) */
//    if (json_value_get_type(val) == JSONNumber) {
//        device_num = (uint32_t)json_value_get_number(val);
//        dprintf("INFO: Device_number %d\n", device_num);
//    } else {
//        dprintf("WARNING: Data type for Device_Number seems wrong, please check\n");
//        device_num = 0;
//		//exit(EXIT_FAILURE);
//    }
//
// 
//    for (i = 1; i < device_num+1; i++) {
//        
//        sprintf(param_name, "Device_%i", i); /* compose parameter path inside JSON structure */
//        val = json_object_get_value(conf, param_name); /* fetch value (if possible) */
//        if (json_value_get_type(val) != JSONObject) {
//            dprintf("INFO: no configuration for Device %i\n", i);
//            continue;
//        }
//        else{
//			dprintf("INFO: Device.%i\n", i);
//		}
//
//		snprintf(param_name, sizeof param_name, "Device_%i.DEUI", i);
//		DEVEUI = json_object_dotget_string(conf, param_name);
//		if (DEVEUI != NULL) {
//			dprintf("INFO: Device.DEUI is configured to %s\n", DEVEUI);
//			hex_to_char(dev_buff,(char*)DEVEUI,8);
//			dev_check = 1;			
//			for(j=0;j<8;j++) {
//				if(dev_eui[j] != dev_buff[j]){
//					dev_check = 0;			
//				}
//				DevEUI[j] = dev_buff[j];
//			}
//		}	
//		
//		snprintf(param_name, sizeof param_name, "Device_%i.AEUI", i);
//		APPEUI = json_object_dotget_string(conf, param_name);
//		if (APPEUI != NULL) {
//			dprintf("INFO: Device.AEUI is configured to %s\n", APPEUI);
//			hex_to_char(app_buff,(char*)APPEUI,8);
//			app_check = 1;			
//			for(j=0;j<8;j++) {
//				if(app_eui[j] != app_buff[j]){
//					app_check = 0;			
//				}
//				AppEUI[j] = app_buff[j];
//			}
//		}
//		
//		if((dev_check == 1) && (app_check == 1)){
//			dprintf("Find update Device!\n");
//			
//			memset(str_buff,0,16);
//			memset(dev_buff,0,16);
//			snprintf(param_name, sizeof param_name, "Device_%i.DVAD", i);
//			
//			str_buff[0] = (dev_addr >> 24) & 0xff;
//			str_buff[1] = (dev_addr >> 16) & 0xff;
//			str_buff[2] = (dev_addr >> 8) & 0xff;
//			str_buff[3] = dev_addr & 0xff;
//			
//			char_to_hex(dev_buff, str_buff, 4);
//			dev_buff[8] = 0;
//			result = json_object_dotset_string(conf, param_name, dev_buff);
//			if(result == JSONFailure){
//				printf("DVAD Fail\n");
//			}
//
//			snprintf(param_name, sizeof param_name, "Device_%i.ASKY", i);			
//			
//			char_to_hex(dev_buff, asky, 16);
//			dev_buff[32] = 0;
//			result = json_object_dotset_string(conf, param_name, dev_buff);
//			if(result == JSONFailure){
//				printf("ASKY Fail\n");
//			}
//			snprintf(param_name, sizeof param_name, "Device_%i.NSKY", i);
//			char_to_hex(dev_buff, nsky, 16);
//			dev_buff[32] = 0;
//			result = json_object_dotset_string(conf, param_name, dev_buff);
//			if(result == JSONFailure){
//				printf("NSKY Fail\n");
//			}
//			
//			json_serialize_to_file_pretty(root_val, (const char*)con_path);
//			json_free_serialized_string(serialized_string);
//			json_value_free(root_val);
//			return 1;
//		}
//    }
//    json_value_free(root_val);
//    return 0;
//}




//int parse_join_configuration(const char * conf_file, uint8_t* dev_eui, uint8_t* app_eui) {
//    int i,j;
//    const char conf_obj[] = "LoRa_Network_Server";
//    char param_name[64]; /* used to generate variable parameter names */
//    const char *str; /* used to store string value from JSON object */
//    uint32_t device_num = 0;
//    const char *DEVEUI;
//    const char *APPEUI;
//    const char *APPKEY;
//    char str_buff[16];
//    char dev_buff[16];
//    char app_buff[16];
//    
//    uint8_t dev_check = 0;
//    uint8_t app_check = 0;
//    
//    JSON_Value *root_val;
//    JSON_Object *root = NULL;
//    JSON_Object *conf = NULL;
//    JSON_Value *val;
//    
//    unsigned long long ull = 0;
//
//    /* try to parse JSON */
//    root_val = json_parse_file_with_comments(conf_file);
//    //printf(" %s \n", root_val);
//    
//    root = json_value_get_object(root_val);
//    if (root == NULL) {
//        dprintf("ERROR: %s id not a valid JSON file\n", conf_file);
//        exit(EXIT_FAILURE);
//    }
//    
//    conf = json_object_get_object(root, conf_obj);
//    if (conf == NULL) {
//        dprintf("INFO: %s does not contain a JSON object named %s\n", conf_file, conf_obj);
//        return -1;
//    } else {
//        dprintf("INFO: %s does contain a JSON object named %s\n", conf_file, conf_obj);
//        //exit(EXIT_FAILURE);
//    }
//
//    /* read Device_Number configuration */
//    val = json_object_get_value(conf, "Device_Number"); /* fetch value (if possible) */
//    if (json_value_get_type(val) == JSONNumber) {
//        device_num = (uint32_t)json_value_get_number(val);
//        dprintf("INFO: Device_number %d\n", device_num);
//    } else {
//        dprintf("WARNING: Data type for Device_Number seems wrong, please check\n");
//        device_num = 0;
//		exit(EXIT_FAILURE);
//    }
//
// 
//    for (i = 1; i < device_num+1; i++) {
//        
//        sprintf(param_name, "Device_%i", i); /* compose parameter path inside JSON structure */
//        val = json_object_get_value(conf, param_name); /* fetch value (if possible) */
//        if (json_value_get_type(val) != JSONObject) {
//            dprintf("INFO: no configuration for Device %i\n", i);
//            continue;
//        }
//        else{
//			dprintf("INFO: Device.%i\n", i);
//		}
//
//		snprintf(param_name, sizeof param_name, "Device_%i.DEUI", i);
//		DEVEUI = json_object_dotget_string(conf, param_name);
//		if (DEVEUI != NULL) {
//			dprintf("INFO: Device.DEUI is configured to %s\n", DEVEUI);
//			hex_to_char(dev_buff,(char*)DEVEUI,8);
//			dev_check = 1;			
//			for(j=0;j<8;j++) {
//				if(dev_eui[j] != dev_buff[j]){
//					dev_check = 0;			
//				}
//				DevEUI[j] = dev_buff[j];
//			}
//		}	
//		
//		snprintf(param_name, sizeof param_name, "Device_%i.AEUI", i);
//		APPEUI = json_object_dotget_string(conf, param_name);
//		if (APPEUI != NULL) {
//			dprintf("INFO: Device.AEUI is configured to %s\n", APPEUI);
//			hex_to_char(app_buff,(char*)APPEUI,8);
//			app_check = 1;			
//			for(j=0;j<8;j++) {
//				if(app_eui[j] != app_buff[j]){
//					app_check = 0;			
//				}
//				AppEUI[j] = app_buff[j];
//			}
//		}
//		
//		if((dev_check == 1) && (app_check == 1)){
//			dprintf("Find Join Device!\n");
//			snprintf(param_name, sizeof param_name, "Device_%i.AKEY", i);
//			APPKEY = json_object_dotget_string(conf, param_name);
//			if (APPKEY != NULL) {
//				dprintf("INFO: Device.AKEY is configured to %s\n", APPKEY);
//				hex_to_char(str_buff,(char*)APPKEY,16);
//				for(j=0;j<16;j++) {
//					AppKey[j] = str_buff[j];
//				}				
//				json_value_free(root_val);
//				return 1;
//			}
//			
//
//		}
//		
//    }
//
//    json_value_free(root_val);
//    return 0;
//}

//int parse_device_configuration(const char * conf_file) {
//    int i,j;
//    const char conf_obj[] = "LoRa_Network_Server";
//    char param_name[64]; /* used to generate variable parameter names */
//    const char *str; /* used to store string value from JSON object */
//    uint32_t device_num = 0;
//    const char *APPKEY;
//    const char *NETKEY;
//    const char *DEUI;
//    char str_buff[16];
//    
//    uint32_t device_address = 0;
//    
//    JSON_Value *root_val;
//    JSON_Object *root = NULL;
//    JSON_Object *conf = NULL;
//    JSON_Value *val;
//    
//    unsigned long long ull = 0;
//
//    /* try to parse JSON */
//    root_val = json_parse_file_with_comments(conf_file);
//    //printf(" %s \n", root_val);
//    
//    root = json_value_get_object(root_val);
//    if (root == NULL) {
//        dprintf("ERROR: %s id not a valid JSON file\n", conf_file);
//        exit(EXIT_FAILURE);
//    }
//    
//    conf = json_object_get_object(root, conf_obj);
//    if (conf == NULL) {
//        dprintf("INFO: %s does not contain a JSON object named %s\n", conf_file, conf_obj);
//        return -1;
//    } else {
//        dprintf("INFO: %s does contain a JSON object named %s\n", conf_file, conf_obj);
//        //exit(EXIT_FAILURE);
//    }
//
//
//    /* read Gateway_port configuration */
//    val = json_object_get_value(conf, "Gateway_port"); /* fetch value (if possible) */
//    if (json_value_get_type(val) == JSONNumber) {
//        GW_PORT = (uint32_t)json_value_get_number(val);
//        dprintf("INFO: Gateway_port %d\n", GW_PORT);
//    } else {
//        dprintf("WARNING: Data type for Gateway_port seems wrong, please check\n");
//        GW_PORT = 0;
//		exit(EXIT_FAILURE);
//    }
//
//
//    /* read Application_port configuration */
//    val = json_object_get_value(conf, "Application_port"); /* fetch value (if possible) */
//    if (json_value_get_type(val) == JSONNumber) {
//        AS_PORT = (uint32_t)json_value_get_number(val);
//        dprintf("INFO: Application_port %d\n", AS_PORT);
//    } else {
//        dprintf("WARNING: Data type for Application_port seems wrong, please check\n");
//        AS_PORT = 0;
//		exit(EXIT_FAILURE);
//    }
//    
//    /* read Network_ID configuration */
//    val = json_object_get_value(conf, "Network_ID"); /* fetch value (if possible) */
//    if (json_value_get_type(val) == JSONNumber) {
//        Network_ID = (uint32_t)json_value_get_number(val);
//        dprintf("INFO: Network_ID %d\n", Network_ID);
//    } else {
//        dprintf("WARNING: Data type for Network_ID seems wrong, please check\n");
//        Network_ID = 0;
//		exit(EXIT_FAILURE);
//    }
//
//    /* read Device_Number configuration */
//    val = json_object_get_value(conf, "Device_Number"); /* fetch value (if possible) */
//    if (json_value_get_type(val) == JSONNumber) {
//        device_num = (uint32_t)json_value_get_number(val);
//        dprintf("INFO: Device_number %d\n", device_num);
//    } else {
//        dprintf("WARNING: Data type for Device_Number seems wrong, please check\n");
//        device_num = 0;
//		exit(EXIT_FAILURE);
//    }
//
//
//    for (i = 1; i < device_num+1; i++) {
//        
//        sprintf(param_name, "Device_%i", i); /* compose parameter path inside JSON structure */
//        val = json_object_get_value(conf, param_name); /* fetch value (if possible) */
//        if (json_value_get_type(val) != JSONObject) {
//            dprintf("INFO: no configuration for Device %i\n", i);
//            continue;
//        }
//        else{
//			dprintf("INFO: Device.%i\n", i);
//		}
//
//		snprintf(param_name, sizeof param_name, "Device_%i.DEUI", i);
//		DEUI = json_object_dotget_string(conf, param_name);
//		if (DEUI != NULL) {
//			dprintf("INFO: Device.DEUI is configured to %s\n", DEUI);
//		}		
//
//		snprintf(param_name, sizeof param_name, "Device_%i.DVAD", i);
//		str = json_object_dotget_string(conf, param_name);
//		if (str != NULL) {
//			sscanf(str, "%x", &device_address);
//			dprintf("INFO: Device.DVAD is configured to %s\n", str);
//		}		
//
//		snprintf(param_name, sizeof param_name, "Device_%i.ASKY", i);
//		APPKEY = json_object_dotget_string(conf, param_name);
//		if (str != NULL) {
//			dprintf("INFO: Device.ASKY is configured to %s\n", APPKEY);
//		}
//
//		snprintf(param_name, sizeof param_name, "Device_%i.NSKY", i);
//		NETKEY = json_object_dotget_string(conf, param_name);
//		if (str != NULL) {
//			dprintf("INFO: Device.NSKY is configured to %s\n", NETKEY);
//		}
//		
//		EndDeviceInfo_t *edInfo;
//		EndDeviceInfo_t *edInfoTmpPrev;
//		EndDeviceInfo_t *edInfoTmpCur;
//
//		edInfo = malloc(sizeof(EndDeviceInfo_t));
//		if(edInfo == NULL)
//		{
//			dprintf("Memory allocation fail!!\n");
//		}
//		dprintf("Add New EndDevice Information - address = %d\n", i);
//
//		edInfo->address = device_address;
//		edInfo->totalRxNum = 0;
//		edInfo->totalRxSize = 0;
//		edInfo->seqNum = 0;
//		edInfo->totalRxNumPrev = 0;
//		edInfo->totalRxSizePrev = 0;
//		edInfo->seqNumPrev = 0;
//		memset(edInfo->AppEUI, 0, 8);
//
//		//for(j=0;j<8;j++) {
//		//	edInfo->AppEUI[j] = AppEUI[j];
//		//}
//		
//		hex_to_char(str_buff, (char*)DEUI, 8);
//		
//		for(j=0;j<8;j++) {
//			edInfo->DevEUI[j] = str_buff[j];
//		}
//		
//		hex_to_char(str_buff,(char*)APPKEY,16);
//			
//		for(j=0;j<16;j++) {
//			edInfo->AppSKey[j] = str_buff[j];
//		}
//
//		hex_to_char(str_buff,(char*)NETKEY,16);
//
//		for(j=0;j<16;j++) {
//			edInfo->NwkSKey[j] = str_buff[j];
//		}
//
//
//		edInfo->devbw = 3;
//		edInfo->devcr = 1;
//		edInfo->devsf = 2;
//		edInfo->devch = 923300000;
//
//		// Primary GW
//		edInfo->gw[0].socket = -1;
//		edInfo->gw[0].rssi = 0;
//		edInfo->gw[0].snr = 0;
//		edInfo->gw[0].totalRxNum = 0;
//		edInfo->gw[0].totalRxSize = 0;
//		edInfo->gw[0].rxTime = 0;
//		// Alternative GW
//		edInfo->gw[1].socket = -1;
//		edInfo->gw[1].rssi = 0;
//		edInfo->gw[1].snr = 0;
//		edInfo->gw[1].totalRxNum = 0;
//		edInfo->gw[1].totalRxSize = 0;
//		edInfo->gw[1].rxTime = 0;
//
//		// Sorting by Address
//		edInfoTmpPrev = &ED_HEAD;
//		edInfoTmpCur = ED_HEAD.next;
//		
//		
//		while(edInfoTmpCur != NULL)
//		{
//			if(edInfoTmpCur->address > edInfo->address)
//			{
//				edInfoTmpPrev->next = edInfo;
//				edInfo->next = edInfoTmpCur;
//				break;
//			}
//			else{
//				
//			}
//			edInfoTmpPrev = edInfoTmpCur;
//			edInfoTmpCur = edInfoTmpCur->next;
//		}
//		if(edInfoTmpCur == NULL){
//			edInfoTmpPrev->next = edInfo;
//			edInfo->next = NULL;	// edInfoTmpCur	
//		}
//	
//
//    }
//
//    json_value_free(root_val);
//    return 0;
//}

void NodeExp(int gwSocket) {
    EndDeviceInfo_t *edInfo;

    EndDeviceInfo_t *edInfoTmpPrev;
    EndDeviceInfo_t *edInfoTmpCur;

    edInfo = malloc(sizeof (EndDeviceInfo_t));
    if (edInfo == NULL) {
        dprintf("Memory allocation fail!!\n");
    }
    dprintf("Add New EndDevice Information - address = 1\n");

    edInfo->address = 0x02000001;
    edInfo->totalRxNum = 0;
    edInfo->totalRxSize = 0;
    edInfo->seqNum = 0;
    edInfo->totalRxNumPrev = 0;
    edInfo->totalRxSizePrev = 0;
    edInfo->seqNumPrev = 0;
    memset(edInfo->AppEUI, 0, 8);
    edInfo->DevNonce[0] = 0;
    edInfo->DevNonce[1] = 1;
    edInfo->AppSKey[0] = 0xa2;
    edInfo->AppSKey[1] = 0xfa;
    edInfo->AppSKey[2] = 0xa5;
    edInfo->AppSKey[3] = 0x11;
    edInfo->AppSKey[4] = 0x90;
    edInfo->AppSKey[5] = 0xc2;
    edInfo->AppSKey[6] = 0xd3;
    edInfo->AppSKey[7] = 0xf9;
    edInfo->AppSKey[8] = 0xac;
    edInfo->AppSKey[9] = 0xe4;
    edInfo->AppSKey[10] = 0x8a;
    edInfo->AppSKey[11] = 0xb4;
    edInfo->AppSKey[12] = 0x91;
    edInfo->AppSKey[13] = 0x05;
    edInfo->AppSKey[14] = 0xc7;
    edInfo->AppSKey[15] = 0x8d;

    edInfo->NwkSKey[0] = 0xfc;
    edInfo->NwkSKey[1] = 0xbe;
    edInfo->NwkSKey[2] = 0x74;
    edInfo->NwkSKey[3] = 0x5f;
    edInfo->NwkSKey[4] = 0xb8;
    edInfo->NwkSKey[5] = 0xb6;
    edInfo->NwkSKey[6] = 0x8d;
    edInfo->NwkSKey[7] = 0xc6;
    edInfo->NwkSKey[8] = 0x61;
    edInfo->NwkSKey[9] = 0xbf;
    edInfo->NwkSKey[10] = 0x7c;
    edInfo->NwkSKey[11] = 0x24;
    edInfo->NwkSKey[12] = 0xa9;
    edInfo->NwkSKey[13] = 0x60;
    edInfo->NwkSKey[14] = 0xf9;
    edInfo->NwkSKey[15] = 0xcc;
    // Primary GW
    edInfo->gw[0].socket = gwSocket;
    edInfo->gw[0].rssi = -200;
    edInfo->gw[0].snr = -100;
    edInfo->gw[0].totalRxNum = 0;
    edInfo->gw[0].totalRxSize = 0;
    time(&(edInfo->gw[0].rxTime));
    // Alternative GW
    edInfo->gw[1].socket = -1;
    edInfo->gw[1].rssi = 0;
    edInfo->gw[1].snr = 0;
    edInfo->gw[1].totalRxNum = 0;
    edInfo->gw[1].totalRxSize = 0;
    edInfo->gw[1].rxTime = 0;

    // Sorting by Address
    edInfoTmpPrev = &ED_HEAD;
    edInfoTmpCur = ED_HEAD.next;
    while (edInfoTmpCur != NULL) {
        if (edInfoTmpCur->address > 0x02000001) {
            edInfoTmpPrev->next = edInfo;
            edInfo->next = edInfoTmpCur;
        }
        edInfoTmpPrev = edInfoTmpCur;
        edInfoTmpCur = edInfoTmpCur->next;
    }
    if (edInfoTmpCur == NULL) {
        edInfoTmpPrev->next = edInfo;
        edInfo->next = NULL; // edInfoTmpCur	
    }


    edInfo = malloc(sizeof (EndDeviceInfo_t));
    if (edInfo == NULL) {
        dprintf("Memory allocation fail!!\n");
    }
    dprintf("Add New EndDevice Information - address = 2\n");

    edInfo->address = 0x02000002;
    edInfo->totalRxNum = 0;
    edInfo->totalRxSize = 0;
    edInfo->seqNum = 0;
    edInfo->totalRxNumPrev = 0;
    edInfo->totalRxSizePrev = 0;
    edInfo->seqNumPrev = 0;
    memset(edInfo->AppEUI, 0, 8);
    edInfo->DevNonce[0] = 0;
    edInfo->DevNonce[1] = 2;

    edInfo->AppSKey[0] = 0xf4;
    edInfo->AppSKey[1] = 0x60;
    edInfo->AppSKey[2] = 0x8c;
    edInfo->AppSKey[3] = 0x0d;
    edInfo->AppSKey[4] = 0x73;
    edInfo->AppSKey[5] = 0xae;
    edInfo->AppSKey[6] = 0xb8;
    edInfo->AppSKey[7] = 0x73;
    edInfo->AppSKey[8] = 0x84;
    edInfo->AppSKey[9] = 0x8b;
    edInfo->AppSKey[10] = 0x24;
    edInfo->AppSKey[11] = 0x7d;
    edInfo->AppSKey[12] = 0x73;
    edInfo->AppSKey[13] = 0x2f;
    edInfo->AppSKey[14] = 0xe4;
    edInfo->AppSKey[15] = 0x2d;

    edInfo->NwkSKey[0] = 0xe3;
    edInfo->NwkSKey[1] = 0xb2;
    edInfo->NwkSKey[2] = 0x7d;
    edInfo->NwkSKey[3] = 0xc9;
    edInfo->NwkSKey[4] = 0xd3;
    edInfo->NwkSKey[5] = 0x28;
    edInfo->NwkSKey[6] = 0x6d;
    edInfo->NwkSKey[7] = 0x3e;
    edInfo->NwkSKey[8] = 0x1a;
    edInfo->NwkSKey[9] = 0x4a;
    edInfo->NwkSKey[10] = 0xd3;
    edInfo->NwkSKey[11] = 0xe1;
    edInfo->NwkSKey[12] = 0x67;
    edInfo->NwkSKey[13] = 0xb5;
    edInfo->NwkSKey[14] = 0x8b;
    edInfo->NwkSKey[15] = 0x3f;

    // Primary GW
    edInfo->gw[0].socket = gwSocket;
    edInfo->gw[0].rssi = -200;
    edInfo->gw[0].snr = -100;
    edInfo->gw[0].totalRxNum = 0;
    edInfo->gw[0].totalRxSize = 0;
    time(&(edInfo->gw[0].rxTime));
    // Alternative GW
    edInfo->gw[1].socket = -1;
    edInfo->gw[1].rssi = 0;
    edInfo->gw[1].snr = 0;
    edInfo->gw[1].totalRxNum = 0;
    edInfo->gw[1].totalRxSize = 0;
    edInfo->gw[1].rxTime = 0;

    // Sorting by Address
    edInfoTmpPrev = &ED_HEAD;
    edInfoTmpCur = ED_HEAD.next;
    while (edInfoTmpCur != NULL) {
        if (edInfoTmpCur->address > 0x02000002) {
            edInfoTmpPrev->next = edInfo;
            edInfo->next = edInfoTmpCur;
        }
        edInfoTmpPrev = edInfoTmpCur;
        edInfoTmpCur = edInfoTmpCur->next;
    }
    if (edInfoTmpCur == NULL) {
        edInfoTmpPrev->next = edInfo;
        edInfo->next = NULL; // edInfoTmpCur	
    }



}













