#include "stdafx.h"
#include "DllMain.h"

#define DbgPrintFile(x) if(szLogPath) _DbgPrintFile x

// Note - the following class names where obtained with SpyXX
// In case they change (via OS upgrade or update) they will need to be discovered again

#define RDPCLIP_WINDOW_CLASS _T("RdpClipRdrWindowClass")
#define RDPCLIP_MAINWINDOW_CLASS _T("RdpClipMainWindowClass")
#define OLECLIP_MAINWINDOW_CLASS _T("CLIPBRDWNDCLASS")
void _DbgPrintFile(const TCHAR *Format, ...);


// hmmm - a whole segment (one page) just for sharing these variables...
#pragma data_seg(".HOOKSHR")
HWND hRDPClip = NULL;
HWND hRDPClipMain = NULL;
DWORD dwRDPClipPID = 0;
HWND hCallBack = NULL;
#pragma data_seg()
#pragma comment (linker,"/section:.HOOKSHR,rws")

HANDLE hInst = NULL;
HHOOK  hHookGetMsg = NULL;
BOOL bInitialised = FALSE;
HHOOK  hHookCBT = NULL;
TCHAR * szLogPath = NULL;
TCHAR szLogFileName[256];
const TCHAR *CFG_REG_KEY = _T("Software\\ClipFilt");
const TCHAR *CFG_REG_DebugFOLDER = _T("DebugLogFolder");

SHELLHOOK_API LRESULT CALLBACK GetMsgHookProc(int nCode, WPARAM wParam, LPARAM lParam);

BOOL APIENTRY DllMain(HANDLE hMod, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		break;
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		break;
	}
	hInst = hMod;
	return TRUE;
}

// function to decide whether a clip should be 
BOOL ShouldBlockThisClipBy()
{
	BOOL bBlocked = FALSE;

	if (OpenClipboard(NULL))
	{
		// enumerate through the clipboard formats of the clip currently on the clipboard
		UINT uFormat = EnumClipboardFormats(0);

		while (uFormat && !bBlocked)
		{
			TCHAR szFormatName[256];
			DWORD z = GetClipboardFormatName(uFormat, szFormatName, 256);
			DbgPrintFile((_T("GetClipboardFormatName returns %d"), z));

			if (_tcsstr(szFormatName, _T("File")) == szFormatName)
			{
				DbgPrintFile((_T("Clip Format %x = %s, which is a file copy... blocking"), uFormat, szFormatName));
				bBlocked = TRUE;
				break;
			}
			else
				DbgPrintFile((_T("Clip Format %x = %s, not blocking"), uFormat, szFormatName));

			uFormat = EnumClipboardFormats(uFormat);
		}
		CloseClipboard();
	}
	else
		DbgPrintFile((_T("Cannot open clipboard: %08x"), GetLastError()));

	return bBlocked;
}


void InitDebug()
{
	HKEY hCfgKey = NULL;

	if (ERROR_SUCCESS == RegOpenKeyEx(HKEY_LOCAL_MACHINE, CFG_REG_KEY, 0, KEY_READ, &hCfgKey))
	{
		DWORD dwType = REG_SZ;
		DWORD cbData = 0;

		if (ERROR_SUCCESS == RegQueryValueEx(hCfgKey, CFG_REG_DebugFOLDER, NULL, &dwType, NULL, &cbData))
		{
			if (dwType == REG_SZ)
			{
				szLogPath = (TCHAR *)malloc(cbData);
				if (ERROR_SUCCESS == RegQueryValueEx(hCfgKey, CFG_REG_DebugFOLDER, NULL, &dwType, (LPBYTE)szLogPath, &cbData))
				{
					szLogPath[cbData - 1] = 0; // force terminator as per MSDN, note cbData is in chars
					_stprintf_s(szLogFileName, 256, _T("%s\\CF_%05d.log"), szLogPath, GetCurrentProcessId());
				}
				else
				{
					free(szLogPath);
					szLogPath = NULL;
				}
			}
		}
		RegCloseKey(hCfgKey);
	}
	// force a log for now:
	szLogPath = (TCHAR *)_T("c:\\users\\public\\");
	_stprintf_s(szLogFileName, 256, _T("%s\\CF_%05d.log"), szLogPath, GetCurrentProcessId());
}

