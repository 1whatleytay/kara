#include <cli/cli.h>

#include <cli/log.h>
#include <cli/config.h>

#include <filesystem>

namespace fs = std::filesystem;

namespace kara::cli {
    void CLICleanOptions::execute() {
        //        clang::DiagnosticsEngine engine(nullptr, nullptr);
        //
        //        clang::driver::Driver driver(root, llvm::sys::getDefaultTargetTriple(), engine);
        //
        //        auto k = clang::driver::Driver::GetResourcesPath(root);
        //
        //        llvm::Triple triple(driver.getTargetTriple());
        //
        //        auto os = triple.getOS();
        //        auto osName = triple.getOSName();
        //        uint32_t major, minor, micro;
        //        uint32_t otherMajor, otherMinor, otherMicro;
        //
        //        auto arch = triple.getArch();
        //        auto archName = triple.getArchName().str();
        //
        //        triple.getOSVersion(major, minor, micro);
        //        triple.getMacOSXVersion(otherMajor, otherMinor, otherMicro);
        //
        ////        auto triple = driver.getTargetTriple();
        //        auto host = driver.HostMachine;
        //        auto hostSys = driver.HostSystem;
        //        auto hostBits = driver.HostBits;
        //        auto hostRelease = driver.HostRelease;
        //        auto sys = driver.SysRoot;
        //
        //        throw;

        auto config = TargetConfig::loadFromThrows(projectFile);

        fs::remove_all(config.outputDirectory);

        log(LogSource::target, "Cleaned");
    }
}
