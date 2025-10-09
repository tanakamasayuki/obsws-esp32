#include "ObsWsEsp32.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_system.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>

namespace
{
    constexpr size_t kEventQueueLength = 10;
    constexpr size_t kAuthSecretBufferSize = 64;
    constexpr size_t kAuthResultBufferSize = 128;
    constexpr size_t kMaxHandshakeHeaderSize = 1024;
    constexpr const char *kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    struct InternalEvent
    {
        char *id = nullptr;
        char *payload = nullptr;
    };

    char *duplicateString(const char *source)
    {
        if (source == nullptr)
        {
            return nullptr;
        }

        const size_t length = std::strlen(source);
        char *copy = static_cast<char *>(std::malloc(length + 1));
        if (copy == nullptr)
        {
            return nullptr;
        }

        std::memcpy(copy, source, length);
        copy[length] = '\0';
        return copy;
    }

    void releaseInternalEvent(InternalEvent *evt)
    {
        if (evt == nullptr)
        {
            return;
        }
        if (evt->id != nullptr)
        {
            std::free(evt->id);
        }
        if (evt->payload != nullptr)
        {
            std::free(evt->payload);
        }
        std::free(evt);
    }

    std::string trim(const std::string &value)
    {
        size_t start = 0;
        size_t end = value.size();
        while (start < end && std::isspace(static_cast<unsigned char>(value[start])) != 0)
        {
            ++start;
        }
        while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
        {
            --end;
        }
        return value.substr(start, end - start);
    }
}

bool ObsWsClient::begin(const Config &config)
{
    close();

    config_ = config;
    placeholderEventDispatched_ = false;
    lastError_ = ObsWsError::None;
    handshakeBuffer_.clear();
    rxBuffer_.clear();

    if (config_.host == nullptr || config_.host[0] == '\0')
    {
        emitLog("OBSWS: Invalid configuration (host not set).");
        emitError(ObsWsError::InvalidConfig);
        return false;
    }

    if (!ensureQueues())
    {
        emitLog("OBSWS: Failed to allocate event queue.");
        emitError(ObsWsError::TransportUnavailable);
        return false;
    }

    changeStatus(ObsWsStatus::Connecting);

    if (!connectTransport())
    {
        emitError(ObsWsError::TransportUnavailable);
        return false;
    }

    if (!performHandshake())
    {
        emitError(ObsWsError::HandshakeRejected);
        return false;
    }

    lastReconnectAttemptMs_ = millis();
    emitLog("OBSWS: WebSocket connection initiated.");
    return true;
}

