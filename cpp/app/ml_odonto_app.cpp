// ML ODONTO.exe — abre o programa numa janela própria do Windows (WebView2), sem navegador nem console.
//
// Sobe o aplicativo R (odontolesao::run_app) escondido numa porta local livre, mostra uma tela de
// carregamento e troca para a interface assim que o servidor responde. Fechar a janela encerra o R:
// o processo fica num job object do Windows, que mata toda a árvore mesmo se este .exe travar.
// Uma segunda execução só traz a janela já aberta para a frente.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <shlobj.h>
#include <ws2tcpip.h>

#include "webview/webview.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr wchar_t kTitle[] = L"ML ODONTO";

fs::path executable_directory() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size < buffer.size()) {
            buffer.resize(size);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return fs::path(buffer).parent_path();
}

std::wstring registry_string(HKEY root, const wchar_t* key, const wchar_t* value) {
    wchar_t buffer[1024];
    DWORD size = sizeof(buffer);
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buffer, &size) == ERROR_SUCCESS) {
        return buffer;
    }
    return {};
}

// R embutido ao lado do .exe (instalador), depois o registrado no Windows, depois o mais novo em Arquivos de Programas.
fs::path find_rscript() {
    const auto bundled = executable_directory() / "R" / "bin" / "Rscript.exe";
    if (fs::exists(bundled)) {
        return bundled;
    }
    for (const HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        const auto install = registry_string(root, L"SOFTWARE\\R-core\\R", L"InstallPath");
        if (!install.empty() && fs::exists(fs::path(install) / "bin" / "Rscript.exe")) {
            return fs::path(install) / "bin" / "Rscript.exe";
        }
    }
    fs::path best;
    std::error_code error;
    for (const auto& base : {L"C:\\Program Files\\R", L"C:\\Program Files (x86)\\R"}) {
        for (const auto& entry : fs::directory_iterator(base, error)) {
            const auto candidate = entry.path() / "bin" / "Rscript.exe";
            if (fs::exists(candidate) && (best.empty() || entry.path().filename() > best.parent_path().parent_path().filename())) {
                best = candidate;
            }
        }
    }
    return best;
}

int free_port() {
    SOCKET probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    int length = sizeof(address);
    int port = 0;
    if (bind(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
        getsockname(probe, reinterpret_cast<sockaddr*>(&address), &length) == 0) {
        port = ntohs(address.sin_port);
    }
    closesocket(probe);
    return port;
}

bool port_answers(int port) {
    SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<u_short>(port));
    const bool ok = connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    closesocket(client);
    return ok;
}

fs::path log_file() {
    PWSTR local = nullptr;
    fs::path folder = fs::temp_directory_path();
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local) == S_OK) {
        folder = fs::path(local) / "ML ODONTO";
    }
    CoTaskMemFree(local);
    std::error_code error;
    fs::create_directories(folder, error);
    return folder / "janela.log";
}

std::string utf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::string page(const std::string& body) {
    return R"(<!doctype html><html lang="pt-BR"><head><meta charset="utf-8"><style>
html,body{height:100%;margin:0;font-family:"Segoe UI",system-ui,sans-serif;background:#f4f6f9;color:#1f2d3d}
.c{height:100%;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:14px;text-align:center;padding:0 24px}
h1{font-size:22px;font-weight:600;letter-spacing:.04em;margin:0;color:#1d4e89}
p{margin:0;color:#5a6b7d;font-size:14px;max-width:560px;line-height:1.5}
.s{width:34px;height:34px;border:3px solid #d5dee8;border-top-color:#1d4e89;border-radius:50%;animation:g 1s linear infinite}
@keyframes g{to{transform:rotate(360deg)}}
code{background:#e8edf3;padding:1px 5px;border-radius:3px;font-size:12px}
</style></head><body><div class="c">)" + body + "</div></body></html>";
}

class RServer {
public:
    bool start(const fs::path& rscript, int port, const fs::path& log) {
        job_ = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

        SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
        HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        startup.hStdOutput = output;
        startup.hStdError = output;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

        std::wstring command = L"\"" + rscript.wstring() +
                               L"\" -e \"odontolesao::run_app(porta = " + std::to_wstring(port) +
                               L", abrir = FALSE)\"";
        const BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
                                            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr,
                                            &startup, &process_);
        if (output != INVALID_HANDLE_VALUE) {
            CloseHandle(output);
        }
        if (!created) {
            return false;
        }
        AssignProcessToJobObject(job_, process_.hProcess);
        ResumeThread(process_.hThread);
        return true;
    }

    [[nodiscard]] bool running() const {
        return process_.hProcess != nullptr && WaitForSingleObject(process_.hProcess, 0) == WAIT_TIMEOUT;
    }

    ~RServer() {
        if (job_ != nullptr) {
            TerminateJobObject(job_, 0);
            CloseHandle(job_);
        }
        if (process_.hProcess != nullptr) {
            CloseHandle(process_.hThread);
            CloseHandle(process_.hProcess);
        }
    }

private:
    HANDLE job_{nullptr};
    PROCESS_INFORMATION process_{};
};

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\ML_ODONTO_janela");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(nullptr, kTitle)) {
            ShowWindow(existing, IsIconic(existing) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(existing);
        }
        return 0;
    }

    WSADATA winsock{};
    WSAStartup(MAKEWORD(2, 2), &winsock);

    webview::webview window(false, nullptr);
    window.set_title(utf8(kTitle));
    window.set_size(1400, 900, WEBVIEW_HINT_NONE);
    auto* handle = static_cast<HWND>(window.window().value());
    if (HICON icon = LoadIconW(instance, L"IDI_APP")) {
        SendMessageW(handle, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
        SendMessageW(handle, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
    }
    ShowWindow(handle, SW_MAXIMIZE);

    const auto log = log_file();
    const auto rscript = find_rscript();
    RServer server;
    const int port = free_port();
    std::atomic<bool> closing{false};
    std::thread watcher;

    if (rscript.empty()) {
        window.set_html(page(
            "<h1>ML ODONTO</h1><p>O R não foi encontrado neste computador. Instale o R 4.6 "
            "(cran.r-project.org) e depois rode <code>Rscript software/instalar_dependencias.R</code> "
            "na pasta do projeto.</p>"));
    } else if (port == 0 || !server.start(rscript, port, log)) {
        window.set_html(page("<h1>ML ODONTO</h1><p>Não foi possível iniciar o R (" + utf8(rscript.wstring()) + ").</p>"));
    } else {
        window.set_html(page("<h1>ML ODONTO</h1><div class=\"s\"></div><p>Carregando o programa…</p>"));
        watcher = std::thread([&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
            while (!closing && std::chrono::steady_clock::now() < deadline) {
                if (port_answers(port)) {
                    window.dispatch([&] { window.navigate("http://127.0.0.1:" + std::to_string(port) + "/"); });
                    return;
                }
                if (!server.running()) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            }
            if (!closing) {
                const auto message = "<h1>ML ODONTO</h1><p>O programa não abriu. O registro está em <code>" +
                                     utf8(log.wstring()) + "</code>.</p>";
                window.dispatch([&window, message] { window.set_html(page(message)); });
            }
        });
    }

    window.run();
    closing = true;
    if (watcher.joinable()) {
        watcher.join();
    }
    WSACleanup();
    ReleaseMutex(single);
    CloseHandle(single);
    return 0;
}
