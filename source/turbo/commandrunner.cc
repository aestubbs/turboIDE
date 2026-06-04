#define Uses_TEventQueue
#include <tvision/tv.h>

#include "commandrunner.h"

CommandRunner::~CommandRunner()
{
    stop();
}

bool CommandRunner::start(const std::string &command, const std::string &cwd) noexcept
{
    stop();
    // Run through a shell so arbitrary commands (pipes, &&, env, globs) work,
    // and merge stderr into stdout up front ('exec 2>&1') so compiler errors
    // are captured in order. turbo::Process otherwise sends stderr to /dev/null.
    std::string script = "exec 2>&1\n" + command;
    if (!proc.start("sh", {"-c", script}, cwd, {}))
        return false;
    proc.closeStdin(); // the command gets no input

    eof_.store(false);
    reaped_ = false;
    running_ = true;
    lineBuf.clear();
    { std::lock_guard<std::mutex> lk(mx); incoming.clear(); }

    reader = std::thread([this] {
        char buf[8192];
        for (;;)
        {
            long n = proc.readStdout(buf, sizeof buf);
            if (n <= 0)
                break; // EOF or error
            {
                std::lock_guard<std::mutex> lk(mx);
                incoming.append(buf, (size_t) n);
            }
            TEventQueue::wakeUp(); // nudge the idle loop to drain & repaint
        }
        eof_.store(true);
        TEventQueue::wakeUp();
    });
    return true;
}

void CommandRunner::pump() noexcept
{
    if (!running_)
        return;

    std::string data;
    {
        std::lock_guard<std::mutex> lk(mx);
        if (!incoming.empty())
            data.swap(incoming);
    }
    if (!data.empty())
    {
        lineBuf += data;
        size_t pos;
        while ((pos = lineBuf.find('\n')) != std::string::npos)
        {
            std::string line = lineBuf.substr(0, pos);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (onLine)
                onLine(line);
            lineBuf.erase(0, pos + 1);
        }
    }

    if (eof_.load() && !reaped_)
    {
        if (!lineBuf.empty()) // flush a trailing line with no newline
        {
            if (onLine)
                onLine(lineBuf);
            lineBuf.clear();
        }
        if (reader.joinable())
            reader.join();
        int code = proc.wait(); // reader is done; safe to reap on the main thread
        reaped_ = true;
        running_ = false;
        if (onExit)
            onExit(code);
    }
}

void CommandRunner::stop() noexcept
{
    if (reader.joinable())
    {
        proc.terminate(); // kill the child; the reader then sees EOF and exits
        reader.join();
    }
    reaped_ = true;
    running_ = false;
    lineBuf.clear();
    {
        std::lock_guard<std::mutex> lk(mx);
        incoming.clear();
    }
}
