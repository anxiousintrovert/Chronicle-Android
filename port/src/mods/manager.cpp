#include "manager.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <functional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>

#include "api.hpp"
#include "native.hpp"

namespace mods {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;

std::string Text(const fs::path &path) {
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}

fs::path Path(const std::string &text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

bool ValidId(std::string_view id) {
    return !id.empty() && id.size() <= 96 && id.front() >= 'a' && id.front() <= 'z' &&
           std::ranges::all_of(id, [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
           });
}

std::string String(const Json &object, const char *key) {
    const auto &value = object.at(key);
    if (!value.is_string()) {
        throw std::runtime_error(std::string(key) + " must be a string");
    }
    auto result = value.get<std::string>();
    if (result.empty() || result.size() > 256 ||
        std::ranges::any_of(result, [](unsigned char c) { return c < 32; })) {
        throw std::runtime_error(std::string(key) + " must be 1-256 characters without control characters");
    }
    return result;
}

std::vector<std::string> Ids(const Json &object, const char *key, const std::string &self) {
    if (!object.contains(key)) {
        return {};
    }
    const auto &array = object.at(key);
    if (!array.is_array() || array.size() > 128) {
        throw std::runtime_error(std::string(key) + " must be an array of at most 128 mod IDs");
    }
    std::set<std::string> values;
    for (const auto &value : array) {
        if (!value.is_string()) {
            throw std::runtime_error(std::string(key) + " must contain mod IDs");
        }
        auto id = value.get<std::string>();
        if (!ValidId(id) || id == self || !values.insert(id).second) {
            throw std::runtime_error(std::string(key) + " contains an invalid, self, or duplicate ID");
        }
    }
    return {values.begin(), values.end()};
}

// The game folds case and path separators. Reject ambiguous paths on every OS.
std::string Relative(std::string value) {
    if (value.empty() || value.size() > 240) {
        throw std::runtime_error("file paths must be 1-240 characters");
    }
    for (char &c : value) {
        if (c == '\\') {
            c = '/';
        }
        if (static_cast<unsigned char>(c) < 32 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            throw std::runtime_error("unsafe file path: " + value);
        }
    }
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const auto end = value.find('/', begin);
        const auto part = value.substr(begin, end == std::string::npos ? end : end - begin);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') {
            throw std::runtime_error("unsafe file path: " + value);
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return value;
}

std::string Fold(std::string value) {
    for (char &c : value) {
        if (c >= 'A' && c <= 'Z') {
            c += 'a' - 'A';
        }
    }
    return value;
}

fs::path Contained(const fs::path &root, const fs::path &file) {
    const auto resolved = fs::canonical(file);
    const auto relative = resolved.lexically_relative(root);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == "..") {
        throw std::runtime_error("file escapes package: " + Text(file));
    }
    if (!fs::is_regular_file(resolved)) {
        throw std::runtime_error("not a regular file: " + Text(file));
    }
    return resolved;
}

void Read(Package &package, const FileIndex &base) {
    const auto manifest = Contained(package.root, package.root / "mod.json");
    if (fs::file_size(manifest) > 256 * 1024) {
        throw std::runtime_error("mod.json exceeds 256 KiB");
    }
    std::ifstream stream(manifest, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot read mod.json");
    }
    std::vector<std::set<std::string>> keys;
    auto                               unique_keys = [&keys](int, Json::parse_event_t event, Json &value) {
        if (event == Json::parse_event_t::object_start) {
            keys.emplace_back();
        }
        if (event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second) {
            throw std::runtime_error("duplicate JSON key: " + value.get<std::string>());
        }
        if (event == Json::parse_event_t::object_end) {
            keys.pop_back();
        }
        return true;
    };
    Json json = Json::parse(stream, unique_keys);
    if (!json.is_object()) {
        throw std::runtime_error("mod.json must be an object");
    }
    const std::set<std::string> fields{"schema_version", "id", "name", "version", "game", "enabled", "requires", "conflicts", "files", "script", "api_version", "permissions", "exclusive", "quests", "requires_capabilities", "requires_operations", "dependency_versions", "resources", "native_library"};
    for (const auto &[key, value] : json.items()) {
        if (!fields.contains(key)) {
            throw std::runtime_error("unknown manifest field: " + key);
        }
    }
    if (!json.at("schema_version").is_number_integer() || json.at("schema_version") != 1) {
        throw std::runtime_error("unsupported schema_version; expected 1");
    }
    package.id = String(json, "id");
    if (!ValidId(package.id)) {
        throw std::runtime_error("invalid mod ID");
    }
    package.name = String(json, "name");
    package.version = String(json, "version");
    if (String(json, "game") != "chronicle") {
        throw std::runtime_error("package does not target Chronicle");
    }
    if (json.contains("enabled")) {
        if (!json["enabled"].is_boolean()) {
            throw std::runtime_error("enabled must be a boolean");
        }
        // Legacy field is validated, but only the user profile can enable a mod.
    }
    package.dependencies = Ids(json, "requires", package.id);
    if (json.contains("dependency_versions")) {
        const auto &versions = json["dependency_versions"];
        if (!versions.is_object() || versions.size() > 128) {
            throw std::runtime_error("dependency_versions must be an object of at most 128 constraints");
        }
        for (const auto &[id, bounds] : versions.items()) {
            if (std::ranges::find(package.dependencies, id) == package.dependencies.end()) {
                throw std::runtime_error("version constraint requires declared dependency: " + id);
            }
            if (!bounds.is_object()) {
                throw std::runtime_error("dependency version constraint must be an object");
            }
            VersionRequirement requirement;
            for (const auto &[key, value] : bounds.items()) {
                if (key == "exact") {
                    requirement.exact = String(bounds, key.c_str());
                } else if (key == "minimum") {
                    requirement.minimum = String(bounds, key.c_str());
                } else if (key == "before") {
                    requirement.before = String(bounds, key.c_str());
                } else {
                    throw std::runtime_error("unknown dependency version bound: " + key);
                }
            }
            auto error = requirement.Validate();
            if (!error.empty()) {
                throw std::runtime_error(id + ": " + error);
            }
            package.dependency_versions.emplace(id, std::move(requirement));
        }
    }
    package.conflicts = Ids(json, "conflicts", package.id);
    package.exclusive = Ids(json, "exclusive", "");
    package.permissions = Ids(json, "permissions", "");
    for (const auto &capability : Ids(json, "requires_capabilities", "")) {
        if (!Supports(capability)) {
            throw std::runtime_error("missing host capability: " + capability);
        }
    }
    if (json.contains("api_version") && (!json["api_version"].is_number_integer() || json["api_version"] != 1)) {
        throw std::runtime_error("unsupported api_version");
    }
    for (const auto &permission : package.permissions) {
        if (permission != "economy.write" && permission != "game.read" && permission != "game.write" && permission != "resources.read" && permission != "native.extension") {
            throw std::runtime_error("unsupported permission: " + permission);
        }
    }
    for (const auto &name : Ids(json, "requires_operations", "")) {
        const auto *operation = FindOperation(name);
        if (!operation) {
            throw std::runtime_error("missing host operation: " + name);
        }
        if (std::ranges::find(package.permissions, operation->permission) == package.permissions.end()) {
            throw std::runtime_error("required operation " + name + " needs permission: " + std::string(operation->permission));
        }
    }
    if (json.contains("native_library")) {
        if (!json.contains("api_version") || json["api_version"] != 1 ||
            std::ranges::find(package.permissions, "native.extension") == package.permissions.end()) {
            throw std::runtime_error("native_library requires api_version 1 and native.extension permission");
        }
        package.native_library = Contained(package.root, package.root / Path(Relative(String(json, "native_library"))));
        const auto extension = package.native_library.extension().string();
        if (extension != ".dll" && extension != ".so" && extension != ".dylib") {
            throw std::runtime_error("native_library must be .dll, .so or .dylib");
        }
        package.native_fingerprint = NativeFingerprint(package);
    }
    if (json.contains("script")) {
        if (!json.contains("api_version") || json["api_version"] != 1) {
            throw std::runtime_error("scripts require api_version 1");
        }
        package.script = Contained(package.root, package.root / Path(Relative(String(json, "script"))));
        if (package.script.extension() != ".lua" || fs::file_size(package.script) > 1024 * 1024) {
            throw std::runtime_error("script must be a .lua file of at most 1 MiB");
        }
    }
    if (json.contains("quests")) {
        if (!json["quests"].is_array() || json["quests"].size() > 64) {
            throw std::runtime_error("quests must be an array of at most 64 definitions");
        }
        std::set<std::string> ids;
        for (const auto &q : json["quests"]) {
            Quest quest;
            quest.id = String(q, "id");
            quest.title = String(q, "title");
            quest.description = String(q, "description");
            if (!ValidId(quest.id) || !ids.insert(quest.id).second) {
                throw std::runtime_error("invalid or duplicate quest ID");
            }
            if (!q.at("goal").is_number_integer() || q["goal"] < 1 || q["goal"] > 1000000) {
                throw std::runtime_error("quest goal must be 1-1000000");
            }
            quest.goal = q["goal"].get<int>();
            if (q.contains("map")) {
                if (!q["map"].is_number_integer() || q["map"] < 0 || q["map"] > 999) {
                    throw std::runtime_error("quest map must be 0-999");
                }
                quest.map = q["map"].get<int>();
            }
            for (const auto &[key, value] : q.items()) {
                if (key != "id" && key != "title" && key != "description" && key != "goal" && key != "map") {
                    throw std::runtime_error("unknown quest field: " + key);
                }
            }
            package.quests.push_back(std::move(quest));
        }
    }
    if (json.contains("resources")) {
        const auto &resources = json["resources"];
        if (!resources.is_object() || resources.size() > 16) {
            throw std::runtime_error("resources must contain at most 16 named files");
        }
        for (const auto &[id, source] : resources.items()) {
            if (!ValidId(id) || !source.is_string()) {
                throw std::runtime_error("resources require valid IDs and relative paths");
            }
            auto path = Contained(package.root, package.root / Path(Relative(source.get<std::string>())));
            if (fs::file_size(path) > 65536) {
                throw std::runtime_error("resource exceeds 64 KiB: " + id);
            }
            package.resources.emplace(id, std::move(path));
        }
    }
    const auto &files = json.at("files");
    if (!files.is_object() || files.size() > 4096) {
        throw std::runtime_error("files must be an object with at most 4096 mappings");
    }
    for (const auto &[target, source] : files.items()) {
        if (!source.is_string()) {
            throw std::runtime_error("replacement file must be a relative path");
        }
        const auto key = Fold(Relative(target));
        if (std::ranges::any_of(key, [](unsigned char c) { return c >= 127; })) {
            throw std::runtime_error("game target paths must use ASCII characters");
        }
        if (key == "data.hd2" || key == "languages.json" || key.starts_with("normalized/")) {
            throw std::runtime_error("cannot replace extraction metadata: " + key);
        }
        const auto original = base.find(key);
        if (original == base.end()) {
            throw std::runtime_error("unknown game file: " + key);
        }
        const auto file = Contained(package.root, package.root / Path(Relative(source.get<std::string>())));
        const auto size = fs::file_size(file);
        if (size == 0 || size != fs::file_size(original->second)) {
            throw std::runtime_error("replacement must match original byte size: " + key);
        }
        if (!package.files.emplace(key, file).second) {
            throw std::runtime_error("duplicate normalized file target: " + key);
        }
    }
}

void Problem(Package &package, const std::string &message) {
    if (std::ranges::find(package.problems, message) == package.problems.end()) {
        package.problems.push_back(message);
    }
}
} // namespace

