// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Daemon/Daemon>
#include "Malterlib_Daemon_PlatformImp_Linux_Shared.h"

namespace NMib
{
	namespace NService
	{
		class CService;

		class CScript : public CServiceSystemInterfaceShared
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

			NStr::CStr fp_GetScriptDirectory(EServiceMode _Mode) const;
			NStr::CStr fp_GetScriptPath(CServiceParams const &_Params) const;
			bool fp_IsScriptThisExecutable(CServiceParams const &_Params) const;
			NStr::CStr fp_CreateScriptFromParams(CServiceParams const &_Params) const;
			EActionResult fp_AddToRunlevels(CServiceParams const &_Params) const;
			EActionResult fp_RemoveFromRunlevels(CServiceParams const &_Params) const;

		public:
			CScript(CService *);
			~CScript();

			static bint fs_IsSupported();

			virtual EActionResult f_Start(CServiceParams const &_Params) override;
			virtual EActionResult f_Stop(CServiceParams const &_Params, bint _bWait = false) override;
			virtual EActionResult f_Restart(CServiceParams const &_Params, bint _bWait = false) override;

			virtual EActionResult f_Add(CServiceParams const &_Params, bint _bCheckForExisting = false) override;
			virtual EActionResult f_Remove(CServiceParams const &_Params) override;

			virtual EActionResult f_Exists(CServiceParams const &_Params, bool &_bExists) const override;

			virtual bool f_SupportsAutoRestart() const override;
		};

	} // namespace NService

} // namespace NMib
