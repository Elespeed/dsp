#ifndef JAW_LANDMARK_BRIDGE_H
#define JAW_LANDMARK_BRIDGE_H

#include <windows.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "FaceSegmentation.h"

namespace JawLandmarkBridge
{
class Worker
{
public:
    Worker() : input_(NULL), output_(NULL), process_(NULL), lastStart_(0) {}
    ~Worker() { Stop(); }

    bool Detect(HMODULE module, int width, int height,
                const BYTE* yPlane, const BYTE* uPlane, const BYTE* vPlane,
                FaceSegmentation::MaskPoint& chin,
                std::vector<FaceSegmentation::MaskPoint>& oval)
    {
        if (!yPlane || !uPlane || !vPlane || width < 320 || height < 240 ||
            width % 2 != 0 || !Start(module))
            return false;

        // 与离线实验使用相同的 RGB 输入顺序，缩小后传给常驻模型进程。
        std::vector<BYTE> rgb(320 * 240 * 3);
        for (int row = 0; row < 240; ++row)
            for (int col = 0; col < 320; ++col)
            {
                const int sourceY = row * height / 240;
                const int sourceX = col * width / 320;
                const int colorIndex = sourceY * (width / 2) + sourceX / 2;
                const int y = yPlane[sourceY * width + sourceX];
                const int u = uPlane[colorIndex] - 128;
                const int v = vPlane[colorIndex] - 128;
                const int index = (row * 320 + col) * 3;
                rgb[index] = Clip(y + static_cast<int>(1.370705 * v));
                rgb[index + 1] = Clip(y - static_cast<int>(0.698001 * v) -
                                      static_cast<int>(0.337633 * u));
                rgb[index + 2] = Clip(y + static_cast<int>(1.732446 * u));
            }
        DWORD written = 0;
        if (!WriteFile(input_, rgb.data(), static_cast<DWORD>(rgb.size()),
                       &written, NULL) || written != rgb.size())
        {
            Stop();
            return false;
        }
        BYTE response[149] = {};
        if (!WaitForBytes(sizeof(response), 500) ||
            !ReadExact(response, sizeof(response)))
        {
            Stop();
            return false;
        }
        if (response[0] != 1)
            return false;
        short coordinates[74];
        std::memcpy(coordinates, response + 1, sizeof(coordinates));
        chin.x = coordinates[0];
        chin.y = coordinates[1];
        oval.resize(36);
        for (int i = 0; i < 36; ++i)
        {
            oval[i].x = coordinates[2 + i * 2];
            oval[i].y = coordinates[3 + i * 2];
        }
        return true;
    }

    void Stop()
    {
        if (input_)
            CloseHandle(input_);
        input_ = NULL;
        if (output_)
            CloseHandle(output_);
        output_ = NULL;
        if (process_)
        {
            if (WaitForSingleObject(process_, 200) == WAIT_TIMEOUT)
                TerminateProcess(process_, 1);
            CloseHandle(process_);
        }
        process_ = NULL;
    }

private:
    static BYTE Clip(int value)
    {
        return static_cast<BYTE>(std::max(0, std::min(255, value)));
    }

    bool WaitForBytes(DWORD needed, DWORD timeout)
    {
        const DWORD started = GetTickCount();
        while (GetTickCount() - started < timeout)
        {
            DWORD available = 0;
            if (!PeekNamedPipe(output_, NULL, 0, NULL, &available, NULL))
                return false;
            if (available >= needed)
                return true;
            Sleep(1);
        }
        return false;
    }

    bool ReadExact(BYTE* data, DWORD size)
    {
        DWORD received = 0;
        while (received < size)
        {
            DWORD count = 0;
            if (!ReadFile(output_, data + received, size - received,
                          &count, NULL) || count == 0)
                return false;
            received += count;
        }
        return true;
    }

    bool Start(HMODULE module)
    {
        if (process_)
            return true;
        if (GetTickCount() - lastStart_ < 5000)
            return false;
        lastStart_ = GetTickCount();
        char modulePath[MAX_PATH] = {};
        const DWORD count = GetModuleFileNameA(module, modulePath, MAX_PATH);
        if (!count || count >= MAX_PATH)
            return false;
        std::string folder(modulePath);
        const size_t separator = folder.find_last_of("\\/");
        if (separator == std::string::npos)
            return false;
        folder.resize(separator);
        const std::string python = folder + "\\.venv\\Scripts\\python.exe";
        const std::string model = folder + "\\face_landmarker.task";
        const std::string script = folder +
            "\\..\\..\\PlugIns\\FaceLocator\\jaw_landmark_worker.py";
        if (GetFileAttributesA(python.c_str()) == INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesA(model.c_str()) == INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesA(script.c_str()) == INVALID_FILE_ATTRIBUTES)
            return false;

        SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
        HANDLE childInput = NULL, childOutput = NULL;
        if (!CreatePipe(&childInput, &input_, &security, 0))
            return false;
        if (!CreatePipe(&output_, &childOutput, &security, 0))
        {
            CloseHandle(childInput);
            Stop();
            return false;
        }
        SetHandleInformation(input_, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(output_, HANDLE_FLAG_INHERIT, 0);
        HANDLE errors = CreateFileA("NUL", GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        STARTUPINFOA startup = {};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = childInput;
        startup.hStdOutput = childOutput;
        startup.hStdError = errors == INVALID_HANDLE_VALUE ? childOutput : errors;
        PROCESS_INFORMATION info = {};
        std::string command = "\"" + python + "\" \"" + script +
                              "\" \"" + model + "\"";
        std::vector<char> commandBuffer(command.begin(), command.end());
        commandBuffer.push_back('\0');
        const BOOL created = CreateProcessA(python.c_str(), commandBuffer.data(),
                                            NULL, NULL, TRUE, CREATE_NO_WINDOW,
                                            NULL, folder.c_str(), &startup, &info);
        CloseHandle(childInput);
        CloseHandle(childOutput);
        if (errors != INVALID_HANDLE_VALUE)
            CloseHandle(errors);
        if (!created)
        {
            Stop();
            return false;
        }
        process_ = info.hProcess;
        CloseHandle(info.hThread);
        BYTE ready = 0;
        if (!WaitForBytes(1, 10000) || !ReadExact(&ready, 1) || ready != 'R')
        {
            Stop();
            return false;
        }
        return true;
    }

    HANDLE input_;
    HANDLE output_;
    HANDLE process_;
    DWORD lastStart_;
};

inline Worker& Instance()
{
    static Worker worker;
    return worker;
}
}

#endif