void ObsWsClient::poll()
{
    const unsigned long now = millis();

    if (status_ == ObsWsStatus::Error || status_ == ObsWsStatus::Disconnected)
    {
        if (config_.autoReconnect && config_.host != nullptr && config_.host[0] != '\0')
        {
            if (now - lastReconnectAttemptMs_ >= config_.reconnectIntervalMs)
            {
                lastReconnectAttemptMs_ = now;
                emitLog("OBSWS: Auto-reconnect attempt.");
                begin(config_);
            }
        }
        return;
    }

    if (transport_ != nullptr)
    {
        if (!transport_->connected())
        {
            emitLog("OBSWS: Transport disconnected.");
            ensureTransportStopped();
            handshakeState_ = HandshakeState::Idle;
            changeStatus(ObsWsStatus::Disconnected);
        }
        else
        {
            while (transport_->available() > 0)
            {
                const int byteRead = transport_->read();
                if (byteRead < 0)
                {
                    break;
                }

                if (handshakeState_ == HandshakeState::AwaitUpgrade)
                {
                    handshakeBuffer_.push_back(static_cast<char>(byteRead));
                    if (handshakeBuffer_.size() > kMaxHandshakeHeaderSize)
                    {
                        emitLog("OBSWS: Handshake header too large.");
                        emitError(ObsWsError::HandshakeRejected);
                        ensureTransportStopped();
                        handshakeState_ = HandshakeState::Idle;
                        return;
                    }
                }
                else
                {
                    rxBuffer_.push_back(static_cast<uint8_t>(byteRead));
                }
            }

            if (handshakeState_ == HandshakeState::AwaitUpgrade)
            {
                if (processHandshakeBuffer())
                {
                    handshakeState_ = HandshakeState::AwaitHello;
                    changeStatus(ObsWsStatus::Authenticating);
                }
            }

            if (handshakeState_ != HandshakeState::AwaitUpgrade && !rxBuffer_.empty())
            {
                processRxBuffer();
            }
        }
    }

    if (handshakeState_ != HandshakeState::Established && config_.handshakeTimeoutMs > 0)
    {
        if (now - handshakeStartMs_ >= config_.handshakeTimeoutMs)
        {
            emitLog("OBSWS: Handshake timeout.");
            emitError(ObsWsError::HandshakeRejected);
            ensureTransportStopped();
            changeStatus(ObsWsStatus::Disconnected);
            handshakeState_ = HandshakeState::Idle;
            lastReconnectAttemptMs_ = now;
            return;
        }
    }

    if (eventQueue_ != nullptr)
    {
        InternalEvent *evt = nullptr;
        while (xQueueReceive(eventQueue_, &evt, 0) == pdTRUE)
        {
            if (evt != nullptr)
            {
                if (config_.onEvent != nullptr)
                {
                    ObsEvent event{evt->id != nullptr ? evt->id : "", evt->payload != nullptr ? evt->payload : ""};
                    config_.onEvent(event);
                }
                releaseInternalEvent(evt);
            }
        }
    }
}

void ObsWsClient::close()
{
    placeholderEventDispatched_ = false;
    handshakeState_ = HandshakeState::Idle;
    handshakeStartMs_ = 0;
    handshakeBuffer_.clear();
    rxBuffer_.clear();

    ensureTransportStopped();
    drainEventQueue();

    if (status_ != ObsWsStatus::Disconnected)
    {
        changeStatus(ObsWsStatus::Disconnected);
    }

    lastError_ = ObsWsError::None;
    emitLog("OBSWS: Connection closed.");
}

bool ObsWsClient::sendRequest(const char *requestType, const char *payload)
{
    if (requestType == nullptr || requestType[0] == '\0')
    {
        emitLog("OBSWS: sendRequest requires a request type.");
        return false;
    }

    if (handshakeState_ != HandshakeState::Established)
    {
        emitLog("OBSWS: sendRequest called before handshake completion.");
        lastError_ = ObsWsError::TransportUnavailable;
        return false;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == nullptr)
    {
        emitLog("OBSWS: Failed to allocate request JSON.");
        return false;
    }

    cJSON_AddNumberToObject(root, "op", 6);
    cJSON *dataNode = cJSON_AddObjectToObject(root, "d");
    if (dataNode == nullptr)
    {
        cJSON_Delete(root);
        emitLog("OBSWS: Failed to build request payload.");
        return false;
    }

    cJSON_AddStringToObject(dataNode, "requestType", requestType);

    char requestId[16];
    std::snprintf(requestId, sizeof(requestId), "%lu", static_cast<unsigned long>(requestCounter_++));
    cJSON_AddStringToObject(dataNode, "requestId", requestId);

    if (payload != nullptr && payload[0] != '\0')
    {
        cJSON *payloadNode = cJSON_Parse(payload);
        if (payloadNode == nullptr)
        {
            emitLog("OBSWS: Request payload is not valid JSON.");
            cJSON_Delete(root);
            return false;
        }
        cJSON_AddItemToObject(dataNode, "requestData", payloadNode);
    }

    char *jsonOut = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (jsonOut == nullptr)
    {
        emitLog("OBSWS: Failed to serialise request JSON.");
        return false;
    }

    const bool sent = sendText(jsonOut, std::strlen(jsonOut));
    std::free(jsonOut);

    if (!sent)
    {
        emitLog("OBSWS: Failed to send request.");
        lastError_ = ObsWsError::TransportUnavailable;
        return false;
    }

    return true;
}

