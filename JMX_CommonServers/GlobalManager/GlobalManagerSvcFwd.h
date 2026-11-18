#pragma once

#include <winsock2.h>
#include <windows.h>
#include <string>

#include "BSLib/thread/Task.h"
#include "ServerFramework/ModuleVersion.h"
#include "ServerFramework/OverlapJob.h"

// Functions of other parts of GlobalManager that the service manager calls (signatures from the
// binary). The overlap jobs are allocated from their pools with a job ID and set up with the
// main process' task; the service manager then posts them (CServiceManagerProcess::PostJob or
// SendOverlapJob of the task's job).
//
// The SM log members of CServiceManagerProcess (0x450B40 WriteUserLog, 0x450B90 WriteAnswerLog,
// 0x450CA0 WriteLog, 0x450DE0 WriteQueryLog) are declared in ServiceManagerProcess.h.

typedef ServerFramework::COverlapJob_Local SM_JOB;

// sub_450F10: the SM log type of an answer opcode (0x8000..0x800F), 0xFF for others.
BYTE SMLog_GetAnswerType(WORD wOpcode);

// COverlapJob_SM_ModuleVersion: UserCertifier_AllocJob_424CA0 / sub_424DD0.
SM_JOB* OverlapJob_AllocSM_ModuleVersion();
void OverlapJob_SetupSM_ModuleVersion(CTask* pTask, SM_JOB* pJob, DWORD dwSessionID);
// COverlapJob_SM_ModuleVersionFile: UserCertifier_AllocJob_424D10 / sub_425020.
SM_JOB* OverlapJob_AllocSM_ModuleVersionFile();
void OverlapJob_SetupSM_ModuleVersionFile(CTask* pTask, SM_JOB* pJob, DWORD dwSessionID,
                                          BYTE byModuleID, BYTE byParam);
// COverlapJob_RecordPatchData: UserCertifier_AllocJob_425290 / sub_425300.
SM_JOB* OverlapJob_AllocRecordPatchData();
void OverlapJob_SetupRecordPatchData(CTask* pTask, SM_JOB* pJob, const MODULE_VERSION* pModule,
                                     ModuleVersionFileList* pFiles, DWORD dwSessionID);
// COverlapJob_ModuleVersionData: UserCertifier_AllocJob_412C40 / sub_412CB0.
SM_JOB* OverlapJob_AllocModuleVersionData();
void OverlapJob_SetupModuleVersionData(CTask* pTask, SM_JOB* pJob, DWORD dwSessionID);
// COverlapJob_RefreshSecurityData_n_SendToSM: UserCertifier_AllocJob_425850 / sub_4258C0.
SM_JOB* OverlapJob_AllocRefreshSecurityData();
void OverlapJob_SetupRefreshSecurityData(CTask* pTask, SM_JOB* pJob, DWORD dwSessionID);
// COverlapJob_QueryUser: UserCertifier_AllocJob_425DD0 / sub_425E40.
SM_JOB* OverlapJob_AllocQueryUser();
void OverlapJob_SetupQueryUser(CTask* pTask, SM_JOB* pJob, const char* pszUserID,
                               DWORD dwSessionID);
// COverlapJob_QuerySMLog: UserCertifier_AllocJob_426260 / sub_4262D0.
SM_JOB* OverlapJob_AllocQuerySMLog();
void OverlapJob_SetupQuerySMLog(CTask* pTask, SM_JOB* pJob, const char* pszBegin,
                                const char* pszEnd, DWORD dwSessionID,
                                std::string* pstrUserID, BYTE byType);
// COverlapJob_QueryContentUserAccount: UserCertifier_AllocJob_4265C0 / sub_426630.
SM_JOB* OverlapJob_AllocQueryContentUserAccount();
void OverlapJob_SetupQueryContentUserAccount(CTask* pTask, SM_JOB* pJob, const char* pszQuery,
                                             DWORD dwSessionID, BYTE byType);
// COverlapJob_QueryPunishment: UserCertifier_AllocJob_4268C0 / sub_426930.
SM_JOB* OverlapJob_AllocQueryPunishment();
void OverlapJob_SetupQueryPunishment(CTask* pTask, SM_JOB* pJob, const char* pszUserID,
                                     DWORD dwSessionID);
// COverlapJob_PunishmentJob: UserCertifier_AllocJob_426CF0 / sub_426D60 (the strings and times
// of the 0x7408 request in their order).
SM_JOB* OverlapJob_AllocPunishmentJob();
void OverlapJob_SetupPunishmentJob(CTask* pTask, SM_JOB* pJob, const char* psz1,
                                   const char* psz2, DWORD dwSessionID, BYTE byJob,
                                   DWORD dwSerial, DWORD dwJID, BYTE byType, WORD wShardID,
                                   const char* psz3, const char* psz4, const char* psz5,
                                   const char* psz6, SYSTEMTIME t1, SYSTEMTIME t2,
                                   SYSTEMTIME t3, SYSTEMTIME t4);
// COverlapJob_QueryCharOwnedUser: UserCertifier_AllocJob_427890 / sub_427900.
SM_JOB* OverlapJob_AllocQueryCharOwnedUser();
void OverlapJob_SetupQueryCharOwnedUser(CTask* pTask, SM_JOB* pJob, const char* pszUserID,
                                        DWORD dwSessionID);
// COverlapJob_QueryUserByUserID: UserCertifier_AllocJob_427BA0 / sub_427C10.
SM_JOB* OverlapJob_AllocQueryUserByUserID();
void OverlapJob_SetupQueryUserByUserID(CTask* pTask, SM_JOB* pJob, const char* pszUserID,
                                       DWORD dwSessionID);
// COverlapJob_PreparePatch: UserCertifier_AllocJob_427EB0 / sub_427F20.
SM_JOB* OverlapJob_AllocPreparePatch();
void OverlapJob_SetupPreparePatch(CTask* pTask, SM_JOB* pJob, DWORD dwSessionID,
                                  BYTE byModuleID, BYTE byParam);
// COverlapJob_ArticleJob: UserCertifier_AllocJob_428330 / sub_4283A0 (byType 0 query, 1 add,
// 2 delete, 3 edit).
SM_JOB* OverlapJob_AllocArticleJob();
void OverlapJob_SetupArticleJob(CTask* pTask, SM_JOB* pJob, BYTE byType, const char* pszTitle,
                                DWORD dwSessionID, BYTE byParam, DWORD dwArticleID,
                                const char* pszText);
// COverlapJob_ConcurrentUserLogJob: UserCertifier_AllocJob_4288D0 / sub_428940.
SM_JOB* OverlapJob_AllocConcurrentUserLogJob();
void OverlapJob_SetupConcurrentUserLogJob(CTask* pTask, SM_JOB* pJob, DWORD dwSessionID,
                                          WORD wShardID, BYTE byType, std::string* pstrBegin,
                                          std::string* pstrEnd);
// COverlapJob_AutomatedPunishmentJob: UserCertifier_AllocJob_427620 / sub_427690.
SM_JOB* OverlapJob_AllocAutomatedPunishmentJob();
void OverlapJob_SetupAutomatedPunishmentJob(CTask* pTask, SM_JOB* pJob,
                                            std::string* pstrUserID, std::string* pstr2,
                                            std::string* pstr3, std::string* pstr4,
                                            DWORD dwParam);
