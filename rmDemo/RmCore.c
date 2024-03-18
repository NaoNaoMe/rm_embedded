//******************************************************************************
// RmCore(Generic)
// Copyright 2024 Naoya Imai
//******************************************************************************

#include "RmCore.h"


#define RM_BYPASS_FUNC_NULL     (rm_bypass_function_t)0x00000000

/* Definitions of SetLogDataFrame */
#define RM_SETLOG_BIT_MASK      0xF0
#define RM_SETLOG_START_BIT     0x10
#define RM_SETLOG_END_BIT       0x20


/* Definitions of Serial Line Internet Protocol */
#define RM_FRAME_CHAR_END       0xC0
#define RM_FRAME_CHAR_ESC       0xDB
#define RM_FRAME_CHAR_ESC_END   0xDC
#define RM_FRAME_CHAR_ESC_ESC   0xDD

/* Definitions of communication mode */

/* Definitions of fixed frame index */
#define RM_FRAME_SEQCODE        0
#define RM_FRAME_PAYLOAD        1


#ifdef RM_ADDRESS_4BYTE
#define RM_LOGCONTENTS_TABLE_SIZE   22
#define RM_LOGCONTENTS_DATA_UNIT    5 /* size(1)+address(4) = 5 */
const uint8_t RM_LogContentsParser[RM_LOGCONTENTS_TABLE_SIZE] =
{
    0,                              /* imaginary-padding */
    0,                              /* code */
    0, 0, 0, 0, 1, 0, 0, 0, 0, 2,   /* (size(1) and address(4)) * 2 */
    0, 0, 0, 0, 3, 0, 0, 0, 0, 4    /* (size(1) and address(4)) * 2 */
};

#define RM_WRITECONTENTS_TABLE_SIZE   14
const uint8_t RM_WriteContentsParser[RM_WRITECONTENTS_TABLE_SIZE] =
{
    0,                      /* imaginary-padding */
    0, 0, 0, 0, 0,          /* size(1) and address(4) */
    1, 2, 0, 4, 0, 0, 0, 8  /* data length */
};
#else
#define RM_LOGCONTENTS_TABLE_SIZE   26
#define RM_LOGCONTENTS_DATA_UNIT    3 /* size(1)+address(2) = 3 */
const uint8_t RM_LogContentsParser[RM_LOGCONTENTS_TABLE_SIZE] =
{
    0,                  /* imaginary-padding */
    0,                  /* code */
    0, 0, 1, 0, 0, 2,   /* (size(1) and address(2)) * 2 */
    0, 0, 3, 0, 0, 4,   /* (size(1) and address(2)) * 2 */
    0, 0, 5, 0, 0, 6,   /* (size(1) and address(2)) * 2 */
    0, 0, 7, 0, 0, 8    /* (size(1) and address(2)) * 2 */
};

#define RM_WRITECONTENTS_TABLE_SIZE   12
const uint8_t RM_WriteContentsParser[RM_WRITECONTENTS_TABLE_SIZE] =
{
    0,                      /* imaginary-padding */
    0, 0, 0,                /* size(1) and address(2) */
    1, 2, 0, 4, 0, 0, 0, 8  /* data length */
};
#endif

/* x^8+x^7+x^4+x^2+1 */
const uint8_t RM_CrcTable[256] =
{
 /*      0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F         */
      0x00, 0xd5, 0x7f, 0xaa, 0xfe, 0x2b, 0x81, 0x54, 0x29, 0xfc, 0x56, 0x83, 0xd7, 0x02, 0xa8, 0x7d /* 0x00 */
    , 0x52, 0x87, 0x2d, 0xf8, 0xac, 0x79, 0xd3, 0x06, 0x7b, 0xae, 0x04, 0xd1, 0x85, 0x50, 0xfa, 0x2f /* 0x10 */
    , 0xa4, 0x71, 0xdb, 0x0e, 0x5a, 0x8f, 0x25, 0xf0, 0x8d, 0x58, 0xf2, 0x27, 0x73, 0xa6, 0x0c, 0xd9 /* 0x20 */
    , 0xf6, 0x23, 0x89, 0x5c, 0x08, 0xdd, 0x77, 0xa2, 0xdf, 0x0a, 0xa0, 0x75, 0x21, 0xf4, 0x5e, 0x8b /* 0x30 */
    , 0x9d, 0x48, 0xe2, 0x37, 0x63, 0xb6, 0x1c, 0xc9, 0xb4, 0x61, 0xcb, 0x1e, 0x4a, 0x9f, 0x35, 0xe0 /* 0x40 */
    , 0xcf, 0x1a, 0xb0, 0x65, 0x31, 0xe4, 0x4e, 0x9b, 0xe6, 0x33, 0x99, 0x4c, 0x18, 0xcd, 0x67, 0xb2 /* 0x50 */
    , 0x39, 0xec, 0x46, 0x93, 0xc7, 0x12, 0xb8, 0x6d, 0x10, 0xc5, 0x6f, 0xba, 0xee, 0x3b, 0x91, 0x44 /* 0x60 */
    , 0x6b, 0xbe, 0x14, 0xc1, 0x95, 0x40, 0xea, 0x3f, 0x42, 0x97, 0x3d, 0xe8, 0xbc, 0x69, 0xc3, 0x16 /* 0x70 */
    , 0xef, 0x3a, 0x90, 0x45, 0x11, 0xc4, 0x6e, 0xbb, 0xc6, 0x13, 0xb9, 0x6c, 0x38, 0xed, 0x47, 0x92 /* 0x80 */
    , 0xbd, 0x68, 0xc2, 0x17, 0x43, 0x96, 0x3c, 0xe9, 0x94, 0x41, 0xeb, 0x3e, 0x6a, 0xbf, 0x15, 0xc0 /* 0x90 */
    , 0x4b, 0x9e, 0x34, 0xe1, 0xb5, 0x60, 0xca, 0x1f, 0x62, 0xb7, 0x1d, 0xc8, 0x9c, 0x49, 0xe3, 0x36 /* 0xA0 */
    , 0x19, 0xcc, 0x66, 0xb3, 0xe7, 0x32, 0x98, 0x4d, 0x30, 0xe5, 0x4f, 0x9a, 0xce, 0x1b, 0xb1, 0x64 /* 0xB0 */
    , 0x72, 0xa7, 0x0d, 0xd8, 0x8c, 0x59, 0xf3, 0x26, 0x5b, 0x8e, 0x24, 0xf1, 0xa5, 0x70, 0xda, 0x0f /* 0xC0 */
    , 0x20, 0xf5, 0x5f, 0x8a, 0xde, 0x0b, 0xa1, 0x74, 0x09, 0xdc, 0x76, 0xa3, 0xf7, 0x22, 0x88, 0x5d /* 0xD0 */
    , 0xd6, 0x03, 0xa9, 0x7c, 0x28, 0xfd, 0x57, 0x82, 0xff, 0x2a, 0x80, 0x55, 0x01, 0xd4, 0x7e, 0xab /* 0xE0 */
    , 0x84, 0x51, 0xfb, 0x2e, 0x7a, 0xaf, 0x05, 0xd0, 0xad, 0x78, 0xd2, 0x07, 0x53, 0x86, 0x2c, 0xf9 /* 0xF0 */
};



