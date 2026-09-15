# TemplateTCPServer

A small cross-platform TCP server that speaks the [CardioSimulator JSON
protocol](../CardioSimulator/Win/docs/tcp-protocol.md). It is intended for
end-to-end testing the CardioSimulator client and any other component that
consumes the protocol — not for medical use.

The app is the client: it connects out, uploads the rhythm catalog
(`manifest.txt`), and then — **on each rhythm the user selects** — asks whether
the server already holds that rhythm and, only if it doesn't, sends the whole
rhythm's raw samples in **one message**. This server implements the
receiving/monitor side of that contract.

Behavior:

- Listens on a TCP port (default `0.0.0.0:9000`); accepts multiple concurrent
  clients. Each connection decodes one JSON object per `\n`-terminated line.
  A `rhythm` line can be large (every lead in one message), so long lines are
  read without a small fixed buffer.
- On `upload` (`manifest.txt`), reads exactly `size` raw bytes into the upload
  directory and replies with an `ack` (never `OK`/`no_data`).
- On `query` — the **cache probe** — replies with exactly one line: `no_data`
  when it doesn't hold that `(pathology, hash)` pair (so the app sends the
  data), or `OK` when it already does (so the app sends nothing). The request
  `id` is echoed as `{"id":…,"status":…}` when present.
- On `rhythm` (sent only after a `no_data`), stores every lead's **raw ADC
  integer** samples in the shared cache, keyed by the queried `(pathology,
  hash)`, so an unchanged rhythm is never re-requested — even across reconnects.
- On `start` — the **play** command (start button) — logs and moves on; it
  carries no data and gets no reply. On `stop`, logs (advisory).
- `points` (the old streamed per-lead frames) is **deprecated** and ignored.
- Logs every decoded message and every protocol error.

The cache is keyed by `(pathology, hash)`: same id + same hash → `OK`; same id +
new hash (an **edited** rhythm) → `no_data`, so the app resends it.

## Layout

```
TemplateTCPServer/
├── CMakeLists.txt
├── README.md
├── cmake/
│   └── toolchain-mingw64.cmake     # optional: Linux -> Windows cross
├── scripts/
│   ├── build-linux.sh
│   ├── build-windows.bat
│   └── build-windows-mingw-from-linux.sh
└── src/
    ├── main.cpp
    ├── Server.{h,cpp}              # listen/accept + per-client loop + handshake
    ├── Protocol.{h,cpp}            # encode/decode protocol messages
    ├── RhythmCache.{h,cpp}         # shared (pathology, hash) -> samples store
    ├── CommandDispatcher.{h,cpp}   # optional per-message observation hook
    ├── ICommandHandler.h           # handler / client-context interfaces
    └── Json.{h,cpp}                # minimal hand-rolled JSON
```

## Requirements

- A C++17 compiler. Verified shapes:
  - **Linux**: `g++ >= 7` or `clang++ >= 6`, plus `make` or `ninja`
  - **Windows**: MSVC 2017+ (Build Tools or Visual Studio), or MinGW-w64
- `cmake >= 3.16`
- No third-party libraries. Sockets use the OS API directly (BSD on
  Linux, Winsock2 on Windows). JSON is hand-rolled and bundled in `src/`.

## Build

### Linux (native)

```bash
./scripts/build-linux.sh
# binary: build/linux/TemplateTCPServer
```

Equivalent manual invocation:

```bash
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux -j
```

### Windows (native, MSVC)

From a *Developer Command Prompt for VS* (so `cl.exe` is on PATH):

```bat
scripts\build-windows.bat
:: binary: build\windows\Release\TemplateTCPServer.exe
```

### Windows (native, MinGW-w64)

From a regular shell with MinGW-w64's `g++` on PATH (e.g. installed via
MSYS2 `pacman -S mingw-w64-x86_64-toolchain`):

```bat
cmake -S . -B build\windows -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build\windows
:: binary: build\windows\TemplateTCPServer.exe
```

