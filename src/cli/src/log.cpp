#include <cli/log.h>

namespace kara::cli {
    LogSource LogSource::package = {
        "PAC", fmt::fg(fmt::color::peach_puff)
    };
    LogSource LogSource::target = {
        "TAR", fmt::fg(fmt::color::yellow_green)
    };
    LogSource LogSource::targetStart = {
        "TAR", fmt::fg(fmt::color::yellow_green), fmt::fg(fmt::color::teal)
    };
    LogSource LogSource::targetDone = {
        "TAR", fmt::fg(fmt::color::yellow_green), fmt::fg(fmt::color::forest_green) | fmt::emphasis::bold
    };
    LogSource LogSource::compileC = {
        "C", fmt::fg(fmt::color::orange_red)
    };
    LogSource LogSource::compileKara = {
        "KARA", fmt::fg(fmt::color::orange)
    };
    LogSource LogSource::error = {
        "ERROR", fmt::fg(fmt::color::orange_red), fmt::fg(fmt::color::orange_red)
    };

    void logHeader(const LogSource &source) {
        fmt::print("[");
        fmt::print(source.header, "{:<4}", source.name);
        fmt::print("] ");
    }
}