/*-- begin: prototype of function --*/

RM_Status RM_AnalyzeReceivedFrame( RM_contents* pContents, uint8_t opcode);
bool      RM_SetTransmitBlockData( RM_contents* pContents );
bool      RM_SetTransmitLogData( RM_contents* pContents );

uint16_t  RM_GetBlockData( RM_Data* pData, RM_TransmittingData* pTransmitData );
uint16_t  RM_GetLogData( RM_LogInformation* pLogInformation, RM_TransmittingData* pTransmitData );
uint8_t   RM_GetCRC( uint8_t buffer[], uint8_t bufferSize );

RM_Status RM_SetLogStart( RM_contents* pContents );
RM_Status RM_SetLogStop( RM_contents* pContents );
RM_Status RM_SetLogPeriod( RM_contents* pContents );
RM_Status RM_WriteValue( RM_contents* pContents );
RM_Status RM_SetLogData( RM_contents* pContents );
RM_Status RM_ValidatePassKey( RM_contents* pContents );
RM_Status RM_SetDumpData( RM_contents* pContents );
RM_Status RM_SetBypassFunction( RM_contents* pContents );

/*-- begin: functions --*/

/** 
 * @brief Initializes RM_contents structure.
 * 
 * @param obj Pointer to RM_contents structure.
 * @param version[] Array containing version information.
 * @param versionSize Size of version array.
 * @param millisCount The millisecond count for calling RM_Task().
 * @param passkey Passkey for authorization.
 */
void RM_Initialize( RM_contents* obj, uint8_t version[], uint16_t versionSize, uint16_t millisCount, uint32_t passkey )
{
#ifdef RM_ADDRESS_4BYTE
    obj->versionInfo.address = (uint32_t)version;
#else
    obj->versionInfo.address = (uint16_t)version;
#endif
    obj->versionInfo.length = versionSize;

    obj->millisCnt = millisCount;

    obj->passKey = passkey;
    obj->logTimeoutCnt = 0;
    obj->logIntervalCnt = 0;
    obj->logIntervalPeriod = RM_SND_DEFAULT_CNT;

    obj->isLogging = false;
    obj->isApproved = false;
    obj->isRequestFinished = true;
    obj->masCnt = 0x00;
    obj->slvCnt = 0x01;  // initial slv_cnt is "0xX1"

    obj->bypassFunction = RM_BYPASS_FUNC_NULL;

    obj->log.currentIndex = 0;
    obj->log.availableIndex = 0;

    obj->rxData.timeoutCnt = 0;
    obj->rxData.length = 0;
    obj->rxData.status = RM_RECEIVED_STATUS_READY;

    obj->txData.currentIndex = 0;
    obj->txData.maxIndex = 0;
    obj->txData.status = RM_TRANSMIT_STATUS_COMPLETE;

}

/** 
 * @fn void RM_Task( RM_contents* obj )
 * @brief Main task function to process received and transmit data, to be called repeatedly.
 * 
 * @param obj Pointer to RM_contents structure.
 */
