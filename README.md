# TemplateTCPServer

A small cross-platform TCP server that speaks the [CardioSimulator JSON
protocol](../CardioSimulator/docs/tcp-protocol.md). It is intended for
end-to-end testing the Android CardioSimulator client and any other
component that consumes the protocol — not for medical use.

Behavior:

- Listens on a TCP port (default `0.0.0.0:9000`).
- Accepts multiple concurrent clients.
- For each client, decodes one JSON object per `\n`-terminated line.
- On `start`, begins streaming synthetic ECG `points` (PQRST shape, 60 bpm)
  back over the same connection.
- On `stop`, halts streaming. Subsequent `start` resumes from offset 0 with
  a fresh series identifier.
- Logs every decoded message and every protocol error.

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
    ├── Server.{h,cpp}              # listen/accept + per-client loop
    ├── Protocol.{h,cpp}            # encode/decode TcpMessage
    ├── EcgGenerator.{h,cpp}        # synthetic PQRST waveform
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
TemplateTCPServer listening on 0.0.0.0:9000 (rate=250, batch=25)
[127.0.0.1:55720] connected
[127.0.0.1:55720] recv: start id=req-1
[127.0.0.1:55720] generator starting @ 250 Hz, batch=25
[127.0.0.1:55720] recv: stop id=req-2
[127.0.0.1:55720] disconnected
```

### Options

| Flag        | Default     | Description                                         |
|-------------|-------------|-----------------------------------------------------|
| `--host`    | `0.0.0.0`   | Bind address (IPv4)                                 |
| `--port`    | `9000`      | Listen port                                         |
| `--rate`    | `250`       | Default sample rate (Hz) when client omits it       |
| `--batch`   | `25`        | Samples per `points` frame                          |
| `--quiet`   | off         | Suppress per-message logging                        |
| `-h`, `--help` |          | Show usage                                          |

## Smoke test

With the server running, in another shell:

```bash
# Linux/macOS (uses ncat/nc)
printf '{"type":"start","sampleRate":100}\n' | nc 127.0.0.1 9000

# Windows PowerShell
"`{`"type`":`"start`",`"sampleRate`":100}" | ncat 127.0.0.1 9000
```

You should see a steady stream of `points` frames flowing back, each one
JSON on its own line. Send `{"type":"stop"}` to halt.

## Protocol

Defined in `../CardioSimulator/docs/tcp-protocol.md`. This server
implements:

- decoder: `start`, `stop`, `points`; rejects unknown `type`, malformed
  JSON, missing `values`, unknown lead tokens.
- encoder: omits optional fields with no value; omits `params` when empty;
  omits `offset` when zero — matches the round-trip rules in the spec.

Lead tokens are accepted case-insensitively; the encoder emits canonical
forms (`I`, `II`, `III`, `aVR`, `aVL`, `aVF`, `V1`–`V6`).

## License

Internal test tool; no license declared. Add one before distribution.
