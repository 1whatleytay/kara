#include <cli/log.h>

namespace kara::cli {
    bool loggingEnabled = true;

    LogSource LogSource::package = { "PAC", fmt::fg(fmt::color::coral) };
    LogSource LogSource::target = { "TAR", fmt::fg(fmt::color::yellow_green) };
    LogSource LogSource::targetStart = { "TAR", fmt::fg(fmt::color::yellow_green), fmt::fg(fmt::color::teal) };
    LogSource LogSource::targetDone
        = { "TAR", fmt::fg(fmt::color::yellow_green), fmt::fg(fmt::color::forest_green) | fmt::emphasis::bold };
    LogSource LogSource::compileC = { "C", fmt::fg(fmt::color::orange_red) };
    LogSource LogSource::compileKara = { "KARA", fmt::fg(fmt::color::orange) };
    LogSource LogSource::error = { "ERROR", fmt::fg(fmt::color::orange_red), fmt::fg(fmt::color::orange_red) };
    LogSource LogSource::platform = { "PLAT", fmt::fg(fmt::color::pale_green) };

    void setLogging(bool value, const std::function<void()> &scope) {
        bool save = loggingEnabled;

        loggingEnabled = value;

        scope();

        loggingEnabled = save;
    }

    bool logHeader(const LogSource &source) {
        if (loggingEnabled) {
            fmt::print("[");
            fmt::print(source.header, "{:<4}", source.name);
            fmt::print("] ");
        }

        return loggingEnabled;
    }
}