BOOL CALLBACK EnumWndProc(HWND hwnd, LPARAM lParam)
{
	TCHAR szClassName[64];
	GetClassName(hwnd, szClassName, 64);

	if (lParam == 0) // looking for RdpClipMainWindow...
	{
		if (_tcscmp(szClassName, RDPCLIP_MAINWINDOW_CLASS) == 0)
		{
			hRDPClipMain = hwnd;
			GetWindowThreadProcessId(hwnd, &dwRDPClipPID);
			return FALSE;
		}
	}
	else
	{
		if (_tcscmp(szClassName, RDPCLIP_WINDOW_CLASS) == 0)
		{
			DWORD dwPID = (DWORD)-1;
			GetWindowThreadProcessId(hwnd, &dwPID);
			if (dwPID == lParam) // this window is in the RDPClip process
			{
				hRDPClip = hwnd;
				return FALSE;
			}
		}
	}
	return TRUE;
}


SHELLHOOK_API BOOL InstallHook(HWND hwndCallBack)
{
	// init debug
	if (!bInitialised)
	{
		InitDebug();
		DbgPrintFile((_T("ClipFilter 0.3 beta: Host starting... (Callback window=%08x)"), hwndCallBack));
		bInitialised = TRUE;
	}

	// save the hwnd to where we can send info
	hCallBack = hwndCallBack;

	// find the RDP clip window
	hRDPClip = NULL;
	dwRDPClipPID = 0;
	EnumWindows(EnumWndProc, 0);
	if (dwRDPClipPID != 0) // found the main window; now find the clip window
		EnumWindows(EnumWndProc, dwRDPClipPID);

	if (hRDPClip != NULL)
	{
		DWORD dwThreadID, dwPID;

		// CBT hook for clipboard messages (on RDPClip's clip window)
		dwThreadID = GetWindowThreadProcessId(hRDPClip, &dwPID);
		DbgPrintFile((_T("Clip: HWND=%08x, PID=%d, TID=%d"), hRDPClip, dwPID, dwThreadID));
		hHookGetMsg = SetWindowsHookEx(WH_GETMESSAGE, GetMsgHookProc, (HINSTANCE)(hInst), dwThreadID);
		DbgPrintFile((_T("hHookGetMsg=%08x, GLE=%08x"), hHookGetMsg, GetLastError()));

		// CBT hook for destroy window (on RDPClip's main window)
		dwThreadID = GetWindowThreadProcessId(hRDPClipMain, &dwPID);
		DbgPrintFile((_T("Main: HWND=%08x, PID=%d, TID=%d"), hRDPClipMain, dwPID, dwThreadID));
		hHookCBT = SetWindowsHookEx(WH_CBT, CBTHookProc, (HINSTANCE)(hInst), dwThreadID);
		DbgPrintFile((_T("hHookCBT=%08x, GLE=%08x"), hHookCBT, GetLastError()));

		if (hHookGetMsg == NULL /* || hHookCBT==NULL*/) // we can survive without CBT
		{
			return FALSE;
		}

		SetLastError(0);
		return TRUE;
	}
	else
	{
		SetLastError(ERROR_FILE_NOT_FOUND);
		return FALSE;
	}
}

SHELLHOOK_API BOOL UninstallHook()
{
	if (hHookGetMsg)
	{
		UnhookWindowsHookEx(hHookGetMsg);
		hHookGetMsg = NULL;
	}

	if (hHookCBT)
	{
		UnhookWindowsHookEx(hHookCBT);
		hHookCBT = NULL;
	}
	return TRUE;
}

SHELLHOOK_API LRESULT CALLBACK CBTHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode<0)
		return CallNextHookEx(hHookCBT, nCode, wParam, lParam); // per MSDN

	// initialisation for inside the hooked process
	if (!bInitialised)
	{
		bInitialised = TRUE;
		InitDebug();
		DbgPrintFile((_T("ClipFilter 0.3 beta: Hook starting in CBTHookProc...")));
	}

	if (nCode == HCBT_DESTROYWND)
	{
		DbgPrintFile((_T("An RDPClip window is being destroyed - will notify CFHost %08x"), hCallBack));
		SendMessage(hCallBack, WM_USER, 3, 0); // send, not post!
	}
	else
		DbgPrintFile((_T("CBT: %08x %08x %08x"), nCode, wParam, lParam));

	return CallNextHookEx(hHookCBT, nCode, wParam, lParam);
}

