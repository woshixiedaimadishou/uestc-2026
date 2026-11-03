#include "dataguard/win_metadata.h"

#include <windows.h>
#include <aclapi.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "dataguard/path_utils.h"

namespace dataguard {
namespace {

// FILETIME（1601 起 100ns）→ Unix 秒（1970 起）
int64_t fileTimeToUnix(const FILETIME& ft) {
    ULARGE_INTEGER ul;
    ul.LowPart = ft.dwLowDateTime;
    ul.HighPart = ft.dwHighDateTime;
    if (ul.QuadPart == 0) return 0;
    return static_cast<int64_t>(ul.QuadPart / 10000000ULL) - 11644473600LL;
}

// Unix 秒 → FILETIME
FILETIME unixToFileTime(int64_t unix_seconds) {
    ULARGE_INTEGER ul;
    ul.QuadPart = static_cast<ULONGLONG>(unix_seconds + 11644473600LL) * 10000000ULL;
    FILETIME ft;
    ft.dwLowDateTime = ul.LowPart;
    ft.dwHighDateTime = ul.HighPart;
    return ft;
}

std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) return std::string();
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        out.data(), size, nullptr, nullptr);
    return out;
}

// 以「链接本身」语义打开（不跟随符号链接），用于读取链接自身的属性/时间
HANDLE openForAttributes(const std::filesystem::path& p) {
    return CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       nullptr, OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
}

// volume serial + file index → 64 位稳定 ID（FNV-1a 混合）
uint64_t makeFileId(const BY_HANDLE_FILE_INFORMATION& info) {
    const uint64_t volume = info.dwVolumeSerialNumber;
    const uint64_t index = (static_cast<uint64_t>(info.nFileIndexHigh) << 32) |
                           static_cast<uint64_t>(info.nFileIndexLow);
    uint64_t hash = volume ^ (index + 0x9e3779b97f4a7c15ULL + (volume << 6) + (volume >> 2));
    hash ^= index;
    return hash;
}

// SID → "DOMAIN\user" 的进程级缓存。
// 一个目录树里的文件通常只属于少数几个 SID，逐个调用 LookupAccountSidW
// 会走 LSA，实测每次约 0.23ms，两万多个文件就是 5 秒以上。
std::mutex g_owner_mutex;
std::unordered_map<std::string, std::string> g_owner_cache;

// 用 SID 的二进制内容做缓存键（不依赖字符串转换）
std::string sidKey(PSID sid) {
    const auto* s = static_cast<const SID*>(sid);
    if (s == nullptr) return std::string();
    const DWORD count = *GetSidSubAuthorityCount(const_cast<PSID>(sid));
    std::string key;
    key.reserve(1 + sizeof(SID_IDENTIFIER_AUTHORITY) + 1 + count * sizeof(DWORD));
    key.push_back(static_cast<char>(s->Revision));
    key.append(reinterpret_cast<const char*>(&s->IdentifierAuthority),
               sizeof(SID_IDENTIFIER_AUTHORITY));
    key.push_back(static_cast<char>(count));
    for (DWORD i = 0; i < count; ++i) {
        const DWORD value = *GetSidSubAuthority(const_cast<PSID>(sid), i);
        key.append(reinterpret_cast<const char*>(&value), sizeof(value));
    }
    return key;
}

std::string lookupSidName(PSID sid) {
    wchar_t name[257] = {};
    wchar_t domain[257] = {};
    DWORD name_len = 257;
    DWORD domain_len = 257;
    SID_NAME_USE use = SidTypeUnknown;
    std::string result;
    if (LookupAccountSidW(nullptr, sid, name, &name_len, domain, &domain_len, &use)) {
        std::wstring full;
        if (domain_len > 0) {
            full.append(domain, domain_len);
            full.push_back(L'\\');
        }
        full.append(name, name_len);
        result = wideToUtf8(full);
    }
    return result;
}

std::string queryOwner(const std::filesystem::path& p) {
    PSID sid = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    const DWORD rc = GetNamedSecurityInfoW(
        const_cast<LPWSTR>(p.c_str()), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
        &sid, nullptr, nullptr, nullptr, &sd);
    if (rc != ERROR_SUCCESS || sid == nullptr) return std::string();

    const std::string key = sidKey(sid);
    {
        std::lock_guard<std::mutex> lock(g_owner_mutex);
        const auto it = g_owner_cache.find(key);
        if (it != g_owner_cache.end()) {
            LocalFree(sd);
            return it->second;
        }
    }

    const std::string name = lookupSidName(sid);
    {
        std::lock_guard<std::mutex> lock(g_owner_mutex);
        g_owner_cache.emplace(key, name);
    }
    LocalFree(sd);
    return name;
}

}  // namespace

