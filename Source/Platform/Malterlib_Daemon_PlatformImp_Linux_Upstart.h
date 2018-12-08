// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Daemon/Daemon>
#include "Malterlib_Daemon_PlatformImp_Linux_Shared.h"

namespace NMib::NDaemon
{
	class CDaemon;

	class CUpstart : public CDaemonSystemInterfaceShared
	{
	private:
		NStr::CStr fp_GetInitctlOptions(CDaemonParams const &_Params) const;
		NStr::CStr fp_GetConfDirectory(CDaemonParams const &_Params) const;
		bool fp_IsConfThisExecutable(CDaemonParams const &_Params) const;

		// Upstart version 1.7 and greater supports a "session job" with the --user flag
		// Versions below 1.7 must use a "user job", these versions are found on Ubuntu 12.x and below
		bool mp_bSupportsUserFlag;

	public:
		CUpstart(CDaemon *);
		~CUpstart();

		static bint fs_IsSupported();

		virtual EActionResult f_Start(CDaemonParams const &_Params) override;
		virtual EActionResult f_Stop(CDaemonParams const &_Params, bint _bWait = false) override;
		virtual EActionResult f_Restart(CDaemonParams const &_Params, bint _bWait = false) override;

		virtual EActionResult f_Add(CDaemonParams const &_Params, bint _bCheckForExisting = false) override;
		virtual EActionResult f_Remove(CDaemonParams const &_Params) override;

		virtual EActionResult f_Exists(CDaemonParams const &_Params, bool &_bExists) const override;

		virtual bool f_SupportsAutoRestart() const override;
	};
}