SHELLHOOK_API LRESULT CALLBACK GetMsgHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode<0)
		return CallNextHookEx(hHookGetMsg, nCode, wParam, lParam); // per MSDN

																   // initialisation for inside the hooked process
	if (!bInitialised)
	{
		bInitialised = TRUE;
		InitDebug();
		DbgPrintFile((_T("ClipFilter 0.3: Hook starting in GetMsgHookProc...")));
	}

	PMSG msg = (PMSG)lParam;
	DbgPrintFile((_T("RDP Window got message: %04x: %08x %08x"), msg->message, msg->wParam, msg->lParam));

	if (msg->message == WM_CLIPBOARDUPDATE)
	{
		HWND hWndClipOwner = GetClipboardOwner();
		TCHAR szClassName[64];
		BOOL bEmptyClipOnFilter = FALSE;

		// SAMPLE CODE - directional filtering
		// if the owning window of the clip is in RDPClip, then the clip must have originated remotely (from the client)

		GetClassName(hWndClipOwner, szClassName, 64);
		DbgPrintFile((_T("WM_CLIPBOARDUPDATE: owner = %08x %s (opened by %08x)"), hWndClipOwner, szClassName, GetOpenClipboardWindow()));
		DWORD dwPID = -1;
		GetWindowThreadProcessId(hWndClipOwner, &dwPID);
		DbgPrintFile((_T("Clipprocess = %d == %d??"), dwPID, dwRDPClipPID));

		if (dwPID == dwRDPClipPID) // RDPClip OLE Clip window is owner, clip is remote
		{
			PostMessage(hCallBack, WM_USER, 2, 0); // let the host app know in case we want a popup notification to the user or something
			DbgPrintFile((_T("WM_CLIPBOARDUPDATE: Allowing remote CB copy in to session (CB=%08x)"), hCallBack));
		}
		else
		{
			PostMessage(hCallBack, WM_USER, 1, 0);  // let the host app know in case we want a popup notification to the user or something
			msg->message = WM_USER;
			PostMessage(hRDPClip, WM_DESTROYCLIPBOARD, NULL, NULL); // this will effectively get RDPClip to stop any attempt to reflect the clip down to the client; whilst not clearing the in-session clipboard
			DbgPrintFile((_T("WM_CLIPBOARDUPDATE: Posting WM_DESTROYCLIPBOARD to hRdpClip...")));
		}


		/*
		// SAMPLE CODE - empty the clipboard if it contains a forbidden clip type 
		if (ShouldBlockThisClip())
		{
			DbgPrintFile((_T("WM_CLIPBOARDUPDATE : block clip : swallowing (CB=%08x)"), hCallBack));
			PostMessage(hCallBack, WM_USER, 1, 0); // let the host app know in case we want a popup notification to the user or something
			msg->message = WM_USER;
			if (dwPID == dwRDPClipPID) // RDPClip OLE Clip window is owner, so must be remote... (at least for Win7)
			{
				DbgPrintFile((_T("WM_CLIPBOARDUPDATE: Clearing local clipboard")));
				OleSetClipboard(NULL);
			}
		}
		else
		{
			PostMessage(hCallBack, WM_USER, 2, 0); // let the host app know in case we want a popup notification to the user or something
			DbgPrintFile((_T("WM_CLIPBOARDUPDATE: Allowing (CB=%08x)"), hCallBack));
		}
		*/

	}

	return CallNextHookEx(hHookGetMsg, nCode, wParam, lParam);
}


void _DbgPrintFile(const TCHAR *Format, ...)
{
	SYSTEMTIME SystemTime;
	TCHAR szOutputText[2048];
	TCHAR szTimeStamp[64];
	HANDLE _hFile;
	DWORD dwBytesWritten;

	if (szLogFileName[0] == 0)
		return;
	va_list vargs;
	va_start(vargs, Format);
	StringCchVPrintf(szOutputText, 2048,  Format, vargs);
	va_end(vargs);

	GetSystemTime(&SystemTime);
	_hFile = CreateFile(szLogFileName,
		FILE_WRITE_DATA | FILE_APPEND_DATA,
		FILE_SHARE_READ,
		NULL,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL);
	if (_hFile != INVALID_HANDLE_VALUE)
	{
		if (SetFilePointer(_hFile, 0, NULL, FILE_END) != 0xffffffff)
		{
			StringCchPrintf(szTimeStamp, 64, _T("%02i/%02i %02i:%02i:%02i.%03i [%05i] - "),
				SystemTime.wMonth,
				SystemTime.wDay,
				SystemTime.wHour,
				SystemTime.wMinute,
				SystemTime.wSecond,
				SystemTime.wMilliseconds,
				GetCurrentThreadId());
			WriteFile(_hFile, szTimeStamp, (DWORD)_tcslen(szTimeStamp) * sizeof(TCHAR), &dwBytesWritten, NULL);


			if (szOutputText[0] != 0)
			{
				if (szOutputText[_tcslen(szOutputText) - 1] == _T('\n'))
					szOutputText[_tcslen(szOutputText) - 1] = 0;
			}

			WriteFile(_hFile, szOutputText, (DWORD)_tcslen(szOutputText) * sizeof(TCHAR), &dwBytesWritten, NULL);
			WriteFile(_hFile, _T("\r\n"), 2 * sizeof(TCHAR), &dwBytesWritten, NULL);
		}
		CloseHandle(_hFile);
	}
}
