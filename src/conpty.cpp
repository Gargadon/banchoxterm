#include "conpty.h"

#ifdef Q_OS_WIN

#include <QDebug>
#include <QCoreApplication>
#include <QProcessEnvironment>

namespace {
struct ConsoleApi {
    decltype(&CreatePseudoConsole) create = &CreatePseudoConsole;
    decltype(&ResizePseudoConsole) resize = &ResizePseudoConsole;
    decltype(&ClosePseudoConsole) close = &ClosePseudoConsole;
    ConsoleApi() {
        const QString path = QCoreApplication::applicationDirPath() + QStringLiteral("/conpty.dll");
        HMODULE module = LoadLibraryExW(reinterpret_cast<LPCWSTR>(path.utf16()), nullptr,
                                        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module)
            return;
        const auto createFn = reinterpret_cast<decltype(create)>(GetProcAddress(module, "ConptyCreatePseudoConsole"));
        const auto resizeFn = reinterpret_cast<decltype(resize)>(GetProcAddress(module, "ConptyResizePseudoConsole"));
        const auto closeFn = reinterpret_cast<decltype(close)>(GetProcAddress(module, "ConptyClosePseudoConsole"));
        if (!createFn || !resizeFn || !closeFn) {
            FreeLibrary(module);
            return;
        }
        create = createFn;
        resize = resizeFn;
        close = closeFn;
        // Keep the DLL loaded while any pseudoconsole can still exist.
    }
};
const ConsoleApi& consoleApi() {
    static const ConsoleApi api;
    return api;
}
} // namespace

DWORD ConPty::exitCode() const {
    if (!m_process)
        return 0;
    DWORD code = 0;
    GetExitCodeProcess(m_process, &code);
    return code;
}

ConPty::~ConPty() {
    close();
}

bool ConPty::start(const QString& program, const QStringList& args, int cols, int rows) {
    close();

    HANDLE inRead = nullptr;
    HANDLE inWrite = nullptr;
    HANDLE outRead = nullptr;
    HANDLE outWrite = nullptr;

    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&inRead, &inWrite, &sa, 0))
        return false;
    if (!SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0))
        return false;
    if (!CreatePipe(&outRead, &outWrite, &sa, 0))
        return false;
    if (!SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0))
        return false;

    COORD size;
    size.X = static_cast<SHORT>(cols);
    size.Y = static_cast<SHORT>(rows);

    HRESULT hr = consoleApi().create(size, inRead, outWrite, 0, &m_hpc);
    if (FAILED(hr)) {
        m_startError = static_cast<DWORD>(hr);
        CloseHandle(inRead);
        CloseHandle(inWrite);
        CloseHandle(outRead);
        CloseHandle(outWrite);
        return false;
    }

    // Build the command line.
    std::wstring wcmd;
    if (program.contains(' ')) {
        wcmd = L"\"" + program.toStdWString() + L"\"";
    } else {
        wcmd = program.toStdWString();
    }
    for (const QString& a : args) {
        wcmd += L" ";
        if (a.contains(' ')) {
            wcmd += L"\"" + a.toStdWString() + L"\"";
        } else {
            wcmd += a.toStdWString();
        }
    }

    STARTUPINFOEXW si;
    memset(&si, 0, sizeof(si));
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nullptr;
    si.StartupInfo.hStdOutput = nullptr;
    si.StartupInfo.hStdError = nullptr;

    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    si.lpAttributeList = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, attrSize));
    if (!si.lpAttributeList) {
        consoleApi().close(m_hpc);
        m_hpc = nullptr;
        CloseHandle(inRead);
        CloseHandle(inWrite);
        CloseHandle(outRead);
        CloseHandle(outWrite);
        return false;
    }
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize)) {
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
        consoleApi().close(m_hpc);
        m_hpc = nullptr;
        CloseHandle(inRead);
        CloseHandle(inWrite);
        CloseHandle(outRead);
        CloseHandle(outWrite);
        return false;
    }
    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, m_hpc, sizeof(HPCON),
                                   nullptr, nullptr)) {
        m_startError = GetLastError();
        DeleteProcThreadAttributeList(si.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
        consoleApi().close(m_hpc);
        m_hpc = nullptr;
        CloseHandle(inRead);
        CloseHandle(inWrite);
        CloseHandle(outRead);
        CloseHandle(outWrite);
        return false;
    }

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    environment.insert(QStringLiteral("TERM_PROGRAM"), QStringLiteral("banchoxterm"));
    if (!environment.contains(QStringLiteral("TERM")))
        environment.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    QStringList entries = environment.toStringList();
    entries.sort(Qt::CaseInsensitive);
    QString block = entries.join(QChar('\0'));
    block.append(QChar('\0'));
    block.append(QChar('\0'));
    BOOL ok = CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, block.data(), nullptr,
                             &si.StartupInfo, &pi);

    DeleteProcThreadAttributeList(si.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
    CloseHandle(inRead);
    CloseHandle(outWrite);

    if (!ok) {
        m_startError = GetLastError();
        consoleApi().close(m_hpc);
        m_hpc = nullptr;
        CloseHandle(inWrite);
        CloseHandle(outRead);
        return false;
    }

    m_inputWrite = inWrite;
    m_outputRead = outRead;
    m_process = pi.hProcess;
    m_thread = pi.hThread;

    CloseHandle(pi.hThread);
    m_thread = nullptr;

    return true;
}

