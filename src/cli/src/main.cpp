#include <options/options.h>

#include <cli/cli.h>

#include <memory>

int main(int count, const char **args) {
    std::make_unique<kara::cli::CLIOptions>(count, args);

    return 0;
}
