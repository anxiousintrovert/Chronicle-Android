#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "mods/manager.hpp"

namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;

class ModManager : public testing::Test {
protected:
    fs::path                    root = fs::temp_directory_path() / ("chronicle-mods-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    mods::FileIndex             base;
    mods::Manager               manager;
    std::map<std::string, bool> enabled;

    void SetUp() override {
        Write(root / "data" / "asset.bin", "base");
        base.emplace("asset.bin", root / "data" / "asset.bin");
    }

    void TearDown() override {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }

    void Write(const fs::path &path, const std::string &text) {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream << text;
        ASSERT_TRUE(stream.good());
    }

    Json Manifest(const std::string &id) {
        return {
            {"schema_version", 1             },
            {"id",             id            },
            {"name",           id            },
            {"version",        "1.0.0"       },
            {"game",           "chronicle"   },
            {"files",          Json::object()}
        };
    }

    void Add(const std::string &folder, Json manifest, bool asset = false) {
        if (asset) {
            manifest["files"] = {
                {"asset.bin", "replacement.bin"}
            };
        }
        enabled[manifest["id"].get<std::string>()] = manifest.value("enabled", true);
        Write(root / "mods" / folder / "mod.json", manifest.dump());
        Write(root / "mods" / folder / "replacement.bin", "mods");
    }

    void Scan() { manager.Scan({root / "mods"}, base, enabled); }

    const mods::Package &Find(const std::string &id) {
        for (const auto &package : manager.Packages()) {
            if (package.id == id) {
                return package;
            }
        }
        throw std::runtime_error("missing test package");
    }
};

TEST_F(ModManager, EmptyFolderPreservesVanilla) {
    Scan();
    EXPECT_TRUE(manager.Errors().empty());
    EXPECT_TRUE(manager.Overrides().empty());
}

TEST_F(ModManager, DiscoversReplacementAndProducesReport) {
    Add("one", Manifest("test.one"), true);
    Scan();
    ASSERT_EQ(manager.Overrides().size(), 1u);
    EXPECT_TRUE(Find("test.one").Active());
    EXPECT_EQ(Json::parse(manager.Report())["mods"][0]["state"], "active");
}

TEST_F(ModManager, CaseFoldedCollisionBlocksEveryOwner) {
    Add("one", Manifest("test.one"), true);
    auto two = Manifest("test.two");
    two["files"] = {
        {"ASSET.BIN", "replacement.bin"}
    };
    Add("two", two);
    Add("three", Manifest("test.three"), true);
    Scan();
    EXPECT_TRUE(manager.Overrides().empty());
    for (const auto &p : manager.Packages()) {
        EXPECT_EQ(p.problems.size(), 2u);
    }
}

TEST_F(ModManager, OneSidedConflictBlocksBothButNotUnrelated) {
    auto a = Manifest("test.a");
    a["conflicts"] = {"test.b"};
    Add("a", a);
    Add("b", Manifest("test.b"));
    Add("c", Manifest("test.c"), true);
    Scan();
    EXPECT_FALSE(Find("test.a").Active());
    EXPECT_FALSE(Find("test.b").Active());
    EXPECT_TRUE(Find("test.c").Active());
}

TEST_F(ModManager, DisabledModDoesNotConflict) {
    auto a = Manifest("test.a");
    a["enabled"] = false;
    Add("a", a, true);
    Add("b", Manifest("test.b"), true);
    Scan();
    EXPECT_FALSE(Find("test.a").Active());
    EXPECT_TRUE(Find("test.b").Active());
}

TEST_F(ModManager, DuplicateIdsHaveNoWinnerAcrossRoots) {
    Add("a", Manifest("test.a"), true);
    Write(root / "other" / "copy" / "mod.json", Manifest("test.a").dump());
    manager.Scan({root / "other", root / "mods"}, base, enabled);
    EXPECT_TRUE(manager.Overrides().empty());
    ASSERT_EQ(manager.Packages().size(), 2u);
    for (const auto &p : manager.Packages()) {
        EXPECT_FALSE(p.Active());
    }
}

TEST_F(ModManager, RepeatedRootDoesNotDuplicatePackages) {
    Add("a", Manifest("test.a"), true);
    manager.Scan({root / "mods", root / "mods"}, base, enabled);
    ASSERT_EQ(manager.Packages().size(), 1u);
    EXPECT_TRUE(manager.Packages()[0].Active());
}

TEST_F(ModManager, DependenciesLoadBeforeDependents) {
    auto a = Manifest("test.a");
    a["requires"] = {"test.z"};
    Add("a", a, true);
    Add("z", Manifest("test.z"));
    Scan();
    EXPECT_EQ(manager.LoadOrder(), (std::vector<std::string>{"test.z", "test.a"}));
}

TEST_F(ModManager, MissingDependencyBlocksTransitiveDependents) {
    auto a = Manifest("test.a"), b = Manifest("test.b");
    a["requires"] = {"test.b"};
    b["requires"] = {"test.missing"};
    Add("a", a, true);
    Add("b", b);
    Scan();
    EXPECT_TRUE(manager.Overrides().empty());
    EXPECT_FALSE(Find("test.a").Active());
    EXPECT_FALSE(Find("test.b").Active());
}

TEST_F(ModManager, CyclesAndTheirDependentsAreBlocked) {
    auto a = Manifest("test.a"), b = Manifest("test.b"), c = Manifest("test.c");
    a["requires"] = {"test.b"};
    b["requires"] = {"test.a"};
    c["requires"] = {"test.a"};
    Add("a", a);
    Add("b", b);
    Add("c", c, true);
    Scan();
    EXPECT_TRUE(manager.LoadOrder().empty());
    EXPECT_NE(Find("test.a").problems.front().find("cycle"), std::string::npos);
    EXPECT_NE(Find("test.c").problems.front().find("Required"), std::string::npos);
}

TEST_F(ModManager, RejectsTraversalAbsoluteAndAlternateStreamPaths) {
    for (const auto *path : {"../escape", "/absolute", "C:/absolute", "asset.bin:stream", "a\\..\\escape"}) {
        auto a = Manifest("test.a");
        a["files"] = {
            {"asset.bin", path}
        };
        Add("a", a);
        Scan();
        EXPECT_FALSE(Find("test.a").Active()) << path;
        EXPECT_TRUE(manager.Overrides().empty());
    }
}

TEST_F(ModManager, RejectsUnknownTargetAndSizeMismatch) {
    auto a = Manifest("test.a");
    a["files"] = {
        {"missing.bin", "replacement.bin"}
    };
    Add("a", a);
    Add("b", Manifest("test.b"), true);
    Write(root / "mods" / "b" / "replacement.bin", "too large");
    Scan();
    EXPECT_TRUE(manager.Overrides().empty());
    EXPECT_FALSE(Find("test.a").Active());
    EXPECT_FALSE(Find("test.b").Active());
}

TEST_F(ModManager, InvalidManifestDoesNotStopHealthyMod) {
    Write(root / "mods" / "broken" / "mod.json", "{broken");
    Add("good", Manifest("test.good"), true);
    Scan();
    EXPECT_TRUE(Find("test.good").Active());
    EXPECT_EQ(manager.Packages().size(), 2u);
}

TEST_F(ModManager, RejectsDuplicateJsonKeysAndUnknownFields) {
    Write(root / "mods" / "duplicate" / "mod.json", R"({"id":"test.a","id":"test.b"})");
    auto a = Manifest("test.unknown");
    a["confict"] = {"test.other"};
    Add("unknown", a);
    Scan();
    ASSERT_EQ(manager.Packages().size(), 2u);
    for (const auto &package : manager.Packages()) {
        EXPECT_FALSE(package.problems.empty());
    }
    EXPECT_TRUE(manager.Overrides().empty());
}

TEST_F(ModManager, RejectsSymlinkEscapingPackage) {
    auto a = Manifest("test.a");
    a["files"] = {
        {"asset.bin", "escape.bin"}
    };
    Add("a", a);
    std::error_code error;
    fs::create_symlink(root / "data/asset.bin", root / "mods/a/escape.bin", error);
    if (error) {
        GTEST_SKIP() << "Symlink creation unavailable: " << error.message();
    }
    Scan();
    EXPECT_FALSE(Find("test.a").Active());
    EXPECT_TRUE(manager.Overrides().empty());
}

TEST_F(ModManager, FailedDiscoveryDoesNotPublishPartialPlan) {
    Add("a", Manifest("test.a"), true);
    Write(root / "not-directory", "x");
    manager.Scan({root / "mods", root / "not-directory"}, base, enabled);
    EXPECT_FALSE(manager.Errors().empty());
    EXPECT_TRUE(manager.Overrides().empty());
}

TEST_F(ModManager, RescanClearsPriorOverrides) {
    Add("a", Manifest("test.a"), true);
    Scan();
    ASSERT_FALSE(manager.Overrides().empty());
    auto disabled = Manifest("test.a");
    disabled["enabled"] = false;
    Add("a", disabled, true);
    Scan();
    EXPECT_TRUE(manager.Overrides().empty());
}

TEST_F(ModManager, RequiredHostOperationsBlockBeforeLoadingAnyCode) {
    auto m = Manifest("test.one");
    m["requires_operations"] = {"story.flag_set"};
    Add("one", m);
    Scan();
    EXPECT_FALSE(Find("test.one").Active());
    EXPECT_NE(manager.Report().find("needs permission"), std::string::npos);
    m["permissions"] = {"game.write"};
    Add("one", m);
    Scan();
    EXPECT_TRUE(Find("test.one").Active());
    m["requires_operations"] = {"entities.spawn"};
    Add("one", m);
    Scan();
    EXPECT_FALSE(Find("test.one").Active());
    EXPECT_NE(manager.Report().find("missing host operation"), std::string::npos);
}

TEST_F(ModManager, VersionMismatchBlocksConsumerAndDependentsWithoutBlockingProvider) {
    auto provider = Manifest("test.provider"), consumer = Manifest("test.consumer"), dependent = Manifest("test.dependent");
    provider["version"] = "2.0.0";
    consumer["requires"] = {"test.provider"};
    consumer["dependency_versions"] = {
        {"test.provider", {{"minimum", "1.2.0"}, {"before", "2.0.0"}}}
    };
    dependent["requires"] = {"test.consumer"};
    Add("provider", provider);
    Add("consumer", consumer);
    Add("dependent", dependent);
    Scan();
    EXPECT_TRUE(Find("test.provider").Active());
    EXPECT_FALSE(Find("test.consumer").Active());
    EXPECT_FALSE(Find("test.dependent").Active());
    EXPECT_NE(manager.Report().find("Installed: 2.0.0"), std::string::npos);
    provider["version"] = "1.2.0+build";
    Add("provider", provider);
    Scan();
    EXPECT_TRUE(Find("test.consumer").Active());
    EXPECT_TRUE(Find("test.dependent").Active());
    ASSERT_EQ(manager.LoadOrder().size(), 3u);
    EXPECT_EQ(manager.LoadOrder()[0], "test.provider");
}

TEST_F(ModManager, InvalidOrUndeclaredVersionConstraintFailsDiscovery) {
    auto m = Manifest("test.consumer");
    m["dependency_versions"] = {
        {"test.provider", {{"exact", "1"}}}
    };
    Add("consumer", m);
    Scan();
    EXPECT_FALSE(Find("test.consumer").Active());
    EXPECT_NE(manager.Report().find("requires declared dependency"), std::string::npos);
    m["requires"] = {"test.provider"};
    m["dependency_versions"]["test.provider"] = {
        {"minimum", "2.0.0"},
        {"before",  "1.0.0"}
    };
    Add("consumer", m);
    Scan();
    EXPECT_NE(manager.Report().find("version range is empty"), std::string::npos);
}

} // namespace

TEST_F(ModManager, AudioDeclarationsAreContainedBoundedAndDisabledByDefault) {
    auto manifest = Manifest("test.audio");
    manifest["audio"] = {{"tone", "sound.WAV"}};
    Add("audio", manifest);
    Write(root / "mods/audio/sound.WAV", std::string(70000, 'x'));
    enabled.clear();
    Scan();
    ASSERT_EQ(manager.Packages().size(), 1);
    EXPECT_TRUE(manager.Packages()[0].problems.empty());
    EXPECT_FALSE(manager.Packages()[0].Active());
    EXPECT_EQ(manager.Packages()[0].audio.size(), 1);
    manifest["audio"]["tone"] = "../../data/asset.bin";
    Add("audio", manifest);
    Scan();
    EXPECT_FALSE(manager.Packages()[0].problems.empty());
    manifest["audio"] = {{"tone", "replacement.bin"}};
    Add("audio", manifest);
    Scan();
    EXPECT_FALSE(manager.Packages()[0].problems.empty());
}
