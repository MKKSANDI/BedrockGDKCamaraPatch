#include "installer/probe_runner.hpp"

#include <windows.h>

#include <array>

namespace mcfix::installer {
namespace {

std::wstring quote_argument(std::wstring_view argument) {
    if (argument.empty()) {
        return L"\"\"";
    }
    if (argument.find_first_of(L" \t\"") == std::wstring_view::npos) {
        return std::wstring(argument);
    }

    std::wstring quoted(1, L'\"');
    std::size_t backslashes = 0;
    for (const auto character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(L'\"');
        } else {
            quoted.append(backslashes, L'\\');
            quoted.push_back(character);
        }
        backslashes = 0;
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'\"');
    return quoted;
}

std::wstring command_line(const ProcessRequest& request) {
    std::wstring value = quote_argument(request.executable.native());
    for (const auto& argument : request.arguments) {
        value.push_back(L' ');
        value += quote_argument(argument);
    }
    return value;
}

struct ReaderContext {
    HANDLE pipe{};
    std::string* output{};
    bool* truncated{};
};

DWORD WINAPI read_output(void* raw_context) {
    auto& context = *static_cast<ReaderContext*>(raw_context);
    constexpr std::size_t output_limit = 64 * 1024;
    std::array<char, 4096> buffer{};
    DWORD read = 0;
    while (ReadFile(context.pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) &&
           read != 0) {
        const auto remaining = output_limit - context.output->size();
        const auto accepted = (std::min)(remaining, static_cast<std::size_t>(read));
        context.output->append(buffer.data(), accepted);
        if (accepted != read) {
            *context.truncated = true;
        }
    }
    return 0;
}

}  // namespace

ProcessResult run_hidden(const ProcessRequest& request) {
    ProcessResult result{};
    if (!request.executable.is_absolute() || request.timeout_ms == 0) {
        result.win32_error = ERROR_INVALID_PARAMETER;
        return result;
    }

    const HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) {
        result.win32_error = GetLastError();
        return result;
    }

    SECURITY_ATTRIBUTES pipe_security{};
    pipe_security.nLength = sizeof(pipe_security);
    pipe_security.bInheritHandle = TRUE;
    HANDLE output_read = nullptr;
    HANDLE output_write = nullptr;
    if (!CreatePipe(&output_read, &output_write, &pipe_security, 0) ||
        !SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0)) {
        result.win32_error = GetLastError();
        if (output_read != nullptr) CloseHandle(output_read);
        if (output_write != nullptr) CloseHandle(output_write);
        CloseHandle(job);
        return result;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        result.win32_error = GetLastError();
        CloseHandle(job);
        return result;
    }

    auto mutable_command = command_line(request);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = output_write;
    startup.hStdError = output_write;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            request.executable.c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &process)) {
        result.win32_error = GetLastError();
        CloseHandle(output_read);
        CloseHandle(output_write);
        CloseHandle(job);
        return result;
    }
    result.launched = true;
    CloseHandle(output_write);
    output_write = nullptr;

    ReaderContext reader_context{
        .pipe = output_read,
        .output = &result.output,
        .truncated = &result.output_truncated,
    };
    const HANDLE reader = CreateThread(nullptr, 0, read_output, &reader_context, 0, nullptr);
    if (reader == nullptr) {
        result.win32_error = GetLastError();
        TerminateProcess(process.hProcess, result.win32_error);
    }

    if (result.win32_error != 0) {
        // The process was terminated above because output could not be drained safely.
    } else if (!AssignProcessToJobObject(job, process.hProcess)) {
        result.win32_error = GetLastError();
        TerminateProcess(process.hProcess, result.win32_error);
    } else if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        result.win32_error = GetLastError();
        TerminateJobObject(job, result.win32_error);
    } else {
        const auto wait = WaitForSingleObject(process.hProcess, request.timeout_ms);
        if (wait == WAIT_TIMEOUT) {
            result.timed_out = true;
            TerminateJobObject(job, WAIT_TIMEOUT);
            WaitForSingleObject(process.hProcess, 2000);
        } else if (wait == WAIT_FAILED) {
            result.win32_error = GetLastError();
            TerminateJobObject(job, result.win32_error);
        }
    }

    DWORD exit_code = 0xFFFFFFFFU;
    if (GetExitCodeProcess(process.hProcess, &exit_code)) {
        result.exit_code = exit_code;
    } else if (result.win32_error == 0) {
        result.win32_error = GetLastError();
    }
    if (reader != nullptr) {
        if (WaitForSingleObject(reader, 2000) == WAIT_TIMEOUT) {
            CloseHandle(output_read);
            output_read = nullptr;
            WaitForSingleObject(reader, 2000);
        }
        CloseHandle(reader);
    }
    if (output_read != nullptr) {
        CloseHandle(output_read);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(job);
    return result;
}

}  // namespace mcfix::installer
