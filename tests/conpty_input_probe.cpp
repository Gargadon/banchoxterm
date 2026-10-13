#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>

// Read actual console key events, as native Windows terminal applications do.
int main(int argc, char** argv) {
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (!SetConsoleMode(input, 0))
        return 1;
    const bool query = argc > 1 && std::string(argv[1]) == "--query";
    if (argc > 1) {
        SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        if (query)
            std::printf("\x1b]11;?\x1b\\");
        else
            std::printf("\x1b[48;2;17;39;61m          \x1b[0m\r\n\x1b[2mDIM_PROBE\x1b[22m\r\n");
    }
    std::puts("INPUT_READY");
    const char* colorTerm = std::getenv("COLORTERM");
    std::printf("COLORTERM=%s\n", colorTerm ? colorTerm : "");
    std::fflush(stdout);
    INPUT_RECORD record;
    DWORD count = 0;
    std::wstring reply;
    while (ReadConsoleInputW(input, &record, 1, &count)) {
        if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown)
            continue;
        const auto& key = record.Event.KeyEvent;
        if (query) {
            reply += key.uChar.UnicodeChar;
            if (reply.find(L"rgb:") != std::wstring::npos && reply.find(L"\x1b\\") != std::wstring::npos) {
                std::puts("QUERY_REPLY_RECEIVED");
                std::fflush(stdout);
                return 0;
            }
        }
        if (const char* path = std::getenv("BANCHO_CONSOLE_SIZE_PROBE")) {
            CONSOLE_SCREEN_BUFFER_INFO info{};
            if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info)) {
                if (FILE* file = std::fopen(path, "w")) {
                    std::fprintf(file, "%d %d", info.srWindow.Right - info.srWindow.Left + 1,
                                 info.srWindow.Bottom - info.srWindow.Top + 1);
                    std::fclose(file);
                }
            }
        }
        if (key.wVirtualKeyCode == VK_RETURN || key.wVirtualKeyCode == VK_LEFT) {
            std::printf("KEY=%u SHIFT=%d\n", key.wVirtualKeyCode, (key.dwControlKeyState & SHIFT_PRESSED) != 0);
            std::fflush(stdout);
            if (key.wVirtualKeyCode == VK_RETURN)
                return 0;
        }
    }
    return 2;
}
