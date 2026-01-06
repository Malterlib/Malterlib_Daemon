// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Daemon/Daemon>
#include "Malterlib_Daemon_PlatformImp_Linux_Shared.h"

namespace NMib::NDaemon
{
	class CDaemon;

	class CScript : public CDaemonSystemInterfaceShared
	{
	private:

		NStr::CStr mp_ScriptDirectory;

		enum EScriptRegistrationMethod
		{
			EScriptRegistrationMethod_None = 0,
			EScriptRegistrationMethod_ChkConfig,
			EScriptRegistrationMethod_UpdateRcD
		};

		EScriptRegistrationMethod mp_ScriptRegistrationMethod;

		NStr::CStr fp_GetScriptDirectory(EDaemonMode _Mode) const;
		NStr::CStr fp_GetScriptPath(CDaemonParams const &_Params) const;
		bool fp_IsScriptThisExecutable(CDaemonParams const &_Params) const;
		NStr::CStr fp_CreateScriptFromParams(CDaemonParams const &_Params) const;
		EActionResult fp_AddToRunlevels(CDaemonParams const &_Params) const;
		EActionResult fp_RemoveFromRunlevels(CDaemonParams const &_Params) const;

	public:
		CScript(CDaemon *);
		~CScript();

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
