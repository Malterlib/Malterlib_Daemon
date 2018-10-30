// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon_PlatformImp_Windows.h"

namespace NMib::NService
{
	void CService::CDetails::fs_AbortDebug()
	{
		msp_TaskIcon.m_bAbortDebug = true;
		PostMessage(msp_TaskIcon.m_hReportWnd, WM_NULL, 0, 0);
	}

	EActionResult CService::CDetails::f_RunAsProgram(bool _bDebug)
	{
		fp_ServiceCreate();

		if (_bDebug)
		{
			HICON Icon = LoadIcon((HINSTANCE)mp_pOwner->f_GetServiceParams().f_GetNativeHandle(), MAKEINTRESOURCE(101));
			msp_TaskIcon.f_Init(Icon);
				
			// Just spin in eternity
			while (1)
			{
				if (msp_TaskIcon.f_Update())
					break;

				Sleep(50);
			}
		}
		else
			NProcess::NPlatform::fg_Process_WaitForTermination();

		fp_ServiceDestroy();

		return EActionResult_Success;
	}

}