ObsWsStatus ObsWsClient::status() const
{
    return status_;
}

ObsWsError ObsWsClient::lastError() const
{
    return lastError_;
}

void ObsWsClient::changeStatus(ObsWsStatus next)
{
    if (status_ == next)
    {
        return;
    }

    status_ = next;
    lastStateChangeMs_ = millis();

    if (status_ != ObsWsStatus::Connected)
    {
        placeholderEventDispatched_ = false;
    }

    if (config_.onStatus != nullptr)
    {
        config_.onStatus(status_);
    }
}

void ObsWsClient::emitError(ObsWsError error)
{
    lastError_ = error;

    if (config_.onError != nullptr)
    {
        config_.onError(error);
    }

    if (error != ObsWsError::None)
    {
        changeStatus(ObsWsStatus::Error);
    }
}

void ObsWsClient::emitLog(const char *message)
{
    if (config_.onLog != nullptr)
    {
        config_.onLog(message);
    }
}

bool ObsWsClient::connectTransport()
{
    ensureTransportStopped();

    if (config_.useTls)
    {
        secureClient_.setInsecure();
        transport_ = &secureClient_;
    }
    else
    {
        transport_ = &plainClient_;
    }

    if (transport_ == nullptr)
    {
        emitLog("OBSWS: No transport available.");
        return false;
    }

    if (!transport_->connect(config_.host, config_.port))
    {
        emitLog("OBSWS: Failed to establish TCP connection.");
        transport_ = nullptr;
        return false;
    }

    if (!sendHandshakeRequest())
    {
        emitLog("OBSWS: Failed to send handshake request.");
        ensureTransportStopped();
        return false;
    }

    handshakeState_ = HandshakeState::AwaitUpgrade;
    handshakeStartMs_ = millis();
    handshakeBuffer_.clear();
    rxBuffer_.clear();
    return true;
}

bool ObsWsClient::performHandshake()
{
    handshakeState_ = HandshakeState::AwaitUpgrade;
    handshakeStartMs_ = millis();
    return true;
}

void ObsWsClient::drainEventQueue()
{
    if (eventQueue_ == nullptr)
    {
        return;
    }

    InternalEvent *evt = nullptr;
    while (xQueueReceive(eventQueue_, &evt, 0) == pdTRUE)
    {
        releaseInternalEvent(evt);
    }
}

bool ObsWsClient::ensureQueues()
{
    if (eventQueue_ != nullptr)
    {
        return true;
    }

    eventQueue_ = xQueueCreate(kEventQueueLength, sizeof(InternalEvent *));
    return eventQueue_ != nullptr;
}

bool ObsWsClient::ensureTransportStopped()
{
    if (transport_ != nullptr)
    {
        transport_->stop();
        transport_ = nullptr;
    }

    plainClient_.stop();
    secureClient_.stop();
    return true;
}

bool ObsWsClient::sendText(const char *text, size_t length)
{
    if (text == nullptr)
    {
        return false;
    }

    return sendFrame(0x1, reinterpret_cast<const uint8_t *>(text), length);
}

