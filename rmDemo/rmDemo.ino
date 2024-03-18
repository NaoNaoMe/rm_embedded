// This program is the minimal code necessary to achieve communication with RM Classic using ArduinoUnoR3.

#include "RmComm.h"

uint32_t previousMillis;

uint32_t testCount = 0;

const char version[] = "ArduinoUnoR3";
const int versionLength = sizeof(version);
const int rmIntervalMillis = 10;
uint32_t previousMillisForRM;

void rm_bg() {
  int size;
  uint8_t data8bit;

  uint32_t current_millis = millis();
  if((current_millis - previousMillisForRM) >= (uint32_t)rmIntervalMillis){
    previousMillisForRM = current_millis;

    // RMComm_SetReceivedData() is expected to be set with received data periodically.
    size = Serial.available();
    while (size-- > 0) {
      RMComm_SetReceivedData(Serial.read());
    }

    // RMComm_Run() is expected to be called periodically.
    RMComm_Run();

    // RMComm_TryTransmission() performs the initial data transmission and then expects transmission interrupts to occur.
    if (RMComm_TryTransmission(&data8bit))
    {
      Serial.write(data8bit);

      while(true)
      {
        // The contents of this while loop are expected to be executed in a transmission interrupt.
        if (RMComm_GetTransmitData(&data8bit))
        {
          // It is expected that the transmission interrupts will continue.
          Serial.write(data8bit);
        }
        else
        {
          // It is expected that the transmission interrupts will be interrupted.
          break;
        }
      }
    }
  }

}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);

  RMComm_Initialize((uint8_t *)version, versionLength, rmIntervalMillis, 0x0000FFFFU);
  previousMillisForRM = millis();
  previousMillis = millis();
}

void loop() {
  // put your main code here, to run repeatedly:
  // Please do not use "delay()", as it will inhibit RM communication.
  uint32_t current_millis = millis();
  if((current_millis - previousMillis) >= 1000){
    previousMillis = current_millis;
    testCount++;
    RMComm_PrintNumber(testCount);
    RMComm_Println();
  }

  rm_bg();
}
