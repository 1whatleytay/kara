#include "fmt/core.h"
#include <options/options.h>

#include <builder/manager.h>

#include <fmt/printf.h>

int main(int count, const char **args) {
   // try {
    std::make_unique<Manager>(Options(count, args));
   // } catch (const std::exception &e) {
   //     return 1;
   // }

    return 0;
}
