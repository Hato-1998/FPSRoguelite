// Copyright Epic Games, Inc. All Rights Reserved.

#include "Localization/FPSRStringTableReload.h"

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
			// Atomic per table: unregister immediately followed by re-register within the same loop iteration, so
			// no caller can observe the table missing from the registry mid-reload (spec §8: "해제→재등록을 한 함수
			// 안에서 원자적으로 수행 — 부분 상태 노출 금지").
			Registry.UnregisterStringTable(Table.Id);
			Registry.Internal_LocTableFromFile(Table.Id, Table.Namespace, Table.FilePath, FPaths::ProjectContentDir());

			if (Registry.FindStringTable(Table.Id).IsValid())
			{
				++SucceededCount;
			}
			else
			{
				UE_LOG(LogFPSRStringTableReload, Error, TEXT("FPSRStringTableReload: failed to reload string table '%s' from '%s' — CSV missing or unreadable."), Table.Id, Table.FilePath);
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
