# OBSWS-ESP32

An Arduino-friendly library that lets ESP32-class devices orchestrate OBS Studio through the official OBS WebSocket protocol. The goal is to expose as much of the OBS control surface as practical from embedded hardware while keeping the API approachable for makers and product teams.

## Status

> ⚠️ Pre-alpha. Specifications and APIs are subject to change while the foundation is built.

## Key Features (planned)

- ✅ Native OBS WebSocket 5.x authentication handshake over Wi-Fi
- ✅ Lightweight client core optimised for FreeRTOS on ESP32
- ✅ Event subscription and dispatch system with flexible callback routing
- ✅ Scene, source, and transition management helpers
- ✅ Streaming, recording, and replay buffer controls
- ✅ Configurable reconnection and heartbeat keep-alive routines
- ✅ Minimal-footprint JSON handling to fit microcontroller constraints
- ✅ Optional command batching and preset macros for latency-sensitive use cases

## Architecture Overview

1. **Transport Layer**: TCP/WebSocket stack built on `WiFiClientSecure` (or alternative network adapters) with TLS optionality.
2. **Protocol Layer**: Implements OBS WebSocket 5.x framing, message signing, and request/response tracking.
3. **Event Layer**: Routes event payloads to user-supplied handlers with filtering utilities.
4. **Facade API**: High-level Arduino-style methods for common OBS operations, plus low-level passthrough hooks for advanced users.

## Hardware & Software Requirements

- ESP32 (single- or dual-core variants); 520 KB RAM minimum recommended.
- Arduino framework (ESP-IDF support is currently out of scope).
- OBS Studio 29+ with OBS WebSocket 5.x enabled.
- Stable Wi-Fi network reachable by both OBS host and ESP32.

## Getting Started (planned flow)

1. **Install dependencies**: Arduino IDE or PlatformIO with ESP32 board package.
2. **Add the library**: Install via Arduino Library Manager (official distribution). For development builds, use a Git submodule or Zip import from this repo.
3. **Configure OBS**: Enable WebSocket server, set authentication, and note host/port/secret.
4. **Write firmware**:
   ```cpp
   #include <ObsWsEsp32.h>

   ObsWsClient client;

   void setup() {
     // ...existing setup code...
     client.begin({
       .host = "192.168.0.50",
       .port = 4455,
       .password = "supersecret",
       .onEvent = handleObsEvent
     });
   }

   void loop() {
     client.poll();
   }

   void handleObsEvent(const ObsEvent& event) {
     // ...existing handler code...
   }
   ```
5. **Upload & monitor**: Flash firmware, open serial monitor, verify handshake and commands.

> All inline comments in library examples will remain in English for consistency.

## Roadmap Highlights

- [ ] Finalise core connection manager and reconnection strategy.
- [ ] Implement request/response wrappers for common OBS endpoints (scenes, sources, transitions, outputs).
- [ ] Provide macro recording/playback utilities for live production workflows.
- [ ] Deliver sample sketches (stream deck, tally lights, scene rotator).
- [ ] Add PlatformIO example project and CI builds.
- [ ] Create integration tests against OBS WebSocket reference server.

## Contributing

- Use English for code comments and commit messages.
- Prefer modern C++17 features that compile with the ESP32 toolchain.
- Run formatting and static checks (TBD) before submitting pull requests.
- File issues describing target OBS functionality and hardware setup for reproducibility.

See `AGENTS.md` for automation guidelines and collaboration workflows.

## Governance & Communication

- **Primary channel**: GitHub Issues & Discussions.
- **Release cadence**: Targeted milestones aligned with major OBS Studio releases.
- **Maintainer team**: TBD; see `AGENTS.md` for roles and responsibilities once defined.

## License

Released under the MIT License (see `LICENSE`). Contributions are accepted under the same terms.

## Acknowledgements

- OBS Project and the OBS WebSocket maintainers.
- ESP32 community contributors and Arduino core maintainers.
