/*
 * Copyright © 2015 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Andreas Pokorny <andreas.pokorny@canonical.com>
 */

#include "platform.h"
#include "guest_platform.h"
#include "linux_virtual_terminal.h"
#include "mir/options/program_option.h"
#include "mir/options/option.h"
#include "mir/udev/wrapper.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <dlfcn.h>

namespace mg = mir::graphics;
namespace mgm = mg::mesa;
namespace mo = mir::options;

namespace
{
char const* bypass_option_name{"bypass"};
char const* vt_option_name{"vt"};

struct RealVTFileOperations : public mgm::VTFileOperations
{
    int open(char const* pathname, int flags)
    {
        return ::open(pathname, flags);
    }

    int close(int fd)
    {
        return ::close(fd);
    }

    int ioctl(int d, int request, int val)
    {
        return ::ioctl(d, request, val);
    }

    int ioctl(int d, int request, void* p_val)
    {
        return ::ioctl(d, request, p_val);
    }

    int tcsetattr(int d, int acts, const struct termios *tcattr)
    {
        return ::tcsetattr(d, acts, tcattr);
    }

    int tcgetattr(int d, struct termios *tcattr)
    {
        return ::tcgetattr(d, tcattr);
    }
};

struct RealPosixProcessOperations : public mgm::PosixProcessOperations
{
    pid_t getpid() const override
    {
        return ::getpid();
    }
    pid_t getppid() const override
    {
        return ::getppid();
    }
    pid_t getpgid(pid_t process) const override
    {
        return ::getpgid(process);
    }
    pid_t getsid(pid_t process) const override
    {
        return ::getsid(process);
    }
    int setpgid(pid_t process, pid_t group) override
    {
        return ::setpgid(process, group);
    }
    pid_t setsid() override
    {
        return ::setsid();
    }
};

// Hack around the way mesa loads mir: This hack makes the
// necessary symbols global.
void ensure_loaded_with_rtld_global()
{
    Dl_info info;
    dladdr(reinterpret_cast<void*>(&ensure_loaded_with_rtld_global), &info);
    dlopen(info.dli_fname,  RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
}

}

std::shared_ptr<mg::Platform> create_host_platform(
    std::shared_ptr<mo::Option> const& options,
    std::shared_ptr<mir::EmergencyCleanupRegistry> const& emergency_cleanup_registry,
    std::shared_ptr<mg::DisplayReport> const& report)
{
    auto real_fops = std::make_shared<RealVTFileOperations>();
    auto real_pops = std::unique_ptr<RealPosixProcessOperations>(new RealPosixProcessOperations{});
    auto vt = std::make_shared<mgm::LinuxVirtualTerminal>(
        real_fops,
        std::move(real_pops),
        options->get<int>(vt_option_name),
        report);

    auto bypass_option = mgm::BypassOption::allowed;
    if (!options->get<bool>(bypass_option_name))
        bypass_option = mgm::BypassOption::prohibited;

    return std::make_shared<mgm::Platform>(
        report, vt, *emergency_cleanup_registry, bypass_option);
}

void add_graphics_platform_options(boost::program_options::options_description& config)
{
    config.add_options()
        (vt_option_name,
         boost::program_options::value<int>()->default_value(0),
         "[platform-specific] VT to run on or 0 to use current.")
        (bypass_option_name,
         boost::program_options::value<bool>()->default_value(true),
         "[platform-specific] utilize the bypass optimization for fullscreen surfaces.");
}

mg::PlatformPriority probe_graphics_platform(mo::ProgramOption const& options)
{
    auto const unparsed_arguments = options.unparsed_command_line();
    auto platform_option_used = false;

    for (auto const& token : unparsed_arguments)
    {
        if (token == (std::string("--") + vt_option_name))
            platform_option_used = true;
    }

    if (options.is_set(vt_option_name))
        platform_option_used = true;

    auto udev = std::make_shared<mir::udev::Context>();

    mir::udev::Enumerator drm_devices{udev};
    drm_devices.match_subsystem("drm");
    drm_devices.match_sysname("card[0-9]*");
    drm_devices.scan_devices();

    for (auto& device : drm_devices)
    {
        static_cast<void>(device);
        if (platform_option_used)
            return mg::PlatformPriority::best;
        else
            return mg::PlatformPriority::supported;
    }

    return mg::PlatformPriority::unsupported;
}

mir::ModuleProperties const description = {
    "mesa-kms",
    MIR_VERSION_MAJOR,
    MIR_VERSION_MINOR,
    MIR_VERSION_MICRO
};

mir::ModuleProperties const* describe_graphics_module()
{
    return &description;
}

std::shared_ptr<mg::Platform> create_guest_platform(
    std::shared_ptr<mg::DisplayReport> const&,
    std::shared_ptr<mg::NestedContext> const& nested_context)
{
    ensure_loaded_with_rtld_global();
    return std::make_shared<mgm::GuestPlatform>(nested_context);
}