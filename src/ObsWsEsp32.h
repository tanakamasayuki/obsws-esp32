#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <cJSON.h>
#include <string>
#include <vector>

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
        uint64_t eventSubscriptions = 0xFFFFFFFFULL;
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
    void drainEventQueue();

    enum class HandshakeState
    {
        Idle,
        AwaitUpgrade,
        AwaitHello,
        AwaitIdentifyResponse,
        Established
    };

    void handleHelloMessage(cJSON *dataNode);
    void handleIdentifiedMessage();
    void handleEventMessage(cJSON *dataNode);
    void handleRequestResponse(cJSON *dataNode);
    bool sendIdentifyMessage(uint32_t rpcVersion, const char *challenge, const char *salt);
    bool enqueueEvent(const char *id, const char *payload);
    bool ensureQueues();
    bool ensureTransportStopped();
    bool sendText(const char *text, size_t length);
    bool sendFrame(uint8_t opcode, const uint8_t *data, size_t length);
    bool sendControlFrame(uint8_t opcode, const uint8_t *data, size_t length);
    bool sendHandshakeRequest();
    bool processHandshakeBuffer();
    void processRxBuffer();
    void handleIncomingFrame(uint8_t opcode, const uint8_t *payload, size_t length);
    void handlePingFrame(const uint8_t *payload, size_t length);
    bool computeAcceptKey(char *out, size_t outSize);
    bool computeAuthentication(const char *password, const char *salt, const char *challenge, char *out, size_t outSize);

    Config config_{};
    ObsWsStatus status_ = ObsWsStatus::Disconnected;
    ObsWsError lastError_ = ObsWsError::None;
    unsigned long lastStateChangeMs_ = 0;
    unsigned long lastReconnectAttemptMs_ = 0;
    bool placeholderEventDispatched_ = false;
    unsigned long handshakeStartMs_ = 0;
    HandshakeState handshakeState_ = HandshakeState::Idle;
    WiFiClient plainClient_;
    WiFiClientSecure secureClient_;
    Client *transport_ = nullptr;
    QueueHandle_t eventQueue_ = nullptr;
    uint32_t requestCounter_ = 1;
    std::string handshakeBuffer_;
    std::vector<uint8_t> rxBuffer_;
    char secWebsocketKey_[32] = {0};
};
