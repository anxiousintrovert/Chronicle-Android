#pragma once
#include "manager.hpp"

namespace mods {
// Active snapshot is immutable until process restart. Preview always reflects the saved profile.
class Session {
public:
    void Start(const std::vector<std::filesystem::path> &roots, const FileIndex &base, const std::filesystem::path &save);
    bool SetEnabled(const std::string &id, bool enabled);
    void Refresh();

    const Manager &Active() const { return active_; }

    const Manager &Preview() const { return preview_; }

    const std::string &Error() const { return error_; }

    bool Pending() const { return enabled_ != started_; }

    bool StartedFor(const std::filesystem::path &save) const { return started_once_ && save_ == save; }

private:
    Manager                            active_, preview_;
    std::vector<std::filesystem::path> roots_;
    FileIndex                          base_;
    std::filesystem::path              save_;
    std::map<std::string, bool>        enabled_, started_;
    std::string                        error_;
    bool                               started_once_ = false, profile_valid_ = true;
};

Session &Shared();
} // namespace mods
