// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRFlowLog.h"
#include "Core/FPSRLogChannels.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/App.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

namespace FPSRFlowLog
{
	namespace
	{
		FCriticalSection GLock;
		FString GLogFilePath;
		bool GInitialized = false;

		/** Lazily resolve/create the per-launch log file under <ProjectDir>/logs/. Caller must hold GLock. */
		void EnsureInitialized_NoLock()
		{
			if (GInitialized)
			{
				return;
			}
			GInitialized = true;

			const FString LogDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("logs"));
			IFileManager::Get().MakeDirectory(*LogDir, /*Tree=*/true);

			// Per-launch file so each test run is a clean, self-contained trace.
			const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
			GLogFilePath = FPaths::Combine(LogDir, FString::Printf(TEXT("FlowLog_%s.log"), *Stamp));

			const FString Header = FString::Printf(
				TEXT("===== FPSRoguelite flow log — %s — %s (%s) =====") LINE_TERMINATOR,
				*FDateTime::Now().ToString(),
				LexToString(FApp::GetBuildConfiguration()),
				FApp::IsGame() ? TEXT("Game") : TEXT("Editor"));
			FFileHelper::SaveStringToFile(Header, *GLogFilePath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
		}

		/** [Server]/[Client]/[Standalone]/[NoWorld] tag from a world-context object. */
		FString NetRoleTag(const UObject* WorldContext)
		{
			const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
			if (!World)
			{
				return TEXT("NoWorld");
			}
			switch (World->GetNetMode())
			{
			case NM_DedicatedServer: return TEXT("DedServer");
			case NM_ListenServer:    return TEXT("Server");
			case NM_Client:          return TEXT("Client");
			case NM_Standalone:      return TEXT("Standalone");
			default:                 return TEXT("?");
			}
		}
	}

	void Event(const FString& Tag, const FString& Message)
	{
		UE_LOG(LogFPSRFlow, Log, TEXT("[%s] %s"), *Tag, *Message);

		const FString Line = FString::Printf(TEXT("[%s][%s] %s") LINE_TERMINATOR,
			*FDateTime::Now().ToString(TEXT("%Y.%m.%d-%H.%M.%S")), *Tag, *Message);

		FScopeLock Lock(&GLock);
		EnsureInitialized_NoLock();
		FFileHelper::SaveStringToFile(Line, *GLogFilePath,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	}

	void Event(const UObject* WorldContext, const FString& Tag, const FString& Message)
	{
		Event(Tag, FString::Printf(TEXT("[%s] %s"), *NetRoleTag(WorldContext), *Message));
	}

	FString GetLogFilePath()
	{
		FScopeLock Lock(&GLock);
		EnsureInitialized_NoLock();   // resolve the path even if queried before the first Event (e.g. the BOOT line).
		return GLogFilePath;
	}
}
