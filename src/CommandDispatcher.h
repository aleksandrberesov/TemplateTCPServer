#pragma once
#include "ICommandHandler.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace tts {

// Maps incoming command names to their handlers.
// Create one instance in main(), register handlers, then pass it to the Server.
// All methods are thread-safe for concurrent dispatch after registration is done.
class CommandDispatcher {
public:
    // Register a subclass of ICommandHandler.
    // The dispatcher takes shared ownership; registration must happen before
    // the server starts accepting connections.
    void registerHandler(const std::string&               name,
                         std::shared_ptr<ICommandHandler> handler);

    // Convenience overload — wraps any callable in an ICommandHandler.
    void registerHandler(
        const std::string& name,
        std::function<void(const json::Value& payload, IClientContext& ctx)> fn);

    // Looks up handler by name and calls handle().
    // Returns true if a handler was found, false if name is unknown.
    bool dispatch(const std::string& name,
                  const json::Value& payload,
                  IClientContext&    ctx) const;

private:
    std::unordered_map<std::string, std::shared_ptr<ICommandHandler>> handlers_;
};

}  // namespace tts
