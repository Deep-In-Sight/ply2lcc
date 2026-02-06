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
    // Verify file was written
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
