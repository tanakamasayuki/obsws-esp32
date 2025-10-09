#include "ObsWsEsp32.h"

namespace
{
    constexpr unsigned long kReconnectBackoffMs = 250U;
}

bool ObsWsClient::begin(const Config &config)
{
    close();

    config_ = config;
    placeholderEventDispatched_ = false;
    lastError_ = ObsWsError::None;

    if (config_.host == nullptr || config_.host[0] == '\0')
    {
        emitLog("OBSWS: Invalid configuration (host not set).");
        emitError(ObsWsError::InvalidConfig);
        return false;
    }

    changeStatus(ObsWsStatus::Connecting);

    if (!connectTransport())
    {
        emitError(ObsWsError::TransportUnavailable);
        return false;
    }

    changeStatus(ObsWsStatus::Authenticating);

    if (!performHandshake())
    {
        emitError(ObsWsError::HandshakeRejected);
        return false;
    }

    if (!authenticate())
    {
        emitError(ObsWsError::AuthenticationFailed);
        return false;
    }

    placeholderEventDispatched_ = false;
    changeStatus(ObsWsStatus::Connected);
    emitLog("OBSWS: Connected (placeholder transport).");
    lastReconnectAttemptMs_ = millis();
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
                lastReconnectAttemptMs_ = now + kReconnectBackoffMs;
                emitLog("OBSWS: Auto-reconnect attempt (placeholder).");
                begin(config_);
            }
        }
        return;
    }

    // TODO: Implement request/response processing and event dispatch.
    if (status_ == ObsWsStatus::Connected && config_.onEvent != nullptr && !placeholderEventDispatched_)
    {
        static ObsEvent placeholder{"noop", ""};
        config_.onEvent(placeholder);
        placeholderEventDispatched_ = true;
    }
}

void ObsWsClient::close()
{
    placeholderEventDispatched_ = false;

    if (status_ == ObsWsStatus::Disconnected)
    {
        return;
    }

    changeStatus(ObsWsStatus::Disconnected);
    lastError_ = ObsWsError::None;
    emitLog("OBSWS: Connection closed.");
}

bool ObsWsClient::sendRequest(const char *requestType, const char *payload)
{
    (void)requestType;
    (void)payload;
    emitLog("OBSWS: sendRequest stub invoked (not implemented).");
    lastError_ = ObsWsError::NotImplemented;
    return false;
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
    emitLog("OBSWS: connectTransport() not yet implemented.");
    return true;
}

bool ObsWsClient::performHandshake()
{
    emitLog("OBSWS: performHandshake() not yet implemented.");
    return true;
}

bool ObsWsClient::authenticate()
{
    if (config_.credentials.password == nullptr || config_.credentials.password[0] == '\0')
    {
        emitLog("OBSWS: No password provided, assuming unauthenticated session.");
        return true;
    }

    emitLog("OBSWS: authenticate() not yet implemented.");
    return true;
}