bool ObsWsClient::sendFrame(uint8_t opcode, const uint8_t *data, size_t length)
{
    if (transport_ == nullptr || !transport_->connected())
    {
        return false;
    }

    uint8_t header[14];
    size_t headerLen = 0;
    header[headerLen++] = static_cast<uint8_t>(0x80 | (opcode & 0x0F));

    if (length < 126)
    {
        header[headerLen++] = static_cast<uint8_t>(0x80 | length);
    }
    else if (length <= 0xFFFF)
    {
        header[headerLen++] = 0x80 | 126;
        header[headerLen++] = static_cast<uint8_t>((length >> 8) & 0xFF);
        header[headerLen++] = static_cast<uint8_t>(length & 0xFF);
    }
    else
    {
        header[headerLen++] = 0x80 | 127;
        for (int i = 7; i >= 0; --i)
        {
            header[headerLen++] = static_cast<uint8_t>((static_cast<uint64_t>(length) >> (8 * i)) & 0xFF);
        }
    }

    uint8_t maskKey[4];
    for (int i = 0; i < 4; ++i)
    {
        maskKey[i] = static_cast<uint8_t>(esp_random() & 0xFF);
        header[headerLen++] = maskKey[i];
    }

    if (transport_->write(header, headerLen) != headerLen)
    {
        return false;
    }

    if (length == 0)
    {
        transport_->flush();
        return true;
    }

    size_t written = 0;
    while (written < length)
    {
        const size_t chunk = std::min(static_cast<size_t>(128), length - written);
        uint8_t buffer[128];
        for (size_t i = 0; i < chunk; ++i)
        {
            const uint8_t source = data != nullptr ? data[written + i] : 0;
            buffer[i] = static_cast<uint8_t>(source ^ maskKey[(written + i) % 4]);
        }
        if (transport_->write(buffer, chunk) != chunk)
        {
            return false;
        }
        written += chunk;
    }

    transport_->flush();
    return true;
}

bool ObsWsClient::sendControlFrame(uint8_t opcode, const uint8_t *data, size_t length)
{
    return sendFrame(opcode, data, length);
}

bool ObsWsClient::sendHandshakeRequest()
{
    if (transport_ == nullptr)
    {
        return false;
    }

    unsigned char rawKey[16];
    for (unsigned char &byte : rawKey)
    {
        byte = static_cast<unsigned char>(esp_random() & 0xFF);
    }

    size_t keyLen = 0;
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char *>(secWebsocketKey_), sizeof(secWebsocketKey_) - 1, &keyLen, rawKey, sizeof(rawKey)) != 0)
    {
        return false;
    }
    secWebsocketKey_[keyLen] = '\0';

    transport_->print("GET / HTTP/1.1\r\n");
    transport_->print("Host: ");
    transport_->print(config_.host);
    transport_->print(":");
    transport_->print(config_.port);
    transport_->print("\r\n");
    transport_->print("Upgrade: websocket\r\n");
    transport_->print("Connection: Upgrade\r\n");
    transport_->print("Sec-WebSocket-Version: 13\r\n");
    transport_->print("Sec-WebSocket-Protocol: obswebsocket.json\r\n");
    transport_->print("User-Agent: OBSWS-ESP32\r\n");
    transport_->print("Sec-WebSocket-Key: ");
    transport_->print(secWebsocketKey_);
    transport_->print("\r\n\r\n");

    transport_->flush();
    return true;
}

bool ObsWsClient::processHandshakeBuffer()
{
    const std::string::size_type terminator = handshakeBuffer_.find("\r\n\r\n");
    if (terminator == std::string::npos)
    {
        return false;
    }

    const std::string headerSection = handshakeBuffer_.substr(0, terminator);
    const std::string remaining = handshakeBuffer_.substr(terminator + 4);

    const std::string::size_type statusEnd = headerSection.find("\r\n");
    if (statusEnd == std::string::npos)
    {
        emitLog("OBSWS: Malformed handshake response.");
        emitError(ObsWsError::HandshakeRejected);
        ensureTransportStopped();
        handshakeState_ = HandshakeState::Idle;
        return false;
    }

    const std::string statusLine = headerSection.substr(0, statusEnd);
    if (statusLine.find("101") == std::string::npos)
    {
        emitLog("OBSWS: HTTP upgrade rejected by OBS.");
        emitError(ObsWsError::HandshakeRejected);
        ensureTransportStopped();
        handshakeState_ = HandshakeState::Idle;
        return false;
    }

    std::string acceptHeader;
    std::string::size_type searchPos = statusEnd + 2;
    const std::string needle = "Sec-WebSocket-Accept:";
    while (searchPos < headerSection.size())
    {
        const std::string::size_type lineEnd = headerSection.find("\r\n", searchPos);
        const std::string line = headerSection.substr(searchPos, lineEnd - searchPos);
        if (line.find(needle) == 0)
        {
            acceptHeader = trim(line.substr(needle.size()));
            break;
        }
        if (lineEnd == std::string::npos)
        {
            break;
        }
        searchPos = lineEnd + 2;
    }

    if (acceptHeader.empty())
    {
        emitLog("OBSWS: Handshake missing Sec-WebSocket-Accept header.");
        emitError(ObsWsError::HandshakeRejected);
        ensureTransportStopped();
        handshakeState_ = HandshakeState::Idle;
        return false;
    }

    char expectedAccept[64] = {0};
    if (!computeAcceptKey(expectedAccept, sizeof(expectedAccept)))
    {
        emitLog("OBSWS: Failed to compute handshake digest.");
        emitError(ObsWsError::HandshakeRejected);
        ensureTransportStopped();
        handshakeState_ = HandshakeState::Idle;
        return false;
    }

    if (acceptHeader != expectedAccept)
    {
        emitLog("OBSWS: Sec-WebSocket-Accept mismatch.");
        emitError(ObsWsError::HandshakeRejected);
        ensureTransportStopped();
        handshakeState_ = HandshakeState::Idle;
        return false;
    }

    if (!remaining.empty())
    {
        rxBuffer_.insert(rxBuffer_.end(), remaining.begin(), remaining.end());
    }

    handshakeBuffer_.clear();
    emitLog("OBSWS: WebSocket upgrade acknowledged.");
    return true;
}

