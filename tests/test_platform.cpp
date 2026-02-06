#include <gtest/gtest.h>
#include "platform.hpp"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class PlatformTest : public ::testing::Test {
protected:
    fs::path test_file;

    void SetUp() override {
        test_file = fs::temp_directory_path() / "platform_test.txt";
        std::ofstream f(test_file);
        f << "Hello, World!";
    }

    void TearDown() override {
        fs::remove(test_file);
    }
};

TEST_F(PlatformTest, FileOpenValid) {
    auto handle = platform::file_open(test_file);
    EXPECT_TRUE(handle.valid());
    EXPECT_EQ(handle.file_size, 13);  // "Hello, World!" = 13 bytes
    platform::file_close(handle);
    EXPECT_FALSE(handle.valid());
}

TEST_F(PlatformTest, FileOpenInvalid) {
    auto handle = platform::file_open("/nonexistent/path/file.txt");
    EXPECT_FALSE(handle.valid());
}

TEST_F(PlatformTest, MmapRead) {
    auto handle = platform::file_open(test_file);
    ASSERT_TRUE(handle.valid());

    void* addr = platform::mmap_read(handle, 0, handle.file_size);
    ASSERT_NE(addr, nullptr);

    // Verify content
    std::string content(static_cast<char*>(addr), handle.file_size);
    EXPECT_EQ(content, "Hello, World!");

    platform::munmap(addr, handle.file_size);
    platform::file_close(handle);
}

TEST_F(PlatformTest, MadviseDoesNotCrash) {
    auto handle = platform::file_open(test_file);
    ASSERT_TRUE(handle.valid());

    void* addr = platform::mmap_read(handle, 0, handle.file_size);
    ASSERT_NE(addr, nullptr);

    // Should not crash
    platform::madvise(addr, handle.file_size, platform::AccessHint::Sequential);
    platform::madvise(addr, handle.file_size, platform::AccessHint::Random);
    platform::madvise(addr, handle.file_size, platform::AccessHint::WillNeed);
    platform::madvise(addr, handle.file_size, platform::AccessHint::DontNeed);

    platform::munmap(addr, handle.file_size);
    platform::file_close(handle);
}

TEST_F(PlatformTest, OfstreamOpen) {
    fs::path out_file = fs::temp_directory_path() / "platform_out.txt";
    {
        auto stream = platform::ofstream_open(out_file);
        EXPECT_TRUE(stream.is_open());
        stream << "Test output";
    }
    std::ifstream in(out_file);
    std::string content;
    std::getline(in, content);
    EXPECT_EQ(content, "Test output");
    fs::remove(out_file);
}

TEST_F(PlatformTest, IfstreamOpen) {
    auto stream = platform::ifstream_open(test_file);
    EXPECT_TRUE(stream.is_open());
    std::string content;
    std::getline(stream, content);
    EXPECT_EQ(content, "Hello, World!");
}

TEST_F(PlatformTest, Fopen) {
    FILE* f = platform::fopen(test_file, "r");
    ASSERT_NE(f, nullptr);
    char buf[20];
    fgets(buf, sizeof(buf), f);
    EXPECT_STREQ(buf, "Hello, World!");
    fclose(f);
}

// Unicode path tests - verify Korean/CJK characters work on all platforms
class UnicodePlatformTest : public ::testing::Test {
protected:
    // Korean: "테스트" (test), Japanese: "テスト", Chinese: "测试"
    fs::path unicode_dir;
    fs::path unicode_file;

    void SetUp() override {
        unicode_dir = fs::temp_directory_path() / fs::u8path("ply2lcc_유니코드_テスト");
        fs::create_directories(unicode_dir);
        unicode_file = unicode_dir / fs::u8path("데이터.txt");
        auto f = platform::ofstream_open(unicode_file, std::ios::out);
        f << "unicode test data";
    }

    void TearDown() override {
        fs::remove_all(unicode_dir);
    }
};

TEST_F(UnicodePlatformTest, FileOpenUnicodePath) {
    auto handle = platform::file_open(unicode_file);
    EXPECT_TRUE(handle.valid());
    EXPECT_EQ(handle.file_size, 17);  // "unicode test data"
    platform::file_close(handle);
}

TEST_F(UnicodePlatformTest, MmapReadUnicodePath) {
    auto handle = platform::file_open(unicode_file);
    ASSERT_TRUE(handle.valid());

    void* addr = platform::mmap_read(handle, 0, handle.file_size);
    ASSERT_NE(addr, nullptr);

    std::string content(static_cast<char*>(addr), handle.file_size);
    EXPECT_EQ(content, "unicode test data");

    platform::munmap(addr, handle.file_size);
    platform::file_close(handle);
}

TEST_F(UnicodePlatformTest, FopenUnicodePath) {
    FILE* f = platform::fopen(unicode_file, "r");
    ASSERT_NE(f, nullptr);
    char buf[32];
    fgets(buf, sizeof(buf), f);
    EXPECT_STREQ(buf, "unicode test data");
    fclose(f);
}

TEST_F(UnicodePlatformTest, IfstreamUnicodePath) {
    auto stream = platform::ifstream_open(unicode_file);
    EXPECT_TRUE(stream.is_open());
    std::string content;
    std::getline(stream, content);
    EXPECT_EQ(content, "unicode test data");
}

TEST_F(UnicodePlatformTest, OfstreamUnicodePath) {
    fs::path out = unicode_dir / fs::u8path("출력.bin");
    {
        auto stream = platform::ofstream_open(out);
        EXPECT_TRUE(stream.is_open());
        stream << "output data";
    }
    auto in = platform::ifstream_open(out);
    std::string content;
    std::getline(in, content);
    EXPECT_EQ(content, "output data");
}

TEST_F(UnicodePlatformTest, FsExistsUnicodePath) {
    EXPECT_TRUE(fs::exists(unicode_file));
    EXPECT_TRUE(fs::exists(unicode_dir));
    EXPECT_FALSE(fs::exists(unicode_dir / fs::u8path("없는파일.txt")));
}

TEST_F(UnicodePlatformTest, U8PathRoundtrip) {
    // Verify u8path → u8string roundtrip preserves Korean characters
    std::string original = "C:/경로/테스트/파일.ply";
    fs::path p = fs::u8path(original);
    std::string recovered = p.u8string();
    EXPECT_EQ(original, recovered);
}
