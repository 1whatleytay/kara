#include <cli/cbp.h>

#include <cli/utility.h>

#include <pugixml.hpp>

#include <fmt/format.h>

#include <array>
#include <regex>
#include <cassert>
#include <fstream>

namespace kara::cli {
    bool cbpTargetNameIsSimilar(std::string targetName, std::string projectName) {
        auto toLower = [](std::string &r) {
            for (char &c : r) {
                c = static_cast<char>(std::tolower(c));
            }
        };

        toLower(targetName);
        toLower(projectName);

        /* Common Patterns, $ = projectName
         * $
         * $-static
         * $-shared
         * lib$
         * lib-$
         * $-lib
         */

        std::array prefix = { "", "lib", "lib-", "lib_" };
        std::array postfix = { "", "_static", "-static", "_shared", "-shared", "lib", "_lib", "-lib" };

        // Really slow, but I can deal with this later.
        for (auto start : prefix) {
            size_t a = 0;

            auto comesUpNext = [&a, &targetName](const char *next) -> bool {
                auto length = strlen(next);

                if (a + length > targetName.size())
                    return false;

                bool test = targetName.substr(a, length) == next;

                a += length;

                return test;
            };

            if (!comesUpNext(start))
                continue;

            if (!comesUpNext(projectName.c_str()))
                continue;

            for (auto end : prefix) {
                // strange but consistent
                if (!comesUpNext(end))
                    continue;

                if (a == targetName.length())
                    return true;
            }
        }

        return false;
    }

    CBPTarget::CBPTarget(const pugi::xml_node &node) {
        name = node.attribute("title").value();
        assert(!name.empty());

        std::string fastText = "/fast";

        fast = fastText.size() <= name.size() && name.substr(name.size() - fastText.size()) == fastText;

        for (const auto &option : node.children("Option")) {
            if (auto value = option.attribute("output"))
                output = value.value();

            if (auto value = option.attribute("working_dir"))
                workingDirectory = value.value();
        }

        for (const auto &command : node.child("MakeCommands").children()) {
            std::string commandName = command.name();
            std::string commandValue = command.attribute("command").value();

            assert(!commandName.empty() && !commandValue.empty());

            commands[commandName] = commandValue;
        }

        for (const auto &option : node.child("Compiler").children("Add")) {
            if (auto value = option.attribute("directory")) {
                includes.emplace_back(value.value());
            }
        }
    }

    std::optional<CBPProject> CBPProject::loadFrom(const std::string &path) {
        std::ifstream stream(path);

        if (!stream.is_open())
            return std::nullopt;

        pugi::xml_document doc;
        doc.load(stream);

        return CBPProject(doc);
    }

    CBPProject CBPProject::loadFromThrows(const std::string &path) {
        auto config = loadFrom(path);

        if (!config) {
            auto absolute = fs::absolute(fs::path(path)).string();

            throw std::runtime_error(fmt::format("Cannot find config file at {}.", absolute));
        }

        return *config;
    }

    CBPProject::CBPProject(const pugi::xml_node &node) {
        auto root = node.child("CodeBlocks_project_file");
        assert(root);

        auto project = root.child("Project");
        assert(project);

        auto build = project.child("Build");
        assert(build);

        for (const auto &option : project.children("Option")) {
            if (auto title = option.attribute("title"))
                name = title.value();
        }

        assert(!name.empty());

        for (const auto &target : build.children("Target")) {
            targets.emplace_back(target);
        }
    }
}
