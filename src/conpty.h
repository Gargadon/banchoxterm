#pragma once
#include <QString>
#include <QStringList>
#include <QByteArray>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>

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
};

#endif
