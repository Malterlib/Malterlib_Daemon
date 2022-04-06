// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon_PlatformImp_Windows.h"

namespace NMib::NDaemon
{
	bool CDaemon::CDetails::fp_PrepareUserAndGroup(CDaemonParams const &_Params, NMib::NStr::CWStr &o_RunAsUser, NMib::NStr::CWStrSecure &o_RunAsUserPassword)
	{
		NStr::CStr StdOut, StdErr;
				
		NStr::CStr GroupName = _Params.f_GetRunAsGroup();
		NStr::CStr UserName = _Params.f_GetRunAsUser();
				
		if (!GroupName.f_IsEmpty())
		{
			NStr::CStr ReturnGID;
				
			try
			{
				if (!NSys::fg_UserManagement_GroupExists(GroupName, ReturnGID))
					NSys::fg_UserManagement_CreateGroup(GroupName, ReturnGID);
			}
			catch (NMib::NException::CException &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Exception when creating group named {}\n{}") << GroupName << _Exception.f_GetErrorStr());
				if (NMib::NProcess::NPlatform::fg_Process_GetElevation() == NMib::NProcess::EProcessElevation_IsNotElevated)
					mp_pOwner->f_ReportError("Perhaps you need to run as administrator?");
				return false;
			}
		}
				
		if (!UserName.f_IsEmpty())
		{
			NStr::CStr ReturnUID;

			NMib::NStr::CWStrSecure UserWindows = UserName;
			o_RunAsUser = ".\\" + UserName;
					
			try
			{
				if (!NSys::fg_UserManagement_UserExists(UserName, ReturnUID))
				{
					o_RunAsUserPassword = NCryptography::fg_HighEntropyRandomID("23456789ABCDEFGHJKLMNPQRSTWXYZabcdefghijkmnopqrstuvwxyz&=*!@~^") + "2Dg&";
					NSys::fg_UserManagement_CreateUser
						(
							GroupName
							, UserName
							, o_RunAsUserPassword
							, _Params.f_GetDaemonDescription()
							, NFile::CFile::fs_GetProgramDirectory()
							, ReturnUID
							, NSys::EUserManagementCreateUserFlag_None
						)
					;

					auto CleanupUser = g_OnScopeExit / [&]
						{
							try
							{
								NSys::fg_UserManagement_DeleteUser(UserName);
							}
							catch (NException::CException const &)
							{
							}
						}
					;

					SID_NAME_USE AccountType;
					NMib::NStr::CWStr ReferencedDomainName;

					SE_SID UserSID;
					uint32 ReferencedDomainNameSize = 8192;
					uint32 SidSize = sizeof(UserSID);
					if (!LookupAccountNameW(nullptr, UserWindows.f_GetStr(), &UserSID.Sid, &SidSize, ReferencedDomainName.f_GetStr(8192+1), &ReferencedDomainNameSize, &AccountType))
						DMibError((NMib::NStr::CFStr256::CFormat("Windows returned an error from LookupAccountNameW(Add login as service privilege): {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr()).f_GetStr());

					LSA_OBJECT_ATTRIBUTES Attributes;
					NMemory::fg_MemClear(Attributes);

					LSA_HANDLE PolicyHandle;

					NTSTATUS Status = LsaOpenPolicy(nullptr, &Attributes, POLICY_CREATE_ACCOUNT | POLICY_LOOKUP_NAMES, &PolicyHandle);
					if (Status)
						DMibError((NMib::NStr::CFStr256::CFormat("Windows returned an error from LsaOpenPolicy(Add login as service privilege): {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(Status)).f_GetStr());

					auto Cleanup = g_OnScopeExit / [&]
						{
							LsaClose(PolicyHandle);
						}
					;

					LSA_UNICODE_STRING PrivilegeString;

					PrivilegeString.Buffer = fg_AutoConstCast(L"SeServiceLogonRight");
					PrivilegeString.Length = NMib::NStr::fg_StrLen(PrivilegeString.Buffer) * sizeof(ch16);
					PrivilegeString.MaximumLength = (PrivilegeString.Length + 1) * sizeof(ch16);

					Status = LsaAddAccountRights(PolicyHandle, &UserSID.Sid, &PrivilegeString, 1);
					if (Status)
						DMibError((NMib::NStr::CFStr256::CFormat("Windows returned an error from LsaAddAccountRights(Add login as service privilege): {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(Status)).f_GetStr());

					CleanupUser.f_Clear();
				}
			}
			catch (NMib::NException::CException const &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Unable to create user named {}\n{}") << UserName << _Exception.f_GetErrorStr());
				if (NMib::NProcess::NPlatform::fg_Process_GetElevation() == NMib::NProcess::EProcessElevation_IsNotElevated)
					mp_pOwner->f_ReportError("Perhaps you need to use sudo?");
				return false;
			}
		}

		return true;
	}
}
