#include <options/options.h>

#include <builder/manager.h>

int main(int count, const char **args) {
     try {
         kara::options::Options options(count, args);
//         std::make_unique<kara::builder::Manager>(options);
     } catch (const std::exception &e) {
         return 1;
     }

    return 0;
}
