//******************************************************************************
// RmComm(Generic)
// Copyright 2024 Naoya Imai
//******************************************************************************

#include "RmComm.h"
#include <string.h>

#define RMCOMM_FRAME_IDENTIFICATION_IDX     0
#define RMCOMM_DERIVED_FRAME        0x00
#define RMCOMM_DERIVED_MODE_IDX     1
#define RMCOMM_DERIVED_MODE_SERIALCOMM_EMULATION 0x01
#define RMCOMM_DERIVED_PAYLOAD_IDX  2

#define RMCOMM_DERIVED_HEADER_SIZE  2


typedef struct RMCOMM_RINGBUFFER
{
    uint8_t* buffer;
    uint16_t head;
    uint16_t tail;
    uint16_t mask;
} RMComm_RingBuffer;


void RMComm_RingBuffer_Initialize(RMComm_RingBuffer* pContents, uint8_t* array, uint16_t size);
bool RMComm_RingBuffer_Peek(RMComm_RingBuffer* pContents, uint8_t* pData);
bool RMComm_RingBuffer_Remove(RMComm_RingBuffer* pContents);
bool RMComm_RingBuffer_Dequeue(RMComm_RingBuffer* pContents, uint8_t* pData);
bool RMComm_RingBuffer_Enqueue(RMComm_RingBuffer* pContents, uint8_t data);
uint16_t RMComm_RingBuffer_Available(RMComm_RingBuffer* pContents);

/*-- begin: static variables --*/
RM_contents RMCore_object;

RMComm_RingBuffer RMComm_receiveData;
uint8_t RMComm_rxBuffer[RMCOMM_RXBUFFER_SIZE];

RMComm_RingBuffer RMComm_receiveInterruptTransfer;
uint8_t RMComm_rxIntrBuffer[RMCOMM_RXINTRBUFFER_SIZE];

RMComm_RingBuffer RMComm_sendInterruptTransfer;
uint8_t RMComm_txIntrBuffer[RMCOMM_TXINTRBUFFER_SIZE];



/*-- begin: prototype of function --*/

/*-- begin: functions --*/

/**
 * @fn void RMComm_Initialize(uint8_t version[], uint16_t versionSize, uint16_t millisCount, uint32_t passkey)
 * @brief Initializes the RMComm communication system.
 *
 * @param version An array representing the version information.
 * @param versionSize The size of the version array.
 * @param millisCount The millisecond count for calling RMComm_Run().
 * @param passkey Passkey for authorization.
 */
void RMComm_Initialize( uint8_t version[], uint16_t versionSize, uint16_t millisCount, uint32_t passkey )
{
    RM_Initialize(&RMCore_object, version, versionSize, millisCount, passkey);

    RMComm_RingBuffer_Initialize(&RMComm_receiveData, &RMComm_rxBuffer[0], sizeof(RMComm_rxBuffer));
    RMComm_RingBuffer_Initialize(&RMComm_receiveInterruptTransfer, &RMComm_rxIntrBuffer[0], sizeof(RMComm_rxIntrBuffer));
    RMComm_RingBuffer_Initialize(&RMComm_sendInterruptTransfer, &RMComm_txIntrBuffer[0], sizeof(RMComm_txIntrBuffer));

}

/**
 * @fn void RMComm_Run(void)
 * @brief Main operational function for RMComm, to be called repeatedly.
 *        Handles the processing of incoming and outgoing data, and manages the state of the communication system.
 */
