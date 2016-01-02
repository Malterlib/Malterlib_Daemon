// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Daemon/Daemon>
#include "Malterlib_Daemon_PlatformImp_Linux_Shared.h"

namespace NMib
{
	namespace NService
	{
		class CService;

		class CGentoo : public CServiceSystemInterfaceShared
		{
			// These values are taken from /lib64/rc/runscript.sh
			enum EGentooServiceStatus {
				EGentooServiceStatus_Started = 0,
				EGentooServiceStatus_Stopped = 3,
				EGentooServiceStatus_Crashed = 32,
				EGentooServiceStatus_Inactive = 16,
				EGentooServiceStatus_Starting = 8,
				EGentooServiceStatus_Stopping = 4,
				EGentooServiceStatus_InvalidService = 255
			};
			
		private:

			NStr::CStr fp_GetScriptDirectory(EServiceMode _Mode) const;
			NStr::CStr fp_GetScriptPath(CServiceParams const &_Params) const;
			bool fp_IsScriptThisExecutable(CServiceParams const &_Params) const;
			NStr::CStr fp_CreateScriptFromParams(CServiceParams const &_Params) const;
			EGentooServiceStatus fp_GetStatus(NStr::CStr const &_ScriptFilePath);
			bool fp_GetStatusAdded(NStr::CStr const &_ServiceName);
			
			NStr::CStr mp_ScriptDirectory;
			
		public:
			CGentoo(CService *);

			static bint fs_IsSupported();

			virtual EActionResult f_Start(CServiceParams const &_Params) override;
			virtual EActionResult f_Stop(CServiceParams const &_Params, bint _bWait = false) override;

			virtual EActionResult f_Add(CServiceParams const &_Params, bint _bCheckForExisting = false) override;
			virtual EActionResult f_Remove(CServiceParams const &_Params) override;

			virtual EActionResult f_Exists(CServiceParams const &_Params, bool &_bExists) const override;
		};

	} // namespace NService

} // namespace NMib