void RM_Task( RM_contents* obj )
{
    RM_Status result;
    uint8_t opcode;
    uint8_t master_count;
    uint8_t crc;

    if( obj->rxData.status == RM_RECEIVED_STATUS_BUSY_NORMAL || obj->rxData.status == RM_RECEIVED_STATUS_BUSY_ESCAPE )
    {
        obj->rxData.timeoutCnt += obj->millisCnt;
        if( obj->rxData.timeoutCnt >= RM_RCV_TIMEOUT_CNT )
        {
            RM_ClearReceivedState(&obj->rxData);
        }
    }
    else if( obj->rxData.status == RM_RECEIVED_STATUS_COMPLETE && obj->isRequestFinished == true )
    {
        crc = RM_GetCRC(obj->rxData.buffer, obj->rxData.length);
        if( crc == 0 )
        {
            obj->rxData.length--;     // delete crc data size
            opcode = obj->rxData.buffer[RM_FRAME_SEQCODE] & (uint8_t)0x0F;
            master_count = obj->rxData.buffer[RM_FRAME_SEQCODE] & (uint8_t)0xF0;

            result = RM_AnalyzeReceivedFrame( obj, opcode );

            if(result != RM_STATUS_ERR)
            {
                obj->masCnt = master_count;
                obj->logTimeoutCnt = 0;
            }

            if(result == RM_STATUS_SUCCESS)
            {
                obj->isRequestFinished = RM_SetTransmitBlockData(obj);
            }

        }

        RM_ClearReceivedState(&obj->rxData);
    }

    if( obj->isRequestFinished == false )
    {
        obj->isRequestFinished = RM_SetTransmitBlockData(obj);
    }

    if(obj->isLogging == true)
    {
        obj->logTimeoutCnt += obj->millisCnt;
        if( obj->logTimeoutCnt >= RM_REQ_TIMEOUT_CNT )
        {
            obj->logTimeoutCnt = 0;
            obj->isLogging = false;
        }

        obj->logIntervalCnt += obj->millisCnt;
        if( obj->logIntervalCnt >= obj->logIntervalPeriod )
        {
            obj->logIntervalCnt = 0;

            if( obj->isRequestFinished == true )
            {
                RM_SetTransmitLogData(obj);
            }

        }

    }

}

/** 
 * @fn void RM_ClearReceivedState(RM_ReceivedData* pReceivedData)
 * @brief Clears the received data state.
 * 
 * @param pReceivedData Pointer to RM_ReceivedData structure.
 */
void RM_ClearReceivedState(RM_ReceivedData* pReceivedData)
{
    pReceivedData->status = RM_RECEIVED_STATUS_READY;
    pReceivedData->length = 0;
    pReceivedData->timeoutCnt = 0;
}

/** 
 * @fn void RM_DecodeReceivedData(RM_ReceivedData* pReceivedData, uint8_t  data)
 * @brief Decodes the received data.
 * 
 * @param pReceivedData Pointer to RM_ReceivedData structure.
 * @param data Received data byte.
 */
void RM_DecodeReceivedData(RM_ReceivedData* pReceivedData, uint8_t  data)
{
    if(pReceivedData->status == RM_RECEIVED_STATUS_COMPLETE)
    {
        return;
    }

    if( pReceivedData->length >= RM_RCV_FRAME_BUFF_SIZE )
    {
        pReceivedData->status = RM_RECEIVED_STATUS_READY;
        pReceivedData->length = 0;
    }

    if(pReceivedData->status == RM_RECEIVED_STATUS_READY)
    {
        if( data == RM_FRAME_CHAR_END )
        {
            pReceivedData->status = RM_RECEIVED_STATUS_BUSY_NORMAL;
        }
    }
    else if(pReceivedData->status == RM_RECEIVED_STATUS_BUSY_NORMAL)
    {
        if( data == RM_FRAME_CHAR_ESC )
        {
            /* Start escape sequence */
            pReceivedData->status = RM_RECEIVED_STATUS_BUSY_ESCAPE;
        }
        else if( data == RM_FRAME_CHAR_END )
        {
            if( pReceivedData->length == 0 )
            {
                /* receive order RM_FRAME_CHAR_END,RM_FRAME_CHAR_END */
                /* Last RM_FRAME_CHAR_END might be Start of Frame */
            }
            else
            {
                /* End SLIP Frame */
                pReceivedData->status = RM_RECEIVED_STATUS_COMPLETE;
            }

        }
        else
        {
            pReceivedData->buffer[pReceivedData->length] = data;
            pReceivedData->length++;

        }

    }
    else if(pReceivedData->status == RM_RECEIVED_STATUS_BUSY_ESCAPE)
    {
        pReceivedData->status = RM_RECEIVED_STATUS_BUSY_NORMAL;
        if( data == RM_FRAME_CHAR_ESC_END )
        {
            pReceivedData->buffer[pReceivedData->length] = RM_FRAME_CHAR_END;
            pReceivedData->length++;
        }
        else if( data == RM_FRAME_CHAR_ESC_ESC )
        {
            pReceivedData->buffer[pReceivedData->length] = RM_FRAME_CHAR_ESC;
            pReceivedData->length++;
        }
        else
        {
            /* Purge received data */
            pReceivedData->status = RM_RECEIVED_STATUS_READY;
            pReceivedData->length = 0;
        }

    }
}

/** 
 * @fn bool RM_SetTransmitBlockData( RM_contents* pContents )
 * @brief Sets the data to be transmitted in block mode.
 * 
 * @param pContents Pointer to RM_contents structure.
 * @return true if the data is set for transmission, false otherwise.
 */
