#include <cstring>
#include <initializer_list>
#include <vector>

#include "check.h"
#include "keys.h"
#include "scan.h"

using namespace scan;

static const char* kSig = "48 8B C4 55 53 56 57 41 56 48 8D 6C 24 90 48 81 EC 70 01 00 00 0F 29 70 C8";

static std::vector<uint8_t> image_with(const Pattern& p, std::initializer_list<size_t> offsets, size_t size = 4096) {
    std::vector<uint8_t> data(size, 0xCC);
    for (size_t at : offsets) memcpy(data.data() + at, p.bytes.data(), p.bytes.size());
    return data;
}

TEST(parse_reads_bytes_and_wildcards) {
    const Pattern p = parse("48 8b ?? C4 ?");
    CHECK(p.ok);
    CHECK(p.bytes.size() == 5);
    CHECK(p.bytes[0] == 0x48 && p.bytes[1] == 0x8B && p.bytes[3] == 0xC4);
    CHECK(!p.wild[0] && !p.wild[1] && p.wild[2] && !p.wild[3] && p.wild[4]);
    CHECK(parse(kSig).bytes.size() == 25);
}

TEST(parse_rejects_bad_patterns) {
    CHECK(!parse(nullptr).ok);
    CHECK(!parse("").ok);
    CHECK(!parse("48 8").ok);
    CHECK(!parse("48 GG").ok);
    CHECK(!parse("488B").ok);
    CHECK(!parse("?? ??").ok);
}

TEST(finds_a_single_match) {
    const Pattern p = parse(kSig);
    const std::vector<uint8_t> data = image_with(p, {1234});
    const uint8_t* hits[4];
    CHECK(find_all(data.data(), data.size(), p, hits, 4) == 1);
    CHECK(hits[0] == data.data() + 1234);
}

TEST(finds_matches_at_both_ends) {
    const Pattern p = parse(kSig);
    const size_t size = 300;
    const std::vector<uint8_t> data = image_with(p, {0, size - p.bytes.size()}, size);
    const uint8_t* hits[4];
    CHECK(find_all(data.data(), data.size(), p, hits, 4) == 2);
    CHECK(hits[0] == data.data());
    CHECK(hits[1] == data.data() + size - p.bytes.size());
}

TEST(counts_matches_beyond_the_hit_buffer) {
    const Pattern p = parse("AA BB");
    const std::vector<uint8_t> data = image_with(p, {10, 20, 30, 40, 50});
    const uint8_t* hits[2];
    CHECK(find_all(data.data(), data.size(), p, hits, 2) == 5);
    CHECK(hits[1] == data.data() + 20);
}

TEST(no_match_and_short_data) {
    const Pattern p = parse(kSig);
    std::vector<uint8_t> data = image_with(p, {100});
    data[100 + 24] ^= 1;  // spoil the last byte
    CHECK(find_all(data.data(), data.size(), p, nullptr, 0) == 0);
    CHECK(find_all(data.data(), 10, p, nullptr, 0) == 0);
    CHECK(find_all(nullptr, 0, p, nullptr, 0) == 0);
}

TEST(overlapping_matches_are_all_found) {
    const Pattern p = parse("AA AA");
    const uint8_t data[] = {0xAA, 0xAA, 0xAA, 0x00};
    CHECK(find_all(data, sizeof(data), p, nullptr, 0) == 2);
}

TEST(wildcards_match_any_byte) {
    const Pattern p = parse("AA ?? CC");
    const uint8_t data[] = {0xAA, 0x01, 0xCC, 0xAA, 0xFF, 0xCC, 0xAA, 0x00, 0xCD};
    CHECK(find_all(data, sizeof(data), p, nullptr, 0) == 2);
}

TEST(wildcard_head_finds_a_hooked_function) {
    const Pattern sig = parse(kSig);
    std::vector<uint8_t> data = image_with(sig, {2000});
    // Another mod's hook: E9 rel32 over the first five bytes.
    const uint8_t jump[5] = {0xE9, 0x10, 0x20, 0x30, 0x40};
    memcpy(data.data() + 2000, jump, 5);

    CHECK(find_all(data.data(), data.size(), sig, nullptr, 0) == 0);
    const Pattern loose = wildcard_head(sig, 5);
    CHECK(loose.ok);
    const uint8_t* hits[4];
    CHECK(find_all(data.data(), data.size(), loose, hits, 4) == 1);
    CHECK(hits[0] == data.data() + 2000);
    CHECK(starts_with_jump(hits[0], 100));
}

TEST(wildcard_head_of_everything_is_not_usable) {
    CHECK(!wildcard_head(parse("AA BB"), 2).ok);
    CHECK(!wildcard_head(parse("AA BB"), 9).ok);
}

TEST(starts_with_jump_shapes) {
    const uint8_t rel[] = {0xE9, 0, 0, 0, 0};
    const uint8_t indirect[] = {0xFF, 0x25, 0, 0, 0, 0};
    const uint8_t viaRax[] = {0x48, 0xB8, 1, 2, 3, 4, 5, 6, 7, 8, 0xFF, 0xE0};
    const uint8_t viaR11[] = {0x49, 0xBB, 1, 2, 3, 4, 5, 6, 7, 8, 0x41, 0xFF, 0xE3};
    const uint8_t plain[] = {0x48, 0x8B, 0xC4, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48, 0x8D, 0x6C, 0x24};
    CHECK(starts_with_jump(rel, sizeof(rel)));
    CHECK(starts_with_jump(indirect, sizeof(indirect)));
    CHECK(starts_with_jump(viaRax, sizeof(viaRax)));
    CHECK(starts_with_jump(viaR11, sizeof(viaR11)));
    CHECK(!starts_with_jump(plain, sizeof(plain)));
    CHECK(!starts_with_jump(rel, 4));  // cut off
    CHECK(!starts_with_jump(nullptr, 16));
}

TEST(key_names) {
    CHECK(keys::parse("H") == 'H');
    CHECK(keys::parse("h") == 'H');
    CHECK(keys::parse("7") == '7');
    CHECK(keys::parse("Up") == 0x26);
    CHECK(keys::parse("pageup") == 0x21);
    CHECK(keys::parse(" PageDown ") == 0x22);
    CHECK(keys::parse("LShift") == 0xA0);
    CHECK(keys::parse("F1") == 0x70);
    CHECK(keys::parse("F12") == 0x7B);
    CHECK(keys::parse("Numpad8") == 0x68);
    CHECK(keys::parse("0x48") == 0x48);
    CHECK(keys::parse("72") == 72);
    CHECK(keys::parse("None") == 0);
    CHECK(keys::parse("") == 0);
    CHECK(keys::parse(nullptr) == 0);
}

TEST(key_names_rejected) {
    CHECK(keys::parse("Banana") == -1);
    CHECK(keys::parse("F25") == -1);
    CHECK(keys::parse("0x") == -1);
    CHECK(keys::parse("0x1FF") == -1);
    CHECK(keys::parse("999") == -1);
    CHECK(keys::parse("-") == -1);
    CHECK(keys::parse("a very long key name that cannot be right") == -1);
}

TEST(key_names_round_trip) {
    for (int vk : {0x48, 0x30, 0x21, 0x22, 0x25, 0x26, 0x27, 0x28, 0xA0, 0x70, 0x7B, 0x87, 0x60, 0x69, 0xBA, 0}) {
        char name[16];
        keys::name_of(vk, name, sizeof(name));
        CHECK(keys::parse(name) == vk);
    }
}
