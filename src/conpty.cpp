#include "conpty.h"

#ifdef Q_OS_WIN

#include <QDebug>

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

    HRESULT hr = CreatePseudoConsole(size, inRead, outWrite, 0, &m_hpc);
    if (FAILED(hr)) {
        CloseHandle(inRead);
        CloseHandle(inWrite);
        CloseHandle(outRead);
        CloseHandle(outWrite);
        return false;
    }

    // Build the command line.
    QString cmdLine = "\"" + program + "\"";
    for (const QString& a : args) {
        cmdLine += " \"" + a + "\"";
    }
    QByteArray cmdLineUtf16;
    cmdLineUtf16.resize((cmdLine.size() + 1) * 2);
    wcscpy(reinterpret_cast<wchar_t*>(cmdLineUtf16.data()), reinterpret_cast<const wchar_t*>(cmdLine.utf16()));

    STARTUPINFOEXW si;
    memset(&si, 0, sizeof(si));
    si.StartupInfo.cb = sizeof(si);

    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    si.lpAttributeList = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, attrSize));
    if (!si.lpAttributeList) {
        ClosePseudoConsole(m_hpc);
        m_hpc = nullptr;
        CloseHandle(inRead);
        CloseHandle(inWrite);
        CloseHandle(outRead);
        CloseHandle(outWrite);
        return false;
    }
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize)) {
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
        ClosePseudoConsole(m_hpc);
        m_hpc = nullptr;
        CloseHandle(inRead);
        CloseHandle(inWrite);
        CloseHandle(outRead);
        CloseHandle(outWrite);
        return false;
    }
    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, m_hpc, sizeof(m_hpc),
                                   nullptr, nullptr)) {
        DeleteProcThreadAttributeList(si.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
        ClosePseudoConsole(m_hpc);
        m_hpc = nullptr;
        CloseHandle(inRead);
        CloseHandle(inWrite);
        CloseHandle(outRead);
        CloseHandle(outWrite);
        return false;
    }

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    BOOL ok = CreateProcessW(nullptr, reinterpret_cast<wchar_t*>(cmdLineUtf16.data()), nullptr, nullptr, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &si.StartupInfo, &pi);

    DeleteProcThreadAttributeList(si.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
    CloseHandle(inRead);
    CloseHandle(outWrite);

    if (!ok) {
        ClosePseudoConsole(m_hpc);
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
    ResizePseudoConsole(m_hpc, size);
}

bool ConPty::write(const QByteArray& data) {
    if (!m_inputWrite || data.isEmpty())
        return false;
    DWORD written = 0;
    return WriteFile(m_inputWrite, data.constData(), static_cast<DWORD>(data.size()), &written, nullptr) && written > 0;
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
        ClosePseudoConsole(m_hpc);
        m_hpc = nullptr;
    }
}

#endif
