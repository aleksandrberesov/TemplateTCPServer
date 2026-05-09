#include "CommandDispatcher.h"

namespace tts {

namespace {

class LambdaHandler : public ICommandHandler {
public:
    using Fn = std::function<void(const json::Value&, IClientContext&)>;
    explicit LambdaHandler(Fn fn) : fn_(std::move(fn)) {}
    void handle(const json::Value& payload, IClientContext& ctx) override {
        fn_(payload, ctx);
    }
private:
    Fn fn_;
};

}  // namespace

void CommandDispatcher::registerHandler(const std::string&               name,
                                        std::shared_ptr<ICommandHandler> handler) {
    handlers_[name] = std::move(handler);
}

void CommandDispatcher::registerHandler(
    const std::string& name,
    std::function<void(const json::Value& payload, IClientContext& ctx)> fn) {
    handlers_[name] = std::make_shared<LambdaHandler>(std::move(fn));
}

bool CommandDispatcher::dispatch(const std::string& name,
                                 const json::Value& payload,
                                 IClientContext&    ctx) const {
    auto it = handlers_.find(name);
    if (it == handlers_.end()) return false;
    it->second->handle(payload, ctx);
    return true;
}

}  // namespace tts
