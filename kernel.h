#ifndef KERNEL_H
#define KERNEL_H

#include <iostream>
#include <windows.h>
#include <winternl.h>
#include <thread>
// #include "ntloadup.h" // disabled: using kdmapper for manual load
#define STATUS_SUCCESS  ((NTSTATUS)0x00000000L)

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

typedef NTSTATUS(
    NTAPI*
    NtDeviceIoControlFile_t)(
        IN HANDLE FileHandle,
        IN HANDLE Event OPTIONAL,
        IN PIO_APC_ROUTINE ApcRoutine OPTIONAL,
        IN PVOID ApcContext OPTIONAL,
        OUT PIO_STATUS_BLOCK IoStatusBlock,
        IN ULONG IoControlCode,
        IN PVOID InputBuffer OPTIONAL,
        IN ULONG InputBufferLength,
        OUT PVOID OutputBuffer OPTIONAL,
        IN ULONG OutputBufferLength
        );

inline BOOLEAN DEBUG = false;

inline void Ulog(const char* const _Format, ...) {
    if (!DEBUG)
        return;

    va_list args;
    va_start(args, _Format);
    vprintf(_Format, args);
    va_end(args);
}

// New IOCTLs aligned with new driver
#define IOCTL_ATTACH CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DETACH CTL_CODE(FILE_DEVICE_UNKNOWN, 0x901, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_READ   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x902, METHOD_OUT_DIRECT, FILE_ANY_ACCESS)
#define IOCTL_WRITE  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x903, METHOD_IN_DIRECT, FILE_ANY_ACCESS)
#define IOCTL_GETMODULE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x904, METHOD_BUFFERED, FILE_ANY_ACCESS)
// New: get DLL base by name
#define IOCTL_GET_DLL_BASE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x905, METHOD_BUFFERED, FILE_ANY_ACCESS)
// New: enumerate module bases
#define IOCTL_ENUM_MODULES CTL_CODE(FILE_DEVICE_UNKNOWN, 0x906, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _MODULE_BASE_REQUEST {
    ULONG ProcessId;
    UINT64 BaseAddress; // out
} MODULE_BASE_REQUEST, * PMODULE_BASE_REQUEST;

typedef struct _MEMORY_OPERATION {
    ULONG ProcessId;
    PVOID Address;
    SIZE_T Size;
} MEMORY_OPERATION, * PMEMORY_OPERATION;

// Must match kernel driver's structure layout
typedef struct _ENUM_MODULES_REQUEST {
    ULONG ProcessId;       // in
    ULONG MaxCount;        // in
    ULONG ReturnedCount;   // out
    UINT64 Bases[1];       // out (variable-length tail)
} ENUM_MODULES_REQUEST, * PENUM_MODULES_REQUEST;

