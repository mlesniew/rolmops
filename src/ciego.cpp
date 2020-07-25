#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <WiFiManager.h>
#include <list>

#define HOSTNAME "Ciego"
#ifndef PASSWORD
#define PASSWORD "rolek"
#endif

#define SR_LTCH D0
#define SR_CLCK D1
#define SR_DATA D2

#define RELAY_ACTIVE_TIME 10000

ESP8266WebServer server{80};

uint8_t relay_mask_up = 0;
uint8_t relay_mask_down = 0;

void set_relays(uint8_t up, uint8_t down) {
    digitalWrite(SR_LTCH, LOW);

    // clear common bits
    uint8_t common = up & down;
    up &= ~common;
    down &= ~common;

    shiftOut(SR_DATA, SR_CLCK, LSBFIRST, ~up);
    shiftOut(SR_DATA, SR_CLCK, MSBFIRST, ~down);

    digitalWrite(SR_LTCH, HIGH);
}

struct ScheduledStop {
    ScheduledStop(uint8_t mask) : create_time(millis()), mask(mask) {}

    unsigned long elapsed() const { return millis() - create_time; }

    const unsigned long create_time;
    uint8_t mask;
};

std::list<ScheduledStop> scheduled_stops;

void cancel_scheduled_stops(uint8_t mask) {
    for (auto & e: scheduled_stops) {
        e.mask &= ~mask;
        // TODO: remove elements with a zero mask
    }
}

void schedule_stop(uint8_t mask) {
    cancel_scheduled_stops(mask);
    scheduled_stops.push_back(ScheduledStop(mask));
}

void handle_schedule_stops() {
    if (scheduled_stops.empty()) {
        // nothing scheduled
        return;
    }

    const auto & front = scheduled_stops.front();
    if (front.elapsed() < RELAY_ACTIVE_TIME) {
        // not time for the next one yet
        return;
    }

    // apply mask
    relay_mask_up &= ~front.mask;
    relay_mask_down &= ~front.mask;
    set_relays(relay_mask_up, relay_mask_down);

    // drop first element
    scheduled_stops.pop_front();
}

void reset() {
    printf("Reset...\n");
    ESP.restart();
    while (1);
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

void stop(uint8_t mask) {
    relay_mask_up &= ~mask;
    relay_mask_down &= ~mask;
    set_relays(relay_mask_up, relay_mask_down);
    cancel_scheduled_stops(mask);
}

void up(uint8_t mask) {
    relay_mask_down &= ~mask;
    relay_mask_up |= mask;
    set_relays(relay_mask_up, relay_mask_down);
    schedule_stop(mask);
}

void down(uint8_t mask) {
    relay_mask_up &= ~mask;
    relay_mask_down |= mask;
    set_relays(relay_mask_up, relay_mask_down);
    schedule_stop(mask);
}

void setup_server() {

    auto handler = [](std::function<void(uint8_t)> fn){
        uint8_t mask = 0;


        /* extract mask parameter */
        if (!server.hasArg("blinds")) {
            mask = 0xff;
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

    set_relays(0, 0);

    LittleFS.begin();
    setup_wifi();
    setup_server();

    printf("Setup complete.\n");
}

void loop() {
    server.handleClient();
    handle_schedule_stops();
}