void RMComm_Run( void )
{
    uint16_t size;
    uint8_t data;
    uint16_t index;

    if(RMCore_object.rxData.status != RM_RECEIVED_STATUS_COMPLETE)
    {
        size = RMComm_RingBuffer_Available(&RMComm_receiveData);
        while(size > 0)
        {
            RMComm_RingBuffer_Dequeue(&RMComm_receiveData, &data);
            size--;
            RM_DecodeReceivedData(&RMCore_object.rxData, data);
            if( RMCore_object.rxData.status == RM_RECEIVED_STATUS_COMPLETE )
            {
                break;
            }

        }

    }

    if( RMCore_object.rxData.status == RM_RECEIVED_STATUS_COMPLETE )
    {
        if( RMCore_object.rxData.buffer[RMCOMM_FRAME_IDENTIFICATION_IDX] == RMCOMM_DERIVED_FRAME )
        {
            if(RMCore_object.rxData.buffer[RMCOMM_DERIVED_MODE_IDX] == RMCOMM_DERIVED_MODE_SERIALCOMM_EMULATION)
            {
                for(index = RMCOMM_DERIVED_PAYLOAD_IDX; index < RMCore_object.rxData.length; index++)
                {
                    RMComm_RingBuffer_Enqueue(&RMComm_receiveInterruptTransfer, RMCore_object.rxData.buffer[index]);
                }
            }

            RM_ClearReceivedState(&RMCore_object.rxData);
        }

    }

    RM_Task(&RMCore_object);

    size = RMComm_RingBuffer_Available(&RMComm_sendInterruptTransfer);
    if( RMCore_object.isLogging == true &&
       RMCore_object.txData.status == RM_TRANSMIT_STATUS_COMPLETE &&
       size > 0)
    {
        RMCore_object.txData.buffer[RMCOMM_FRAME_IDENTIFICATION_IDX] = RMCOMM_DERIVED_FRAME;
        RMCore_object.txData.buffer[RMCOMM_DERIVED_MODE_IDX] = RMCOMM_DERIVED_MODE_SERIALCOMM_EMULATION;

        if(size > sizeof(RMCore_object.txData.buffer))
        {
            size = sizeof(RMCore_object.txData.buffer);
        }

        for(index = 0; index < size; index++)
        {
            RMComm_RingBuffer_Dequeue(&RMComm_sendInterruptTransfer, &data);
            RMCore_object.txData.buffer[RMCOMM_DERIVED_PAYLOAD_IDX+index] = data;
        }

        RMCore_object.txData.currentIndex = 0;
        RMCore_object.txData.maxIndex = RMCOMM_DERIVED_HEADER_SIZE + size;
        RMCore_object.txData.status = RM_TRANSMIT_STATUS_READY;

    }
    
}

/**
 * @fn bool RMComm_TryTransmission(uint8_t* pbyte)
 * @brief Attempts to transmit a byte via RMComm.
 *
 * @param pbyte Pointer to the byte to be transmitted.
 * @return True if the transmission data was available, false otherwise.
 */
bool RMComm_TryTransmission( uint8_t* pData )
{
    bool is_available = false;

    if( RMCore_object.txData.status == RM_TRANSMIT_STATUS_READY )
    {
        is_available = RMComm_GetTransmitData(pData);
    }

    return is_available;
}

/**
 * @fn bool RMComm_GetTransmitData(uint8_t* pbyte)
 * @brief Retrieves the next byte of data to be transmitted.
 *
 * @param pbyte Pointer to store the next byte for transmission.
 * @return True if there is data to be transmitted, false if the transmit buffer is empty.
 */
bool RMComm_GetTransmitData( uint8_t* pData )
{
    return RM_EncodeTransmitData(&RMCore_object.txData, pData);
}

/**
 * @fn void RMComm_SetReceivedData(uint8_t data)
 * @brief Stores a byte of data received by communication.
 *
 * @param data The byte of data received.
 */
void RMComm_SetReceivedData( uint8_t data )
{
    RMComm_RingBuffer_Enqueue(&RMComm_receiveData, data);
}

/**
 * @fn bool RMComm_IsConnected(void)
 * @brief Checks if the microcontroller is currently connected via RMComm.
 *
 * @return True if RMComm is established, false otherwise.
 */
bool RMComm_IsConnected()
{
    return RMCore_object.isLogging;
}

/**
 * @fn void RMComm_AttachBypassFunction(rm_bypass_function_t func)
 * @brief Attaches a user-defined bypass function to the RMComm system.
 *
 * @param func The function pointer to the bypass function.
 */
void RMComm_AttachBypassFunction( rm_bypass_function_t func )
{
    RMCore_object.bypassFunction = func;
}

/**
 * @fn void RMComm_RingBuffer_Initialize(RMComm_RingBuffer* pContents, uint8_t* array, uint16_t size)
 * Initializes a ring buffer.
 *
 * @param pContents Pointer to the ring buffer to initialize.
 * @param array Pointer to the data array to use as the buffer.
 * @param size Size of the buffer array.
 */
