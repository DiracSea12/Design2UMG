// Copyright (c) 2026, Diracsea12. SPDX-License-Identifier: MIT

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <userenv.h>
#include <sddl.h>
#include <aclapi.h>
#include "quickjs.h"
#include "PencilScriptPolicy.h"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void reset(HANDLE h = nullptr) { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); value = h; }
};
std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    if (n) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}
std::string Quote(const std::string& text) {
    std::string out = "\"";
    for (unsigned char ch : text) {
        if (ch == '"' || ch == '\\') { out += '\\'; out += ch; }
        else if (ch < 32) { char tmp[8]; sprintf_s(tmp, "\\u%04x", ch); out += tmp; }
        else out += ch;
    }
    return out + "\"";
}
void Emit(const std::string& text) {
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}
bool GrantReadExecute(const std::wstring& path, PSID sid) {
    PACL oldAcl = nullptr, newAcl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    DWORD code = GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &oldAcl, nullptr, &descriptor);
    if (code != ERROR_SUCCESS) return false;
    EXPLICIT_ACCESSW entry{};
    entry.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
    entry.grfAccessMode = GRANT_ACCESS;
    entry.grfInheritance = NO_INHERITANCE;
    entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entry.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
    entry.Trustee.ptstrName = static_cast<LPWSTR>(sid);
    code = SetEntriesInAclW(1, &entry, oldAcl, &newAcl);
    if (code == ERROR_SUCCESS) code = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, newAcl, nullptr);
    if (newAcl) LocalFree(newAcl);
    if (descriptor) LocalFree(descriptor);
    return code == ERROR_SUCCESS;
}
struct FileAclScope {
    std::wstring path;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL acl = nullptr;
    bool active = false;
    DWORD restored = ERROR_SUCCESS;
    explicit FileAclScope(const std::wstring& value) : path(value) {
        if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &acl, nullptr, &descriptor) != ERROR_SUCCESS)
            throw std::runtime_error("image ACL snapshot failed");
        active = true;
    }
    void Restore() {
        if (active) {
            restored = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr);
            active = false;
        }
    }
    ~FileAclScope() { Restore(); if (descriptor) LocalFree(descriptor); }
};

int Worker() {
    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken)) return 10;
    Handle token(rawToken);
    DWORD isContainer = 0, size = 0;
    if (!GetTokenInformation(token.value, TokenIsAppContainer, &isContainer, sizeof(isContainer), &size) || !isContainer) return 11;
    DWORD capSize = 0;
    GetTokenInformation(token.value, TokenCapabilities, nullptr, 0, &capSize);
    std::vector<BYTE> caps(capSize);
    if (!GetTokenInformation(token.value, TokenCapabilities, caps.data(), capSize, &size)
        || reinterpret_cast<TOKEN_GROUPS*>(caps.data())->GroupCount != 0) return 12;
    std::string input;
    char buffer[4096]; DWORD count = 0;
    while (ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof(buffer), &count, nullptr) && count) {
        input.append(buffer, count);
        if (input.size() > PencilScriptPolicy::MaxRequestUtf8Bytes) return 13;
    }
    JSRuntime* runtime = JS_NewRuntime();
    if (!runtime) return 14;
    JS_SetMemoryLimit(runtime, PencilScriptPolicy::MaxRuntimeMemoryBytes);
    JS_SetMaxStackSize(runtime, PencilScriptPolicy::MaxRuntimeStackBytes);
    bool runtimeFinalized = false;
    if (JS_AddRuntimeFinalizer(runtime, [](JSRuntime*, void* value) { *static_cast<bool*>(value) = true; }, &runtimeFinalized) < 0) {
        JS_FreeRuntime(runtime); return 15;
    }
    JSContext* context = JS_NewContext(runtime);
    if (!context) { JS_FreeRuntime(runtime); return 16; }
    struct Deadline {
        std::chrono::steady_clock::time_point at;
        bool fired = false;
    } deadline{std::chrono::steady_clock::now() + std::chrono::milliseconds(PencilScriptPolicy::RuntimeExecutionMilliseconds)};
    JS_SetInterruptHandler(runtime, [](JSRuntime*, void* value) -> int {
        auto& state = *static_cast<Deadline*>(value);
        state.fired = state.fired || std::chrono::steady_clock::now() >= state.at;
        return state.fired ? 1 : 0;
    }, &deadline);
    JSValue result = JS_Eval(context, input.data(), input.size(), "pencil-script.js", JS_EVAL_TYPE_GLOBAL);
    const bool exception = JS_IsException(result);
    std::string resultJSON, exceptionText;
    int resultCode = 0;
    if (exception) {
        JSValue error = JS_GetException(context);
        const char* message = JS_ToCString(context, error);
        exceptionText = message ? message : "exception string unavailable";
        if (message) JS_FreeCString(context, message);
        JS_FreeValue(context, error);
    } else if (JS_IsString(result)) {
        size_t length = 0;
        const char* text = JS_ToCStringLen(context, &length, result);
        if (text && length <= PencilScriptPolicy::MaxResultJsonUtf8Bytes) resultJSON.assign(text, length);
        else resultCode = 1;
        if (text) JS_FreeCString(context, text);
    } else resultCode = 2;
    JS_FreeValue(context, result);
    JS_FreeContext(context);
    JS_RunGC(runtime);
    JS_FreeRuntime(runtime);
    std::ostringstream out;
    out << "{\"protocol\":1,\"engine\":\"QuickJS-NG 0.17.0\",\"isolated\":true,\"run_code\":" << (exception ? 1 : 0)
        << ",\"result_code\":" << resultCode << ",\"exception\":" << Quote(exceptionText)
        << ",\"interrupted\":" << (deadline.fired ? "true" : "false")
        << ",\"runtime_finalized\":" << (runtimeFinalized ? "true" : "false")
        << ",\"result_json\":" << Quote(resultJSON) << "}";
    Emit(out.str());
    return 0;
}

