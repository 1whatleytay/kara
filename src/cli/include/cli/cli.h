#pragma once

#include <options/options.h>

#include <string>

namespace CLI {
    struct App;
}

namespace kara::cli {
    struct CLIHook {
    protected:
        CLI::App *app = nullptr;
        std::string root;

        virtual void execute() = 0;
        virtual void connect() = 0;

    public:
        void attach(CLI::App *app, std::string root);

        // I don't need this, but it's worth keeping around.
        virtual ~CLIHook() = default;
    };

    struct CLICreateOptions : public CLIHook {
        virtual void execute() override;
        virtual void connect() override;
    };

    struct CLIAddOptions : public CLIHook {
        std::string name;
        std::string url;
        std::string projectFile = "project.yaml";

        virtual void execute() override;
        virtual void connect() override;
    };

    struct CLIRemoveOptions : public CLIHook {
        virtual void execute() override;
        virtual void connect() override;
    };

    struct CLICleanOptions : public CLIHook {
        std::string projectFile = "project.yaml";

        virtual void execute() override;
        virtual void connect() override;
    };

    struct CLIRunOptions : public CLIHook {
        std::string target;
        std::string triple;
        std::string linkerType = "macho";
        std::string projectFile = "project.yaml";

        virtual void execute() override;
        virtual void connect() override;
    };

    struct CLIBuildOptions : public CLIHook {
        std::string target;
        std::string triple;
        std::string linkerType = "macho";
        std::string projectFile = "project.yaml";

        bool printIr = false;

        virtual void execute() override;
        virtual void connect() override;
    };

    struct CLICompileOptions : public CLIHook {
        kara::options::Options compileOptions;

        virtual void execute() override;
        virtual void connect() override;
    };

    struct CLIOptions {
        CLIAddOptions install;
        CLIRemoveOptions remove;
        CLICleanOptions clean;
        CLIRunOptions run;
        CLIBuildOptions build;
        CLICompileOptions compile;

        CLIOptions(int count, const char **args);
    };
}
