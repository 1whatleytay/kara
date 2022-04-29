#include <cli/utility.h>

#include <fmt/format.h>

#include <unistd.h>
#include <sys/wait.h>

#include <array>
#include <filesystem>

namespace fs = std::filesystem;

namespace kara::cli {
    // unix/posix, first is status code, second is stdout socket
    std::pair<int, int> invokeCLIWithSocket(
        const std::string &program, std::vector<std::string> arguments, const std::string &currentDirectory) {
        std::vector<char *> cstrings;
        cstrings.reserve(arguments.size());

        for (auto &arg : arguments)
            cstrings.push_back(arg.data());

        cstrings.push_back(nullptr);

        std::array<int, 2> fd = {};

        if (pipe(fd.data()))
            throw std::runtime_error(fmt::format("Cannot create pipe for invokeCLI (invoking {}).", program));

        auto in = fd[0];
        auto out = fd[1];

        // POSIX/Unix?
        auto process = fork();

        if (!process) {
            dup2(out, STDOUT_FILENO);
            close(in);
            close(out);

            if (!currentDirectory.empty())
                chdir(fs::absolute(currentDirectory).string().c_str());

            execvp(program.c_str(), cstrings.data());

            exit(1);
        }

        close(out);

        int status {};
        waitpid(process, &status, 0);

        return { status, in };
    }

    std::pair<int, std::vector<uint8_t>> invokeCLIWithStdOut(
        const std::string &program, std::vector<std::string> arguments, const std::string &currentDirectory) {
        auto [status, socket] = invokeCLIWithSocket(program, std::move(arguments), currentDirectory);

        std::vector<uint8_t> result;
        std::array<uint8_t, 8096> buffer = {};

        ssize_t bytes = read(socket, buffer.data(), buffer.size());
        while (bytes > 0 && bytes <= buffer.size()) {
            result.insert(result.end(), buffer.begin(), buffer.begin() + bytes);

            bytes = read(socket, buffer.data(), buffer.size());
        }

        close(socket);

        return { status, std::move(result) };
    }

    int invokeCLI(const std::string &program, std::vector<std::string> arguments, const std::string &currentDirectory) {
        auto [status, socket] = invokeCLIWithSocket(program, std::move(arguments), currentDirectory);

        close(socket);

        return status;
    }
}
