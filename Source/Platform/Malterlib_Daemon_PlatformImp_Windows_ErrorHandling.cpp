// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Daemon_PlatformImp_Windows.h"

namespace NMib::NDaemon
{
	bool CDaemon::CDetails::fp_CheckParamsSupported(CDaemonParams const &_Params) const
	{
		bool bRet = true;
		switch (_Params.f_GetDaemonMode())
		{
		case EDaemonMode_LocalUser:
			{
				if (_Params.f_GetRunAsUser() || _Params.f_GetRunAsGroup())
				{
					mp_pOwner->f_ReportError("User services (--mode user) cannot specify another user or group to run as");
					bRet = false;
				}
			}
			break;
		case EDaemonMode_AllUsers:
			{
				if (_Params.f_GetRunAsUser() || _Params.f_GetRunAsGroup())
				{
					mp_pOwner->f_ReportError("User services (--mode all-users) cannot specify another user or group to run as");
					bRet = false;
				}
			}
			break;
		case EDaemonMode_Global:
			break;
		}

		return bRet;
	}

	void CDaemon::CDetails::f_ReportInformation(NStr::CStr const &_Heading, NStr::CStr const &_Message) const
	{
		mp_pOwner->f_GetDaemonParams().f_ReportInformation(_Heading, _Message);
	}

	void CDaemon::CDetails::f_ReportError(NStr::CStr const &_Message) const
	{
		mp_pOwner->f_GetDaemonParams().f_ReportError(_Message);
	}

	EReportError CDaemon::CDetails::f_ReportErrorYesNo(NStr::CStr const &_Message, EReportError _Default) const
	{
		return mp_pOwner->f_GetDaemonParams().f_ReportErrorYesNo(_Message, _Default);
	}
}