void RMComm_RingBuffer_Initialize(RMComm_RingBuffer *pContents, uint8_t *array, uint16_t size)
{
    int i;
    uint16_t bit_position = 0;

    pContents->head = 0;
    pContents->tail = 0;
    pContents->buffer = array;

    for(i=0; i<16; i++)
    {
        if((size & 0x0001) == 0x0001)
        {
            bit_position = i;
        } 
        size = size >> 1;
    }

    pContents->mask = (1 << bit_position) - 1;
}

/**
 * @fn bool RMComm_RingBuffer_Peek(RMComm_RingBuffer* pContents, uint8_t* pData)
 * Peeks at the next byte in the ring buffer without removing it.
 *
 * @param pContents Pointer to the ring buffer.
 * @param pData Pointer where the peeked data will be stored.
 * @return True if data is available, false otherwise.
 */
bool RMComm_RingBuffer_Peek(RMComm_RingBuffer *pContents, uint8_t *pData)
{
    if (pContents->head == pContents->tail)
    {
        return false;
    }
    *pData = pContents->buffer[pContents->head];
    return true;
}

/**
 * @fn bool RMComm_RingBuffer_Remove(RMComm_RingBuffer* pContents)
 * Removes the next byte from the ring buffer.
 *
 * @param pContents Pointer to the ring buffer.
 * @return True if a byte was successfully removed, false otherwise.
 */
bool RMComm_RingBuffer_Remove(RMComm_RingBuffer *pContents)
{
    if (pContents->head == pContents->tail)
    {
        return false;
    }
    pContents->head = (pContents->head + 1) & pContents->mask;
    return true;
}

/**
 * @fn bool RMComm_RingBuffer_Dequeue(RMComm_RingBuffer* pContents, uint8_t* pData)
 * Dequeues the next byte from the ring buffer.
 *
 * @param pContents Pointer to the ring buffer.
 * @param pData Pointer where the dequeued data will be stored.
 * @return True if a byte was successfully dequeued, false otherwise.
 */
bool RMComm_RingBuffer_Dequeue(RMComm_RingBuffer *pContents, uint8_t *pData)
{
    if (!RMComm_RingBuffer_Peek(pContents, pData))
    {
        return false;
    }

    return RMComm_RingBuffer_Remove(pContents);
}

/**
 * @fn bool RMComm_RingBuffer_Enqueue(RMComm_RingBuffer* pContents, uint8_t data)
 * Enqueues a byte into the ring buffer.
 *
 * @param pContents Pointer to the ring buffer.
 * @param data Byte to enqueue.
 * @return True if the byte was successfully enqueued, false if the buffer is full.
 */
bool RMComm_RingBuffer_Enqueue(RMComm_RingBuffer *pContents, uint8_t data)
{
    uint16_t next_tail = (pContents->tail + 1) & pContents->mask;
    if (next_tail == pContents->head)
    {
        return false;
    }
    pContents->buffer[pContents->tail] = data;
    pContents->tail = next_tail;
    return true;
}

/**
 * @fn uint16_t RMComm_RingBuffer_Available(RMComm_RingBuffer* pContents)
 * Returns the number of bytes available in the ring buffer.
 *
 * @param pContents Pointer to the ring buffer.
 * @return The number of available bytes.
 */
uint16_t RMComm_RingBuffer_Available(RMComm_RingBuffer *pContents)
{
    uint16_t length;
    length = (pContents->tail - pContents->head) & pContents->mask;
    return length;
}

/**
 * @fn void RMComm_WriteArray(const char *str)
 * Writes a string to the RMComm send interrupt transfer buffer.
 *
 * @param str Pointer to the string to be written.
 */
void RMComm_WriteArray(const char *str) {
    const uint8_t *buffer = (const uint8_t *)str;
    uint16_t size = strlen(str);
    
    if (str != NULL)
    {
        while (size--)
        {
            RMComm_Write(*buffer++);
        }
    }
}

