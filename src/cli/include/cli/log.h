#pragma once

#include <fmt/color.h>
#include <fmt/printf.h>

#include <optional>

namespace kara::cli {
    struct LogSource {
        std::string name;
        fmt::text_style header;
        fmt::text_style body;

        static LogSource package;
        static LogSource target;
        static LogSource targetStart;
        static LogSource targetDone;
        static LogSource compileC;
        static LogSource compileKara;
        static LogSource error;
        static LogSource platform;
    };

    extern bool enableLogging; // sorry for the global

    void setLogging(bool value, const std::function<void()> &scope);

    bool logHeader(const LogSource &source);

    template <typename... Args>
    void log(const LogSource &source, const char *format, Args &&...args) {
        if (logHeader(source)) {
            fmt::print(source.body, format, args...);
            fmt::print("\n");
        }
    }
}
