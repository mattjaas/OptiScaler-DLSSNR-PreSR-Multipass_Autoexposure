#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>

namespace DlssNr::EditDump
{
// Capture-only CPU work. CREATE_NEW owns every output; retries remove only this
// attempt's files. A collision in any companion advances the whole bundle name.
class Files
{
    void Close(bool keep)
    {
        for (size_t i = 0; i < files.size(); ++i)
        {
            CloseHandle(files[i]);
            if (!keep)
                DeleteFileW(paths[i].c_str());
        }
        files.clear();
    }

  public:
    std::vector<std::filesystem::path> paths;
    std::vector<HANDLE> files;
    DWORD error = ERROR_SUCCESS;
    Files() = default;
    Files(const Files&) = delete;
    Files& operator=(const Files&) = delete;
    ~Files() { Close(false); }
    void Finish(bool keep) { Close(keep); }
    bool Reserve(const std::filesystem::path& original, const std::vector<std::string>& extensions)
    {
        Close(false);
        error = ERROR_SUCCESS;
        for (unsigned attempt = 0; attempt < 10000; ++attempt)
        {
            auto image = original;
            if (attempt)
            {
                auto number = std::to_wstring(attempt);
                if (number.size() < 3)
                    number.insert(0, 3 - number.size(), L'0');
                image = original.parent_path() /
                        (original.stem().wstring() + L"-" + number + original.extension().wstring());
            }
            bool occupied = false;
            // Also avoid orphan companions when this capture does not request guides.
            for (const char* suffix : { ".dds", ".depth.dds", ".motion.dds", ".json" })
            {
                const auto candidate = std::filesystem::path(image).replace_extension(suffix);
                if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
                {
                    occupied = true;
                    break;
                }
                const auto e = GetLastError();
                if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND)
                {
                    error = e;
                    return false;
                }
            }
            if (occupied)
                continue;
            paths.clear();
            paths.push_back(image);
            for (const auto& ext : extensions)
                paths.push_back(std::filesystem::path(image).replace_extension(ext));
            for (const auto& path : paths)
            {
                const auto file =
                    CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file == INVALID_HANDLE_VALUE)
                {
                    error = GetLastError();
                    break;
                }
                files.push_back(file);
            }
            if (files.size() == paths.size())
            {
                error = ERROR_SUCCESS;
                return true;
            }
            Close(false);
            if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
                return false;
        }
        error = ERROR_FILE_EXISTS;
        return false;
    }
};
} // namespace DlssNr::EditDump
