#include "StdioMcpServer.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace sample::common
{
    namespace
    {
        bool IsPipeHandle(HANDLE handle)
        {
            if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
            {
                return false;
            }
            SetLastError(NO_ERROR);
            const DWORD type = GetFileType(handle);
            return type == FILE_TYPE_PIPE;
        }

        std::string WindowsErrorMessage(const char* operation, DWORD code)
        {
            return std::string(operation) + " failed with Win32 error " + std::to_string(code) + ".";
        }

        void WriteStderr(const std::string& message)
        {
            HANDLE errorHandle = GetStdHandle(STD_ERROR_HANDLE);
            if (errorHandle == nullptr || errorHandle == INVALID_HANDLE_VALUE)
            {
                return;
            }
            const std::string line = message + "\r\n";
            DWORD written = 0;
            WriteFile(errorHandle, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        }
    }

    FramingResult NewlineJsonFramer::Feed(std::string_view bytes)
    {
        FramingResult result;
        if (failed_)
        {
            result.failed = true;
            result.error = "The framing stream is already in a failed state.";
            return result;
        }

        for (const char character : bytes)
        {
            if (character == '\n')
            {
                if (!pending_.empty() && pending_.back() == '\r')
                {
                    pending_.pop_back();
                }
                result.messages.push_back(std::move(pending_));
                pending_.clear();
                continue;
            }

            pending_.push_back(character);
            if (pending_.size() > MaximumMessageBytes)
            {
                failed_ = true;
                pending_.clear();
                result.messages.clear();
                result.failed = true;
                result.error = "MCP message exceeds the 1 MiB input limit.";
                return result;
            }
        }
        return result;
    }

    FramingResult NewlineJsonFramer::Finish()
    {
        FramingResult result;
        if (failed_)
        {
            result.failed = true;
            result.error = "The framing stream is already in a failed state.";
            return result;
        }
        if (!pending_.empty())
        {
            if (pending_.back() == '\r')
            {
                pending_.pop_back();
            }
            result.messages.push_back(std::move(pending_));
            pending_.clear();
        }
        return result;
    }

    void NewlineJsonFramer::Reset()
    {
        pending_.clear();
        failed_ = false;
    }

    bool CommandLineHasMcpFlag()
    {
        int argumentCount = 0;
        wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        if (arguments == nullptr)
        {
            return false;
        }

        bool found = false;
        for (int index = 1; index < argumentCount; ++index)
        {
            if (std::wstring_view(arguments[index]) == L"--mcp")
            {
                found = true;
                break;
            }
        }
        LocalFree(arguments);
        return found;
    }

    bool ValidateMcpStandardHandles(std::string& error)
    {
        if (!IsPipeHandle(GetStdHandle(STD_INPUT_HANDLE)))
        {
            error = "--mcp requires stdin to be a pipe.";
            return false;
        }
        if (!IsPipeHandle(GetStdHandle(STD_OUTPUT_HANDLE)))
        {
            error = "--mcp requires stdout to be a pipe.";
            return false;
        }
        error.clear();
        return true;
    }

    struct StdioMcpServer::Impl
    {
        explicit Impl(SceneStateStore& stateStore)
            : store(stateStore), dispatcher(stateStore)
        {
        }

        ~Impl()
        {
            Stop();
            Join();
            if (stopEvent != nullptr)
            {
                CloseHandle(stopEvent);
                stopEvent = nullptr;
            }
        }

        bool Start(void* nativeWindow, std::string& error)
        {
            if (worker.joinable())
            {
                error = "The MCP stdio server has already been started.";
                return false;
            }
            if (!ValidateMcpStandardHandles(error))
            {
                return false;
            }

            input = GetStdHandle(STD_INPUT_HANDLE);
            output = GetStdHandle(STD_OUTPUT_HANDLE);
            window = static_cast<HWND>(nativeWindow);
            stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (stopEvent == nullptr)
            {
                error = WindowsErrorMessage("CreateEventW", GetLastError());
                return false;
            }

            stopping.store(false, std::memory_order_release);
            running.store(true, std::memory_order_release);
            store.SetMcpRequested(true);
            store.SetMcpRunning(true);
            try
            {
                worker = std::thread([this] { Run(); });
            }
            catch (const std::exception& exception)
            {
                running.store(false, std::memory_order_release);
                store.SetMcpRunning(false);
                error = std::string("Failed to create the MCP I/O thread: ") + exception.what();
                CloseHandle(stopEvent);
                stopEvent = nullptr;
                return false;
            }
            error.clear();
            return true;
        }

        void Stop()
        {
            if (stopping.exchange(true, std::memory_order_acq_rel))
            {
                return;
            }
            if (stopEvent != nullptr)
            {
                SetEvent(stopEvent);
            }
            if (worker.joinable())
            {
                CancelSynchronousIo(static_cast<HANDLE>(worker.native_handle()));
            }
        }

        void Join()
        {
            if (worker.joinable() && worker.get_id() != std::this_thread::get_id())
            {
                worker.join();
            }
            running.store(false, std::memory_order_release);
            store.SetMcpRunning(false);
        }

        bool WriteResponse(const std::string& response)
        {
            std::lock_guard lock(outputMutex);
            const std::string line = response + "\n";
            std::size_t offset = 0;
            while (offset < line.size() && !stopping.load(std::memory_order_acquire))
            {
                const DWORD remaining = static_cast<DWORD>(std::min<std::size_t>(
                    line.size() - offset,
                    static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
                DWORD written = 0;
                if (!WriteFile(output, line.data() + offset, remaining, &written, nullptr) || written == 0)
                {
                    return false;
                }
                offset += written;
            }
            return offset == line.size();
        }

        bool DispatchMessages(const std::vector<std::string>& messages)
        {
            for (const std::string& message : messages)
            {
                const McpDispatchResult result = dispatcher.DispatchLine(message);
                if (result.hasResponse && !WriteResponse(result.responseLine))
                {
                    return false;
                }
            }
            return true;
        }

        void CloseWindowForRemoteDisconnect()
        {
            if (!stopping.load(std::memory_order_acquire) && window != nullptr)
            {
                PostMessageW(window, WM_CLOSE, 0, 0);
            }
        }

        void Run()
        {
            NewlineJsonFramer framer;
            bool remoteDisconnected = false;
            std::string terminalError;

            while (!stopping.load(std::memory_order_acquire))
            {
                if (WaitForSingleObject(stopEvent, 10) == WAIT_OBJECT_0)
                {
                    break;
                }

                DWORD available = 0;
                if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr))
                {
                    const DWORD errorCode = GetLastError();
                    if (errorCode != ERROR_BROKEN_PIPE && errorCode != ERROR_HANDLE_EOF &&
                        errorCode != ERROR_OPERATION_ABORTED)
                    {
                        terminalError = WindowsErrorMessage("PeekNamedPipe", errorCode);
                    }
                    remoteDisconnected = errorCode != ERROR_OPERATION_ABORTED;
                    break;
                }

                if (available == 0)
                {
                    continue;
                }

                char buffer[64 * 1024];
                const DWORD requested = std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer)));
                DWORD bytesRead = 0;
                if (!ReadFile(input, buffer, requested, &bytesRead, nullptr) || bytesRead == 0)
                {
                    const DWORD errorCode = GetLastError();
                    if (errorCode != ERROR_BROKEN_PIPE && errorCode != ERROR_HANDLE_EOF &&
                        errorCode != ERROR_OPERATION_ABORTED)
                    {
                        terminalError = WindowsErrorMessage("ReadFile", errorCode);
                    }
                    remoteDisconnected = errorCode != ERROR_OPERATION_ABORTED;
                    break;
                }

                FramingResult framed = framer.Feed(std::string_view(buffer, bytesRead));
                if (framed.failed)
                {
                    terminalError = framed.error;
                    const std::string errorResponse =
                        R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"MCP message exceeds the 1 MiB input limit."}})";
                    WriteResponse(errorResponse);
                    remoteDisconnected = true;
                    break;
                }
                if (!DispatchMessages(framed.messages))
                {
                    remoteDisconnected = true;
                    break;
                }
            }

            if (remoteDisconnected)
            {
                const FramingResult finalMessages = framer.Finish();
                if (!finalMessages.failed)
                {
                    DispatchMessages(finalMessages.messages);
                }
            }

            if (!terminalError.empty())
            {
                store.RecordMcpActivity("transport", {}, false, terminalError, false);
                WriteStderr(terminalError);
            }

            running.store(false, std::memory_order_release);
            store.SetMcpRunning(false);
            if (remoteDisconnected)
            {
                CloseWindowForRemoteDisconnect();
            }
        }

        SceneStateStore& store;
        McpDispatcher dispatcher;
        HANDLE input = INVALID_HANDLE_VALUE;
        HANDLE output = INVALID_HANDLE_VALUE;
        HANDLE stopEvent = nullptr;
        HWND window = nullptr;
        std::thread worker;
        std::mutex outputMutex;
        std::atomic<bool> stopping{ false };
        std::atomic<bool> running{ false };
    };

    StdioMcpServer::StdioMcpServer(SceneStateStore& store)
        : impl_(std::make_unique<Impl>(store))
    {
    }

    StdioMcpServer::~StdioMcpServer() = default;

    bool StdioMcpServer::Start(void* nativeWindow, std::string& error)
    {
        return impl_->Start(nativeWindow, error);
    }

    void StdioMcpServer::Stop()
    {
        impl_->Stop();
    }

    void StdioMcpServer::Join()
    {
        impl_->Join();
    }

    bool StdioMcpServer::IsRunning() const noexcept
    {
        return impl_->running.load(std::memory_order_acquire);
    }
}
