#include "injection.h"
#include <thread>
#include <chrono>
#include <Psapi.h>
#include <Shlwapi.h>
#include <TlHelp32.h>
#include <taskschd.h>
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "Advapi32.lib")

// Прототипы функций
bool SetPrivilege();
bool IsTargetProcess(const char* processName);
HANDLE GetProcessByName(const char* processName);
int GetPIDByName(const char* ProcName);
bool GetProcessPath(DWORD processId, wchar_t* path, size_t size);

const char* ProcName = "RustClient.exe";
const wchar_t* TargetPath = L"C:\\Users\\d-560\\Desktop\\261\\RustClient.exe";
const wchar_t* DllPath = L"C:\\Windows\\System32\\CoreMessagingConfig.dll";

// Добавляем переменную для хранения PID процесса с инжектом
DWORD g_InjectedPID = 0;

bool CreateStartupTask() {
	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr)) return false;

	ITaskService* pService = NULL;
	hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService);
	if (FAILED(hr)) {
		CoUninitialize();
		return false;
	}

	hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
	if (FAILED(hr)) {
		pService->Release();
		CoUninitialize();
		return false;
	}

	ITaskFolder* pRootFolder = NULL;
	hr = pService->GetFolder(_bstr_t(L"\\"), &pRootFolder);
	if (FAILED(hr)) {
		pService->Release();
		CoUninitialize();
		return false;
	}

	// Удаляем существующую задачу если она есть
	pRootFolder->DeleteTask(_bstr_t(L"RuntimeService"), 0);

	ITaskDefinition* pTask = NULL;
	hr = pService->NewTask(0, &pTask);
	if (FAILED(hr)) {
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return false;
	}

	IRegistrationInfo* pRegInfo = NULL;
	hr = pTask->get_RegistrationInfo(&pRegInfo);
	if (FAILED(hr)) {
		pTask->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return false;
	}

	hr = pRegInfo->put_Author(_bstr_t(L"Microsoft Corporation"));
	pRegInfo->Release();

	IPrincipal* pPrincipal = NULL;
	hr = pTask->get_Principal(&pPrincipal);
	if (FAILED(hr)) {
		pTask->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return false;
	}

	hr = pPrincipal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
	pPrincipal->Release();

	ITaskSettings* pSettings = NULL;
	hr = pTask->get_Settings(&pSettings);
	if (FAILED(hr)) {
		pTask->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return false;
	}

	hr = pSettings->put_StartWhenAvailable(VARIANT_TRUE);
	hr = pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
	hr = pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
	hr = pSettings->put_ExecutionTimeLimit(_bstr_t(L"PT0S")); // Бесконечное время выполнения
	hr = pSettings->put_Hidden(VARIANT_TRUE); // Скрываем задачу
	pSettings->Release();

	ITriggerCollection* pTriggerCollection = NULL;
	hr = pTask->get_Triggers(&pTriggerCollection);
	if (FAILED(hr)) {
		pTask->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return false;
	}

	ITrigger* pTrigger = NULL;
	hr = pTriggerCollection->Create(TASK_TRIGGER_LOGON, &pTrigger);
	pTriggerCollection->Release();
	if (FAILED(hr)) {
		pTask->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return false;
	}

	ILogonTrigger* pLogonTrigger = NULL;
	hr = pTrigger->QueryInterface(IID_ILogonTrigger, (void**)&pLogonTrigger);
	pTrigger->Release();
	if (FAILED(hr)) {
		pTask->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return false;
	}

	hr = pLogonTrigger->put_Id(_bstr_t(L"Trigger1"));
	hr = pLogonTrigger->put_Delay(_bstr_t(L"PT30S")); // Задержка 30 секунд после входа
	pLogonTrigger->Release();

	IActionCollection* pActionCollection = NULL;
	hr = pTask->get_Actions(&pActionCollection);
	if (FAILED(hr)) {
		pTask->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return false;
	}

	IAction* pAction = NULL;
	hr = pActionCollection->Create(TASK_ACTION_EXEC, &pAction);
	pActionCollection->Release();
	if (FAILED(hr)) {
		pTask->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return false;
	}

	IExecAction* pExecAction = NULL;
	hr = pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction);
	pAction->Release();
	if (FAILED(hr)) {
		pTask->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return false;
	}

	wchar_t exePath[MAX_PATH];
	GetModuleFileNameW(NULL, exePath, MAX_PATH);
	hr = pExecAction->put_Path(_bstr_t(exePath));
	pExecAction->Release();

	IRegisteredTask* pRegisteredTask = NULL;
	hr = pRootFolder->RegisterTaskDefinition(
		_bstr_t(L"RuntimeService"),
		pTask,
		TASK_CREATE_OR_UPDATE,
		_variant_t(),
		_variant_t(),
		TASK_LOGON_INTERACTIVE_TOKEN,
		_variant_t(L""),
		&pRegisteredTask);

	if (pRegisteredTask) pRegisteredTask->Release();
	pTask->Release();
	pRootFolder->Release();
	pService->Release();
	CoUninitialize();

	return SUCCEEDED(hr);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	SetPrivilege();

	static bool taskCreated = false;
	if (!taskCreated) {
		CreateStartupTask();
		taskCreated = true;
	}

	// Основной цикл инжектора
	bool isInjected = false;
	while (true) {
		if ((GetAsyncKeyState(VK_NUMPAD5) & 0x8000) && (GetAsyncKeyState(VK_NUMPAD6) & 0x8000) && !isInjected) {
			DWORD currentPID = GetPIDByName(ProcName);
			if (currentPID != 0 && currentPID != g_InjectedPID && IsTargetProcess(ProcName)) {
				Sleep(2000); // Ждем инициализацию процесса

				HANDLE hProc = GetProcessByName(ProcName);
				if (hProc) {
					char ansiPath[MAX_PATH];
					size_t convertedChars = 0;
					wcstombs_s(&convertedChars, ansiPath, DllPath, MAX_PATH);

					if (ManualMap(hProc, ansiPath)) {
						g_InjectedPID = currentPID;
						isInjected = true;
					}
					CloseHandle(hProc);
				}
			}
		}
		else if (!(GetAsyncKeyState(VK_NUMPAD5) & 0x8000) && !(GetAsyncKeyState(VK_NUMPAD6) & 0x8000)) {
			isInjected = false;
		}
		Sleep(50);
	}

	return 0;
}

