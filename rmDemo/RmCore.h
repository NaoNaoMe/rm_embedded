#ifndef RM_CORE_H
#define RM_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>


#define RM_SUPPORT_64BIT

#ifdef __AVR__              //This statement for AVR
#define RM_ADDRESS_2BYTE
#elif defined(__arm__)      //This statement for ARM
#define RM_ADDRESS_4BYTE
#else
#define RM_ADDRESS_4BYTE    //You can change address width according to your application
#endif

/*-- begin: definitions --*/
#define RM_LOG_FACTOR_MAX       32
#define RM_SND_PAYLOAD_SIZE     (RM_LOG_FACTOR_MAX*4)
#define RM_SND_FRAME_BUFF_SIZE  (RM_SND_PAYLOAD_SIZE+2)

#define RM_RCV_FRAME_BUFF_SIZE  32

#define RM_RCV_TIMEOUT_CNT      100     // ms
#define RM_REQ_TIMEOUT_CNT      2000    // ms
#define RM_SND_DEFAULT_CNT      500     // ms

typedef enum
{
  RM_STATUS_ERR = 0,
  RM_STATUS_SUCCESS,
  RM_STATUS_SUCCESS_NR  // no response
} RM_Status;


typedef struct RM_BYPASS_RESPONSE
{
    RM_Status status;
    uint8_t *buffer;
    uint16_t length;
} RM_BypassResponse;


typedef enum
{
  RM_RECEIVED_STATUS_READY = 0,
  RM_RECEIVED_STATUS_BUSY_NORMAL,
  RM_RECEIVED_STATUS_BUSY_ESCAPE,
  RM_RECEIVED_STATUS_COMPLETE
} RM_ReceivedStatus;

typedef enum
{
  RM_TRANSMIT_STATUS_COMPLETE = 0,
  RM_TRANSMIT_STATUS_READY,
  RM_TRANSMIT_STATUS_BUSY,
  RM_TRANSMIT_STATUS_CLOSING,
} RM_TransmitStatus;



typedef RM_BypassResponse (*rm_bypass_function_t)(uint8_t payload[], uint16_t length);


typedef struct RM_RECEIVINGDATA
{
    RM_ReceivedStatus status;
    uint8_t buffer[RM_RCV_FRAME_BUFF_SIZE];
    uint16_t length;
    uint16_t timeoutCnt;
} RM_ReceivedData;

typedef struct RM_TRANSMITTINGDATA
{
    RM_TransmitStatus status;
    uint8_t  buffer[RM_SND_FRAME_BUFF_SIZE];
    uint16_t  currentIndex;
    uint16_t  maxIndex;
} RM_TransmittingData;

typedef struct RM_DATA
{
#ifdef RM_ADDRESS_4BYTE
    uint32_t address;
    uint16_t length;
#else
    uint32_t address;
    uint16_t length;
#endif
} RM_Data;

typedef struct RM_LOGINFORMATION
{
#ifdef RM_ADDRESS_4BYTE
    uint8_t  sizeArray[RM_LOG_FACTOR_MAX];
    uint32_t addressArray[RM_LOG_FACTOR_MAX];
#else
    uint8_t  sizeArray[RM_LOG_FACTOR_MAX];
    uint16_t addressArray[RM_LOG_FACTOR_MAX];
#endif
    uint16_t currentIndex;
    uint16_t availableIndex;
} RM_LogInformation;


/**
 * @struct RM_contents
 * @brief Structure to manage and maintain various data and state information for communication and logging.
 * 
 * This structure encapsulates the state and control information for receiving and transmitting data, 
 * logging details, and various operational parameters and flags.
 * 
 * @var RM_contents::rxData
 * Holds the data and status related to received data processing.
 * 
 * @var RM_contents::txData
 * Contains the data and status for data to be transmitted.
 * 
 * @var RM_contents::log
 * Manages the information related to logging activities, including log data and control parameters.
 * 
 * @var RM_contents::block
 * Stores block data information for transmission.
 * 
 * @var RM_contents::versionInfo
 * Holds version information.
 * 
 * @var RM_contents::isLogging
 * Flag indicating whether logging is currently active.
 * 
 * @var RM_contents::isApproved
 * Flag indicating whether the current operation or request is approved.
 * 
 * @var RM_contents::isRequestFinished
 * Flag to indicate the completion status of a request or operation.
 * 
 * @var RM_contents::masCnt
 * Counter used for tracking master operations or sequences.
 * 
 * @var RM_contents::slvCnt
 * Counter used for tracking slave operations or sequences.
 * 
 * @var RM_contents::millisCnt
 * Millisecond counter, generally used for calling RM_Task().
 * 
 * @var RM_contents::logTimeoutCnt
 * Counter for tracking timeout in logging operations.
 * 
 * @var RM_contents::logIntervalCnt
 * Counter for managing intervals between log entries.
 * 
 * @var RM_contents::logIntervalPeriod
 * Specifies the period for logging intervals.
 * 
 * @var RM_contents::passKey
 * Stores the passkey used for authentication.
 * 
 * @var RM_contents::bypassFunction
 * Pointer to a function used to bypass standard operations, typically for custom or specialized procedures.
 */
typedef struct RM_CONTENTS
{
    RM_ReceivedData rxData;
    RM_TransmittingData txData;
    RM_LogInformation log;
    
    RM_Data block;
    RM_Data versionInfo;

    bool     isLogging;
    bool     isApproved;
    bool     isRequestFinished;
    uint16_t masCnt;
    uint16_t slvCnt;

    uint16_t millisCnt;
    uint16_t logTimeoutCnt;

    uint16_t logIntervalCnt;
    uint16_t logIntervalPeriod;

    uint32_t passKey;

    rm_bypass_function_t bypassFunction;

} RM_contents;

void RM_Initialize( RM_contents* obj, uint8_t version[], uint16_t versionSize, uint16_t millisCount, uint32_t passkey );
void RM_Task( RM_contents* obj  );

void RM_DecodeReceivedData(RM_ReceivedData* pReceivingData, uint8_t data);
bool RM_EncodeTransmitData( RM_TransmittingData* pTransmitData, uint8_t* pData );

void RM_ClearReceivedState(RM_ReceivedData* pReceivingData);

#ifdef __cplusplus
}
#endif

#endif  /* RM_CORE_H */

/*-- end of file --*/
