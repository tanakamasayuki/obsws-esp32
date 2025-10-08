#include "ObsWsEsp32.h"

void ObsWsClient::begin(const Config &config)
{
    config_ = config;
    // TODO: Implement OBS WebSocket handshake and authentication.
}

void ObsWsClient::poll()
{
    // TODO: Implement request/response processing and event dispatch.
    if (config_.onEvent != nullptr)
    {
        static ObsEvent placeholder{"noop", ""};
        config_.onEvent(placeholder);
    }
}
