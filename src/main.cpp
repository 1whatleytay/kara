#include <options/options.h>

#include <parser/root.h>

#include <builder/error.h>
#include <builder/builder.h>

#include <fmt/printf.h>

#include <fstream>
#include <sstream>

int main(int count, const char **args) {
    try {
        Options options(count, args);

        std::string source;
        {
            std::ifstream stream(options.inputFile);

            if (!stream.is_open()) {
                fmt::print("Cannot find input file {}.\n", options.inputFile);
                return 0;
            }

            std::stringstream buffer;
            buffer << stream.rdbuf();
            source = buffer.str();
        }

        State state(source);

        try {
            RootNode root(state);

            try {
                Builder builder(&root, options);
            } catch (const VerifyError &e) {
                LineDetails details(source, e.node->index, false);

                fmt::print("{} [line {}]\n{}\n{}\n",
                    e.issue, details.lineNumber, details.line, details.marker);
            }
        } catch (const ParseError &e) {
            LineDetails details(source, e.index);

            fmt::print("{} [line {}]\n{}\n{}\n",
                e.issue, details.lineNumber, details.line, details.marker);
        }
    } catch (const OptionsError &e) {
        return 1;
    }

	return 0;
}