// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon_PlatformImp_Windows.h"

namespace NMib::NService
{
	bool CService::CDetails::fp_CheckParamsSupported(CServiceParams const &_Params) const
	{
		bool bRet = true;
		switch (_Params.f_GetServiceMode())
		{
		case EServiceMode_AllUsers:
			{
				mp_pOwner->f_ReportError("User services (-AllUsers) are not supported on this platform");
				bRet = false;
			}
			break;
		}
			
		return bRet;			
	}

	void CService::CDetails::f_ReportInformation(NStr::CStr const &_Heading, NStr::CStr const &_Message) const
	{
		mp_pOwner->f_GetServiceParams().f_ReportInformation(_Heading, _Message);
	}

	void CService::CDetails::f_ReportError(NStr::CStr const &_Message) const
	{
		mp_pOwner->f_GetServiceParams().f_ReportError(_Message);
	}

	EReportError CService::CDetails::f_ReportErrorYesNo(NStr::CStr const &_Message, EReportError _Default) const
	{
		return mp_pOwner->f_GetServiceParams().f_ReportErrorYesNo(_Message, _Default);
	}
}