void ObsWsClient::processRxBuffer()
{
    while (rxBuffer_.size() >= 2)
    {
        const uint8_t byte0 = rxBuffer_[0];
        const uint8_t byte1 = rxBuffer_[1];
        const bool fin = (byte0 & 0x80U) != 0;
        const uint8_t opcode = byte0 & 0x0FU;
        const bool masked = (byte1 & 0x80U) != 0;
        uint64_t payloadLen = byte1 & 0x7FU;
        size_t index = 2;

        if (!fin)
        {
            emitLog("OBSWS: Fragmented frames are not supported.");
            emitError(ObsWsError::NotImplemented);
            return;
        }

        if (payloadLen == 126)
        {
            if (rxBuffer_.size() < index + 2)
            {
                return;
            }
            payloadLen = (static_cast<uint64_t>(rxBuffer_[index]) << 8) | rxBuffer_[index + 1];
            index += 2;
        }
        else if (payloadLen == 127)
        {
            if (rxBuffer_.size() < index + 8)
            {
                return;
            }
            payloadLen = 0;
            for (int i = 0; i < 8; ++i)
            {
                payloadLen = (payloadLen << 8) | rxBuffer_[index + i];
            }
            index += 8;
        }

        uint8_t maskKey[4] = {0, 0, 0, 0};
        if (masked)
        {
            if (rxBuffer_.size() < index + 4)
            {
                return;
            }
            for (int i = 0; i < 4; ++i)
            {
                maskKey[i] = rxBuffer_[index + i];
            }
            index += 4;
        }

        if (rxBuffer_.size() < index + payloadLen)
        {
            return;
        }

        uint8_t *payload = rxBuffer_.data() + index;
        if (masked)
        {
            for (uint64_t i = 0; i < payloadLen; ++i)
            {
                payload[i] = static_cast<uint8_t>(payload[i] ^ maskKey[i % 4]);
            }
        }

        handleIncomingFrame(opcode, payload, static_cast<size_t>(payloadLen));
        rxBuffer_.erase(rxBuffer_.begin(), rxBuffer_.begin() + index + static_cast<size_t>(payloadLen));
    }
}

