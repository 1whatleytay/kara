#pragma once

#include <cli/lock.h>

#include <string>
#include <vector>
#include <memory>

namespace kara::cli {
    struct Platform {
        std::string root;

        std::string triple;
        BuildLockFile &lock;

        // but this is supposed to parse arguments like `-Wl,-rpath,...` passed to clang linker
        virtual std::vector<std::string> parseCLIArgumentsFromString(std::string_view input);
        virtual std::vector<std::string> parseLinkerDriverArgument(std::string_view argument);
        virtual std::vector<std::string> parseDirectoriesFromCompileArgument(std::string_view argument);

        virtual std::vector<std::string> defaultLinkerArguments();

        static std::unique_ptr<Platform> byNative(std::string root, BuildLockFile &lock);
        static std::unique_ptr<Platform> byTriple(std::string root, std::string name, BuildLockFile &lock);

        explicit Platform(std::string root, std::string triple, BuildLockFile &lock);
        virtual ~Platform() = default;
    };

    struct MacOSPlatform : public Platform {
        std::vector<std::string> defaultLinkerArguments() override;

        explicit MacOSPlatform(std::string root, std::string triple, BuildLockFile &lock);
    };
}