int Host(wchar_t** argv) {
    // The trusted editor passes a request file; only its bytes cross into LPAC.
    std::ifstream file(argv[1], std::ios::binary);
    std::string input((std::istreambuf_iterator<char>(file)), {});
    if (!file || input.size() > PencilScriptPolicy::MaxRequestUtf8Bytes) throw std::runtime_error("input read failed");
    wchar_t executable[32768];
    GetModuleFileNameW(nullptr, executable, 32768);
    std::wstring exe(executable);
    std::wstring profile = L"D2U.PencilScript." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
    PSID sid = nullptr;
    HRESULT createProfile = S_OK;
    createProfile = CreateAppContainerProfile(profile.c_str(), profile.c_str(), L"Pencil script import worker", nullptr, 0, &sid);
    if (FAILED(createProfile)) throw std::runtime_error("CreateAppContainerProfile failed " + std::to_string(static_cast<unsigned>(createProfile)));
    struct ProfileScope {
        std::wstring name; PSID sid; HRESULT deleted = E_FAIL;
        void Close() { if (sid) { FreeSid(sid); sid = nullptr; deleted = DeleteAppContainerProfile(name.c_str()); } }
        ~ProfileScope() { Close(); }
    } profileScope{profile, sid};
    FileAclScope imageAcl(exe);
    if (!GrantReadExecute(exe, sid)) throw std::runtime_error("worker image ACL grant failed");
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), nullptr, TRUE};
    HANDLE stdinRead = nullptr, stdinWrite = nullptr, stdoutRead = nullptr, stdoutWrite = nullptr;
    if (!CreatePipe(&stdinRead, &stdinWrite, &attributes, 0) || !CreatePipe(&stdoutRead, &stdoutWrite, &attributes, 0)) throw std::runtime_error("pipe creation failed");
    Handle inputRead(stdinRead), inputWrite(stdinWrite), outputRead(stdoutRead), outputWrite(stdoutWrite);
    SetHandleInformation(inputWrite.value, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outputRead.value, HANDLE_FLAG_INHERIT, 0);
    SIZE_T size = 0;
    const DWORD attributeCount = 3;
    InitializeProcThreadAttributeList(nullptr, attributeCount, 0, &size);
    std::vector<BYTE> attributeBytes(size);
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeBytes.data());
    if (!InitializeProcThreadAttributeList(list, attributeCount, 0, &size)) throw std::runtime_error("attribute list failed");
    struct AttributeScope { LPPROC_THREAD_ATTRIBUTE_LIST value; ~AttributeScope() { DeleteProcThreadAttributeList(value); } } attributeScope{list};
    SECURITY_CAPABILITIES capabilities{sid, nullptr, 0, 0};
    HANDLE inherited[]{inputRead.value, outputWrite.value};
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr)) throw std::runtime_error("handle restriction failed");
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &capabilities, sizeof(capabilities), nullptr, nullptr)) throw std::runtime_error("security capabilities failed");
    DWORD packagePolicy = PROCESS_CREATION_ALL_APPLICATION_PACKAGES_OPT_OUT;
    if (!UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY, &packagePolicy, sizeof(packagePolicy), nullptr, nullptr)) throw std::runtime_error("LPAC policy failed");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = inputRead.value;
    startup.StartupInfo.hStdOutput = outputWrite.value;
    startup.StartupInfo.hStdError = outputWrite.value;
    startup.lpAttributeList = list;
    std::wstring command = L"\"" + exe + L"\" --worker";
    std::vector<std::wstring> variables;
    for (const wchar_t* key : {L"SystemRoot", L"SystemDrive", L"WINDIR", L"USERPROFILE", L"LOCALAPPDATA", L"APPDATA", L"ALLUSERSPROFILE", L"PUBLIC", L"TEMP", L"TMP"}) {
        DWORD n = GetEnvironmentVariableW(key, nullptr, 0);
        if (!n) continue;
        std::vector<wchar_t> value(n);
        GetEnvironmentVariableW(key, value.data(), n);
        variables.push_back(std::wstring(key) + L"=" + value.data());
    }
    std::sort(variables.begin(), variables.end(), [](const auto& left, const auto& right) { return _wcsicmp(left.c_str(), right.c_str()) < 0; });
    std::wstring environment;
    for (const auto& variable : variables) { environment += variable; environment += L'\0'; }
    environment += L'\0';
    wchar_t systemDirectory[MAX_PATH];
    if (!GetSystemDirectoryW(systemDirectory, MAX_PATH)) throw std::runtime_error("system directory lookup failed");
    PROCESS_INFORMATION process{};
    BOOL created = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED | CREATE_NO_WINDOW, environment.data(), systemDirectory, &startup.StartupInfo, &process);
    if (!created) throw std::runtime_error("CreateProcess failed " + std::to_string(GetLastError()));
    Handle child(process.hProcess), thread(process.hThread);
    Handle job(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.BasicLimitInformation.ActiveProcessLimit = PencilScriptPolicy::MaxHostActiveProcesses;
    limits.ProcessMemoryLimit = PencilScriptPolicy::MaxHostProcessMemoryBytes;
    if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) || !AssignProcessToJobObject(job.value, child.value)) {
        TerminateProcess(child.value, 99); throw std::runtime_error("job setup failed");
    }
    if (ResumeThread(thread.value) == static_cast<DWORD>(-1)) { TerminateProcess(child.value, 96); throw std::runtime_error("ResumeThread failed"); }
    inputRead.reset(); outputWrite.reset();
    std::string output;
    std::atomic<bool> outputOverflow(false);
    std::thread reader([&] {
        char data[4096]; DWORD n;
        while (ReadFile(outputRead.value, data, sizeof(data), &n, nullptr) && n) {
            if (output.size() + n > PencilScriptPolicy::MaxHostOutputBytes) { outputOverflow = true; TerminateJobObject(job.value, 98); break; }
            output.append(data, n);
        }
    });
    std::atomic<bool> inputComplete(false);
    std::thread writer([&] {
        size_t offset = 0;
        while (offset < input.size()) {
            DWORD written = 0;
            if (!WriteFile(inputWrite.value, input.data() + offset, static_cast<DWORD>(input.size() - offset), &written, nullptr) || !written) break;
            offset += written;
        }
        inputComplete = offset == input.size();
        inputWrite.reset();
    });
    const DWORD wait = WaitForSingleObject(child.value, PencilScriptPolicy::HostWaitMilliseconds);
    const bool hardTimeout = wait == WAIT_TIMEOUT;
    if (hardTimeout) { TerminateJobObject(job.value, 97); WaitForSingleObject(child.value, PencilScriptPolicy::HostTerminationWaitMilliseconds); }
    DWORD exitCode = 0;
    GetExitCodeProcess(child.value, &exitCode);
    writer.join();
    reader.join();
    child.reset(); thread.reset(); job.reset();
    profileScope.Close();
    imageAcl.Restore();
    std::ostringstream out;
    out << "{\"protocol\":1,\"contained\":" << "true" << ",\"profile_create_hr\":" << static_cast<unsigned>(createProfile)
        << ",\"lpac_policy\":" << "true"
        << ",\"profile_delete_hr\":" << static_cast<unsigned>(profileScope.deleted) << ",\"hard_timeout\":" << (hardTimeout ? "true" : "false")
        << ",\"output_overflow\":" << (outputOverflow ? "true" : "false") << ",\"worker_exit\":" << exitCode << ",\"worker_stdout\":" << Quote(output) << "}";
    // Attach lifecycle facts outside the untrusted worker's output.
    std::string json = out.str();
    json.pop_back();
    json += ",\"image_acl_restore_error\":" + std::to_string(imageAcl.restored)
        + ",\"input_complete\":" + (inputComplete ? "true" : "false") + "}";
    Emit(json);
    return 0;
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 2 && std::wstring(argv[1]) == L"--worker") return Worker();
        if (argc == 2) return Host(argv);
        Emit("{\"usage_error\":true}"); return 2;
    } catch (const std::exception& error) { Emit("{\"host_error\":" + Quote(error.what()) + "}"); return 3; }
}
