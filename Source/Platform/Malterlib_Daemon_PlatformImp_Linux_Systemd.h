// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Daemon/Daemon>
#include "Malterlib_Daemon_PlatformImp_Linux_Shared.h"

namespace NMib::NDaemon
{
	class CDaemon;

	class CSystemd : public CDaemonSystemInterfaceShared
	{
	private:

		NStr::CStr fp_GetUnitConfigDirectory(CDaemonParams const &_Params) const;
		bool fp_IsUnitConfigThisExecutable(CDaemon *pOwner, CDaemonParams const &_Params) const;
		EActionResult fp_SetUnitEnable(CDaemonParams const &_Params, bool _bEnable) const;
		EActionResult fp_IsUnitEnabled(CDaemonParams const &_Params, bool& _bIsEnabled) const;
		static NStr::CStr fsp_FindExecutable(NStr::CStr const &_Executable);

		NContainer::TCVector<NStr::CStr> mp_SystemdSystemUnitDirectories;
		NContainer::TCVector<NStr::CStr> mp_SystemdUserUnitDirectories;
		NStr::CStr mp_SystemCtlExecutable;
		NStr::CStr mp_PkgConfigExecutable;

	public:
		CSystemd(CDaemon *);
		~CSystemd();

		static bool fs_IsSupported();

		virtual EActionResult f_Start(CDaemonParams const &_Params) override;
		virtual EActionResult f_Stop(CDaemonParams const &_Params, bool _bWait = false) override;
		virtual EActionResult f_Restart(CDaemonParams const &_Params, bool _bWait = false) override;

		virtual EActionResult f_Add(CDaemonParams const &_Params, bool _bCheckForExisting = false) override;
		virtual EActionResult f_Remove(CDaemonParams const &_Params) override;

		virtual EActionResult f_Exists(CDaemonParams const &_Params, bool &_bExists) const override;

		virtual bool f_SupportsAutoRestart() const override;
	};
}
