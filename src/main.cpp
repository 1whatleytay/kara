#include <options/options.h>

#include <builder/manager.h>

int main(int count, const char **args) {
    // try {
    std::make_unique<kara::builder::Manager>(kara::options::Options(count, args));
    // } catch (const std::exception &e) {
    //     return 1;
    // }

    return 0;
}