inline class _kernel
{
private:
    NtDeviceIoControlFile_t NtDeviceIoControlFileImport = nullptr;
    typedef NTSTATUS(NTAPI* NtQuerySystemInformation_t)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
    NtQuerySystemInformation_t NtQuerySystemInformationImport = nullptr;
public:
    HANDLE kernelHandle = INVALID_HANDLE_VALUE;
    ULONG Pid = 0; // PID целевого процесса (используется драйвером; это не HANDLE)

    bool CacheProcessDirectoryTableBase()
    {
        if (kernelHandle == INVALID_HANDLE_VALUE)
            return false;

        ULONG targetPid = (ULONG)Pid;
        IO_STATUS_BLOCK ioStatus{};
        NTSTATUS status = NtDeviceIoControlFileImport(
            kernelHandle,
            NULL,
            NULL,
            NULL,
            &ioStatus,
            IOCTL_ATTACH,
            &targetPid,
            sizeof(targetPid),
            &targetPid,
            sizeof(targetPid)
        );
        return NT_SUCCESS(status) && NT_SUCCESS(ioStatus.Status);
    }

    bool Attach(const wchar_t* ProcessName)
    {
        HMODULE NTDLL = GetModuleHandleA("ntdll.dll");
        if (!NTDLL) return false;

        NtDeviceIoControlFileImport = (NtDeviceIoControlFile_t)GetProcAddress(NTDLL, "NtDeviceIoControlFile");
        NtQuerySystemInformationImport = (NtQuerySystemInformation_t)GetProcAddress(NTDLL, "NtQuerySystemInformation");
        if (!NtDeviceIoControlFileImport || !NtQuerySystemInformationImport)
            return false;

        // if (!driver::isloaded())
        // {
        //     driver::load(driver::rawData, sizeof(driver::rawData));
        // }

        // Enumerate processes via NtQuerySystemInformation (no Toolhelp)
        ULONG bufSize = 1 << 16; // 64KB start
        PBYTE buffer = nullptr;
        NTSTATUS qst = 0;
        for (int i = 0; i < 8; ++i) {
            buffer = (PBYTE)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bufSize);
            if (!buffer) return false;
            qst = NtQuerySystemInformationImport(SystemProcessInformation, buffer, bufSize, nullptr);
            if (qst == 0) break; // STATUS_SUCCESS
            HeapFree(GetProcessHeap(), 0, buffer);
            buffer = nullptr;
            bufSize <<= 1; // grow
        }
        if (qst != 0 || !buffer) return false;

        typedef struct _SYSTEM_PROCESS_INFORMATION_LOCAL {
            ULONG NextEntryOffset;
            ULONG NumberOfThreads;
            LARGE_INTEGER WorkingSetPrivateSize; // Reserved
            ULONG HardFaultCount;
            ULONG NumberOfThreadsHighWatermark;
            ULONGLONG CycleTime;
            LARGE_INTEGER CreateTime;
            LARGE_INTEGER UserTime;
            LARGE_INTEGER KernelTime;
            UNICODE_STRING ImageName;
            KPRIORITY BasePriority;
            HANDLE UniqueProcessId;
            HANDLE InheritedFromUniqueProcessId;
        } SYSTEM_PROCESS_INFORMATION_LOCAL, * PSYSTEM_PROCESS_INFORMATION_LOCAL;

        PSYSTEM_PROCESS_INFORMATION_LOCAL p = (PSYSTEM_PROCESS_INFORMATION_LOCAL)buffer;
        for (;;) {
            if (p->ImageName.Buffer && _wcsicmp(p->ImageName.Buffer, ProcessName) == 0) {
                Pid = (ULONG)(ULONG_PTR)p->UniqueProcessId;
                break;
            }
            if (p->NextEntryOffset == 0) break;
            p = (PSYSTEM_PROCESS_INFORMATION_LOCAL)((PUCHAR)p + p->NextEntryOffset);
        }
        HeapFree(GetProcessHeap(), 0, buffer);

        if (Pid == 0)
        {
            Ulog("failed to find process\n");
            return false;
        }

        kernelHandle = CreateFileW(
            L"\\\\.\\MySecureLink",
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (kernelHandle == INVALID_HANDLE_VALUE)
        {
            DWORD err = GetLastError();
            Ulog("failed to get driver handle, GLE=%lu\n", err);
            // Fallback: open device directly if symlink is missing
            if (err == ERROR_FILE_NOT_FOUND)
            {
                kernelHandle = CreateFileW(
                    L"\\\\.\\Globalroot\\Device\\MySecureDrv",
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL
                );
                if (kernelHandle == INVALID_HANDLE_VALUE)
                {
                    DWORD err2 = GetLastError();
                    Ulog("fallback open by \\.\\Globalroot\\Device failed, GLE=%lu\n", err2);
                    return false;
                }
            }
            else
            {
                return false;
            }
        }

        // Ensure the DTB is cached before returning so subsequent operations can succeed immediately
        bool cached = false;
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            if (CacheProcessDirectoryTableBase())
            {
                cached = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!cached)
        {
            Ulog("failed to cache DTB for process\n");
            CloseHandle(kernelHandle);
            kernelHandle = INVALID_HANDLE_VALUE;
            Pid = 0;
            return false;
        }

        return true;
    }

    void Detach()
    {
        if (kernelHandle != INVALID_HANDLE_VALUE)
        {
            if (NtDeviceIoControlFileImport)
            {
                IO_STATUS_BLOCK ioStatus{};
                NtDeviceIoControlFileImport(
                    kernelHandle,
                    NULL,
                    NULL,
                    NULL,
                    &ioStatus,
                    IOCTL_DETACH,
                    NULL,
                    0,
                    NULL,
                    0
                );
            }
            CloseHandle(kernelHandle);
            kernelHandle = INVALID_HANDLE_VALUE;
        }
        Pid = 0;
    }

    bool ReadVirtualMemory(uintptr_t Address, void* Buffer, SIZE_T Size)
    {
        if (kernelHandle == INVALID_HANDLE_VALUE)
            return false;
        if (Size == 0 || Size > 0x100000)
            return false;

        MEMORY_OPERATION op{};
        op.ProcessId = (ULONG)Pid;
        op.Address = (PVOID)Address;
        op.Size = Size;

        IO_STATUS_BLOCK ioStatus{};
        NTSTATUS status = NtDeviceIoControlFileImport(
            kernelHandle,
            NULL,
            NULL,
            NULL,
            &ioStatus,
            IOCTL_READ,
            &op,
            sizeof(op),
            Buffer,
            (ULONG)Size
        );
        if (NT_SUCCESS(status) && NT_SUCCESS(ioStatus.Status))
            return true;

        // One-time retry after refreshing DTB cache
        if (CacheProcessDirectoryTableBase()) {
            ZeroMemory(&ioStatus, sizeof(ioStatus));
            status = NtDeviceIoControlFileImport(
                kernelHandle,
                NULL,
                NULL,
                NULL,
                &ioStatus,
                IOCTL_READ,
                &op,
                sizeof(op),
                Buffer,
                (ULONG)Size
            );
            if (NT_SUCCESS(status) && NT_SUCCESS(ioStatus.Status))
                return true;
        }
        return false;
    }

    bool WriteVirtualMemory(uintptr_t Address, void* Buffer, SIZE_T Size)
    {
        if (kernelHandle == INVALID_HANDLE_VALUE)
            return false;
        if (Size == 0 || Size > 0x100000)
            return false;

        MEMORY_OPERATION op{};
        op.ProcessId = (ULONG)Pid;
        op.Address = (PVOID)Address;
        op.Size = Size;

        IO_STATUS_BLOCK ioStatus{};
        NTSTATUS status = NtDeviceIoControlFileImport(
            kernelHandle,
            NULL,
            NULL,
            NULL,
            &ioStatus,
            IOCTL_WRITE,
            &op,
            sizeof(op),
            Buffer,
            (ULONG)Size
        );
        if (NT_SUCCESS(status) && NT_SUCCESS(ioStatus.Status))
            return true;

        // One-time retry after refreshing DTB cache
        if (CacheProcessDirectoryTableBase()) {
            ZeroMemory(&ioStatus, sizeof(ioStatus));
            status = NtDeviceIoControlFileImport(
                kernelHandle,
                NULL,
                NULL,
                NULL,
                &ioStatus,
                IOCTL_WRITE,
                &op,
                sizeof(op),
                Buffer,
                (ULONG)Size
            );
            if (NT_SUCCESS(status) && NT_SUCCESS(ioStatus.Status))
                return true;
        }
        return false;
    }

    // Const-friendly overload
    bool WriteVirtualMemory(uintptr_t Address, const void* Buffer, SIZE_T Size)
    {
        return WriteVirtualMemory(Address, const_cast<void*>(Buffer), Size);
    }

    bool EnumModuleBases(uint64_t* Bases, ULONG Capacity, ULONG* Returned)
    {
        if (kernelHandle == INVALID_HANDLE_VALUE || Pid == 0 || Bases == nullptr || Capacity == 0 || Returned == nullptr)
            return false;

        SIZE_T reqSize = sizeof(ENUM_MODULES_REQUEST) + (SIZE_T)Capacity * sizeof(UINT64);
        PENUM_MODULES_REQUEST rq = (PENUM_MODULES_REQUEST)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, reqSize);
        if (!rq) return false;

        rq->ProcessId = (ULONG)Pid;
        rq->MaxCount = Capacity;
        rq->ReturnedCount = 0;

        IO_STATUS_BLOCK ioStatus{};
        NTSTATUS status = NtDeviceIoControlFileImport(
            kernelHandle,
            NULL,
            NULL,
            NULL,
            &ioStatus,
            IOCTL_ENUM_MODULES,
            rq,
            (ULONG)reqSize,
            rq,
            (ULONG)reqSize
        );

        bool ok = NT_SUCCESS(status) && NT_SUCCESS(ioStatus.Status);
        if (ok)
        {
            ULONG count = rq->ReturnedCount;
            if (count > Capacity) count = Capacity; // safety
            memcpy(Bases, rq->Bases, (SIZE_T)count * sizeof(UINT64));
            *Returned = rq->ReturnedCount;
        }

        HeapFree(GetProcessHeap(), 0, rq);
        return ok;
    }

    // Removed Toolhelp-based GetModuleBase; use GETMODULE macro (IOCTL) instead

    uintptr_t GetMainModuleBaseViaIoctl()
    {
        if (kernelHandle == INVALID_HANDLE_VALUE || Pid == 0)
            return 0;

        MODULE_BASE_REQUEST rq{};
        rq.ProcessId = (ULONG)Pid;
        IO_STATUS_BLOCK ioStatus{};
        NTSTATUS status = NtDeviceIoControlFileImport(
            kernelHandle,
            NULL,
            NULL,
            NULL,
            &ioStatus,
            IOCTL_GETMODULE,
            &rq,
            sizeof(rq),
            &rq,
            sizeof(rq)
        );
        if (NT_SUCCESS(status) && NT_SUCCESS(ioStatus.Status))
        {
            return (uintptr_t)rq.BaseAddress;
        }
        return 0;
    }

    uintptr_t GetModuleDll(const wchar_t* dllName)
    {
        if (kernelHandle == INVALID_HANDLE_VALUE || Pid == 0 || dllName == nullptr)
            return 0;

        struct DLL_BASE_REQUEST_LOCAL {
            ULONG ProcessId;
            WCHAR DllName[260];
            UINT64 DllBase;
        } rq{};

        rq.ProcessId = (ULONG)Pid;
        wcsncpy_s(rq.DllName, dllName, _TRUNCATE);

        IO_STATUS_BLOCK ioStatus{};
        NTSTATUS status = NtDeviceIoControlFileImport(
            kernelHandle,
            NULL,
            NULL,
            NULL,
            &ioStatus,
            IOCTL_GET_DLL_BASE,
            &rq,
            sizeof(rq),
            &rq,
            sizeof(rq)
        );
        if (NT_SUCCESS(status) && NT_SUCCESS(ioStatus.Status))
        {
            return (uintptr_t)rq.DllBase;
        }
        return 0;
    }


    template<typename T>
    T read(uintptr_t Address)
    {
        T Buffer{};
        this->ReadVirtualMemory(Address, &Buffer, sizeof(T));
        return Buffer;
    }

    template<typename T>
    void write(uintptr_t Address, const T& Buffer)
    {
        T temp = Buffer; // create non-const copy to satisfy void* signature
        this->WriteVirtualMemory(Address, &temp, sizeof(T));
    }

} kernel;

// Plain free-function API (no class prefix)
inline bool Attach(const wchar_t* ProcessName) { return kernel.Attach(ProcessName); }
inline void Detach() { kernel.Detach(); }
inline bool Read(uintptr_t Address, void* Buffer, SIZE_T Size) { return kernel.ReadVirtualMemory(Address, Buffer, Size); }
inline bool Write(uintptr_t Address, void* Buffer, SIZE_T Size) { return kernel.WriteVirtualMemory(Address, Buffer, Size); }
inline uintptr_t GetModuleBase() { return kernel.GetMainModuleBaseViaIoctl(); }
inline uintptr_t GetModuleBase(const wchar_t*) { return kernel.GetMainModuleBaseViaIoctl(); }
inline uintptr_t GetModuleDll(const wchar_t* name) { return kernel.GetModuleDll(name); }
inline bool EnumModuleBases(uint64_t* Bases, ULONG Capacity, ULONG* Returned) { return kernel.EnumModuleBases(Bases, Capacity, Returned); }

template<typename T>
inline T Read(uintptr_t Address) { return kernel.read<T>(Address); }

template<typename T>
inline void Write(uintptr_t Address, const T& value) { kernel.write<T>(Address, value); }



#endif // !KERNEL_H
