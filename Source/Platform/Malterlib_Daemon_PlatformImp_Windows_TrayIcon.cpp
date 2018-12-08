// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

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
		DestroyWindow(m_hReportWnd);
		UnregisterClassA("MalterlibDaemon_ReportWindow", 0);
	}

	LRESULT CALLBACK CDaemon::CDetails::CTaskIconCleaner::fs_ReportWindowProc(HWND _hWnd, UINT _Message, WPARAM _WParam, LPARAM _LParam)
	{
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
			SetForegroundWindow(_hWnd);
			HMENU hMenu = CreatePopupMenu();
			AppendMenuA(hMenu, MF_STRING, 123, "Pause");
			AppendMenuA(hMenu, MF_STRING, 124, "Resume");
			AppendMenuA(hMenu, MF_STRING, 125, "Quit");
			AppendMenuA(hMenu, MF_SEPARATOR, 126, "Separator");
			AppendMenuA(hMenu, MF_STRING, 127, "Cancel");
			POINT Pos;
			GetCursorPos(&Pos);
			TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON, Pos.x, Pos.y, 0, _hWnd, nullptr);
 			PostMessage(_hWnd, WM_NULL, 0, 0);
			DestroyMenu(hMenu);

			return true;
		}

		return DefWindowProc(_hWnd, _Message, _WParam, _LParam);
	}

	void CDaemon::CDetails::CTaskIconCleaner::f_Init(HICON _hIcon)
	{
		if (!m_bInit)
		{
			m_bInit = true;

			WNDCLASSA WndClass;
			memset(&WndClass, 0, sizeof(WndClass));
			WndClass.lpszClassName = "MalterlibDaemon_ReportWindow" ;
			WndClass.lpfnWndProc = fs_ReportWindowProc;
			WndClass.hInstance = 0;
			RegisterClassA(&WndClass);
			m_hReportWnd = CreateWindowA("MalterlibDaemon_ReportWindow", "MalterlibDaemon_ReportWindow", 0, 0, 0, 0, 0, HWND_MESSAGE, 0, 0, 0);

			NMemory::fg_MemClear(m_NotifyIconData);
			if (_hIcon)
			{
				m_NotifyIconData.cbSize = sizeof(m_NotifyIconData);
				m_NotifyIconData.hWnd = m_hReportWnd;
				m_NotifyIconData.hIcon = _hIcon;
				m_NotifyIconData.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
				m_NotifyIconData.uID = 0x0101;
				m_NotifyIconData.uCallbackMessage = WM_USER+20;
				m_NotifyIconData.dwState = 0;
				m_NotifyIconData.dwStateMask = 0;
				NStr::fg_StrCopy(m_NotifyIconData.szTip, "Double click to quit " + msp_pThis->mp_pOwner->f_GetDaemonParams().f_GetDaemonName());

				Shell_NotifyIcon(NIM_ADD, &m_NotifyIconData);
			}
		}
	}

	bint CDaemon::CDetails::CTaskIconCleaner::f_Update()
	{	
		MSG Message;

		int32 Ret = GetMessage( &Message, nullptr, 0, 0 );
		if (Ret == -1 || Ret == 0 || Message.message == WM_QUIT)
		{
			return true;
		}

		TranslateMessage(&Message);
		DispatchMessage(&Message);

		return m_bAbortDebug;
	}
}
