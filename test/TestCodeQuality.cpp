#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <vector>

namespace fs = std::filesystem;

class CodeQualityTest : public ::testing::Test {
protected:
    std::vector<fs::path> sourceFiles_;

    void SetUp() override {
        const fs::path srcDir = fs::path(TEST_SOURCE_DIR) / "src";
        if (fs::exists(srcDir)) {
            for (auto& entry : fs::recursive_directory_iterator(srcDir)) {
                auto ext = entry.path().extension();
                if (ext == ".h" || ext == ".cpp")
                    sourceFiles_.push_back(entry.path());
            }
        }
    }

    std::vector<std::pair<fs::path, int>> findPatternExempt(
        const std::regex& pattern, const std::string& exemption = "ALLOW_RAW_PTR") {
        std::vector<std::pair<fs::path, int>> matches;
        for (auto& path : sourceFiles_) {
            std::ifstream file(path);
            std::string line;
            int lineno = 0;
            while (std::getline(file, line)) {
                ++lineno;
                if (line.find("// " + exemption) != std::string::npos) continue;
                if (std::regex_search(line, pattern))
                    matches.emplace_back(path, lineno);
            }
        }
        return matches;
    }

    std::string formatReport(const std::vector<std::pair<fs::path, int>>& matches) {
        std::string report;
        for (auto& [path, line] : matches)
            report += path.filename().string() + ":" + std::to_string(line) + "\n";
        return report;
    }
};

// ── 1. No raw owning pointers as class members ──────────────────────────
// Warning only — many members are non-owning observer pointers (same as Java refs).
TEST_F(CodeQualityTest, NoRawOwnerMembers) {
    std::regex memberPtr(R"(\b[A-Z][A-Za-z0-9_]*\s+\*\s+\w+[_=;])");
    auto matches = findPatternExempt(memberPtr);

    std::vector<std::pair<fs::path, int>> filtered;
    for (auto& [path, line] : matches) {
        std::ifstream file(path);
        std::string lineText;
        int cur = 0;
        while (std::getline(file, lineText)) {
            if (++cur == line) break;
        }
        bool skip = lineText.find(" static ") != std::string::npos;
        if (!skip) filtered.emplace_back(path, line);
    }

    if (!filtered.empty()) {
        GTEST_LOG_(WARNING) << "Raw pointer class members (consider unique_ptr):\n"
            << formatReport(filtered);
    }
}

// ── 2. Function length warning (> 200 lines) ────────────────────────────
TEST_F(CodeQualityTest, FunctionLengthWarning) {
    std::vector<std::pair<fs::path, int>> longFuncs;
    for (auto& path : sourceFiles_) {
        std::ifstream file(path);
        std::string line;
        int lineno = 0, braceDepth = 0, funcStart = 0;
        bool inFunc = false;
        while (std::getline(file, line)) {
            ++lineno;
            for (char c : line) {
                if (c == '{') {
                    if (!inFunc) { funcStart = lineno; inFunc = true; }
                    ++braceDepth;
                }
                if (c == '}') {
                    --braceDepth;
                    if (inFunc && braceDepth == 0) {
                        int len = lineno - funcStart + 1;
                        if (len > 200)
                            longFuncs.emplace_back(path, funcStart);
                        inFunc = false;
                    }
                }
            }
        }
    }
    if (!longFuncs.empty()) {
        GTEST_LOG_(WARNING) << "Long functions (>200 lines):\n"
            << formatReport(longFuncs);
    }
}
