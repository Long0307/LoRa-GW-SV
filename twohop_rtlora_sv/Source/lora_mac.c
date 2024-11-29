/******************************************************************************
 * Filename     : loramac.c                                                    *
 * Program      : Private LoRa Network Server Program                          *
 * Copyright    : Copyright (C) 2017-2017, ETRI                                *
 *              : URL << http://www.etri.re.kr >>                              *
 * Authors      : Woo-Sung Jung (woosung@etri.re.kr)                           *
 *              : Tae Hyun Yoon (thyoon0820@etri.re.kr)                        *
 *              : Dae Seung Yoo (ooseyds@etri.re.kr)                           *
 * Description  : Private LoRa Network Server LoRa MAC handling routine        *
 * Created at   : Mon Jul. 24. 2017.                                           *
 * Modified by  :                                                              *
 * Modified at  :                                                              *
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "lora_mac.h"
#include "application.h"
#include "crypto.h"
#include "aes.h"
#include "debug.h"
#include "device_management.h"
#include "conf.h"

// Rx Handler --> Move to End Device Management..
uint8_t Network_ID = Nwk_ID;
uint8_t DevEUI[8] = LORAWAN_DEVICE_EUI;
uint8_t AppEUI[8] = LORAWAN_APPLICATION_EUI;
uint8_t AppKey[16] = LORAWAN_APPLICATION_KEY;

uint8_t NwkSKey[16] = LORAWAN_NWKSKEY;
uint8_t AppSKey[16] = LORAWAN_APPSKEY;

uint8_t AppNonce[3];

extern char con_path[256];
extern char dev_path[256];

// Local Variables..
uint16_t FCntUp; // Frame counter from end device to network server
uint16_t FCntDown; // Frame counter from network server to end device
uint16_t AdrAckCounter; // ADR ACK Counter for data rate validation
// TODO:: Each GW Status???
LoRaMacStatus_t MacStatus;


// TODO:: Multiple Buffer management for supporting multiple LoRa Gateway communication

/******************************************************************************
 * Function Name        : LoRaMAC_Init
 * Input Parameters     : None
 * Return Value         : None
 * Function Description : LoRa MAC Initialization routine
 ******************************************************************************/
void LoRaMAC_Init(void) {
    uint32_t loopCnt;

    FCntDown = 0;

    // TODO:: Controled by each GW??
    MacStatus.Joined = LoRa_MAC_NotJoined;
    MacStatus.ADRACKRequested = LoRa_MAC_ADRACK_Not_Request;
    MacStatus.ACKRequested = LoRa_MAC_ACK_Not_Request;
    MacStatus.ADR = LoRa_MAC_ADR_DEVICE;
}

/******************************************************************************
 * Function Name        : ReceiveFrameFromLoRaGW
 * Input Parameters     : uint8_t *rxFrame - Received Frame Data
 *                      : int size         - Received Frame Size
 *                      : int gwSocket     - Received GW socket
 * Return Value         : None
 * Function Description : Receive LoRa Frame from Gateway
 ******************************************************************************/
