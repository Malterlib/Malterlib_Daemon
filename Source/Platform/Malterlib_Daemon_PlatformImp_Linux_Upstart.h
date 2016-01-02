// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Daemon/Daemon>
#include "Malterlib_Daemon_PlatformImp_Linux_Shared.h"

namespace NMib
{
	namespace NService
	{
		class CService;

		class CUpstart : public CServiceSystemInterfaceShared
		{
		private:
			NStr::CStr fp_GetInitctlOptions(CServiceParams const &_Params) const;
			NStr::CStr fp_GetConfDirectory(CServiceParams const &_Params) const;
			bool fp_IsConfThisExecutable(CServiceParams const &_Params) const;

			// Upstart version 1.7 and greater supports a "session job" with the --user flag
			// Versions below 1.7 must use a "user job", these versions are found on Ubuntu 12.x and below
			bool mp_bSupportsUserFlag;

		public:
			CUpstart(CService *);
			~CUpstart();

			static bint fs_IsSupported();

			virtual EActionResult f_Start(CServiceParams const &_Params) override;
			virtual EActionResult f_Stop(CServiceParams const &_Params, bint _bWait = false) override;

			virtual EActionResult f_Add(CServiceParams const &_Params, bint _bCheckForExisting = false) override;
			virtual EActionResult f_Remove(CServiceParams const &_Params) override;

			virtual EActionResult f_Exists(CServiceParams const &_Params, bool &_bExists) const override;
		};

	} // namespace NService

} // namespace NMib