bool readFileMetadata(const std::filesystem::path& path,
                      FileEntry& entry,
                      std::string* error) {
    std::error_code ec;
    entry.abs_path = std::filesystem::absolute(path, ec);
    if (ec) entry.abs_path = path;

    // 只开一次句柄：属性、三个时间、大小、硬链接数、文件 ID 一次拿全。
    // 原实现是 symlink_status + file_size + CreateFileW 三次系统调用，两万多个文件时开销明显。
    HANDLE handle = openForAttributes(path);
    if (handle != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION info = {};
        const bool got = GetFileInformationByHandle(handle, &info) != 0;
        CloseHandle(handle);

        if (got) {
            entry.attrs = info.dwFileAttributes;
            entry.hard_link_count = info.nNumberOfLinks;
            entry.file_id = makeFileId(info);
            entry.ctime = fileTimeToUnix(info.ftCreationTime);
            entry.atime = fileTimeToUnix(info.ftLastAccessTime);
            entry.mtime = fileTimeToUnix(info.ftLastWriteTime);

            if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                entry.kind = FileKind::Directory;
                entry.size = 0;
            } else if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                // 重解析点里只有真正的符号链接按链接处理，其余（junction/占位符）记为 Other
                std::error_code link_ec;
                const auto status = std::filesystem::symlink_status(path, link_ec);
                if (!link_ec && std::filesystem::is_symlink(status)) {
                    entry.kind = FileKind::Symlink;
                    std::error_code target_ec;
                    const auto target = std::filesystem::read_symlink(path, target_ec);
                    if (!target_ec) entry.link_target_utf8 = pathToUtf8(target);
                    entry.size = 0;
                } else {
                    entry.kind = FileKind::Other;
                    entry.size = 0;
                }
            } else {
                entry.kind = FileKind::File;
                entry.size = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) |
                             static_cast<uint64_t>(info.nFileSizeLow);
            }

            entry.owner = queryOwner(path);
            return true;
        }
    }

    // 回退路径：句柄拿不到时（权限不足、特殊文件）退回 filesystem 查询
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec || status.type() == std::filesystem::file_type::not_found) {
        if (error) *error = "无法读取文件状态: " + pathToUtf8(path);
        return false;
    }

    if (std::filesystem::is_symlink(status)) {
        entry.kind = FileKind::Symlink;
        std::error_code link_ec;
        const auto target = std::filesystem::read_symlink(path, link_ec);
        if (!link_ec) entry.link_target_utf8 = pathToUtf8(target);
        entry.size = 0;
    } else if (std::filesystem::is_directory(status)) {
        entry.kind = FileKind::Directory;
        entry.size = 0;
    } else if (std::filesystem::is_regular_file(status)) {
        entry.kind = FileKind::File;
        std::error_code size_ec;
        const auto sz = std::filesystem::file_size(path, size_ec);
        entry.size = size_ec ? 0 : static_cast<uint64_t>(sz);
    } else {
        entry.kind = FileKind::Other;
        entry.size = 0;
    }

    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) entry.attrs = attrs;

    entry.owner = queryOwner(path);
    return true;
}

bool applyFileMetadata(const std::filesystem::path& path,
                       const FileEntry& entry,
                       std::string* error) {
    bool ok = true;

    if (entry.attrs != 0) {
        if (SetFileAttributesW(path.c_str(), entry.attrs) == 0) {
            ok = false;
            if (error) *error = "设置文件属性失败: " + pathToUtf8(path);
        }
    }

    HANDLE handle = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error && ok) *error = "打开文件以设置时间失败: " + pathToUtf8(path);
        return false;
    }

    FILETIME creation = unixToFileTime(entry.ctime);
    FILETIME access = unixToFileTime(entry.atime);
    FILETIME write = unixToFileTime(entry.mtime);
    if (SetFileTime(handle, &creation, &access, &write) == 0) {
        ok = false;
        if (error) *error = "设置文件时间失败: " + pathToUtf8(path);
    }
    CloseHandle(handle);
    return ok;
}

uint64_t getFileId(const std::filesystem::path& path) {
    HANDLE handle = openForAttributes(path);
    if (handle == INVALID_HANDLE_VALUE) return 0;

    BY_HANDLE_FILE_INFORMATION info = {};
    uint64_t id = 0;
    if (GetFileInformationByHandle(handle, &info)) {
        id = makeFileId(info);
    }
    CloseHandle(handle);
    return id;
}

}  // namespace dataguard
