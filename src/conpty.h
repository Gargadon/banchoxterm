#pragma once

#if defined(_WIN32) || defined(WIN32)
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000006
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <QString>
#include <QStringList>
#include <QByteArray>

// Minimal wrapper around the Windows Pseudo Console (ConPTY) API. Runs a
// process (e.g. cmd.exe / powershell.exe) attached to a ConPTY and exposes
// read/write access to its terminal stream.
class ConPty {
public:
    ConPty() = default;
    ~ConPty();

    ConPty(const ConPty&) = delete;
    ConPty& operator=(const ConPty&) = delete;

    bool start(const QString& program, const QStringList& args, int cols, int rows);
    void resize(int cols, int rows);
    bool write(const QByteArray& data);
    QByteArray read();
    bool isRunning() const;
    DWORD exitCode() const;
    DWORD startError() const { return m_startError; }
    void close();

    HANDLE outputReadHandle() const {
        return m_outputRead;
    }

private:
    HPCON m_hpc = nullptr;
    HANDLE m_outputRead = nullptr;
    HANDLE m_inputWrite = nullptr;
    HANDLE m_process = nullptr;
    HANDLE m_thread = nullptr;
    DWORD m_startError = 0;
};
#endif
