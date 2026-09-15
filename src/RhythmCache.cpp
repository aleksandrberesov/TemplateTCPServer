#include "RhythmCache.h"

namespace tts {

bool RhythmCache::contains(const std::string& pathology, const std::string& hash) const {
    if (pathology.empty()) return false;
    std::lock_guard<std::mutex> g(mu_);
    auto it = byPathology_.find(pathology);
    if (it == byPathology_.end()) return false;
    return it->second.hash == hash;
}

void RhythmCache::store(const std::string& pathology, Rhythm rhythm) {
    if (pathology.empty()) return;
    std::lock_guard<std::mutex> g(mu_);
    byPathology_[pathology] = std::move(rhythm);
}

std::optional<RhythmCache::Rhythm> RhythmCache::get(const std::string& pathology) const {
    std::lock_guard<std::mutex> g(mu_);
    auto it = byPathology_.find(pathology);
    if (it == byPathology_.end()) return std::nullopt;
    return it->second;
}

std::size_t RhythmCache::size() const {
    std::lock_guard<std::mutex> g(mu_);
    return byPathology_.size();
}

}  // namespace tts