void ConPty::resize(int cols, int rows) {
    if (!m_hpc)
        return;
    COORD size;
    size.X = static_cast<SHORT>(cols);
    size.Y = static_cast<SHORT>(rows);
    consoleApi().resize(m_hpc, size);
}

bool ConPty::write(const QByteArray& data) {
    if (!m_inputWrite || data.isEmpty())
        return false;
    QByteArray input = data;
    // ConPTY reconstructs Windows KEY_EVENT_RECORDs using win32-input-mode,
    // not CSI-u. Native console applications need the actual modifier state.
    int virtualKey = 0;
    int scanCode = 0;
    int character = 0;
    if (input == QByteArrayLiteral("\x1b")) {
        // Send a physical Escape key, rather than an ambiguous ANSI prefix.
        virtualKey = VK_ESCAPE;
        scanCode = 1;
        character = 27;
    } else if (input == QByteArrayLiteral("\x1b[13;2u")) {
        virtualKey = VK_RETURN;
        scanCode = 28;
        character = 13;
    } else if (input == QByteArrayLiteral("\x1b[1;2D")) {
        virtualKey = VK_LEFT;
        scanCode = 75;
    } else if (input == QByteArrayLiteral("\x1b[1;2C")) {
        virtualKey = VK_RIGHT;
        scanCode = 77;
    } else if (input == QByteArrayLiteral("\x1b[1;2A")) {
        virtualKey = VK_UP;
        scanCode = 72;
    } else if (input == QByteArrayLiteral("\x1b[1;2B")) {
        virtualKey = VK_DOWN;
        scanCode = 80;
    }
    if (virtualKey) {
        const int state = virtualKey == VK_ESCAPE ? 0 : SHIFT_PRESSED | (virtualKey == VK_RETURN ? 0 : ENHANCED_KEY);
        input = QStringLiteral("\x1b[%1;%2;%3;1;%4;1_\x1b[%1;%2;%3;0;%4;1_")
                    .arg(virtualKey)
                    .arg(scanCode)
                    .arg(character)
                    .arg(state)
                    .toLatin1();
    }
    DWORD written = 0;
    return WriteFile(m_inputWrite, input.constData(), static_cast<DWORD>(input.size()), &written, nullptr) &&
           written > 0;
}

QByteArray ConPty::read() {
    QByteArray result;
    if (!m_outputRead)
        return result;

    DWORD available = 0;
    if (!PeekNamedPipe(m_outputRead, nullptr, 0, nullptr, &available, nullptr))
        return result;
    if (available == 0)
        return result;

    char buf[8192];
    DWORD toRead = qMin<DWORD>(available, sizeof(buf));
    DWORD bytesRead = 0;
    if (ReadFile(m_outputRead, buf, toRead, &bytesRead, nullptr) && bytesRead > 0) {
        result.append(buf, static_cast<int>(bytesRead));
    }
    return result;
}

bool ConPty::isRunning() const {
    if (!m_process)
        return false;
    DWORD code = 0;
    if (!GetExitCodeProcess(m_process, &code))
        return false;
    return code == STILL_ACTIVE;
}

void ConPty::close() {
    if (m_process) {
        TerminateProcess(m_process, 0);
        CloseHandle(m_process);
        m_process = nullptr;
    }
    if (m_thread) {
        CloseHandle(m_thread);
        m_thread = nullptr;
    }
    if (m_inputWrite) {
        CloseHandle(m_inputWrite);
        m_inputWrite = nullptr;
    }
    if (m_outputRead) {
        CloseHandle(m_outputRead);
        m_outputRead = nullptr;
    }
    if (m_hpc) {
        consoleApi().close(m_hpc);
        m_hpc = nullptr;
    }
}

#endif