bool RM_SetTransmitBlockData( RM_contents* pContents )
{
    uint8_t  crc;
    uint8_t  data_size;
    uint8_t  frame_size;

    if(pContents->txData.status != RM_TRANSMIT_STATUS_COMPLETE)
    {
        return false;
    }

    pContents->slvCnt++;
    if( pContents->slvCnt > 0x0F )
    {
        pContents->slvCnt = 0x01;
    }
    
    data_size = RM_GetBlockData( &pContents->block, &pContents->txData );

    /* response opcode */
    pContents->txData.buffer[RM_FRAME_SEQCODE] = pContents->masCnt + pContents->slvCnt;

    frame_size = 1;
    frame_size += data_size;
    crc = RM_GetCRC(pContents->txData.buffer, frame_size);
    pContents->txData.buffer[frame_size] = crc;
    frame_size++;

    pContents->txData.currentIndex = 0;
    pContents->txData.maxIndex = frame_size;
    pContents->txData.status = RM_TRANSMIT_STATUS_READY;

    return true;
}

/** 
 * @fn bool RM_SetTransmitLogData( RM_contents* pContents )
 * @brief Sets the data to be transmitted in log mode.
 * 
 * @param pContents Pointer to RM_contents structure.
 * @return true if the data is set for transmission, false otherwise.
 */
bool RM_SetTransmitLogData( RM_contents* pContents )
{
    uint8_t  crc;
    uint8_t  data_size;
    uint8_t  frame_size;

    pContents->slvCnt++;
    if( pContents->slvCnt > 0x0F )
    {
        pContents->slvCnt = 0x01;
    }

    if(pContents->txData.status != RM_TRANSMIT_STATUS_COMPLETE)
    {
        return false;
    }

    data_size = RM_GetLogData( &pContents->log, &pContents->txData );

    if( data_size == 0 )
    {
        return false;
    }

    /* response opcode */
    pContents->txData.buffer[RM_FRAME_SEQCODE] = pContents->masCnt + pContents->slvCnt;

    frame_size = 1;
    frame_size += data_size;
    crc = RM_GetCRC(pContents->txData.buffer, frame_size);
    pContents->txData.buffer[frame_size] = crc;
    frame_size++;

    pContents->txData.currentIndex = 0;
    pContents->txData.maxIndex = frame_size;
    pContents->txData.status = RM_TRANSMIT_STATUS_READY;

    return true;
}

/** 
 * @fn RM_Status RM_AnalyzeReceivedFrame( RM_contents* pContents, uint8_t opcode)
 * @brief Analyzes the received frame and performs appropriate actions based on the opcode.
 * 
 * @param pContents Pointer to RM_contents structure.
 * @param opcode The operation code from the received frame.
 * @return RM_Status indicating the status of operation.
 */
RM_Status RM_AnalyzeReceivedFrame( RM_contents* pContents, uint8_t opcode)
{
    RM_Status result;

    result = RM_STATUS_ERR;

    if( pContents->isApproved == false )
    {
        if( opcode == 0x06 )
        {
            /* Try connecting */
            result = RM_ValidatePassKey( pContents );
        }

    }
    else
    {
        switch( opcode )
        {
        case 0x01:
            result = RM_SetLogStart( pContents );
            break;

        case 0x02:
            result = RM_SetLogStop( pContents );
            break;

        case 0x03:
            result = RM_SetLogPeriod( pContents );
            break;

        case 0x04:
            result = RM_WriteValue( pContents );
            break;

        case 0x05:
            result = RM_SetLogData( pContents );
            break;

        case 0x06:
            result = RM_ValidatePassKey( pContents );
            break;

        case 0x07:
            result = RM_SetDumpData( pContents );
            break;

        case 0x08:
            result = RM_SetBypassFunction( pContents );
            break;

        default:
            break;
        }

    }

    return result;

}

/** 
 * @fn RM_Status RM_SetLogStart( RM_contents* pContents )
 * @brief Starts logging data.
 * 
 * Initiates the logging process in the system. This function configures the necessary parameters to begin data logging.
 * 
 * @param pContents Pointer to RM_contents structure containing relevant data and configurations.
 * @return RM_Status indicating the success or failure of the operation.
 */
RM_Status RM_SetLogStart( RM_contents* pContents )
{
    if( pContents->rxData.length != (1 + 0) )
    {
        return RM_STATUS_ERR;
    }

    pContents->block.address = 0;
    pContents->block.length = 0;

    if(pContents->isLogging == true)
    {
        return RM_STATUS_SUCCESS_NR;
    }

    pContents->isLogging = true;
    
    return RM_STATUS_SUCCESS;
}

/** 
 * @fn RM_Status RM_SetLogStop( RM_contents* pContents )
 * @brief Stops logging data.
 * 
 * Terminates the ongoing data logging process. This function is responsible for safely concluding any logging activities.
 * 
 * @param pContents Pointer to RM_contents structure containing relevant data and configurations.
 * @return RM_Status indicating the success or failure of the operation.
 */
RM_Status RM_SetLogStop( RM_contents* pContents )
{
    if( pContents->rxData.length != (1 + 0) )
    {
        return RM_STATUS_ERR;
    }

    pContents->block.address = 0;
    pContents->block.length = 0;
    pContents->isLogging = false;
    
    return RM_STATUS_SUCCESS;
}

