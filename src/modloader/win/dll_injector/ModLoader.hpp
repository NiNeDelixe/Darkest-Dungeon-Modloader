#ifndef DLL_INJECTOR_MOD_LOADER_HPP_
#define DLL_INJECTOR_MOD_LOADER_HPP_

#include <memory>
#include <mutex>

class ModLoader
{
public:
    ModLoader() = default;
    ~ModLoader() = default;

    virtual std::recursive_mutex& get_hook_monitor_mutex() = 0;

private:
    
};

extern std::unique_ptr<ModLoader> g_loader;

#endif // DLL_INJECTOR_MOD_LOADER_HPP_
