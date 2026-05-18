#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <vector>

namespace fs = std::filesystem;

// Basic code quality checks — no TODOs, no FIXMEs, no commented-out code blocks
class CodeQualityTest : public ::testing::Test {
protected:
    std::vector<fs::path> sourceFiles_;

    static void SetUpTestSuite() {
    }

    void SetUp() override {
        const fs::path srcDir = fs::path(TEST_SOURCE_DIR) / "src";
        if (!fs::exists(srcDir)) return;
        for (auto& entry : fs::recursive_directory_iterator(srcDir)) {
            auto ext = entry.path().extension();
            if (ext == ".h" || ext == ".cpp") {
                sourceFiles_.push_back(entry.path());
            }
        }
    }

    std::vector<std::pair<fs::path, int>> findPattern(const std::regex& pattern) {
        std::vector<std::pair<fs::path, int>> matches;
        for (auto& path : sourceFiles_) {
            std::ifstream file(path);
            std::string line;
            int lineno = 0;
            while (std::getline(file, line)) {
                ++lineno;
                if (std::regex_search(line, pattern)) {
                    matches.emplace_back(path, lineno);
                }
            }
        }
        return matches;
    }
};

// No TODO/FIXME in production code (ok in tests)
TEST_F(CodeQualityTest, NoTODOsInSource) {
    auto matches = findPattern(std::regex(R"(\b(TODO|FIXME|HACK|XXX)\b)"));
    std::string report;
    for (auto& [path, line] : matches) {
        report += path.filename().string() + ":" + std::to_string(line) + "\n";
    }
    EXPECT_TRUE(matches.empty()) << "Found " << matches.size()
        << " TODO/FIXME/HACK/XXX in source:\n" << report;
}

// No large commented-out blocks (5+ consecutive lines starting with //)
TEST_F(CodeQualityTest, NoLargeCommentedBlocks) {
    std::vector<std::pair<fs::path, int>> blocks;
    for (auto& path : sourceFiles_) {
        std::ifstream file(path);
        std::string line;
        int lineno = 0;
        int commentCount = 0;
        int blockStart = 0;
        while (std::getline(file, line)) {
            ++lineno;
            bool isComment = line.find("//") == 0 || line.find("///") == 0;
            if (isComment) {
                if (commentCount == 0) blockStart = lineno;
                ++commentCount;
            } else {
                if (commentCount >= 5) {
                    blocks.emplace_back(path, blockStart);
                }
                commentCount = 0;
            }
        }
        if (commentCount >= 5) {
            blocks.emplace_back(path, blockStart);
        }
    }
    std::string report;
    for (auto& [path, line] : blocks) {
        report += path.filename().string() + ":" + std::to_string(line) + " (5+ comment lines)\n";
    }
    EXPECT_TRUE(blocks.empty()) << "Large commented-out blocks:\n" << report;
}
