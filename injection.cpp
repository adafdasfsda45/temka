#include "injection.h"

static size_t
WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp) {
	size_t realsize = size * nmemb;
	struct MemoryStruct* mem = (struct MemoryStruct*)userp;
	char* ptr = (char*)realloc(mem->memory, mem->size + realsize + 1);
	mem->memory = ptr;
	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;

	return realsize;
}

static int ProgressBar(void* ptr, double TotalToDownload, double NowDownloaded, double TotalToUpload, double NowUploaded) {
	if (TotalToDownload <= 0.0)
		return 0;

	int totaldotz = 40;
	double fractiondownloaded = NowDownloaded / TotalToDownload;
	int dotz = (int)round(fractiondownloaded * totaldotz);
	int ii = 0;

	printf("%3.0f%% [", fractiondownloaded * 100);

	for (; ii < dotz; ii++)
		printf("=");

	for (; ii < totaldotz; ii++)
		printf(" ");

	printf("]\r");
	fflush(stdout);

	return 0;
}

bool ManualMap(HANDLE hProc, const char* dllPath) {
	FILE* file = nullptr;
	fopen_s(&file, dllPath, "rb");
	if (!file) return false;

	fseek(file, 0, SEEK_END);
	long fileSize = ftell(file);
	fseek(file, 0, SEEK_SET);

	char* fileData = (char*)malloc(fileSize);
	if (!fileData) {
		fclose(file);
		return false;
	}

	size_t bytesRead = fread(fileData, 1, fileSize, file);
	fclose(file);

	if (bytesRead != fileSize) {
		free(fileData);
		return false;
	}

	BYTE* pSrcData = reinterpret_cast<BYTE*>(fileData);
	IMAGE_NT_HEADERS* pOldNtHeader = nullptr;
	IMAGE_OPTIONAL_HEADER* pOldOptHeader = nullptr;
	IMAGE_FILE_HEADER* pOldFileHeader = nullptr;
	BYTE* pTargetBase = nullptr;

	if (reinterpret_cast<IMAGE_DOS_HEADER*>(pSrcData)->e_magic != 0x5A4D) {
		free(fileData);
		return false;
	}

	pOldNtHeader = reinterpret_cast<IMAGE_NT_HEADERS*>(pSrcData + reinterpret_cast<IMAGE_DOS_HEADER*>(pSrcData)->e_lfanew);
	pOldOptHeader = &pOldNtHeader->OptionalHeader;
	pOldFileHeader = &pOldNtHeader->FileHeader;

	pTargetBase = reinterpret_cast<BYTE*>(VirtualAllocEx(hProc, nullptr, pOldOptHeader->SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
	if (!pTargetBase) {
		free(fileData);
		return false;
	}

	DWORD oldProtect;
	if (!VirtualProtectEx(hProc, pTargetBase, pOldOptHeader->SizeOfImage, PAGE_EXECUTE_READWRITE, &oldProtect)) {
		VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
		free(fileData);
		return false;
	}

	MANUAL_MAPPING_DATA data{ 0 };
	data.pLoadLibraryA = LoadLibraryA;
	data.pGetProcAddress = GetProcAddress;
	data.pRtlAddFunctionTable = (f_RtlAddFunctionTable)RtlAddFunctionTable;
	data.pbase = pTargetBase;
	data.fdwReasonParam = DLL_PROCESS_ATTACH;
	data.reservedParam = 0;
	data.SEHSupport = true;

	if (!WriteProcessMemory(hProc, pTargetBase, pSrcData, 0x1000, nullptr)) {
		VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
		free(fileData);
		return false;
	}

	IMAGE_SECTION_HEADER* pSectionHeader = IMAGE_FIRST_SECTION(pOldNtHeader);
	for (UINT i = 0; i != pOldFileHeader->NumberOfSections; ++i, ++pSectionHeader) {
		if (pSectionHeader->SizeOfRawData) {
			// Устанавливаем права доступа для каждой секции отдельно
			DWORD sectionProtection = PAGE_READWRITE; // По умолчанию READ/WRITE
			
			if (pSectionHeader->Characteristics & IMAGE_SCN_MEM_EXECUTE) {
				sectionProtection = PAGE_EXECUTE_READWRITE;
			}
			
			DWORD oldProtect;
			if (!VirtualProtectEx(hProc, pTargetBase + pSectionHeader->VirtualAddress, 
				pSectionHeader->SizeOfRawData, sectionProtection, &oldProtect)) {
				continue;
			}
			
			// Записываем данные секции
			if (!WriteProcessMemory(hProc, pTargetBase + pSectionHeader->VirtualAddress, 
				pSrcData + pSectionHeader->PointerToRawData, pSectionHeader->SizeOfRawData, nullptr)) {
				VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
				free(fileData);
				return false;
			}
		}
	}

	BYTE* MappingDataAlloc = reinterpret_cast<BYTE*>(VirtualAllocEx(hProc, nullptr, sizeof(MANUAL_MAPPING_DATA), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	if (!MappingDataAlloc) {
		VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
		free(fileData);
		return false;
	}

	if (!WriteProcessMemory(hProc, MappingDataAlloc, &data, sizeof(MANUAL_MAPPING_DATA), nullptr)) {
		VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
		VirtualFreeEx(hProc, MappingDataAlloc, 0, MEM_RELEASE);
		free(fileData);
		return false;
	}

	void* pShellcode = VirtualAllocEx(hProc, nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (!pShellcode) {
		VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
		VirtualFreeEx(hProc, MappingDataAlloc, 0, MEM_RELEASE);
		free(fileData);
		return false;
	}

	if (!WriteProcessMemory(hProc, pShellcode, Shellcode, 0x1000, nullptr)) {
		VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
		VirtualFreeEx(hProc, MappingDataAlloc, 0, MEM_RELEASE);
		VirtualFreeEx(hProc, pShellcode, 0, MEM_RELEASE);
		free(fileData);
		return false;
	}

	HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(pShellcode), MappingDataAlloc, 0, nullptr);
	if (!hThread) {
		VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
		VirtualFreeEx(hProc, MappingDataAlloc, 0, MEM_RELEASE);
		VirtualFreeEx(hProc, pShellcode, 0, MEM_RELEASE);
		free(fileData);
		return false;
	}

	Sleep(2000); // Уменьшено с 10000 до 2000

	DWORD exitcode = 0;
	if (!GetExitCodeProcess(hProc, &exitcode) || exitcode != STILL_ACTIVE) {
		CloseHandle(hThread);
		free(fileData);
		return false;
	}

	if (WaitForSingleObject(hThread, 5000) != WAIT_OBJECT_0) { // Уменьшено с 30000 до 5000
		CloseHandle(hThread);
		VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
		VirtualFreeEx(hProc, MappingDataAlloc, 0, MEM_RELEASE);
		VirtualFreeEx(hProc, pShellcode, 0, MEM_RELEASE);
		free(fileData);
		return false;
	}

	CloseHandle(hThread);

	Sleep(2000); // Уменьшено с 8000 до 2000

	if (!GetExitCodeProcess(hProc, &exitcode) || exitcode != STILL_ACTIVE) {
		free(fileData);
		return false;
	}

	HINSTANCE hCheck = NULL;
	int maxRetries = 3; // Уменьшено с 5 до 3
	for (int i = 0; i < maxRetries; i++) {
		if (!GetExitCodeProcess(hProc, &exitcode) || exitcode != STILL_ACTIVE) {
			free(fileData);
			return false;
		}

		MANUAL_MAPPING_DATA data_checked{ 0 };
		if (!ReadProcessMemory(hProc, MappingDataAlloc, &data_checked, sizeof(data_checked), nullptr)) {
			Sleep(500); // Уменьшено с 2000 до 500
			continue;
		}
		
		hCheck = data_checked.hMod;
		
		if (hCheck && hCheck != (HINSTANCE)0x404040) {
			break;
		}

		if (hCheck == (HINSTANCE)0x404040) {
			VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
			VirtualFreeEx(hProc, MappingDataAlloc, 0, MEM_RELEASE);
			VirtualFreeEx(hProc, pShellcode, 0, MEM_RELEASE);
			free(fileData);
			return false;
		}

		Sleep(500); // Уменьшено с 2000 до 500
	}

	if (!hCheck || hCheck == (HINSTANCE)0x404040) {
		VirtualFreeEx(hProc, pTargetBase, 0, MEM_RELEASE);
		VirtualFreeEx(hProc, MappingDataAlloc, 0, MEM_RELEASE);
		VirtualFreeEx(hProc, pShellcode, 0, MEM_RELEASE);
		free(fileData);
		return false;
	}

	Sleep(1000); // Уменьшено с 5000 до 1000

	free(fileData);
	return true;
}

#define RELOC_FLAG32(RelInfo) ((RelInfo >> 0x0C) == IMAGE_REL_BASED_HIGHLOW)
#define RELOC_FLAG64(RelInfo) ((RelInfo >> 0x0C) == IMAGE_REL_BASED_DIR64)
#define RELOC_FLAG RELOC_FLAG64

#pragma runtime_checks( "", off )
#pragma optimize( "", off )
void __stdcall Shellcode(MANUAL_MAPPING_DATA* pData) {
	if (!pData) {
		pData->hMod = (HINSTANCE)0x404040;
		return;
	}

	BYTE* pBase = pData->pbase;
	auto* pOpt = &reinterpret_cast<IMAGE_NT_HEADERS*>(pBase + reinterpret_cast<IMAGE_DOS_HEADER*>((uintptr_t)pBase)->e_lfanew)->OptionalHeader;

	auto _LoadLibraryA = pData->pLoadLibraryA;
	auto _GetProcAddress = pData->pGetProcAddress;
	auto _RtlAddFunctionTable = pData->pRtlAddFunctionTable;
	auto _DllMain = reinterpret_cast<f_DLL_ENTRY_POINT>(pBase + pOpt->AddressOfEntryPoint);

	BYTE* LocationDelta = pBase - pOpt->ImageBase;
	if (LocationDelta) {
		if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size) {
			auto* pRelocData = reinterpret_cast<IMAGE_BASE_RELOCATION*>(pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
			const auto* pRelocEnd = reinterpret_cast<IMAGE_BASE_RELOCATION*>(reinterpret_cast<uintptr_t>(pRelocData) + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size);
			while (pRelocData < pRelocEnd && pRelocData->SizeOfBlock) {
				UINT AmountOfEntries = (pRelocData->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
				WORD* pRelativeInfo = reinterpret_cast<WORD*>(pRelocData + 1);

				for (UINT i = 0; i != AmountOfEntries; ++i, ++pRelativeInfo) {
					if (RELOC_FLAG(*pRelativeInfo)) {
						UINT_PTR* pPatch = reinterpret_cast<UINT_PTR*>(pBase + pRelocData->VirtualAddress + ((*pRelativeInfo) & 0xFFF));
						*pPatch += reinterpret_cast<UINT_PTR>(LocationDelta);
					}
				}
				pRelocData = reinterpret_cast<IMAGE_BASE_RELOCATION*>(reinterpret_cast<BYTE*>(pRelocData) + pRelocData->SizeOfBlock);
			}
		}
	}

	if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size) {
		auto* pImportDescr = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
		while (pImportDescr->Name) {
			char* szMod = reinterpret_cast<char*>(pBase + pImportDescr->Name);
			HINSTANCE hDll = _LoadLibraryA(szMod);

			ULONG_PTR* pThunkRef = reinterpret_cast<ULONG_PTR*>(pBase + pImportDescr->OriginalFirstThunk);
			ULONG_PTR* pFuncRef = reinterpret_cast<ULONG_PTR*>(pBase + pImportDescr->FirstThunk);

			if (!pThunkRef)
				pThunkRef = pFuncRef;

			for (; *pThunkRef; ++pThunkRef, ++pFuncRef) {
				if (IMAGE_SNAP_BY_ORDINAL(*pThunkRef)) {
					*pFuncRef = (ULONG_PTR)_GetProcAddress(hDll, reinterpret_cast<char*>(*pThunkRef & 0xFFFF));
				}
				else {
					auto* pImport = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(pBase + (*pThunkRef));
					*pFuncRef = (ULONG_PTR)_GetProcAddress(hDll, pImport->Name);
				}
			}
			++pImportDescr;
		}
	}

	if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size) {
		auto* pTLS = reinterpret_cast<IMAGE_TLS_DIRECTORY*>(pBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress);
		auto* pCallback = reinterpret_cast<PIMAGE_TLS_CALLBACK*>(pTLS->AddressOfCallBacks);
		for (; pCallback && *pCallback; ++pCallback)
			(*pCallback)(pBase, DLL_PROCESS_ATTACH, nullptr);
	}

	bool ExceptionSupportFailed = false;

	if (pData->SEHSupport) {
		auto excep = pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
		if (excep.Size) {
			if (!_RtlAddFunctionTable(
				reinterpret_cast<IMAGE_RUNTIME_FUNCTION_ENTRY*>(pBase + excep.VirtualAddress),
				excep.Size / sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY), (DWORD64)pBase)) {
				ExceptionSupportFailed = true;
			}
		}
	}

	_DllMain(pBase, pData->fdwReasonParam, pData->reservedParam);

	if (ExceptionSupportFailed)
		pData->hMod = reinterpret_cast<HINSTANCE>(0x505050);
	else
		pData->hMod = reinterpret_cast<HINSTANCE>(pBase);
}

bool InjectDll(HANDLE hProc, const wchar_t* dllPath) {
	// Проверяем существование DLL
	DWORD fileAttrs = GetFileAttributesW(dllPath);
	if (fileAttrs == INVALID_FILE_ATTRIBUTES) {
		printf("[-] DLL file not found: %ws\n", dllPath);
		return false;
	}

	// Проверяем права доступа к DLL
	HANDLE hFile = CreateFileW(dllPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		printf("[-] Cannot open DLL file: 0x%X\n", GetLastError());
		return false;
	}
	CloseHandle(hFile);

	// Выделяем память в целевом процессе для пути к DLL
	SIZE_T pathSize = (wcslen(dllPath) + 1) * sizeof(wchar_t);
	if (pathSize > MAXDWORD) {
		printf("[-] DLL path too long\n");
		return false;
	}

	printf("[+] Allocating memory for DLL path\n");
	LPVOID remotePath = VirtualAllocEx(hProc, NULL, pathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!remotePath) {
		printf("[-] VirtualAllocEx failed: 0x%X\n", GetLastError());
		return false;
	}

	// Записываем путь к DLL в память процесса
	printf("[+] Writing DLL path to process memory\n");
	if (!WriteProcessMemory(hProc, remotePath, dllPath, static_cast<DWORD>(pathSize), NULL)) {
		printf("[-] WriteProcessMemory failed: 0x%X\n", GetLastError());
		VirtualFreeEx(hProc, remotePath, 0, MEM_RELEASE);
		return false;
	}

	// Получаем адрес LoadLibraryW
	printf("[+] Getting LoadLibraryW address\n");
	HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
	if (!kernel32) {
		printf("[-] GetModuleHandleW failed: 0x%X\n", GetLastError());
		VirtualFreeEx(hProc, remotePath, 0, MEM_RELEASE);
		return false;
	}

	LPVOID loadLibraryAddr = GetProcAddress(kernel32, "LoadLibraryW");
	if (!loadLibraryAddr) {
		printf("[-] GetProcAddress failed: 0x%X\n", GetLastError());
		VirtualFreeEx(hProc, remotePath, 0, MEM_RELEASE);
		return false;
	}

	// Создаем удаленный поток
	printf("[+] Creating remote thread\n");
	HANDLE hThread = CreateRemoteThread(hProc, NULL, 0, 
		(LPTHREAD_START_ROUTINE)loadLibraryAddr, remotePath, 0, NULL);
	if (!hThread) {
		printf("[-] CreateRemoteThread failed: 0x%X\n", GetLastError());
		VirtualFreeEx(hProc, remotePath, 0, MEM_RELEASE);
		return false;
	}

	// Ждем завершения загрузки
	printf("[+] Waiting for thread completion\n");
	if (WaitForSingleObject(hThread, 10000) != WAIT_OBJECT_0) {
		printf("[-] Thread wait timeout\n");
		CloseHandle(hThread);
		VirtualFreeEx(hProc, remotePath, 0, MEM_RELEASE);
		return false;
	}

	// Проверяем результат
	DWORD exitCode = 0;
	GetExitCodeThread(hThread, &exitCode);
	if (exitCode == 0) {
		printf("[-] DLL load failed\n");
		CloseHandle(hThread);
		VirtualFreeEx(hProc, remotePath, 0, MEM_RELEASE);
		return false;
	}

	printf("[+] DLL loaded successfully\n");
	CloseHandle(hThread);
	VirtualFreeEx(hProc, remotePath, 0, MEM_RELEASE);
	return true;
}
