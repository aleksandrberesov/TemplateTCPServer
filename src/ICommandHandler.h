#pragma once
#include "Json.h"
#include <string>

namespace tts {

// Exposes the client connection to a command handler.
// Valid only for the duration of ICommandHandler::handle() — do not store.
class IClientContext {
public:
    virtual ~IClientContext() = default;

    // Serialise msg to JSON and send it to the connected client (appends '\n').
    virtual void sendJson(const json::Value& msg) = 0;

    // The "ip:port" string of the connected peer, e.g. "192.168.1.5:49152".
    virtual const std::string& peerAddress() const = 0;
};

// Inherit from this to implement a named incoming command handler.
// Register instances (or lambdas) with CommandDispatcher::registerHandler().
//
// Wire format received from the client:
//   {"type":"command","name":"<yourName>","id":"<opt>","payload":{...}}
//
// Class-based example:
//   class MyHandler : public tts::ICommandHandler {
//   public:
//       void handle(const tts::json::Value& payload,
//                   tts::IClientContext&    ctx) override
//       {
//           // read payload fields, then optionally reply:
//           tts::json::Value::Object resp;
//           resp["type"]   = "response";
//           resp["name"]   = "myCommand";
//           resp["status"] = "ok";
//           ctx.sendJson(tts::json::Value(std::move(resp)));
//       }
//   };
//
// Lambda example:
//   dispatcher.registerHandler("myCommand",
//       [](const tts::json::Value& payload, tts::IClientContext& ctx) {
//           // ...
//       });
class ICommandHandler {
public:
    virtual ~ICommandHandler() = default;

    // Called on the session thread when a matching command arrives.
    // payload — the "payload" field from the JSON message, or a null Value
    //           if the client omitted it.
    // ctx     — send back a response if needed; only valid during this call.
    virtual void handle(const json::Value& payload, IClientContext& ctx) = 0;
};

}  // namespace tts