bool GetProcessPath(DWORD processId, wchar_t* path, size_t size) {
	HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
	if (hProcess == NULL) return false;

	bool result = GetModuleFileNameExW(hProcess, NULL, path, size) != 0;
	CloseHandle(hProcess);
	return result;
}

HANDLE GetProcessByName(const char* processName) {
	DWORD processId = 0;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) return NULL;

	PROCESSENTRY32W processEntry = { sizeof(PROCESSENTRY32W) };
	wchar_t wProcessName[MAX_PATH];
	MultiByteToWideChar(CP_UTF8, 0, processName, -1, wProcessName, MAX_PATH);

	if (Process32FirstW(snapshot, &processEntry)) {
		do {
			if (_wcsicmp(processEntry.szExeFile, wProcessName) == 0) {
				wchar_t processPath[MAX_PATH];
				if (GetProcessPath(processEntry.th32ProcessID, processPath, MAX_PATH)) {
					if (_wcsicmp(processPath, TargetPath) == 0) {
						processId = processEntry.th32ProcessID;
						break;
					}
				}
			}
		} while (Process32NextW(snapshot, &processEntry));
	}

	CloseHandle(snapshot);

	if (processId == 0) return NULL;

	return OpenProcess(
		PROCESS_CREATE_THREAD |
		PROCESS_QUERY_INFORMATION |
		PROCESS_VM_OPERATION |
		PROCESS_VM_WRITE |
		PROCESS_VM_READ,
		FALSE,
		processId
	);
}

bool IsTargetProcess(const char* processName) {
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) return false;

	PROCESSENTRY32W processEntry = { sizeof(PROCESSENTRY32W) };
	bool found = false;

	wchar_t wProcessName[MAX_PATH];
	MultiByteToWideChar(CP_UTF8, 0, processName, -1, wProcessName, MAX_PATH);

	if (Process32FirstW(snapshot, &processEntry)) {
		do {
			if (_wcsicmp(processEntry.szExeFile, wProcessName) == 0) {
				wchar_t processPath[MAX_PATH];
				if (GetProcessPath(processEntry.th32ProcessID, processPath, MAX_PATH)) {
					if (_wcsicmp(processPath, TargetPath) == 0) {
						found = true;
						break;
					}
				}
			}
		} while (Process32NextW(snapshot, &processEntry));
	}

	CloseHandle(snapshot);
	return found;
}

bool SetPrivilege() {
	HANDLE hToken;
	TOKEN_PRIVILEGES tp;
	LUID luid;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
		return false;
	}

	if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
		CloseHandle(hToken);
		return false;
	}

	tp.PrivilegeCount = 1;
	tp.Privileges[0].Luid = luid;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL)) {
		CloseHandle(hToken);
		return false;
	}

	CloseHandle(hToken);
	return true;
}
