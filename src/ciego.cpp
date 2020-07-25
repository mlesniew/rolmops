#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <WiFiManager.h>

#define HOSTNAME "Ciego"
#ifndef PASSWORD
#define PASSWORD "rolek"
#endif

#define SR_LTCH D0
#define SR_CLCK D1
#define SR_DATA D2

ESP8266WebServer server{80};

void reset() {
    printf("Reset...\n");
    ESP.restart();
    while (1);
}

void set_relays(uint16_t data) {
    digitalWrite(SR_LTCH, LOW);

    uint8_t a = (data >> 8) & 0xff;
    uint8_t b = data & 0xff;

    shiftOut(SR_DATA, SR_CLCK, MSBFIRST, a);
    shiftOut(SR_DATA, SR_CLCK, MSBFIRST, b);

    digitalWrite(SR_LTCH, HIGH);
}

void setup_wifi() {
    WiFi.hostname(HOSTNAME);
    WiFiManager wifiManager;

    wifiManager.setConfigPortalTimeout(60 * 10);
    if (!wifiManager.autoConnect(HOSTNAME, PASSWORD)) {
        printf("AutoConnect failed.\n");
        reset();
    }
}

int mask_from_comma_separated_list(const String & str) {
    int ret = 0;
    int start_idx = 0;

    printf("Decoding mask %s\n", str.c_str());

    while (1) {
        int comma_idx = str.indexOf(',', start_idx);

        const auto value = (comma_idx < 0 ? str.substring(start_idx) : str.substring(start_idx, comma_idx)).toInt();
        if ((value < 1) || (value > 8)) {
            // error, invalid value or error converting
            printf("Error at char %i.\n", start_idx);
            return 0;
        }

        printf(" * %i\n", value);
        ret |= 1 << (value - 1);

        if (comma_idx < 0)
            break;

        // next time start searching after the comma
        start_idx = comma_idx + 1;
    }
    printf("Parsing complete, mask: %i\n", ret);

    return ret;
}

void up(uint8_t mask) {

}

void down(uint8_t mask) {

}

void stop(uint8_t mask) {

}

void setup_server() {

    auto handler = [](std::function<void(uint8_t)> fn){
        uint8_t mask = 0;


        /* extract mask parameter */
        if (!server.hasArg("blinds")) {
            mask = 0xf;
        } else {
            mask = mask_from_comma_separated_list(server.arg("blinds"));
            if (mask <= 0)
            {
                server.send(400, "text/plain", "Malformed mask");
                return;
            }
        }

        fn(mask);

        server.send(200, "text/plain", "OK");
    };

    server.on("/up", [handler]{ handler(up); });
    server.on("/down", [handler]{ handler(down); });
    server.on("/stop", [handler]{ handler(stop); });

    server.on("/version", []{
            server.send(200, "text/plain", HOSTNAME " " __DATE__ " " __TIME__);
            });

    server.serveStatic("/", LittleFS, "/index.html");
    server.serveStatic("/", LittleFS, "/");

    server.begin();
}

void setup() {
    Serial.begin(9600);
    printf(HOSTNAME " " __DATE__ " " __TIME__ "\n");

    pinMode(SR_LTCH, OUTPUT);
    pinMode(SR_CLCK, OUTPUT);
    pinMode(SR_DATA, OUTPUT);

    set_relays(0);

    LittleFS.begin();
    setup_wifi();
    setup_server();

    printf("Setup complete.\n");
}

void loop() {
    server.handleClient();
}
