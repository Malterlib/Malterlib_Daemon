// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Daemon/Daemon>
#include "Malterlib_Daemon_PlatformImp_Linux_Shared.h"

namespace NMib::NDaemon
{
	class CDaemon;

	class CGentoo : public CDaemonSystemInterfaceShared
	{
		// These values are taken from /lib64/rc/runscript.sh
		enum EGentooDaemonStatus {
			EGentooDaemonStatus_Started = 0,
			EGentooDaemonStatus_Stopped = 3,
			EGentooDaemonStatus_Crashed = 32,
			EGentooDaemonStatus_Inactive = 16,
			EGentooDaemonStatus_Starting = 8,
			EGentooDaemonStatus_Stopping = 4,
			EGentooDaemonStatus_InvalidDaemon = 255
		};

	private:

		NStr::CStr fp_GetScriptDirectory(EDaemonMode _Mode) const;
		NStr::CStr fp_GetScriptPath(CDaemonParams const &_Params) const;
		bool fp_IsScriptThisExecutable(CDaemonParams const &_Params) const;
		NStr::CStr fp_CreateScriptFromParams(CDaemonParams const &_Params) const;
		EGentooDaemonStatus fp_GetStatus(NStr::CStr const &_ScriptFilePath);
		bool fp_GetStatusAdded(NStr::CStr const &_DaemonName);

		NStr::CStr mp_ScriptDirectory;

	public:
		CGentoo(CDaemon *);

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
