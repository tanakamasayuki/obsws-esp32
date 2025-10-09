#pragma once

#include <Arduino.h>

struct ObsEvent
{
    const char *id;
    const char *payload;
};

enum class ObsWsStatus
{
    Disconnected,
    Connecting,
    Authenticating,
    Connected,
    Error
};

enum class ObsWsError
{
    None,
    InvalidConfig,
    TransportUnavailable,
    HandshakeRejected,
    AuthenticationFailed,
    NotImplemented
};

class ObsWsClient
{
public:
    using EventCallback = void (*)(const ObsEvent &);
    using StatusCallback = void (*)(ObsWsStatus);
    using ErrorCallback = void (*)(ObsWsError);
    using LogCallback = void (*)(const char *message);

    struct Credentials
    {
        const char *password = nullptr;
    };

    struct Config
    {
        const char *host = nullptr;
        uint16_t port = 4455;
        bool useTls = false;
        Credentials credentials{};
        EventCallback onEvent = nullptr;
        StatusCallback onStatus = nullptr;
        ErrorCallback onError = nullptr;
        LogCallback onLog = nullptr;
        bool autoReconnect = true;
        uint32_t reconnectIntervalMs = 5000;
        uint32_t handshakeTimeoutMs = 8000;
    };

    bool begin(const Config &config);
    void poll();
    void close();
    bool sendRequest(const char *requestType, const char *payload);

    ObsWsStatus status() const;
    ObsWsError lastError() const;

private:
    void changeStatus(ObsWsStatus next);
    void emitError(ObsWsError error);
    void emitLog(const char *message);

    bool connectTransport();
    bool performHandshake();
    bool authenticate();

    Config config_{};
    ObsWsStatus status_ = ObsWsStatus::Disconnected;
    ObsWsError lastError_ = ObsWsError::None;
    unsigned long lastStateChangeMs_ = 0;
    unsigned long lastReconnectAttemptMs_ = 0;
    bool placeholderEventDispatched_ = false;
};
