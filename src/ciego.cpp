#include <Arduino.h>

#define SR_LTCH D0
#define SR_CLCK D1
#define SR_DATA D2

void set_relays(uint16_t data) {
    digitalWrite(SR_LTCH, LOW);

    uint8_t a = (data >> 8) & 0xff;
    uint8_t b = data & 0xff;

    shiftOut(SR_DATA, SR_CLCK, MSBFIRST, a);
    shiftOut(SR_DATA, SR_CLCK, MSBFIRST, b);

    digitalWrite(SR_LTCH, HIGH);
}

void setup() {
    Serial.begin(9600);

    pinMode(SR_LTCH, OUTPUT);
    pinMode(SR_CLCK, OUTPUT);
    pinMode(SR_DATA, OUTPUT);

    set_relays(0);
}

void loop() {
    static unsigned int i = 0;
    uint16_t mask = 1 << i;

    printf("%u\n", i);

    set_relays(mask);
    delay(1000);

    ++i;
    i &= 0xf;
}