void ReceiveFrameFromLoRaGW(uint8_t *rxFrame, int size, int gwSocket) {
    uint32_t loopCnt;
    uint32_t findStartOfFrame = 0;
    uint32_t remainingBufferSize = 0;
    uint32_t rxFrameSize = 0;
    GateWayInfo_t *gatewayInfo;
    gatewayInfo = FindGateWay(gwSocket);

    if (gatewayInfo == NULL) {
        dprintf("Gateway information is not vaild.. Ingnoring rx frame\n");
        return;
    }

    // Check Buffer overflow..
    if ((gatewayInfo->currentRxBufferSize + (uint32_t) size) > TCP_STREAM_BUFFER_SIZE) {
        dprintf("Gateway Rx Frame Buffer is overflowed..Ignoring old buffer data\n");
        // clear old buffer..
        gatewayInfo->currentRxBufferSize = 0;
    }

    // concatenate previous remaining data and new arrived data
    memcpy(&(gatewayInfo->rxBuffer[gatewayInfo->currentRxBufferSize]), rxFrame, size);
    gatewayInfo->currentRxBufferSize += size;

    // We need 2 bytes at least .. SOF, SIZE
    while (gatewayInfo->currentRxBufferSize > 2) {
        // Check Start of Frame..
        if (gatewayInfo->rxBuffer[0] != START_OF_FRAME) {
            // Ignored rx data until meeting SOF
            for (loopCnt = 0; loopCnt < gatewayInfo->currentRxBufferSize; loopCnt++) {
                if (gatewayInfo->rxBuffer[loopCnt] == START_OF_FRAME) {
                    findStartOfFrame = loopCnt;
                    remainingBufferSize = gatewayInfo->currentRxBufferSize - loopCnt;
                    break;
                }
            }
            // Move remaining data to start of buffer
            if (findStartOfFrame != 0) {
                memmove(gatewayInfo->rxBuffer, &(gatewayInfo->rxBuffer[findStartOfFrame]), remainingBufferSize);
                gatewayInfo->currentRxBufferSize = remainingBufferSize;
                findStartOfFrame = 0;
            } else {
                // Cannot find START_OF_FRAME.. clear Buffer
                gatewayInfo->currentRxBufferSize = 0;
            }
        } else {

            rxFrameSize = gatewayInfo->rxBuffer[1] + RX_TUNNELING_OVERHEAD;
            // Check Size..
            if (rxFrameSize <= gatewayInfo->currentRxBufferSize) {
                // Check End of Frame
                //if (gatewayInfo->rxBuffer[gatewayInfo->currentRxBufferSize-1] == END_OF_FRAME)
                if (gatewayInfo->rxBuffer[rxFrameSize - 1] == END_OF_FRAME) {

                    // Parsing LoRa Frame.. Here..
                    NetworkServerReceiveFrame(gatewayInfo->rxBuffer, gwSocket);

                    // Remaining Part..
                    remainingBufferSize = gatewayInfo->currentRxBufferSize - rxFrameSize;
                    memmove(gatewayInfo->rxBuffer, &(gatewayInfo->rxBuffer[gatewayInfo->currentRxBufferSize]), remainingBufferSize);
                    gatewayInfo->currentRxBufferSize = remainingBufferSize;
                } else {
                    // Find new SOF
                    gatewayInfo->rxBuffer[0] = 0;
                }
            } else {
                // need more data stream..
                return;
            }
        }
    }

    // Too small rx buffer..
    return;

}

/******************************************************************************
 * Function Name        : NetworkServerReceiveFrame
 * Input Parameters     : Rx Frame
 *                      : GateWay Socket
 * Return Value         : None
 * Function Description : When LoRa MAC status change to RX,
 *                      : LoRaMacStatusUpdate calls it.
 *                      : It can handle both gateway and end device.
 ******************************************************************************/
void NetworkServerReceiveFrame(uint8_t *rxFrameBuffer, int gwSocket) {
    dprintf("Enter\n");
    LoRaRxFrameInfo_t *rxFrame; // Rx Frame (Tunneling Format)
    LoRaMACHeader_t macHeader; // LoRa MAC Header
    LoRaFrameHeader_t fHeader; // LoRa Frame Header
    EndDeviceInfo_t *edInfo; // Device Information Structure

    uint8_t mic_rx[4], mic_cal[4]; // Message Integrity Check
    uint16_t diffcounter;
    uint8_t port;
    uint8_t len;
    uint8_t optlen;
    uint8_t *payload;
    uint16_t size;
    int16_t rssi;
    int8_t snr;
    uint8_t codingrate;
    uint8_t spreadingfactor;
    uint32_t frequency;
    uint8_t bandwidth;
    uint8_t *gwID;

    uint8_t appData[256];
    uint8_t AppKey[16];
    uint8_t i, j;

    rxFrame = (LoRaRxFrameInfo_t *) rxFrameBuffer;

    payload = rxFrame->loraframe;
    size = (uint16_t) rxFrame->size;
    rssi = rxFrame->netInfo.rssi;
    snr = rxFrame->netInfo.snr;

    codingrate = rxFrame->netInfo.cr;
    spreadingfactor = rxFrame->netInfo.sf;
    frequency = rxFrame->netInfo.freq;
    bandwidth = rxFrame->netInfo.bw;
    gwID = rxFrame->netInfo.gatewayID;

    // Get MAC Header..
    len = 0;
    macHeader.value = payload[len++];

    // Version Check..
    if (macHeader.bits.Major != LoRa_MAC_LoRaWAN_R1) {
        // Version is invalid.. RxError..
        dprintf("Version is invalid.. RxError..\n");
        return;
    }

    // Find MType..
    // GW just receives and forwards to Network Server... Network Server has to handle LoRa Frame..
    switch (macHeader.bits.MType) {
            // GW: Join Request .. GW received Join Request Msg. from End Device ..
        case LoRa_Frame_Join_Request:

            break;

            // ED: Join Accept Response .. End Device received Join Accept Response Msg. from GW ..
            // GW: Unconfirmed Data Up .. GW received Unconfirmed Data from End Device ..
            // GW: Confirmed Data Up .. GW received Confirmed Data from End device ..
        case LoRa_Frame_Unconfirm_Data_Up:
        case LoRa_Frame_Confirm_Data_Up:

            break;

            // Both: Proprietary ..
        case LoRa_Frame_Proprietary:
            break;

            // default ..
        default:
            // Unknown message type..
            break;

    }
    //State = RX;
}

