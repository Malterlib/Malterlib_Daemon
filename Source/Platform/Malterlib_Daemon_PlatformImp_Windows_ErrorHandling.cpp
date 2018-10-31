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
		case EServiceMode_LocalUser:
			{
				if (_Params.f_GetRunAsUser() || _Params.f_GetRunAsGroup())
				{
					mp_pOwner->f_ReportError("User services (--mode user) cannot specify another user or group to run as");
					bRet = false;
				}
			}
			break;
		case EServiceMode_AllUsers:
			{
				if (_Params.f_GetRunAsUser() || _Params.f_GetRunAsGroup())
				{
					mp_pOwner->f_ReportError("User services (--mode all-users) cannot specify another user or group to run as");
					bRet = false;
				}
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