void ObsWsClient::handleIncomingFrame(uint8_t opcode, const uint8_t *payload, size_t length)
{
    switch (opcode)
    {
    case 0x1: // Text
    {
        char *jsonBuffer = static_cast<char *>(std::malloc(length + 1));
        if (jsonBuffer == nullptr)
        {
            emitLog("OBSWS: Failed to allocate buffer for incoming message.");
            return;
        }
        if (payload != nullptr && length > 0)
        {
            std::memcpy(jsonBuffer, payload, length);
        }
        jsonBuffer[length] = '\0';

        cJSON *root = cJSON_ParseWithLength(jsonBuffer, length);
        std::free(jsonBuffer);

        if (root == nullptr)
        {
            emitLog("OBSWS: Failed to parse incoming JSON.");
            return;
        }

        cJSON *opNode = cJSON_GetObjectItemCaseSensitive(root, "op");
        cJSON *dataNode = cJSON_GetObjectItemCaseSensitive(root, "d");
        if (!cJSON_IsNumber(opNode) || dataNode == nullptr)
        {
            cJSON_Delete(root);
            emitLog("OBSWS: Incoming message missing op or data.");
            return;
        }

        const int messageOpcode = opNode->valueint;
        switch (messageOpcode)
        {
        case 0:
            handleHelloMessage(dataNode);
            break;
        case 2:
            handleIdentifiedMessage();
            break;
        case 5:
            handleEventMessage(dataNode);
            break;
        case 7:
            handleRequestResponse(dataNode);
            break;
        default:
            emitLog("OBSWS: Ignoring unsupported opcode.");
            break;
        }

        cJSON_Delete(root);
        break;
    }
    case 0x8: // Close
        emitLog("OBSWS: Close frame received from server.");
        sendControlFrame(0x8, nullptr, 0);
        ensureTransportStopped();
        handshakeState_ = HandshakeState::Idle;
        changeStatus(ObsWsStatus::Disconnected);
        break;
    case 0x9: // Ping
        handlePingFrame(payload, length);
        break;
    case 0xA: // Pong
        break;
    default:
        emitLog("OBSWS: Unsupported frame opcode received.");
        break;
    }
}

void ObsWsClient::handlePingFrame(const uint8_t *payload, size_t length)
{
    if (!sendControlFrame(0xA, payload, length))
    {
        emitLog("OBSWS: Failed to send pong response.");
    }
}

void ObsWsClient::handleHelloMessage(cJSON *dataNode)
{
    if (dataNode == nullptr || handshakeState_ != HandshakeState::AwaitHello)
    {
        return;
    }

    cJSON *rpcNode = cJSON_GetObjectItemCaseSensitive(dataNode, "rpcVersion");
    if (!cJSON_IsNumber(rpcNode))
    {
        emitLog("OBSWS: Hello message missing rpcVersion.");
        emitError(ObsWsError::HandshakeRejected);
        return;
    }

    uint32_t rpcVersion = static_cast<uint32_t>(rpcNode->valuedouble);
    const char *challenge = nullptr;
    const char *salt = nullptr;

    cJSON *authNode = cJSON_GetObjectItemCaseSensitive(dataNode, "authentication");
    if (authNode != nullptr)
    {
        cJSON *challengeNode = cJSON_GetObjectItemCaseSensitive(authNode, "challenge");
        cJSON *saltNode = cJSON_GetObjectItemCaseSensitive(authNode, "salt");
        if (cJSON_IsString(challengeNode) && challengeNode->valuestring != nullptr)
        {
            challenge = challengeNode->valuestring;
        }
        if (cJSON_IsString(saltNode) && saltNode->valuestring != nullptr)
        {
            salt = saltNode->valuestring;
        }
    }

    if (!sendIdentifyMessage(rpcVersion, challenge, salt))
    {
        emitError(ObsWsError::AuthenticationFailed);
        return;
    }

    handshakeState_ = HandshakeState::AwaitIdentifyResponse;
}

void ObsWsClient::handleIdentifiedMessage()
{
    if (handshakeState_ != HandshakeState::AwaitIdentifyResponse)
    {
        return;
    }

    handshakeState_ = HandshakeState::Established;
    changeStatus(ObsWsStatus::Connected);
    emitLog("OBSWS: Handshake complete.");
}

