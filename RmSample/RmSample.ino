#include "src/rm/rm_comm.h"

#define LED_PIN 3
#define SW_PIN 4

#define INTERVAL_CNT 5
#define RM_PASSWORD  0x0000FFFFU

char VersionInfo[] = "RmSample";

volatile boolean isCounting  = true;

int count = 0;
boolean isLEDOn = false;

int debounceCount = 0;
unsigned long previousMillis = 0;

void setup() {
    pinMode(LED_PIN, OUTPUT);
    pinMode(SW_PIN, INPUT);

    // initialize serial:
    Serial.begin(9600);

    rm_comm_init((uint8_t *)VersionInfo, sizeof(VersionInfo), INTERVAL_CNT, RM_PASSWORD);

    digitalWrite(LED_PIN, HIGH);

}

void loop() {

    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= INTERVAL_CNT) {
        previousMillis = currentMillis;

        if(isCounting == true)
            count++;

        int inPin = digitalRead(SW_PIN);
        if (inPin == LOW)
        {
            debounceCount++;
            if( debounceCount > 200 )
            {
                debounceCount = 200;
                isLEDOn = true;
            }
        }
        else
        {
            debounceCount = 0;
            isLEDOn = false;
        }
        
        if( isLEDOn == true )
            digitalWrite(LED_PIN, LOW);
        else
            digitalWrite(LED_PIN, HIGH);

        uint16_t rcv_count = rm_comm_available();
        while (rcv_count > 0) {
            rm_comm_write(rm_comm_read());
            rcv_count--;
        }

        serialRxEvent();

        rm_comm_run();

        serialTxEvent();

    }

}

void serialRxEvent() {
    int rcv_count = Serial.available();
    while (rcv_count > 0) {
        rm_comm_push_rx_byte((uint8_t)Serial.read());
        rcv_count--;
    }
}

void serialTxEvent() {
    uint8_t data;
    if(rm_comm_try_transmit(&data)) {
        Serial.write(data);
        while( rm_comm_get_tx_byte(&data) )
            Serial.write(data);
    }
    
}