#include <ObsWsEsp32.h>

ObsWsClient client;

void handleObsEvent(const ObsEvent &event)
{
    Serial.printf("[OBS] event=%s payload=%s\n", event.id, event.payload);
}

void setup()
{
    Serial.begin(115200);
    while (!Serial)
    {
        delay(10);
    }

    ObsWsClient::Config config;
    config.host = "192.168.0.2";  // TODO: update with your OBS host
    config.port = 4455;           // Default OBS WebSocket 5.x port
    config.password = "changeme"; // TODO: use OBS WebSocket password
    config.onEvent = handleObsEvent;

    client.begin(config);
}

void loop()
{
    client.poll();
    delay(10);
}