/******************************************************************************
 * Function Name        : SendJoinAcceptFrame
 * Input Parameters     : int gwSocket           - Gateway Socket
 *                      : uint32_t deviceAddress - EndDevice Address
 * Return Value         : None
 * Function Description : Join Response
 ******************************************************************************/
void SendJoinAcceptFrame(int gwSocket, uint32_t deviceAddress) {
    // TODO:: ALOHA RANDOM DELAY will be added.. Also 4 sec LBT...
    uint8_t lorabuffer[256];
    uint8_t tempbuffer[256];
    uint8_t temp2buffer[256]; // will be remove..
    uint8_t tx_msg_buff[512];
    uint8_t lorasize;
    uint32_t result;
    LoRaMACHeader_t mHeader;
    uint8_t key[16];
    uint8_t mic[4];
    uint8_t i;

    EndDeviceInfo_t *edInfo;
    LoRaTxFrameInfo_t TxFrame;


    memset(lorabuffer, 0, 256);
    memset(tempbuffer, 0, 256);
    memset(temp2buffer, 0, 256);

    // FRAME = MHDR | Join-Accept | MIC
    mHeader.bits.Major = LORAWAN_R1;
    mHeader.bits.RFU = 0;
    mHeader.bits.MType = LoRa_Frame_Join_Accept;
    lorasize = 0;
    tempbuffer[lorasize++] = mHeader.value;

    // AppNonce (3) / NetID (3) / DevAddr (4) / DLSettings (1) / RxDelay (1) / options.. (0-16)
    GetAppNonce(&tempbuffer[lorasize]);
    lorasize += 3;

    // The format of the NetID is as follows: The seven LSB of the NetID are called NwkID and
    // match the seven MSB of the short address of an end-device as described before.
    tempbuffer[lorasize++] = (Network_ID >> 16) & 0xFF;
    tempbuffer[lorasize++] = (Network_ID >> 8) & 0xFF;
    tempbuffer[lorasize++] = (Network_ID) & 0xFF;

    tempbuffer[lorasize++] = (deviceAddress >> 24) & 0xFF;
    tempbuffer[lorasize++] = (deviceAddress >> 16) & 0xFF;
    tempbuffer[lorasize++] = (deviceAddress >> 8) & 0xFF;
    tempbuffer[lorasize++] = (deviceAddress) & 0xFF;

    LoRaDLSetting_t dl;
    //TODO:: Set Rx 2 Policy..
    dl.bits.Rx2DR = 0;
    // The RX1DRoffset field sets the offset between the uplink data rate and the downlink data rate used 
    // to communicate with the end-device on the first reception slot (RX1). By default this offset is 0..
    dl.bits.Rx1DRoff = 0;
    tempbuffer[lorasize++] = dl.value;

    tempbuffer[lorasize++] = (uint8_t) ((RECEIVE_DELAY1) / 1000);

    // MIC
    GetAppKey(key);
    GenerateMICforJoinResponse(tempbuffer, key, mic);
    tempbuffer[lorasize++] = mic[0];
    tempbuffer[lorasize++] = mic[1];
    tempbuffer[lorasize++] = mic[2];
    tempbuffer[lorasize++] = mic[3];

    //
    dprintf("LoRa Join Accept(ORI): ");
    for (i = 0; i < lorasize; i++)
        dprintfc("%02x", tempbuffer[i]);
    dprintfc("\n");
    //

    // Encryption.. using APPKEY

    lorabuffer[0] = tempbuffer[0];
    EncryptJoinAccept(&lorabuffer[1], &tempbuffer[1], lorasize - 1, key);

    //
    dprintf("lorasize = %d\n", lorasize);
    dprintf("LoRa Join Accept(Ori): ");
    for (i = 0; i < lorasize + 5; i++)
        dprintfc("%02x", tempbuffer[i]);
    dprintfc("\n");
    dprintf("LoRa Join Accept(Enc): ");
    for (i = 0; i < lorasize + 5; i++)
        dprintfc("%02x", lorabuffer[i]);
    dprintfc("\n");
    //

    // find End Device Information Structure
    edInfo = FindEndDevice(deviceAddress);

    // Add SOF, Size, EOF	
    TxFrame.startOfFrame = 0xDE;
    TxFrame.frameSize = lorasize;
    TxFrame.cr = edInfo->devcr;
    TxFrame.sf = edInfo->devsf;
    TxFrame.ch = edInfo->devch;
    TxFrame.bw = edInfo->devbw;
    TxFrame.appPayloadSize = lorasize;
    TxFrame.startOfAppPayload = 9;
    memset(TxFrame.payload, 0, sizeof (TxFrame.payload));
    memcpy(TxFrame.payload, lorabuffer, lorasize);
    TxFrame.payload[lorasize] = 0xCA; //EOF

    dprintf("LoRa Join Accept     : %d %d %d %d\n", edInfo->devcr, edInfo->devsf, edInfo->devch, edInfo->devbw);

    memcpy(tx_msg_buff, (void *) &TxFrame, lorasize + TX_TUNNELING_OVERHEAD);

    result = write(gwSocket, tx_msg_buff, lorasize + TX_TUNNELING_OVERHEAD);
    dprintf("LoRa Join Accept     : ");
    for (i = 0; i < lorasize + TX_TUNNELING_OVERHEAD; i++)
        dprintfc("%02x", tx_msg_buff[i]);
    dprintfc("\n");
    dprintf("Send result = %d\n", result);

}