/**
 * @fn void RMComm_Write(uint8_t data)
 * Writes a single byte to the RMComm send interrupt transfer buffer.
 *
 * @param data Byte to be written.
 */
void RMComm_Write(uint8_t data)
{
    if( RMCore_object.isLogging == false )
    {
        return;
    }
    
    RMComm_RingBuffer_Enqueue(&RMComm_sendInterruptTransfer, data);
}

/**
 * @fn uint8_t RMComm_Read(void)
 * Reads a single byte from the RMComm receive interrupt transfer buffer.
 *
 * @return The read byte, or 0 if no data is available.
 */
uint8_t RMComm_Read(void)
{
    uint8_t data = 0;
    if (RMComm_RingBuffer_Available(&RMComm_receiveInterruptTransfer) == 0)
    {
        return 0;
    }

    RMComm_RingBuffer_Dequeue(&RMComm_receiveInterruptTransfer, &data);
    return data;
}

/**
 * @fn uint16_t RMComm_Available(void)
 * Returns the number of bytes available in the RMComm receive interrupt transfer buffer.
 *
 * @return The number of available bytes.
 */
uint16_t RMComm_Available(void)
{
    return RMComm_RingBuffer_Available(&RMComm_receiveInterruptTransfer);
}

/**
 * @fn void RMComm_Print(const char str[])
 * Prints a string via the RMComm communication interface.
 *
 * @param str Pointer to the string to be printed.
 */
void RMComm_Print(const char str[])
{
    uint16_t size = strlen(str);
    const uint8_t *buffer = (const uint8_t *)str;
    while (size--) {
        RMComm_Write(*buffer++);
    }
}

/**
 * @fn void RMComm_Println(void)
 * Prints a newline character via the RMComm communication interface.
 */
void RMComm_Println(void)
{
    RMComm_Write('\r');
    RMComm_Write('\n');
}

/**
 * @fn void RMComm_PrintSignedNumber(int32_t n)
 * Prints a signed integer via the RMComm communication interface.
 *
 * @param n Signed integer to be printed.
 */
void RMComm_PrintSignedNumber(int32_t n)
{
    if (n < 0) {
        RMComm_Write('-');
        n = -n;
    }
    RMComm_PrintNumber((uint32_t)n);
}

/**
 * @fn void RMComm_PrintNumber(uint32_t n)
 * Prints an unsigned integer via the RMComm communication interface.
 *
 * @param n Unsigned integer to be printed.
 */
void RMComm_PrintNumber(uint32_t n)
{
    uint8_t base = 10;
    char buf[8 * sizeof(uint32_t) + 1]; // Assumes 8-bit chars plus zero byte.
    char *str = &buf[sizeof(buf) - 1];

    *str = '\0';

    do {
        char c = n % base;
        n /= base;
        *--str = c < 10 ? c + '0' : c + 'A' - 10;
    } while(n);
    
    RMComm_WriteArray(str);
}

/**
 * @fn void RMComm_PrintFloat(float32_t number)
 * Prints a floating-point number via the RMComm communication interface.
 *
 * @param number Floating-point number to be printed.
 */
void RMComm_PrintFloat(float32_t number)
{
    uint8_t i;
    uint8_t digits = 2;
    float32_t rounding;
    uint32_t int_part;
    float32_t remainder;
    uint32_t toPrint;

    // Handle negative numbers
    if (number < 0.0)
    {
        RMComm_Write('-');
        number = -number;
    }

    // Round correctly so that print(1.999, 2) prints as "2.00"
    rounding = 0.5;
    for (i=0; i<digits; ++i)
    {
        rounding /= 10.0;
    }

    number += rounding;

    // Extract the integer part of the number and print it
    int_part = (uint32_t)number;
    remainder = number - (float32_t)int_part;
    RMComm_PrintNumber(int_part);

    // Print the decimal point, but only if there are digits beyond
    if (digits > 0) {
        RMComm_Write('.'); 
    }

    // Extract digits from the remainder one at a time
    while (digits-- > 0)
    {
        remainder *= 10.0;
        toPrint = (uint32_t)(remainder);
        RMComm_PrintNumber(toPrint);
        remainder -= toPrint; 
    }
}

/*-- end of file --*/