/** 
 * @fn RM_Status RM_SetLogPeriod( RM_contents* pContents )
 * @brief Sets the period for logging data.
 * 
 * Configures the time interval for the logging process. This function determines how frequently the data is logged.
 * 
 * @param pContents Pointer to RM_contents structure containing relevant data and configurations.
 * @return RM_Status indicating the success or failure of the operation.
 */
RM_Status RM_SetLogPeriod( RM_contents* pContents )
{
    uint16_t period;

    if( pContents->rxData.length != (1 + 2) )
    {
        return RM_STATUS_ERR;
    }

    period  = (uint16_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 1];
    period  = period << 8;
    period |= (uint16_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 0];

    if(period == 0)
    {
        return RM_STATUS_ERR;
    }

    pContents->logIntervalPeriod = period;

    pContents->block.address = 0;
    pContents->block.length = 0;
    pContents->isLogging = false;

    return RM_STATUS_SUCCESS;
}

/** 
 * @fn RM_Status RM_WriteValue( RM_contents* pContents )
 * @brief Writes a value to a specific location.
 * 
 * This function is used to write a specified value to a designated address or location within the system.
 * 
 * @param pContents Pointer to RM_contents structure containing relevant data and configurations.
 * @return RM_Status indicating the success or failure of the operation.
 */
RM_Status RM_WriteValue( RM_contents* pContents )
{
#ifdef RM_ADDRESS_4BYTE
    uint32_t address;
#else
    uint16_t address;
#endif

    uint16_t offset_index;

    uint8_t size;

    uint8_t  data_8bit;
    uint8_t* ptr_8bit;

    uint16_t  data_16bit;
    uint16_t* ptr_16bit;

    uint32_t  data_32bit;
    uint32_t* ptr_32bit;

#ifdef RM_SUPPORT_64BIT
    uint64_t  data_64bit;
    uint64_t* ptr_64bit;
#endif

    uint16_t available_size = pContents->rxData.length - 1;
    if( (available_size > RM_WRITECONTENTS_TABLE_SIZE) ||
        (available_size == 0 ) )
    {
        return RM_STATUS_ERR;
    }

    size = pContents->rxData.buffer[RM_FRAME_PAYLOAD + 0];
    if( (size == 0) ||
        (size != RM_WriteContentsParser[available_size]) )
    {
        return RM_STATUS_ERR;
    }

#ifdef RM_ADDRESS_4BYTE
    address  = (uint32_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 4];
    address  = address << 8;
    address |= (uint32_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 3];
    address  = address << 8;
    address |= (uint32_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 2];
    address  = address << 8;
    address |= (uint32_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 1];

    offset_index = 5;

#else
    address  = (uint16_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 2];
    address  = address << 8;
    address |= (uint16_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 1];

    offset_index = 3;

#endif

    switch( size )
    {
    case 1:
        data_8bit = pContents->rxData.buffer[RM_FRAME_PAYLOAD + offset_index];

        ptr_8bit = (uint8_t*)address;
        *ptr_8bit = data_8bit;

        break;

    case 2:
        data_16bit  = (uint16_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 1 + offset_index];
        data_16bit  = data_16bit << 8;
        data_16bit |= (uint16_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + offset_index];

        ptr_16bit = (uint16_t*)address;
        *ptr_16bit = data_16bit;

        break;

    case 4:
        data_32bit  = (uint32_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 3 + offset_index];
        data_32bit  = data_32bit << 8;
        data_32bit |= (uint32_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 2 + offset_index];
        data_32bit  = data_32bit << 8;
        data_32bit |= (uint32_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 1 + offset_index];
        data_32bit  = data_32bit << 8;
        data_32bit |= (uint32_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + offset_index];

        ptr_32bit = (uint32_t*)address;
        *ptr_32bit = data_32bit;

        break;

#ifdef RM_SUPPORT_64BIT
    case 8:
        data_64bit  = (uint64_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 7 + offset_index];
        data_64bit  = data_64bit << 8;
        data_64bit |= (uint64_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 6 + offset_index];
        data_64bit  = data_64bit << 8;
        data_64bit |= (uint64_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 5 + offset_index];
        data_64bit  = data_64bit << 8;
        data_64bit |= (uint64_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 4 + offset_index];
        data_64bit  = data_64bit << 8;
        data_64bit |= (uint64_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 3 + offset_index];
        data_64bit  = data_64bit << 8;
        data_64bit |= (uint64_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 2 + offset_index];
        data_64bit  = data_64bit << 8;
        data_64bit |= (uint64_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 1 + offset_index];
        data_64bit  = data_64bit << 8;
        data_64bit |= (uint64_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + offset_index];

        ptr_64bit = (uint64_t*)address;
        *ptr_64bit = data_64bit;

        break;
#endif

    default:
        break;

    }
    
    pContents->block.address = 0;
    pContents->block.length = 0;

    if(pContents->isLogging == true)
    {
        return RM_STATUS_SUCCESS_NR;
    }

    return RM_STATUS_SUCCESS;
}

/** 
 * @fn RM_Status RM_SetLogData( RM_contents* pContents )
 * @brief Sets data for logging.
 * 
 * Configures what data should be logged. This function specifies the data parameters and details that will be recorded in the log.
 * 
 * @param pContents Pointer to RM_contents structure containing relevant data and configurations.
 * @return RM_Status indicating the success or failure of the operation.
 */
