#pragma once

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

namespace pugi {
    struct xml_node;
}

namespace kara::cli {
    // Light approach, but I want something like this in the PM
    bool cbpTargetNameIsSimilar(std::string targetName, std::string projectName);

    struct CBPTarget {
        std::string name;

        bool fast = false;

        std::string output;
        std::vector<std::string> includes;

        std::string workingDirectory;
        std::unordered_map<std::string, std::string> commands;

        explicit CBPTarget(const pugi::xml_node &node);
    };

    struct CBPProject {
        std::string name;
        std::vector<CBPTarget> targets;

        static std::optional<CBPProject> loadFrom(const std::string &path);
        static CBPProject loadFromThrows(const std::string &path);

        explicit CBPProject(const pugi::xml_node &node);
    };
}