#include <WiFi.h>
#include <ObsWsEsp32.h>

#ifndef WIFI_SSID
#define WIFI_SSID "defaultSSID"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "defaultPassword"
#endif

#ifndef OBS_WS_HOST
#define OBS_WS_HOST "192.168.0.2"
#endif

#ifndef OBS_WS_PORT
#define OBS_WS_PORT 4455
#endif

#ifndef OBS_WS_PASSWORD
#define OBS_WS_PASSWORD "YOUR_PASSWORD"
#endif

namespace
{
    constexpr uint32_t kWifiConnectTimeoutMs = 20000;
    constexpr uint32_t kWifiRetryDelayMs = 5000;
    unsigned long g_lastWifiAttemptMs = 0;
    constexpr uint32_t kObsRetryDelayMs = 5000;
    unsigned long g_lastObsAttemptMs = 0;
}

ObsWsClient client;
ObsWsClient::Config clientConfig;

const char *statusToString(ObsWsStatus status)
{
    switch (status)
    {
    case ObsWsStatus::Disconnected:
        return "Disconnected";
    case ObsWsStatus::Connecting:
        return "Connecting";
    case ObsWsStatus::Authenticating:
        return "Authenticating";
    case ObsWsStatus::Connected:
        return "Connected";
    case ObsWsStatus::Error:
        return "Error";
    default:
        return "Unknown";
    }
}

const char *errorToString(ObsWsError error)
{
    switch (error)
    {
    case ObsWsError::None:
        return "None";
    case ObsWsError::InvalidConfig:
        return "InvalidConfig";
    case ObsWsError::TransportUnavailable:
        return "TransportUnavailable";
    case ObsWsError::HandshakeRejected:
        return "HandshakeRejected";
    case ObsWsError::AuthenticationFailed:
        return "AuthenticationFailed";
    case ObsWsError::NotImplemented:
        return "NotImplemented";
    default:
        return "Unknown";
    }
}

void handleObsEvent(const ObsEvent &event)
{
    Serial.printf("[OBS] event=%s payload=%s\n", event.id, event.payload);
}

void handleObsStatus(ObsWsStatus status)
{
    Serial.printf("[OBS] status=%s\n", statusToString(status));
}

void handleObsError(ObsWsError error)
{
    Serial.printf("[OBS] error=%s\n", errorToString(error));
}

void handleObsLog(const char *message)
{
    Serial.printf("[OBS] %s\n", message);
}

bool ensureWifiConnected()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        return true;
    }

    const unsigned long now = millis();
    if (now - g_lastWifiAttemptMs < kWifiRetryDelayMs)
    {
        return false;
    }

    g_lastWifiAttemptMs = now;

    Serial.printf("[WiFi] Connecting to %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiConnectTimeoutMs)
    {
        delay(250);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.printf("[WiFi] Connected. IP=%s\n", WiFi.localIP().toString().c_str());
        return true;
    }

    Serial.println("[WiFi] Connection timed out.");
    WiFi.disconnect(true);
    return false;
}

void setup()
{
    Serial.begin(115200);
    while (!Serial)
    {
        delay(10);
    }

    Serial.println("[OBSWS-ESP32] Demo starting...");

    WiFi.setAutoReconnect(true);

    if (!ensureWifiConnected())
    {
        Serial.println("[OBSWS-ESP32] WiFi not ready yet; OBS connection will be retried.");
    }

    clientConfig.host = OBS_WS_HOST;
    clientConfig.port = static_cast<uint16_t>(OBS_WS_PORT);
    clientConfig.credentials.password = OBS_WS_PASSWORD;
    clientConfig.onEvent = handleObsEvent;
    clientConfig.onStatus = handleObsStatus;
    clientConfig.onError = handleObsError;
    clientConfig.onLog = handleObsLog;

    if (!client.begin(clientConfig))
    {
        Serial.println("[OBSWS-ESP32] Failed to start OBS client.");
    }
    else
    {
        g_lastObsAttemptMs = millis();
    }
}

void loop()
{
    const bool wifiReady = ensureWifiConnected();

    if (wifiReady)
    {
        const ObsWsStatus status = client.status();
        if (status == ObsWsStatus::Disconnected || status == ObsWsStatus::Error)
        {
            const unsigned long now = millis();
            if (g_lastObsAttemptMs == 0 || now - g_lastObsAttemptMs >= kObsRetryDelayMs)
            {
                g_lastObsAttemptMs = now;
                Serial.println("[OBSWS-ESP32] Attempting OBS reconnection...");
                client.begin(clientConfig);
            }
        }
    }

    if (!wifiReady)
    {
        client.close();
        g_lastObsAttemptMs = 0;
    }

    client.poll();
    delay(10);
}