RM_Status RM_SetLogData( RM_contents* pContents )
{
#ifdef RM_ADDRESS_4BYTE
    uint32_t address;
#else
    uint16_t address;
#endif
    uint8_t  size;
    uint16_t total_size;
    uint16_t base_index;
    uint16_t index;
    uint8_t  bitmap;
    uint16_t max_index;

    uint16_t available_size = pContents->rxData.length - 1;
    if( (available_size > RM_LOGCONTENTS_TABLE_SIZE) ||
        (available_size == 0 ) )
    {
        goto RM_LABEL_SETLOG_FAILED;
    }

    max_index = (uint16_t)RM_LogContentsParser[available_size];

    if( max_index == 0 )
    {
        goto RM_LABEL_SETLOG_FAILED;
    }

    bitmap = pContents->rxData.buffer[RM_FRAME_PAYLOAD + 0] & RM_SETLOG_BIT_MASK;

    /* Detect Start of SetLogDataFrame */
    if( (bitmap & RM_SETLOG_START_BIT ) == RM_SETLOG_START_BIT )
    {
        pContents->log.currentIndex = 0;
    }

    if( (pContents->log.currentIndex + max_index) > RM_LOG_FACTOR_MAX )
    {
        goto RM_LABEL_SETLOG_FAILED;
    }

    for( index = 0; index < max_index; index++ )
    {
        base_index = index * RM_LOGCONTENTS_DATA_UNIT;
        base_index++;

        size = pContents->rxData.buffer[RM_FRAME_PAYLOAD + base_index];

#ifdef RM_SUPPORT_64BIT
        if( (size != 1) && (size != 2) && (size != 4) && (size != 8) )
#else
        if( (size != 1) && (size != 2) && (size != 4) )
#endif
        {
            goto RM_LABEL_SETLOG_FAILED;
        }

        pContents->log.sizeArray[pContents->log.currentIndex] = size;

#ifdef RM_ADDRESS_4BYTE
        address  = (uint32_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + base_index+4];
        address  = address << 8;
        address |= (uint32_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + base_index+3];
        address  = address << 8;
        address |= (uint32_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + base_index+2];
        address  = address << 8;
        address |= (uint32_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + base_index+1];

#else
        address  = (uint16_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + base_index+2];
        address  = address << 8;
        address |= (uint16_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + base_index+1];

#endif
        pContents->log.addressArray[pContents->log.currentIndex] = address;
        pContents->log.currentIndex++;
    }

    /* Detect End of SetLogDataFrame */
    if( (bitmap & RM_SETLOG_END_BIT ) == RM_SETLOG_END_BIT )
    {
        pContents->log.availableIndex = pContents->log.currentIndex;
    }
    
    total_size = 0;
    for( index = 0; index < pContents->log.currentIndex; index++ )
    {
        total_size += (uint16_t)pContents->log.sizeArray[index];
    }

    if( total_size > RM_SND_PAYLOAD_SIZE )
    {
        goto RM_LABEL_SETLOG_FAILED;
    }

RM_LABEL_SETLOG_SUCCESS:
    pContents->block.address = 0;
    pContents->block.length = 0;
    pContents->isLogging = false;
    
    return RM_STATUS_SUCCESS;

RM_LABEL_SETLOG_FAILED:
    pContents->log.currentIndex = 0;
    pContents->log.availableIndex = 0;
    
    pContents->block.address = 0;
    pContents->block.length = 0;
    pContents->isLogging = false;

    return RM_STATUS_ERR;
}

/** 
 * @fn RM_Status RM_ValidatePassKey( RM_contents* pContents )
 * @brief Validates the passkey for authentication.
 * 
 * Checks if the provided passkey matches the expected value for authentication purposes. This is crucial for ensuring secure access.
 * 
 * @param pContents Pointer to RM_contents structure containing relevant data and configurations.
 * @return RM_Status indicating the success or failure of the operation.
 */
RM_Status RM_ValidatePassKey( RM_contents* pContents )
{
#ifdef RM_ADDRESS_4BYTE
    uint32_t address;
#else
    uint16_t address;
#endif
    uint16_t length;
    uint32_t passkey;

    if( pContents->rxData.length != (1 + 4))
    {
        pContents->isApproved = false;
        return RM_STATUS_ERR;
    }

    passkey  = pContents->rxData.buffer[RM_FRAME_PAYLOAD + 3];
    passkey  = passkey << 8;
    passkey |= pContents->rxData.buffer[RM_FRAME_PAYLOAD + 2];
    passkey  = passkey << 8;
    passkey |= pContents->rxData.buffer[RM_FRAME_PAYLOAD + 1];
    passkey  = passkey << 8;
    passkey |= pContents->rxData.buffer[RM_FRAME_PAYLOAD + 0];

    if( passkey != pContents->passKey )
    {
        pContents->isApproved = false;
        return RM_STATUS_ERR;
    }

    address = pContents->versionInfo.address;
    length = pContents->versionInfo.length;

    if(length > RM_SND_PAYLOAD_SIZE)
    {
        length = RM_SND_PAYLOAD_SIZE;
    }

    pContents->isApproved = true;
    pContents->block.address = address;
    pContents->block.length = length;
    pContents->isLogging = false;
    
    return RM_STATUS_SUCCESS;
}

