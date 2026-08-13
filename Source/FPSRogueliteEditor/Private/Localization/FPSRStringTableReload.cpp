// Copyright Epic Games, Inc. All Rights Reserved.

#include "Localization/FPSRStringTableReload.h"

#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"
#include "Misc/Paths.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPSRStringTableReload, Log, All);

namespace FPSRStringTableReload
{
	namespace
	{
		struct FFPSRStringTableReloadSpec
		{
			const TCHAR* Id;
			const TCHAR* Namespace;
			const TCHAR* FilePath;
		};

		// Mirrors FFPSRogueliteGameModule::StartupModule 1:1 (Source/FPSRoguelite/Private/FPSRoguelite.cpp) — kept
		// as its own literal list (not shared code) because the runtime registration must stay a *literal*
		// LOCTABLE_FROMFILE_GAME macro call site for the gather commandlet's source scan to discover it
		// (Docs/Specs/LOC0_StringTablePipeline.md §3-2); the macro itself can't be driven by a runtime loop since
		// its arguments are stringized at compile time. This editor-only utility only needs the same three
		// (Id, Namespace, FilePath) triples to unregister/re-register the tables at runtime.
		const FFPSRStringTableReloadSpec GTables[] =
		{
			{ TEXT("UI"), TEXT("UI"), TEXT("StringTables/ST_UI.csv") },
			{ TEXT("CardEffect"), TEXT("CardEffect"), TEXT("StringTables/ST_CardEffect.csv") },
			{ TEXT("Card"), TEXT("Card"), TEXT("StringTables/ST_Card.csv") },
		};
	}

	int32 ReloadAll()
	{
		FStringTableRegistry& Registry = FStringTableRegistry::Get();
		int32 SucceededCount = 0;

		for (const FFPSRStringTableReloadSpec& Table : GTables)
		{
			// In-place import on the already-registered table — the same path the engine's own CSV file-watcher uses
			// (FStringTableRegistry::Internal_OnDirectoryChanged → FindMutableStringTable → ImportStrings). ImportStrings
			// validates the file (load + header) BEFORE clearing, so a broken/locked CSV returns false and leaves the
			// previous strings intact, and the table never leaves the registry (no unregister window). The old
			// unregister→Internal_LocTableFromFile approach was both destructive (Internal_LocTableFromFile registers an
			// EMPTY table even when ImportStrings fails — StringTableRegistry.cpp:198-209) and unobservable (the
			// registered-check was always true) — 레드팀 P2-1.
			const FString FullPath = FPaths::ProjectContentDir() / Table.FilePath;
			bool bSucceeded = false;

			FStringTablePtr ExistingTable = Registry.FindMutableStringTable(Table.Id);
			if (ExistingTable.IsValid())
			{
				bSucceeded = ExistingTable->ImportStrings(FullPath);
			}
			else
			{
				// Not registered (e.g. the CSV was missing at module startup): validate on a scratch table first and
				// register only on success — never expose an empty table under the id.
				FStringTableRef NewTable = FStringTable::NewStringTable();
				NewTable->SetNamespace(Table.Namespace);
				if (NewTable->ImportStrings(FullPath))
				{
					Registry.RegisterStringTable(Table.Id, NewTable);
					bSucceeded = true;
				}
			}

			if (bSucceeded)
			{
				++SucceededCount;
			}
			else
			{
				UE_LOG(LogFPSRStringTableReload, Error, TEXT("FPSRStringTableReload: failed to reload string table '%s' from '%s' — CSV missing, locked, or malformed. Previous strings (if any) were left untouched."), Table.Id, Table.FilePath);
			}
		}

		const int32 TotalCount = UE_ARRAY_COUNT(GTables);
		UE_LOG(LogFPSRStringTableReload, Log, TEXT("FPSRStringTableReload: reloaded %d/%d string table(s)."), SucceededCount, TotalCount);

		if (SucceededCount < TotalCount)
		{
			FNotificationInfo Info(FText::Format(
				NSLOCTEXT("FPSRStringTableReload", "ReloadPartialFailure", "StringTable CSV 리로드: {0}/{1} 성공 — 실패 테이블은 Output Log 확인"),
				FText::AsNumber(SucceededCount), FText::AsNumber(TotalCount)));
			Info.ExpireDuration = 6.0f;
			Info.bUseSuccessFailIcons = true;
			TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
			if (Notification.IsValid())
			{
				Notification->SetCompletionState(SNotificationItem::CS_Fail);
			}
		}

		return SucceededCount;
	}
}
