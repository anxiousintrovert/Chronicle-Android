#include <gtest/gtest.h>
#include "../mods/music_pack.hpp"
#include "audio_retail_fixture.hpp"
#include "data_fixture.hpp"

TEST(ModMusicPack, StagesBanksAndSequencesInNativeOrderWithoutPlayback) {
    const std::string list = "test_a.hd\r\ntest_a.sq\r\nshort\r\ntest_e.hd\r\n";
    const auto hd = chronicle_audio::fixture::MakeHd({});
    const auto bd = chronicle_audio::fixture::SquareVag(2);
    auto pack = datafix::MakePack({{"bgm3.txt", {list.begin(),list.end()}}, {"test_a.hd", hd}, {"test_a.bd", bd},
        {"test_a.sq", chronicle_audio::fixture::MakeSq(60, 127, 480)}, {"test_e.hd", hd}, {"test_e.bd", bd}});
    std::string error;
    srand(42); const auto expected = rand(); srand(42);
    auto prepared = mods::PrepareMusicPack(pack, 3, error);
    ASSERT_NE(prepared, nullptr) << error;
    ASSERT_EQ(prepared->entries.size(), 3);
    EXPECT_TRUE(prepared->entries[0].bank);
    EXPECT_TRUE(prepared->entries[1].sequence);
    EXPECT_EQ(prepared->entries[2].slot, 'e');
    EXPECT_EQ(rand(), expected);
    pack.clear();
    EXPECT_FALSE(prepared->entries[1].sequence->Songs().empty());
}
TEST(ModMusicPack, RejectsMissingMalformedAndDuplicateMembersWithoutPartialResult) {
    std::string error;
    EXPECT_FALSE(mods::PrepareMusicPack({}, 0, error));
    const std::string list = "test_a.hd\n";
    auto pack = datafix::MakePack({{"bgm0.txt", {list.begin(),list.end()}}});
    EXPECT_FALSE(mods::PrepareMusicPack(pack, 0, error));
    pack = datafix::MakePack({{"bgm0.txt", {'a'}}, {"BGM0.TXT", {'b'}}});
    EXPECT_FALSE(mods::PrepareMusicPack(pack, 0, error));
    pack = datafix::MakePack({{"bgm0.txt", {'a'}}});
    datafix::Put32(pack, 64, 0xffffffff);
    EXPECT_FALSE(mods::PrepareMusicPack(pack, 0, error));
    EXPECT_FALSE(mods::PrepareMusicPack(pack, 256, error));
}
TEST(ModMusicPack, RejectsBrokenSequenceAfterDecodedBankAndOversizedConfig) {
    const auto hd = chronicle_audio::fixture::MakeHd({});
    const auto bd = chronicle_audio::fixture::SquareVag(2);
    const std::string list = "test_a.hd\ntest_a.sq\n";
    std::string error;
    auto pack = datafix::MakePack({{"bgm0.txt", {list.begin(), list.end()}},
        {"test_a.hd", hd}, {"test_a.bd", bd}, {"test_a.sq", {0, 1, 2}}});
    EXPECT_FALSE(mods::PrepareMusicPack(pack, 0, error));
    EXPECT_EQ(error, "cannot decode music sequence: test_a.sq");
    pack = datafix::MakePack({{"bgm0.txt", {list.begin(), list.end()}},
        {"test_a.hd", hd}, {"test_a.bd", bd},
        {"test_a.sq", chronicle_audio::fixture::MakeSq(60, 127, 480)},
        {"bgm0.cfg", std::vector<std::uint8_t>(65537, 'x')}});
    EXPECT_FALSE(mods::PrepareMusicPack(pack, 0, error));
    EXPECT_EQ(error, "music config exceeds 64 KiB");
}

TEST(ModMusicPack, PalArchivesPreflightWhenDataProvided) {
    const auto *data = std::getenv("DC_DATA");
    if (!data) GTEST_SKIP() << "Set DC_DATA to extracted PAL assets";
    int count = 0, rejected = 0;
    for (const auto &file : std::filesystem::directory_iterator(std::filesystem::path(data) / "sound/bgm")) {
        if (file.path().extension() != ".snd") continue;
        const auto stem = file.path().stem().string();
        const int set = std::stoi(stem.substr(3));
        std::string error;
        auto prepared = mods::PrepareMusicPack(datafix::ReadBytes(file.path()), set, error);
        // These retail lists refer to an absent *_a sequence, while the archive
        // contains a *_b sequence. Do not guess a substitute or partially load
        // the bank: mod requests must fail before changing live driver state.
        if (set == 120 || set == 130) {
            EXPECT_FALSE(prepared) << file.path();
            EXPECT_EQ(error, set == 120 ? "missing music pack member: d03a_a.sq"
                                       : "missing music pack member: d04a_a.sq");
            ++rejected;
        } else {
            ASSERT_NE(prepared, nullptr) << file.path() << ": " << error;
            ++count;
        }
    }
    EXPECT_EQ(count, 55);
    EXPECT_EQ(rejected, 2);
}
