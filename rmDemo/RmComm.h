#ifndef RM_COMM_H
#define RM_COMM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "RmCore.h"

/* Raw received data buffer size for rx-irq*/
#define RMCOMM_RXBUFFER_SIZE        16    /* buffer size should be 2^n (n:2-7) */
#define RMCOMM_RXINTRBUFFER_SIZE    32    /* buffer size should be 2^n (n:2-7) */
#define RMCOMM_TXINTRBUFFER_SIZE    128    /* buffer size should be 2^n (n:2-7) */

/* Definitions of return status values */
#ifndef float32_t
typedef float              float32_t;
#endif

#ifndef float64_t
typedef long double        float64_t;
#endif


void RMComm_Initialize( uint8_t version[], uint16_t versionSize, uint16_t millisCount, uint32_t passkey );
void RMComm_Run( void );
bool RMComm_TryTransmission( uint8_t* pbyte );
bool RMComm_GetTransmitData( uint8_t* pbyte );
void RMComm_SetReceivedData( uint8_t data );
bool RMComm_IsConnected();
void RMComm_AttachBypassFunction( rm_bypass_function_t func );

void RMComm_Write(uint8_t data);
uint8_t RMComm_Read(void);
uint16_t RMComm_Available(void);
void RMComm_Print(const char str[]);
void RMComm_Println(void);
void RMComm_PrintSingedNumber(int32_t n);
void RMComm_PrintNumber(uint32_t n);
void RMComm_PrintFloat(float32_t number);



#ifdef __cplusplus
}
#endif

#endif  /* RM_COMM_H */

/*-- end of file --*/