/** 
 * @fn RM_Status RM_SetDumpData( RM_contents* pContents )
 * @brief Sets data for dumping.
 * 
 * Configures the system to dump a certain amount of data.
 * 
 * @param pContents Pointer to RM_contents structure containing relevant data and configurations.
 * @return RM_Status indicating the success or failure of the operation.
 */
RM_Status RM_SetDumpData( RM_contents* pContents )
{
#ifdef RM_ADDRESS_4BYTE
    uint32_t address;
    uint16_t length;

    uint16_t available_size = pContents->rxData.length - 1;
    if(available_size != 5)
    {
        return RM_STATUS_ERR;
    }

    length = pContents->rxData.buffer[RM_FRAME_PAYLOAD + 4];

    address  = (uint32_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 3];
    address  = address << 8;
    address |= (uint32_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 2];
    address  = address << 8;
    address |= (uint32_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 1];
    address  = address << 8;
    address |= (uint32_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD];

#else
    uint16_t address;
    uint16_t length;

    uint16_t available_size = pContents->rxData.length - 1;
    if(available_size != 3)
    {
        return RM_STATUS_ERR;
    }

    length = pContents->rxData.buffer[RM_FRAME_PAYLOAD + 2];

    address  = (uint16_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD + 1];
    address  = address << 8;
    address |= (uint32_t)pContents->rxData.buffer[RM_FRAME_PAYLOAD];

#endif

    if(length > RM_SND_PAYLOAD_SIZE)
    {
        return RM_STATUS_ERR;
    }

    pContents->block.address = address;
    pContents->block.length = length;
    pContents->isLogging = false;
    
    return RM_STATUS_SUCCESS;
}

/** 
 * @fn RM_Status RM_SetBypassFunction( RM_contents* pContents )
 * @brief Sets a bypass function.
 * 
 * Configures a function to bypass certain standard procedures or checks. This can be used for special operations.
 * 
 * @param pContents Pointer to RM_contents structure containing relevant data and configurations.
 * @return RM_Status indicating the success or failure of the operation.
 */
RM_Status RM_SetBypassFunction( RM_contents* pContents )
{
#ifdef RM_ADDRESS_4BYTE
    uint32_t address;
#else
    uint16_t address;
#endif
    uint16_t length;
    RM_BypassResponse response;
    response.status = RM_STATUS_ERR;

    if( pContents->bypassFunction != RM_BYPASS_FUNC_NULL )
    {
        response = pContents->bypassFunction( &pContents->rxData.buffer[RM_FRAME_PAYLOAD], (pContents->rxData.length - 1) );

        address = (uint32_t)response.buffer;
        length = response.length;

        if(length > RM_SND_PAYLOAD_SIZE)
        {
            length = RM_SND_PAYLOAD_SIZE;
        }

    }

    pContents->block.address = address;
    pContents->block.length = length;
    pContents->isLogging = false;
    
    return response.status;
}

/**
 * @fn uint16_t RM_GetLogData( RM_LogInformation* pLogInformation, RM_TransmittingData* pTransmitData )
 * @brief Retrieves log data for transmission.
 * 
 * @param pLogInformation Pointer to RM_LogInformation structure.
 * @param pTransmitData Pointer to RM_TransmittingData structure.
 * @return The size of the log data prepared for transmission.
 */
uint16_t RM_GetLogData( RM_LogInformation* pLogInformation, RM_TransmittingData* pTransmitData )
{
#ifdef RM_ADDRESS_4BYTE
    uint32_t address;
#else
    uint16_t address;
#endif
    uint16_t index;
    uint16_t payload_index;

    uint8_t  data_8bit;
    uint8_t* ptr_8bit;

    uint16_t  data_16bit;
    uint16_t* ptr_16bit;

    uint32_t  data_32bit;
    uint32_t* ptr_32bit;

#ifdef RM_SUPPORT_64BIT
    uint64_t  data_64bit;
    uint64_t* ptr_64bit;
#endif

    if(pLogInformation->availableIndex == 0)
    {
        return 0;
    }

    payload_index = RM_FRAME_PAYLOAD;
    for( index = 0; index < pLogInformation->availableIndex; index++ )
    {
        address = pLogInformation->addressArray[index];

        switch( pLogInformation->sizeArray[index] )
        {
        case 1:
            ptr_8bit = (uint8_t*)address;
            data_8bit = *ptr_8bit;

            pTransmitData->buffer[payload_index] = data_8bit;
            payload_index++;

            break;

        case 2:
            ptr_16bit = (uint16_t*)address;
            data_16bit = *ptr_16bit;

            pTransmitData->buffer[payload_index] = (uint8_t)(data_16bit);
            payload_index++;
            data_16bit = data_16bit >> 8;
            pTransmitData->buffer[payload_index] = (uint8_t)(data_16bit);
            payload_index++;

            break;

        case 4:
            ptr_32bit = (uint32_t*)address;
            data_32bit = *ptr_32bit;

            pTransmitData->buffer[payload_index] = (uint8_t)(data_32bit);
            payload_index++;
            data_32bit = data_32bit >> 8;
            pTransmitData->buffer[payload_index] = (uint8_t)(data_32bit);
            payload_index++;
            data_32bit = data_32bit >> 8;
            pTransmitData->buffer[payload_index] = (uint8_t)(data_32bit);
            payload_index++;
            data_32bit = data_32bit >> 8;
            pTransmitData->buffer[payload_index] = (uint8_t)(data_32bit);
            payload_index++;

            break;

#ifdef RM_SUPPORT_64BIT
        case 8:
            ptr_64bit = (uint64_t*)address;
            data_64bit = *ptr_64bit;

            pTransmitData->buffer[payload_index] = (uint8_t)(data_64bit);
            payload_index++;
            data_64bit = data_64bit >> 8;
            pTransmitData->buffer[payload_index] = (uint8_t)(data_64bit);
            payload_index++;
            data_64bit = data_64bit >> 8;
            pTransmitData->buffer[payload_index] = (uint8_t)(data_64bit);
            payload_index++;
            data_64bit = data_64bit >> 8;
            pTransmitData->buffer[payload_index] = (uint8_t)(data_64bit);
            payload_index++;
            data_64bit = data_64bit >> 8;
            pTransmitData->buffer[payload_index] = (uint8_t)(data_64bit);
            payload_index++;
            data_64bit = data_64bit >> 8;
            pTransmitData->buffer[payload_index] = (uint8_t)(data_64bit);
            payload_index++;
            data_64bit = data_64bit >> 8;
            pTransmitData->buffer[payload_index] = (uint8_t)(data_64bit);
            payload_index++;
            data_64bit = data_64bit >> 8;
            pTransmitData->buffer[payload_index] = (uint8_t)(data_64bit);
            payload_index++;

            break;

#endif
        default:
            break;

        }

    }

    return payload_index;
}

