// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Editor-only: re-register the game's CSV string tables without restarting the editor.
 *  Unregisters each table id then re-runs the same registration the game module performs. */
namespace FPSRStringTableReload
{
	/** 반환: 재등록 성공 테이블 수. 실패 테이블은 로그 Error + 알림 토스트. */
	int32 ReloadAll();
}