### Linux -> Windows cross-compile

If you have `mingw-w64` installed on Linux (`apt install mingw-w64` /
`dnf install mingw64-gcc-c++`):

```bash
./scripts/build-windows-mingw-from-linux.sh
# binary: build/windows-mingw/TemplateTCPServer.exe (static, no DLL deps)
```

## Run

```bash
./TemplateTCPServer --port 9000
```

```
TemplateTCPServer listening on 0.0.0.0:9000
[127.0.0.1:55720] connected
[127.0.0.1:55720] recv: upload id=u1
[127.0.0.1:55720] upload complete: manifest.txt (8123 bytes)
[127.0.0.1:55720] query pathology='afib' hash='9f3c1a20b7e4d5c8' -> no_data (need samples)
[127.0.0.1:55720] rhythm 'afib' hash='9f3c1a20b7e4d5c8' (12 leads, 6000 samples @ 500 Hz)
[127.0.0.1:55720] start (play) pathology='afib' [cached]
[127.0.0.1:55720] disconnected
```

### Options

| Flag               | Default   | Description                                     |
|--------------------|-----------|-------------------------------------------------|
| `--host`           | `0.0.0.0` | Bind address (IPv4)                             |
| `--port`           | `9000`    | Listen port                                     |
| `--upload-dir`     | `uploads` | Directory to save uploads (e.g. `manifest.txt`) |
| `--max-upload-mb`  | `100`     | Reject uploads larger than N MB                 |
| `--max-line-mb`    | `64`      | Reject a single JSON line larger than N MB      |
| `--quiet`          | off       | Suppress per-message logging                    |
| `-h`, `--help`     |           | Show usage                                      |

## Smoke test

The handshake (manifest → `query`/`no_data` → `rhythm` → `OK` on re-select →
`no_data` after an edited hash) with the server running on port 9000:

```bash
printf '{"type":"query","id":"1","pathology":"x","hash":"h"}\n' | nc 127.0.0.1 9000
# -> {"id":"1","status":"no_data"}
```

Because it isn't cached, the server asks for the data. If you then send the
matching `{"type":"rhythm","pathology":"x","leads":{…}}` on the same connection,
a repeat of the same `query` answers `{"id":"1","status":"ok"}`.

## Extending it

`main.cpp` registers optional observation handlers on the `CommandDispatcher`,
keyed by message type (`query`, `rhythm`, `start`, `stop`). The server invokes
them **after** it has already sent any mandatory handshake reply, so they are
for logging/metrics/side effects only — a handler must **not** send its own
`OK`/`no_data`, or it would desync the app's FIFO reply matching. Drop the
dispatcher wiring entirely if you don't need it.

## Protocol

Defined in [`../CardioSimulator/Win/docs/tcp-protocol.md`](../CardioSimulator/Win/docs/tcp-protocol.md).
This server implements:

- transport: line-delimited JSON over one long-lived TCP connection (large
  `rhythm` lines included), plus the raw-binary `upload` payload framing (read
  exactly `size` bytes, then resume line parsing — bytes pipelined after the
  payload, such as the `query` the app sends right after the manifest, are kept).
- decoder: `query`, `rhythm`, `start`, `stop`, `upload`, `ack` (and the
  deprecated `points`); rejects unknown `type`, malformed JSON, a missing
  `pathology`/`leads`, and unknown lead tokens.
- handshake: one `OK`/`no_data` reply per `query` in socket order, newline
  terminated, echoing the request `id` when present; the `manifest.txt` upload
  gets an `ack`, never a verdict.
- cache: keyed by `(pathology, hash)`, shared across all sessions and surviving
  reconnects. The `rhythm` message carries no hash, so the server keys each
  entry by the `(pathology, hash)` from the `query` that requested it.

Lead tokens are accepted case-insensitively; canonical forms are
`I`, `II`, `III`, `aVR`, `aVL`, `aVF`, `V1`–`V6`.

## License

Internal test tool; no license declared. Add one before distribution.
