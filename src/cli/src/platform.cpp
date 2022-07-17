#include <cli/platform.h>

#include <cli/log.h>
#include <cli/utility.h>

#include <llvm/ADT/Triple.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetSelect.h>

#include <fmt/format.h>

#include <sstream>
#include <unordered_set>
#include <filesystem>

namespace fs = std::filesystem;

namespace kara::cli {
    namespace {
        std::string_view trimString(std::string_view input) {
            size_t beginTrim = 0;

            while (beginTrim < input.size()) {
                if (!std::isspace(input[beginTrim]))
                    break;

                beginTrim++;
            }

            if (beginTrim > 0)
                input = input.substr(beginTrim);

            size_t endTrim = 0;

            while (endTrim < input.size()) {
                if (!std::isspace(input[input.size() - 1 - endTrim]))
                    break;

                endTrim++;
            }

            if (endTrim > 0)
                input = input.substr(0, input.size() - endTrim);

            return input;
        }
    }

    std::unique_ptr<Platform> Platform::byNative(std::string root, BuildLockFile &lock) {
        return Platform::byTriple(std::move(root), llvm::sys::getDefaultTargetTriple(), lock);
    }

    std::unique_ptr<Platform> Platform::byTriple(std::string root, std::string name, BuildLockFile &lock) {
        llvm::Triple llvmTriple(name);

        switch (llvmTriple.getOS()) {
        case llvm::Triple::OSType::Darwin:
            return std::make_unique<MacOSPlatform>(std::move(root), std::move(name), lock);

        default:
            return std::make_unique<Platform>(std::move(root), std::move(name), lock);
        }
    }

    std::vector<std::string> Platform::parseCLIArgumentsFromString(std::string_view input) {
        // this is supposed to do a good enough job at interpreting output from cmake --find-package
        // not a perfect cli interpreter, custom parser here, probably many bugs

        std::vector<std::string> result;

        size_t index = 0;
        std::optional<char> currentQuote;
        std::stringstream current;

        std::unordered_set<char> quotes = { '\'', '\"' };

        auto push = [&]() {
            auto str = current.str();

            if (!str.empty()) {
                result.push_back(str);
                current.str("");
            }
        };

        auto isQuote = [&](char c) { return quotes.find(c) != quotes.end(); };

        // important escapes: \space and \"

        while (index < input.size()) {
            char c = input[index];

            if (currentQuote) {
                auto currentQuoteEscape = [&]() { return std::string("\\") + *currentQuote; };

                if (c == *currentQuote) { // end quote
                    currentQuote = std::nullopt;
                    index += 1;
                } else if (input.size() > index + 1 && input.substr(index, 2) == currentQuoteEscape()) { // escape
                    current << *currentQuote;
                    index += 2;
                } else { // next char
                    current << c;
                    index += 1;
                }
            } else {
                if (std::isspace(c)) { // end argument
                    push();
                    index += 1;
                } else if (isQuote(c)) { // start quote
                    currentQuote = c;
                    index += 1;
                } else if (input.size() > index + 1 && c == '\\'
                    && (isQuote(input[index + 1]) || input[index + 1] == ' ' || input[index + 1] == '\\')) { // escape
                    current << input[index + 1];
                    index += 2;
                } else { // next char
                    current << c;
                    index += 1;
                }
            }
        }

        push();

        return result;
    }

    std::vector<std::string> Platform::parseLinkerDriverArgument(std::string_view argument) {
        // custom parser here, probably many bugs
        std::vector<std::string> result;

        if (argument.size() >= 4 && argument.substr(0, 4) == "-Wl,") {
            // there are escape commas but im solving that later
            size_t index = 4;
            std::stringstream current;

            auto push = [&]() {
                auto str = current.str();

                if (!str.empty()) {
                    result.push_back(str);
                    current.str("");
                }
            };

            while (index < argument.size()) {
                if (argument.size() > index + 1 && argument.substr(index, 2) == "\\,") {
                    current << ',';
                    index += 2;
                } else if (argument[index] == ',') {
                    push();
                    index += 1;
                } else {
                    current << argument[index];
                    index += 1;
                }
            }

            push();
        } else {
            result.emplace_back(argument);
        }

        return result;
    }

    std::vector<std::string> Platform::parseDirectoriesFromCompileArgument(std::string_view argument) {
        if (argument.size() > 2 && argument.substr(0, 2) == "-I") {
            return { std::string(argument.substr(2)) };
        }

        return {};
    }

    std::vector<std::string> Platform::defaultLinkerArguments() { return {}; }

    Platform::Platform(std::string root, std::string triple, BuildLockFile &lock)
        : root(std::move(root))
        , triple(std::move(triple))
        , lock(lock) { }

    std::vector<std::string> MacOSPlatform::defaultLinkerArguments() {
        llvm::Triple tripleInfo(this->triple);

        llvm::VersionTuple version;
        tripleInfo.getMacOSXVersion(version);

        auto versionText = fmt::format("{}.{}.{}",
            version.getMajor(), version.getMinor().getValueOr(0), version.getSubminor().getValueOr(0));

        std::string sysroot;

        auto sysrootIt = lock.parameters.find("macos-sysroot");
        if (sysrootIt != lock.parameters.end())
            sysroot = sysrootIt->second;

        if (sysroot.empty() || !fs::exists(sysroot)) {
            // xcrun --sdk macosx --show-sdk-path
            auto [status, buffer] = invokeCLIWithStdOut("xcrun", { root, "-sdk", "macosx", "--show-sdk-path" });

            if (status == 0) {
                std::string result(buffer.begin(), buffer.end());

                lock.parameters["macos-sysroot"] = result;

                sysroot = result;
            }
        }

        sysroot = std::string(trimString(sysroot));

        std::vector<std::string> result = {
            "-arch",
            tripleInfo.getArchName().str(),
            "-platform_version",
            "macos",
            versionText,
            versionText,
        };

        if (sysroot.empty()) {
            log(LogSource::platform, "XCode sysroot cannot be found, link will probably fail. are devtools installed?");
        } else {
            result.insert(result.end(),
                {
                    "-syslibroot",
                    sysroot,
                    fmt::format("-L{}/usr/lib", sysroot), // can we drop?
                    "-lSystem",
                });
        }

        return result;
    }

    MacOSPlatform::MacOSPlatform(std::string root, std::string triple, BuildLockFile &lock)
        : Platform(std::move(root), std::move(triple), lock) { }
}
