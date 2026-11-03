// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Daemon_PlatformImp_Windows.h"

namespace NMib::NDaemon
{
	CDaemon::CDetails::CTaskIconCleaner::CTaskIconCleaner() = default;

	CDaemon::CDetails::CTaskIconCleaner::~CTaskIconCleaner()
	{
		if (!m_bInit)
			return;

		if (m_NotifyIconData.hIcon)
			Shell_NotifyIcon(NIM_DELETE, &m_NotifyIconData);
		if (m_pReportWindow)
			DestroyWindow(m_pReportWindow);
		UnregisterClassA("MalterlibDaemon_ReportWindow", 0);
	}

	LRESULT CALLBACK CDaemon::CDetails::CTaskIconCleaner::fs_ReportWindowProc(HWND _pWindow, UINT _Message, WPARAM _WParam, LPARAM _LParam)
	{
		if (_Message == WM_ENDSESSION || _Message == WM_QUERYENDSESSION)
			NPlatform::fg_ReportIsShuttingDown();

		if (_Message == WM_COMMAND)
		{
			if (_WParam == 123)
			{
				msp_pThis->fp_DaemonPause();
				return true;
			}
			else if (_WParam == 124)
			{
				msp_pThis->fp_DaemonResume();
				return true;
			}
			else if (_WParam == 125)
			{
				PostQuitMessage(0);
				return true;
			}
		}
		if (_Message == WM_USER + 20 && _LParam == WM_LBUTTONDBLCLK)
		{
			PostQuitMessage(0);

			return true;
		}

		if (_Message == WM_USER + 20 && _LParam == WM_RBUTTONUP)
		{
			SetForegroundWindow(_pWindow);
			HMENU pMenu = CreatePopupMenu();
			AppendMenuA(pMenu, MF_STRING, 123, "Pause");
			AppendMenuA(pMenu, MF_STRING, 124, "Resume");
			AppendMenuA(pMenu, MF_STRING, 125, "Quit");
			AppendMenuA(pMenu, MF_SEPARATOR, 126, "Separator");
			AppendMenuA(pMenu, MF_STRING, 127, "Cancel");
			POINT Pos;
			GetCursorPos(&Pos);
			TrackPopupMenu(pMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON, Pos.x, Pos.y, 0, _pWindow, nullptr);
			PostMessage(_pWindow, WM_NULL, 0, 0);
			DestroyMenu(pMenu);

			return true;
		}

		return DefWindowProc(_pWindow, _Message, _WParam, _LParam);
	}

	void CDaemon::CDetails::CTaskIconCleaner::f_Init(HICON _pIcon)
	{
		if (!m_bInit)
		{
			m_bInit = true;

			auto pName = L"MalterlibDaemon_ReportWindow";

			WNDCLASSW WndClass;
			memset(&WndClass, 0, sizeof(WndClass));
			WndClass.lpszClassName = pName;
			WndClass.lpfnWndProc = fs_ReportWindowProc;
			WndClass.hInstance = 0;
			if (!RegisterClassW(&WndClass))
				DMibError((NStr::CStr::CFormat("Windows returned an error from RegisterClassW: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr()).f_GetStr());

			m_pReportWindow = CreateWindowExW(0L, pName, pName, 0, 0, 0, 0, 0, HWND_MESSAGE, 0, 0, 0);
			if (!m_pReportWindow)
				DMibError((NStr::CStr::CFormat("Windows returned an error from CreateWindowW: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr()).f_GetStr());

			NMemory::fg_MemClear(m_NotifyIconData);
			if (_pIcon)
			{
				m_NotifyIconData.cbSize = sizeof(m_NotifyIconData);
				m_NotifyIconData.hWnd = m_pReportWindow;
				m_NotifyIconData.hIcon = _pIcon;
				m_NotifyIconData.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
				m_NotifyIconData.uID = 0x0101;
				m_NotifyIconData.uCallbackMessage = WM_USER + 20;
				m_NotifyIconData.dwState = 0;
				m_NotifyIconData.dwStateMask = 0;
				NStr::fg_StrCopy(m_NotifyIconData.szTip, "Double click to quit " + msp_pThis->mp_pOwner->f_GetDaemonParams().f_GetDaemonName());

				Shell_NotifyIcon(NIM_ADD, &m_NotifyIconData);
			}
		}
	}

	bool CDaemon::CDetails::CTaskIconCleaner::f_Update()
	{
		MSG Message;

		int32 Ret = GetMessage(&Message, nullptr, 0, 0);
		if (Ret == -1 || Ret == 0 || Message.message == WM_QUIT)
			return true;

		TranslateMessage(&Message);
		DispatchMessage(&Message);

		return m_bAbortDebug;
	}
}