/** 
 * @fn uint16_t RM_GetBlockData( RM_Data* pData, RM_TransmittingData* pTransmitData )
 * @brief Retrieves block data for transmission.
 * 
 * @param pData Pointer to RM_Data structure.
 * @param pTransmitData Pointer to RM_TransmittingData structure.
 * @return The size of the block data prepared for transmission.
 */
uint16_t RM_GetBlockData( RM_Data* pData, RM_TransmittingData* pTransmitData )
{
    uint16_t index;
    uint8_t* ptr_data;

    ptr_data = (uint8_t*)pData->address;
    for( index = 0; index < pData->length; index++ )
    {
        pTransmitData->buffer[RM_FRAME_PAYLOAD + index] = *ptr_data;
        ptr_data++;
    }

    return index;
}

/** 
 * @fn uint8_t RM_GetCRC( uint8_t buffer[], uint8_t bufferSize )
 * @brief Calculates the CRC (Cyclic Redundancy Check) for a given buffer.
 * 
 * @param buffer[] Array containing the data to calculate CRC.
 * @param bufferSize Size of the buffer array.
 * @return The calculated CRC value.
 */
uint8_t RM_GetCRC( uint8_t buffer[], uint8_t bufferSize )
{
    uint8_t index;
    uint8_t crc_index;
    uint8_t crc;

    crc = 0;

    for( index = 0; index < bufferSize; index++ )
    {
      crc_index = crc ^ buffer[ index ];
        crc = RM_CrcTable[ crc_index ];
    }

    return crc;

}

/** 
 * @fn bool RM_EncodeTransmitData( RM_TransmittingData* pTransmitData, uint8_t* pData )
 * @brief Encodes data for transmission.
 * 
 * @param pTransmitData Pointer to RM_TransmittingData structure.
 * @param pData Pointer to store the encoded data.
 * @return true if more data is available for transmission, false if transmission is complete.
 */
bool RM_EncodeTransmitData( RM_TransmittingData* pTransmitData, uint8_t* pData )
{
    bool is_available = true;
    uint8_t tmp = 0x00;

    if( pTransmitData->status == RM_TRANSMIT_STATUS_COMPLETE )
    {
        return false;
    }

    if( pTransmitData->status == RM_TRANSMIT_STATUS_READY )
    {
        pTransmitData->status = RM_TRANSMIT_STATUS_BUSY;
        pTransmitData->currentIndex = 0;
        tmp = RM_FRAME_CHAR_END;
    }
    else if( pTransmitData->status == RM_TRANSMIT_STATUS_BUSY )
    {
        if( pTransmitData->currentIndex < pTransmitData->maxIndex )
        {
            tmp = pTransmitData->buffer[ pTransmitData->currentIndex ];

            if( tmp == RM_FRAME_CHAR_END )
            {
                tmp = RM_FRAME_CHAR_ESC;
                pTransmitData->buffer[ pTransmitData->currentIndex ] = RM_FRAME_CHAR_ESC_END;
            }
            else if( tmp == RM_FRAME_CHAR_ESC )
            {
                tmp = RM_FRAME_CHAR_ESC;
                pTransmitData->buffer[ pTransmitData->currentIndex ] = RM_FRAME_CHAR_ESC_ESC;
            }
            else
            {
                pTransmitData->currentIndex++;
            }
        }
        else
        {
            pTransmitData->status = RM_TRANSMIT_STATUS_CLOSING;
            tmp = RM_FRAME_CHAR_END;
        }
    }
    else if( pTransmitData->status == RM_TRANSMIT_STATUS_CLOSING )
    {
        pTransmitData->status = RM_TRANSMIT_STATUS_COMPLETE;
        is_available = false;
    }

    *pData = tmp;
    return is_available;
}


/*-- end of file --*/
