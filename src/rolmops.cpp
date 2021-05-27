#include <list>

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Ticker.h>

#include <utils/led.h>
#include <utils/reset.h>
#include <utils/wifi_control.h>

#ifdef USE_SPIFFS
#include <FS.h>
#define FileSystem SPIFFS
#else
#include <LittleFS.h>
#define FileSystem LittleFS
#endif

#define HOSTNAME "Rolmops"
#ifndef PASSWORD
#define PASSWORD "rolek"
#endif

#define SR_LTCH D0
#define SR_CLCK D1
#define SR_DATA D2

#define PIN_LED D4

#define RELAY_ACTIVE_TIME (30 * 1000)

BlinkingLed wifi_led(PIN_LED, 0, 91, true);
WiFiControl wifi_control(wifi_led);
ESP8266WebServer server{80};

uint8_t relay_mask_up = 0;
uint8_t relay_mask_down = 0;

void set_relays(uint8_t up, uint8_t down) {
    digitalWrite(SR_LTCH, LOW);

    // clear common bits
    uint8_t common = up & down;
    up &= ~common;
    down &= ~common;

    shiftOut(SR_DATA, SR_CLCK, MSBFIRST, ~down);
    shiftOut(SR_DATA, SR_CLCK, MSBFIRST, ~up);

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

int mask_from_comma_separated_list(const String & str) {
    int ret = 0;
    int start_idx = 0;

    printf("Decoding mask %s\n", str.c_str());

    while (1) {
        int comma_idx = str.indexOf(',', start_idx);

        const long value = (comma_idx < 0 ? str.substring(start_idx) : str.substring(start_idx, comma_idx)).toInt();
        if ((value < 1) || (value > 8)) {
            // error, invalid value or error converting
            printf("Error at char %i.\n", start_idx);
            return 0;
        }

        printf(" * %li\n", value);
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
        wifi_led.set_pattern(0b10);
        BackgroundBlinker blinker(wifi_led);

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
        wifi_led.set_pattern(0);
    };

    server.on("/up", [handler]{ handler(up); });
    server.on("/down", [handler]{ handler(down); });
    server.on("/stop", [handler]{ handler(stop); });

    server.on("/version", []{
            server.send(200, "text/plain", HOSTNAME " " __DATE__ " " __TIME__);
            });

    server.serveStatic("/", FileSystem, "/index.html");
    server.serveStatic("/", FileSystem, "/");

    server.begin();
}

void setup() {
    pinMode(SR_LTCH, OUTPUT);
    pinMode(SR_CLCK, OUTPUT);
    pinMode(SR_DATA, OUTPUT);
    set_relays(0, 0);

    Serial.begin(9600);
    Serial.print(F(
        "88''Yb  dP'Yb  88     8b    d8  dP'Yb  88''Yb .dP'Y8\n"
        "88__dP dP   Yb 88     88b  d88 dP   Yb 88__dP `Ybo.'\n"
        "88'Yb  Yb   dP 88  .o 88YbdP88 Yb   dP 88'''  o.`Y8b\n"
        "88  Yb  YbodP  88ood8 88 YY 88  YbodP  88     8bodP'\n\n"
        HOSTNAME " " __DATE__ " " __TIME__ "\n\n"));

    Serial.print(F("Setup...\n"));

    if (!wifi_control.init(WiFiInitMode::automatic, HOSTNAME, PASSWORD, 5 * 60)) {
        Serial.println(F("Connection setup failed."));
        reset();
    }

    FileSystem.begin();

    MDNS.begin(HOSTNAME);
    setup_server();

    Serial.print(F("Setup complete.\n"));
}

void loop() {
    wifi_control.tick();
    MDNS.update();
    server.handleClient();
    handle_schedule_stops();
}
