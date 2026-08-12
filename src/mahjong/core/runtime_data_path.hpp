#ifndef MAHJONG_CPP_RUNTIME_DATA_PATH_H
#define MAHJONG_CPP_RUNTIME_DATA_PATH_H

#include <filesystem>
#include <system_error>

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <windows.h>
#elif defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <unistd.h>
#elif defined(__APPLE__) && !defined(__EMSCRIPTEN__)
#include <mach-o/dyld.h>
#endif

namespace mahjong::detail
{

inline std::filesystem::path runtime_data_directory()
{
#ifdef __EMSCRIPTEN__
    return "/mahjong-data";
#elif defined(_WIN32)
    std::wstring path(32768, L'\0');
    const DWORD length =
        GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length != 0 && length < path.size()) {
        path.resize(length);
        return std::filesystem::path(path).parent_path();
    }
#elif defined(__linux__)
    std::string path(4096, '\0');
    const ssize_t length = readlink("/proc/self/exe", path.data(), path.size());
    if (length > 0 && static_cast<size_t>(length) < path.size()) {
        path.resize(static_cast<size_t>(length));
        return std::filesystem::path(path).parent_path();
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) == 0) {
        return std::filesystem::weakly_canonical(path).parent_path();
    }
#endif
    std::error_code error;
    const std::filesystem::path current = std::filesystem::current_path(error);
    if (!error) {
        return current;
    }
    return ".";
}

} // namespace mahjong::detail

#endif // MAHJONG_CPP_RUNTIME_DATA_PATH_H
