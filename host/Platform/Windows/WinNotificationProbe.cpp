#include "WinNotificationProbe.h"

#include <windows.h>
#include <unknwn.h>  // before C++/WinRT: classic COM interop
#include <winsqlite/winsqlite3.h>

#include <winrt/Windows.ApplicationModel.h>  // AppInfo of a UserNotification
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.Management.h>
#include <winrt/Windows.UI.Notifications.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "ToastText.h"
#include "WinStrings.h"

namespace stm32_audio::host::win {

namespace {

using winrt::Windows::UI::Notifications::KnownNotificationBindings;
using winrt::Windows::UI::Notifications::NotificationKinds;
using winrt::Windows::UI::Notifications::UserNotification;
using winrt::Windows::UI::Notifications::Management::UserNotificationListener;
using winrt::Windows::UI::Notifications::Management::UserNotificationListenerAccessStatus;

std::string hresultText(const winrt::hresult_error& e)
{
    char code[16];
    std::snprintf(code, sizeof(code), "0x%08X", static_cast<unsigned>(e.code()));
    return std::string(code) + " " + winrt::to_string(e.message());
}

const char* accessName(UserNotificationListenerAccessStatus s)
{
    switch (s) {
    case UserNotificationListenerAccessStatus::Allowed:
        return "Allowed";
    case UserNotificationListenerAccessStatus::Denied:
        return "Denied";
    default:
        return "Unspecified";
    }
}

std::string localTimeText(std::int64_t filetime);
std::int64_t filetimeNow();

// A toast as the listener gives it.
struct ListenerToast {
    std::uint32_t id = 0;
    std::int64_t created = 0;  // FILETIME
    std::string app;           // display name
    std::string aumid;         // AppUserModelId
    std::vector<std::string> texts;
};

ListenerToast listenerToast(const UserNotification& n)
{
    ListenerToast t;
    t.id = n.Id();
    t.created = static_cast<std::int64_t>(winrt::clock::to_file_time(n.CreationTime()).value);
    try {
        t.app = winrt::to_string(n.AppInfo().DisplayInfo().DisplayName());
        t.aumid = winrt::to_string(n.AppInfo().AppUserModelId());
    } catch (const winrt::hresult_error&) {
        t.app = "?";
    }
    try {
        const auto binding = n.Notification().Visual().GetBinding(KnownNotificationBindings::ToastGeneric());
        if (binding) {
            for (const auto& text : binding.GetTextElements()) {
                t.texts.push_back(winrt::to_string(text.Text()));
            }
        }
    } catch (const winrt::hresult_error&) {
        t.texts.push_back("(texts: FAIL)");
    }
    return t;
}

void printListenerToast(const ListenerToast& t, const PrintFn& print, const char* tag,
                        std::optional<std::int64_t> seenAt = std::nullopt)
{
    std::string line = std::string(tag) + localTimeText(t.created) + "  id=" + std::to_string(t.id) + "  app=" + t.app +
                       "  aumid=" + t.aumid;
    if (seenAt) {
        line += "  delay_ms=" + std::to_string((*seenAt - t.created) / 10000);
    }
    print(line);
    for (const std::string& text : t.texts) {
        print("    " + text);
    }
}

// Way 1: the documented listener.
void probeListener(const PrintFn& print)
{
    print("== 1. UserNotificationListener");
    try {
        UserNotificationListener listener = UserNotificationListener::Current();
        print("Current: OK");
        print(std::string("GetAccessStatus: ") + accessName(listener.GetAccessStatus()));
        try {
            const auto asked = listener.RequestAccessAsync().get();
            print(std::string("RequestAccessAsync: ") + accessName(asked));
        } catch (const winrt::hresult_error& e) {
            print("RequestAccessAsync: FAIL " + hresultText(e));
        }
        try {
            const auto list = listener.GetNotificationsAsync(NotificationKinds::Toast).get();
            print("GetNotificationsAsync: " + std::to_string(list.Size()) + " toast(s)");
            for (const auto& n : list) {
                printListenerToast(listenerToast(n), print, "  ");
            }
        } catch (const winrt::hresult_error& e) {
            print("GetNotificationsAsync: FAIL " + hresultText(e));
        }
    } catch (const winrt::hresult_error& e) {
        print("Current: FAIL " + hresultText(e));
    } catch (...) {
        print("FAIL: unknown exception");
    }
}

std::wstring databasePath()
{
    wchar_t buf[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    return std::wstring(buf) + L"\\Microsoft\\Windows\\Notifications\\wpndatabase.db";
}

// The database and its WAL copied to %TEMP%, for when the live file cannot
// be opened read-only next to the notification service.
std::wstring copyDatabase(const std::wstring& source)
{
    wchar_t temp[MAX_PATH];
    if (GetTempPathW(MAX_PATH, temp) == 0) {
        return {};
    }
    const std::wstring target = std::wstring(temp) + L"stm32-desk-dashboard-wpn.db";
    if (!CopyFileW(source.c_str(), target.c_str(), FALSE)) {
        return {};
    }
    (void)CopyFileW((source + L"-wal").c_str(), (target + L"-wal").c_str(), FALSE);
    DeleteFileW((target + L"-shm").c_str());
    return target;
}

struct Toast {
    std::int64_t id = 0;
    std::int64_t arrival = 0;  // FILETIME
    std::string app;
    std::vector<std::string> texts;
};

class Database {
public:
    ~Database()
    {
        if (db_ != nullptr) {
            sqlite3_close(db_);
        }
    }

    bool open(const std::wstring& path, bool readOnly, const PrintFn& print)
    {
        const std::string utf8 = toUtf8(path.c_str());
        const int rc = sqlite3_open_v2(utf8.c_str(), &db_, readOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE,
                                       nullptr);
        if (rc != SQLITE_OK) {
            print("open: FAIL " + std::string(db_ != nullptr ? sqlite3_errmsg(db_) : sqlite3_errstr(rc)));
            if (db_ != nullptr) {
                sqlite3_close(db_);
                db_ = nullptr;
            }
            return false;
        }
        sqlite3_busy_timeout(db_, 1000);
        return true;
    }

    // Names and SQL of the tables, to learn the schema of this Windows.
    void printSchema(const PrintFn& print)
    {
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT name, sql FROM sqlite_master WHERE type='table' ORDER BY name", -1, &st,
                               nullptr) != SQLITE_OK) {
            print("schema: FAIL " + std::string(sqlite3_errmsg(db_)));
            return;
        }
        while (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char* sql = sqlite3_column_text(st, 1);
            print("  " + std::string(sql != nullptr ? reinterpret_cast<const char*>(sql) : ""));
        }
        sqlite3_finalize(st);
    }

