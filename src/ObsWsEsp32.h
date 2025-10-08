#pragma once

#include <Arduino.h>

struct ObsEvent
{
    const char *id;
    const char *payload;
};

class ObsWsClient
{
public:
    struct Config
    {
        const char *host = nullptr;
        uint16_t port = 4455;
        const char *password = nullptr;
        void (*onEvent)(const ObsEvent &) = nullptr;
    };

    void begin(const Config &config);
    void poll();

private:
    Config config_{};
};