bool Supports(std::string_view capability) {
    return capability == "assets.replace.v1" || capability == "scripts.lua.v1" ||
           capability == "quests.progress.v1" || capability == "save.metadata.v1" ||
           capability == "economy.write.v1" || capability == "events.map.v1" || capability == "events.tick.v1" || capability == "player.read.v1" ||
           capability == "party.read.v1" || capability == "inventory.read.v1" ||
           capability == "resources.read.v1" || capability == "logging.v1" || capability == "host.calls.v1" || capability == "plugins.native.v1" || capability == "plugins.native.v2" || capability == "events.lifecycle.v1" || capability == "services.query.v1" || capability == "menu.actions.v1" || capability == "menu.native.v1";
}

void Manager::Scan(const std::vector<fs::path> &roots, const FileIndex &base, const std::map<std::string, bool> &enabled,
                   const std::map<std::string, std::string> &trusted) {
    packages_.clear();
    errors_.clear();
    order_.clear();
    overrides_.clear();
    std::set<fs::path> seen;
    for (const auto &root : roots) {
        try {
            if (!fs::exists(root)) {
                continue;
            }
            for (const auto &entry : fs::directory_iterator(root)) {
                if (!entry.is_directory() || Text(entry.path().filename()).starts_with('.')) {
                    continue;
                }
                const auto canonical = fs::canonical(entry.path());
                if (!seen.insert(canonical).second) {
                    continue;
                }
                if (packages_.size() >= 1024) {
                    throw std::runtime_error("at most 1024 mod packages may be discovered");
                }
                Package package;
                package.root = canonical;
                try {
                    Read(package, base);
                    if (const auto found = enabled.find(package.id); found != enabled.end()) {
                        package.enabled = found->second;
                    }
                    if (!package.native_library.empty()) {
                        const auto trust = trusted.find(package.id);
                        package.native_trusted = trust != trusted.end() && trust->second == package.native_fingerprint;
                        if (!package.native_trusted) {
                            Problem(package, "Native library requires separate local trust confirmation");
                        }
                    }
                } catch (const std::exception &error) {
                    Problem(package, error.what());
                }
                packages_.push_back(std::move(package));
            }
        } catch (const std::exception &error) {
            errors_.push_back(Text(root) + ": " + error.what());
        }
    }
    std::ranges::sort(packages_, [](const Package &a, const Package &b) {
        return std::tie(a.id, a.root) < std::tie(b.id, b.root);
    });
    std::map<std::string, std::vector<std::size_t>> by_id;
    for (std::size_t i = 0; i < packages_.size(); ++i) {
        if (!packages_[i].id.empty()) {
            by_id[packages_[i].id].push_back(i);
        }
    }
    for (const auto &[id, indices] : by_id) {
        if (indices.size() > 1) {
            for (auto i : indices) {
                Problem(packages_[i], "Duplicate mod ID '" + id + "'; remove or rename duplicate packages");
            }
        }
    }

    // Snapshot eligibility: a prior collision must not hide later collisions.
    std::vector<bool> eligible;
    for (const auto &package : packages_) {
        eligible.push_back(package.Active());
    }
    for (std::size_t i = 0; i < packages_.size(); ++i) {
        if (!eligible[i]) {
            continue;
        }
        auto &a = packages_[i];
        for (std::size_t j = i + 1; j < packages_.size(); ++j) {
            if (!eligible[j]) {
                continue;
            }
            auto &b = packages_[j];
            for (const auto &claim : a.exclusive) {
                if (std::ranges::find(b.exclusive, claim) != b.exclusive.end()) {
                    Problem(a, "Exclusive gameplay feature '" + claim + "' also claimed by '" + b.id + "'");
                    Problem(b, "Exclusive gameplay feature '" + claim + "' also claimed by '" + a.id + "'");
                }
            }
            if (std::ranges::find(a.conflicts, b.id) != a.conflicts.end() || std::ranges::find(b.conflicts, a.id) != b.conflicts.end()) {
                Problem(a, "Declared conflict with '" + b.id + "'");
                Problem(b, "Declared conflict with '" + a.id + "'");
            }
            for (const auto &[target, file] : a.files) {
                if (b.files.contains(target)) {
                    Problem(a, "File '" + target + "' also replaced by '" + b.id + "'");
                    Problem(b, "File '" + target + "' also replaced by '" + a.id + "'");
                }
            }
        }
    }

    // DFS identifies the actual cycle; dependents receive dependency diagnostics below.
    std::vector<int>                 visited(packages_.size(), 0);
    std::vector<std::size_t>         stack;
    std::vector<std::size_t>         sorted;
    std::function<void(std::size_t)> visit = [&](std::size_t i) {
        if (visited[i] == 2 || !packages_[i].Active()) {
            return;
        }
        if (visited[i] == 1) {
            const auto start = std::ranges::find(stack, i);
            for (auto at = start; at != stack.end(); ++at) {
                Problem(packages_[*at], "Dependency cycle involving '" + packages_[i].id + "'");
            }
            return;
        }
        visited[i] = 1;
        stack.push_back(i);
        for (const auto &id : packages_[i].dependencies) {
            const auto found = by_id.find(id);
            if (found != by_id.end() && found->second.size() == 1) {
                visit(found->second.front());
            }
        }
        stack.pop_back();
        visited[i] = 2;
        sorted.push_back(i);
    };
    for (std::size_t i = 0; i < packages_.size(); ++i) {
        visit(i);
    }
    bool changed;
    do {
        changed = false;
        for (auto &package : packages_) {
            if (!package.Active()) {
                continue;
            }
            for (const auto &id : package.dependencies) {
                const auto found = by_id.find(id);
                if (found == by_id.end() || found->second.size() != 1 || !packages_[found->second.front()].Active()) {
                    Problem(package, "Required mod '" + id + "' is missing, disabled, or blocked");
                    changed = true;
                } else if (const auto requirement = package.dependency_versions.find(id); requirement != package.dependency_versions.end() && !requirement->second.Accepts(packages_[found->second.front()].version)) {
                    Problem(package, "Required mod '" + id + "' version " + requirement->second.Describe() + "\nInstalled: " + packages_[found->second.front()].version);
                    changed = true;
                }
            }
        }
    } while (changed);
    // An unreadable discovery root means the conflict scan is incomplete.
    if (!errors_.empty()) {
        for (auto &package : packages_) {
            if (package.Active()) {
                Problem(package, "Mod discovery incomplete; see scan errors");
            }
        }
    }
    for (auto i : sorted) {
        if (packages_[i].Active()) {
            order_.push_back(packages_[i].id);
            overrides_.insert(packages_[i].files.begin(), packages_[i].files.end());
        }
    }
}

std::string Manager::Report() const {
    Json report{
        {"schema_version", 1            },
        {"game",           "chronicle"  },
        {"errors",         errors_      },
        {"load_order",     order_       },
        {"mods",           Json::array()}
    };
    for (const auto &package : packages_) {
        Json version_rules = Json::object();
        for (const auto &[id, rule] : package.dependency_versions) {
            version_rules[id] = rule.Describe();
        }
        report["mods"].push_back({
            {"id",                  package.id                                                                 },
            {"name",                package.name                                                               },
            {"version",             package.version                                                            },
            {"directory",           Text(package.root)                                                         },
            {"enabled",             package.enabled                                                            },
            {"state",               !package.problems.empty() ? "blocked" : package.enabled ? "active"
                                                                              : "disabled"},
            {"problems",            package.problems                                                           },
            {"file_count",          package.files.size()                                                       },
            {"dependencies",        package.dependencies                                                       },
            {"dependency_versions", version_rules                                                              },
            {"permissions",         package.permissions                                                        },
            {"native_library",      Text(package.native_library)                                               },
            {"native_trusted",      package.native_trusted                                                     },
            {"native_fingerprint",  package.native_fingerprint                                                 }
        });
    }
    return report.dump(2) + '\n';
}
} // namespace mods