void ObsWsClient::handleEventMessage(cJSON *dataNode)
{
    if (dataNode == nullptr)
    {
        return;
    }

    cJSON *eventTypeNode = cJSON_GetObjectItemCaseSensitive(dataNode, "eventType");
    cJSON *eventDataNode = cJSON_GetObjectItemCaseSensitive(dataNode, "eventData");

    const char *eventType = cJSON_IsString(eventTypeNode) ? eventTypeNode->valuestring : "unknown";
    char *payload = eventDataNode != nullptr ? cJSON_PrintUnformatted(eventDataNode) : duplicateString("");
    if (payload == nullptr)
    {
        payload = duplicateString("");
    }

    enqueueEvent(eventType, payload);
    if (payload != nullptr)
    {
        std::free(payload);
    }
}

void ObsWsClient::handleRequestResponse(cJSON *dataNode)
{
    if (dataNode == nullptr)
    {
        return;
    }

    cJSON *requestIdNode = cJSON_GetObjectItemCaseSensitive(dataNode, "requestId");
    const char *requestId = cJSON_IsString(requestIdNode) ? requestIdNode->valuestring : "unknown-request";

    char *payload = cJSON_PrintUnformatted(dataNode);
    if (payload == nullptr)
    {
        payload = duplicateString("");
    }

    enqueueEvent(requestId, payload);
    if (payload != nullptr)
    {
        std::free(payload);
    }
}

bool ObsWsClient::sendIdentifyMessage(uint32_t rpcVersion, const char *challenge, const char *salt)
{
    cJSON *root = cJSON_CreateObject();
    if (root == nullptr)
    {
        emitLog("OBSWS: Failed to allocate Identify message.");
        return false;
    }

    cJSON_AddNumberToObject(root, "op", 1);
    cJSON *dataNode = cJSON_AddObjectToObject(root, "d");
    if (dataNode == nullptr)
    {
        cJSON_Delete(root);
        emitLog("OBSWS: Failed to create Identify payload.");
        return false;
    }

    cJSON_AddNumberToObject(dataNode, "rpcVersion", static_cast<double>(rpcVersion));
    cJSON_AddNumberToObject(dataNode, "eventSubscriptions", static_cast<double>(config_.eventSubscriptions));

    if (challenge != nullptr && salt != nullptr)
    {
        if (config_.credentials.password == nullptr || config_.credentials.password[0] == '\0')
        {
            emitLog("OBSWS: Server requires authentication but no password was provided.");
            cJSON_Delete(root);
            return false;
        }

        char authBuffer[kAuthResultBufferSize] = {0};
        if (!computeAuthentication(config_.credentials.password, salt, challenge, authBuffer, sizeof(authBuffer)))
        {
            emitLog("OBSWS: Failed to compute authentication signature.");
            cJSON_Delete(root);
            return false;
        }

        cJSON_AddStringToObject(dataNode, "authentication", authBuffer);
    }

    char *jsonOut = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (jsonOut == nullptr)
    {
        emitLog("OBSWS: Failed to serialise Identify message.");
        return false;
    }

    const bool sent = sendText(jsonOut, std::strlen(jsonOut));
    std::free(jsonOut);

    return sent;
}

bool ObsWsClient::enqueueEvent(const char *id, const char *payload)
{
    if (!ensureQueues())
    {
        return false;
    }

    InternalEvent *evt = static_cast<InternalEvent *>(std::malloc(sizeof(InternalEvent)));
    if (evt == nullptr)
    {
        emitLog("OBSWS: Failed to allocate event container.");
        return false;
    }

    evt->id = duplicateString(id != nullptr ? id : "");
    evt->payload = duplicateString(payload != nullptr ? payload : "");

    if (evt->id == nullptr || evt->payload == nullptr)
    {
        emitLog("OBSWS: Failed to duplicate event strings.");
        releaseInternalEvent(evt);
        return false;
    }

    if (xQueueSend(eventQueue_, &evt, 0) != pdTRUE)
    {
        emitLog("OBSWS: Event queue full, dropping message.");
        releaseInternalEvent(evt);
        return false;
    }

    return true;
}

