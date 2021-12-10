#include <cli/cli.h>

#include <cli/log.h>
#include <cli/config.h>

#include <fmt/format.h>

#include <fstream>

namespace kara::cli {
    void CLICreateOptions::execute() {
        fs::path path(name);

        if (fs::exists(path))
            throw std::runtime_error(fmt::format("Directory {} already exists.", fs::absolute(path).string()));

        fs::create_directories(path);

        TargetConfig base;

        base.type = TargetType::Executable;
        base.name = name;
        base.files = { "main.kara" };

        {
            std::ofstream stream(path / "project.yaml");
            if (!stream.is_open())
                throw std::runtime_error("Failed to write project.yaml.");

            stream << base.serialize();
        }

        {
            std::ofstream stream(path / "main.kara");
            if (!stream.is_open())
                throw std::runtime_error("Failed to write project.yaml.");

            stream << fmt::format("main int => 0\n");
        }

        log(LogSource::target, "Created project {}.", name);
    }
}