    // Toasts newer than @p afterId, newest first, at most @p limit.
    std::optional<std::vector<Toast>> toasts(std::int64_t afterId, unsigned limit, const PrintFn& print)
    {
        static const char* const sql =
            "SELECT n.Id, n.ArrivalTime, h.PrimaryId, n.Payload FROM Notification n "
            "LEFT JOIN NotificationHandler h ON h.RecordId = n.HandlerId "
            "WHERE n.Type = 'toast' AND n.Id > ?1 ORDER BY n.Id DESC LIMIT ?2";
        sqlite3_stmt* st = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) {
            print("query: FAIL " + std::string(sqlite3_errmsg(db_)));
            return std::nullopt;
        }
        sqlite3_bind_int64(st, 1, afterId);
        sqlite3_bind_int(st, 2, static_cast<int>(limit));
        std::vector<Toast> out;
        int rc = SQLITE_ROW;
        while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
            Toast t;
            t.id = sqlite3_column_int64(st, 0);
            t.arrival = sqlite3_column_int64(st, 1);
            const unsigned char* app = sqlite3_column_text(st, 2);
            t.app = app != nullptr ? reinterpret_cast<const char*>(app) : "";
            const void* payload = sqlite3_column_blob(st, 3);
            const int bytes = sqlite3_column_bytes(st, 3);
            if (payload != nullptr && bytes > 0) {
                t.texts = toastTexts(std::string_view(static_cast<const char*>(payload), static_cast<std::size_t>(bytes)));
            }
            out.push_back(std::move(t));
        }
        if (rc != SQLITE_DONE) {
            print("query: FAIL " + std::string(sqlite3_errmsg(db_)));
        }
        sqlite3_finalize(st);
        return out;
    }

private:
    sqlite3* db_ = nullptr;
};

std::string localTimeText(std::int64_t filetime)
{
    FILETIME ft{};
    ft.dwLowDateTime = static_cast<DWORD>(static_cast<std::uint64_t>(filetime) & 0xFFFFFFFFULL);
    ft.dwHighDateTime = static_cast<DWORD>(static_cast<std::uint64_t>(filetime) >> 32);
    FILETIME local{};
    SYSTEMTIME st{};
    if (!FileTimeToLocalFileTime(&ft, &local) || !FileTimeToSystemTime(&local, &st)) {
        return "?";
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u", static_cast<unsigned>(st.wYear),
                  static_cast<unsigned>(st.wMonth), static_cast<unsigned>(st.wDay), static_cast<unsigned>(st.wHour),
                  static_cast<unsigned>(st.wMinute), static_cast<unsigned>(st.wSecond));
    return buf;
}