bool ObsWsClient::computeAcceptKey(char *out, size_t outSize)
{
    if (out == nullptr || outSize == 0)
    {
        return false;
    }

    const std::string combined = std::string(secWebsocketKey_) + kWebSocketGuid;

    const mbedtls_md_info_t *mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (mdInfo == nullptr)
    {
        return false;
    }

    unsigned char shaOutput[20] = {0};
    mbedtls_md_context_t mdCtx;
    mbedtls_md_init(&mdCtx);
    if (mbedtls_md_setup(&mdCtx, mdInfo, 0) != 0 || mbedtls_md_starts(&mdCtx) != 0 || mbedtls_md_update(&mdCtx, reinterpret_cast<const unsigned char *>(combined.data()), combined.size()) != 0 || mbedtls_md_finish(&mdCtx, shaOutput) != 0)
    {
        mbedtls_md_free(&mdCtx);
        return false;
    }
    mbedtls_md_free(&mdCtx);

    size_t encodedLen = 0;
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char *>(out), outSize - 1, &encodedLen, shaOutput, sizeof(shaOutput)) != 0)
    {
        return false;
    }

    out[encodedLen] = '\0';
    return true;
}

bool ObsWsClient::computeAuthentication(const char *password, const char *salt, const char *challenge, char *out, size_t outSize)
{
    if (password == nullptr || salt == nullptr || challenge == nullptr || out == nullptr || outSize == 0)
    {
        return false;
    }

    const size_t passwordLen = std::strlen(password);
    const size_t saltLen = std::strlen(salt);
    const size_t challengeLen = std::strlen(challenge);

    if (passwordLen == 0 || saltLen == 0 || challengeLen == 0)
    {
        return false;
    }

    const size_t combinedLen = passwordLen + saltLen;
    unsigned char *combined = static_cast<unsigned char *>(std::malloc(combinedLen));
    if (combined == nullptr)
    {
        return false;
    }
    std::memcpy(combined, password, passwordLen);
    std::memcpy(combined + passwordLen, salt, saltLen);

    const mbedtls_md_info_t *mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (mdInfo == nullptr)
    {
        std::free(combined);
        return false;
    }

    unsigned char shaOutput[32] = {0};
    mbedtls_md_context_t mdCtx;
    mbedtls_md_init(&mdCtx);
    if (mbedtls_md_setup(&mdCtx, mdInfo, 0) != 0 || mbedtls_md_starts(&mdCtx) != 0 || mbedtls_md_update(&mdCtx, combined, combinedLen) != 0 || mbedtls_md_finish(&mdCtx, shaOutput) != 0)
    {
        mbedtls_md_free(&mdCtx);
        std::free(combined);
        return false;
    }
    mbedtls_md_free(&mdCtx);
    std::free(combined);

    unsigned char secretBase64[kAuthSecretBufferSize] = {0};
    size_t secretLen = 0;
    if (mbedtls_base64_encode(secretBase64, sizeof(secretBase64) - 1, &secretLen, shaOutput, sizeof(shaOutput)) != 0)
    {
        return false;
    }
    secretBase64[secretLen] = '\0';

    const size_t authInputLen = secretLen + challengeLen;
    unsigned char *authInput = static_cast<unsigned char *>(std::malloc(authInputLen));
    if (authInput == nullptr)
    {
        return false;
    }

    std::memcpy(authInput, secretBase64, secretLen);
    std::memcpy(authInput + secretLen, challenge, challengeLen);

    unsigned char authSha[32] = {0};
    mbedtls_md_init(&mdCtx);
    if (mbedtls_md_setup(&mdCtx, mdInfo, 0) != 0 || mbedtls_md_starts(&mdCtx) != 0 || mbedtls_md_update(&mdCtx, authInput, authInputLen) != 0 || mbedtls_md_finish(&mdCtx, authSha) != 0)
    {
        mbedtls_md_free(&mdCtx);
        std::free(authInput);
        return false;
    }
    mbedtls_md_free(&mdCtx);
    std::free(authInput);

    size_t authLen = 0;
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char *>(out), outSize - 1, &authLen, authSha, sizeof(authSha)) != 0)
    {
        return false;
    }
    out[authLen] = '\0';
    return true;
}