/******************************************************************************
 * Function Name        : SendFrame
 * Input Parameters     : uint8_t *buffer   - Application payload
 *                      : uint8_t size      - Application Size
 *                      : uint8_t port      - LoRa Frame Port
 *                      :                     Port = 0 (Only MAC command)
 *                      :                     Port = 1..223 (Application Specific)
 *                      :                     Port = 224 (LoRaWAN Test Protocol)
 *                      : uint8_t ack_req   - Confirmed data(1) or Unconfirmed data(0)
 *                      : uint8_t direction - UpLink (0) or DownLink (1)
 *                      : uint32_t nwkAddr  - Target Device Network Address 
 *                      : uint32_t nwkID    - Target Device Network ID
 * Return Value         : uint32_t          - Transmitted Data Byte
 * Function Description : Generate LoRa MAC Frame and Send it
 ******************************************************************************/
uint32_t SendFrame(uint8_t *buffer, uint8_t size, uint8_t port, uint8_t ack_req, uint8_t direction, uint32_t nwkAddr, uint32_t nwkID) {
    // TODO:: ALOHA RANDOM DELAY will be added.. Also 4 sec LBT...
    uint8_t lorabuffer[256];
    uint8_t lorasize;
    LoRaMACHeader_t mHeader;
    LoRaFrameHeader_t fHeader;
    uint8_t mic[4];
    int gwSocket;
    int result;

    if (direction == LoRa_DN_LINK) {
        // TODO::Check joined network ..
        //if(MacStatus.Joined == LoRa_MAC_NotJoined)
        //{
        //	return;
        //}

        // Check current MAC status..
        // If MAC is busy (Tx running or Tx waiting) -> Cannot transmission..

        // Reserved two byte for SOF and Frame Size
        lorasize = 2;

        // Generate Frame Header
        mHeader.bits.Major = LORAWAN_R1;
        if (ack_req == 0) {
            //Unconfirmed Message
            mHeader.bits.MType = LoRa_Frame_Unconfirm_Data_Down;
        } else {
            //Confirmed Message
            mHeader.bits.MType = LoRa_Frame_Confirm_Data_Down;
        }

        lorabuffer[lorasize] = mHeader.value;
        lorasize += 1;

        // Generate Frame Header.. DevAddr(4) FCtrl(1) FCnt(2) FOpts(0..15)
        fHeader.DevAddr.Bit.NwkAddr = nwkAddr;
        fHeader.DevAddr.Bit.NwkID = nwkID;
        // ADR control
        if (MacStatus.ADR == LoRa_MAC_ADR_NETWORK)
            fHeader.FCtrl.dn.bits.ADR = LoRa_MAC_ADR_NETWORK;
        else
            fHeader.FCtrl.dn.bits.ADR = LoRa_MAC_ADR_DEVICE;

        if (MacStatus.ACKRequested == LoRa_MAC_ACK_Requested)
            fHeader.FCtrl.dn.bits.ACK = LoRa_MAC_ACK_Requested;
        else
            fHeader.FCtrl.dn.bits.ACK = LoRa_MAC_ACK_Not_Request;

        // Frame Pending Bit
        if (MacStatus.FPending == LoRa_MAC_PENDING)
            fHeader.FCtrl.dn.bits.ACK = LoRa_MAC_PENDING;
        else
            fHeader.FCtrl.dn.bits.ACK = LoRa_MAC_NO_PENDING;

        fHeader.FCtrl.dn.bits.FOptsLen = 0;

        fHeader.FrameCounter = FCntDown++;

        memcpy(&lorabuffer[lorasize], &fHeader.DevAddr.Address, 4);
        lorasize += 4;
        lorabuffer[lorasize] = fHeader.FCtrl.dn.value;
        lorasize += 1;
        memcpy(&lorabuffer[lorasize], &fHeader.FrameCounter, 2);
        lorasize += 2;

        // Frame Port
        lorabuffer[lorasize] = port;
        lorasize += 1;

        // Frame Payload and Encrypt
        memset(&lorabuffer[lorasize], '\0', size);
        EncryptPayload(&lorabuffer[lorasize], buffer, size, direction, fHeader, port, NwkSKey, AppSKey);
        lorasize += size;

        //MIC .. using LoRa crypto
        GenerateMIC(lorabuffer, lorasize, direction, fHeader, NwkSKey, mic);

        lorabuffer[lorasize++] = mic[0];
        lorabuffer[lorasize++] = mic[1];
        lorabuffer[lorasize++] = mic[2];
        lorabuffer[lorasize++] = mic[3];

        // TODO::Select channel (get channel settings)

        // Get Rx1, Rx2 Delay..

        // Check Time on Air..
        // airtime = Radio->TimeOnAir(lorabuffer, lorasize);


        // Send via primary Gateway socket
        // a. Find socket number of GW, which connected End-device
        gwSocket = FindGWSocket(fHeader.DevAddr.Address);
        dprintf("GW socket is %d\n", gwSocket);
        // b. Send Socket
        if (gwSocket != -1) {
            // Add SOF, Size, LoRa Frame, EOF
            lorabuffer[0] = START_OF_FRAME;
            lorabuffer[lorasize] = END_OF_FRAME;
            lorasize++;
            lorabuffer[1] = lorasize;

            result = write(gwSocket, lorabuffer, lorasize);
            dprintf("Send result = %d\n", result);
            return lorasize;
        } else {
            dprintf("Cannot Find Target GW information\n");
            return 0;
        }

    } else {
        dprintf("We can only handle downlink message\n");
        return 0;
    }
    return 0;
}