std::int64_t filetimeNow()
{
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    return static_cast<std::int64_t>((static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime);
}

void printToast(const Toast& t, const PrintFn& print, std::optional<std::int64_t> seenAt = std::nullopt)
{
    std::string line = localTimeText(t.arrival) + "  id=" + std::to_string(t.id) + "  app=" + t.app;
    if (seenAt) {
        line += "  delay_ms=" + std::to_string((*seenAt - t.arrival) / 10000);
    }
    print(line);
    for (const std::string& text : t.texts) {
        print("    " + text);
    }
}

// Open the live database read-only, else a copy; report which.
bool openDatabase(Database& db, const PrintFn& print, bool quiet)
{
    const std::wstring path = databasePath();
    if (path.empty()) {
        print("FAIL: %LOCALAPPDATA% not set");
        return false;
    }
    if (!quiet) {
        print("path: " + toUtf8(path.c_str()));
        print(std::string("exists: ") + (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES ? "yes" : "no"));
    }
    if (db.open(path, true, print)) {
        if (!quiet) {
            print("open: OK (live file, read-only)");
        }
        return true;
    }
    const std::wstring copy = copyDatabase(path);
    if (!copy.empty() && db.open(copy, false, print)) {
        print("open: OK (a copy in %TEMP%)");
        return true;
    }
    return false;
}

}  // namespace

int runNotificationProbe(unsigned count, const PrintFn& print)
{
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error& e) {
        print("init_apartment: " + hresultText(e));  // COM already set up on this thread: go on
    }
    probeListener(print);

    print("== 2. Notification database");
    Database db;
    if (!openDatabase(db, print, false)) {
        return 1;
    }
    print("tables:");
    db.printSchema(print);
    const std::optional<std::vector<Toast>> toasts = db.toasts(0, count, print);
    if (!toasts) {
        return 1;
    }
    print("last " + std::to_string(toasts->size()) + " toast(s):");
    for (const Toast& t : *toasts) {
        printToast(t, print);
    }
    return 0;
}

int runNotificationWatch(const PrintFn& print)
{
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error& e) {
        print("init_apartment: " + hresultText(e));
    }

    // The listener: what is there now, then polled every 500 ms; and its
    // NotificationChanged event, if an unpackaged program may have it.
    std::optional<UserNotificationListener> listener;
    std::set<std::uint32_t> listenerSeen;
    std::atomic<int> events{0};
    winrt::event_token token{};
    try {
        listener = UserNotificationListener::Current();
        for (const auto& n : listener->GetNotificationsAsync(NotificationKinds::Toast).get()) {
            listenerSeen.insert(n.Id());
        }
        print("listener: " + std::to_string(listenerSeen.size()) + " toast(s) now");
        try {
            token = listener->NotificationChanged([&print, &events](const auto&, const auto& args) {
                ++events;
                print("[event] NotificationChanged id=" + std::to_string(args.UserNotificationId()) +
                      " kind=" + std::to_string(static_cast<int>(args.ChangeKind())));
            });
            print("NotificationChanged: subscribed");
            (void)token;  // kept for as long as the probe runs
        } catch (const winrt::hresult_error& e) {
            print("NotificationChanged: FAIL " + hresultText(e));
        }
    } catch (const winrt::hresult_error& e) {
        print("listener: FAIL " + hresultText(e));
        listener.reset();
    }

    std::int64_t lastId = 0;
    {
        Database db;
        if (!openDatabase(db, print, false)) {
            return 1;
        }
        const std::optional<std::vector<Toast>> newest = db.toasts(0, 1, print);
        if (!newest) {
            return 1;
        }
        if (!newest->empty()) {
            lastId = newest->front().id;
        }
    }
    print("Watching for new toasts: [listener] every 500 ms, [database] every 1 s (Ctrl+C stops)...");
    for (unsigned tick = 1;; ++tick) {
        Sleep(500);
        if (listener) {
            try {
                const auto list = listener->GetNotificationsAsync(NotificationKinds::Toast).get();
                const std::int64_t seen = filetimeNow();
                std::set<std::uint32_t> now;
                for (const auto& n : list) {
                    now.insert(n.Id());
                    if (listenerSeen.count(n.Id()) == 0U) {
                        printListenerToast(listenerToast(n), print, "[listener] ", seen);
                    }
                }
                for (std::uint32_t id : listenerSeen) {
                    if (now.count(id) == 0U) {
                        print("[listener] gone id=" + std::to_string(id));
                    }
                }
                listenerSeen.swap(now);
            } catch (const winrt::hresult_error& e) {
                print("[listener] FAIL " + hresultText(e));
            }
        }
        if ((tick % 2U) != 0U) {
            continue;
        }
        Database db;  // opened again each time: the live file changes under us
        if (!openDatabase(db, print, true)) {
            continue;
        }
        const std::optional<std::vector<Toast>> fresh = db.toasts(lastId, 20, print);
        if (!fresh) {
            continue;
        }
        const std::int64_t seen = filetimeNow();
        for (auto it = fresh->rbegin(); it != fresh->rend(); ++it) {  // oldest first
            print("[database]");
            printToast(*it, print, seen);
            lastId = it->id > lastId ? it->id : lastId;
        }
    }
}

}  // namespace stm32_audio::host::win
