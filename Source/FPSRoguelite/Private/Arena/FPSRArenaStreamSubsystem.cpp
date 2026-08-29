// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaStreamSubsystem.h"
#include "Arena/FPSRArenaActor.h"
#include "Enemy/FPSREnemySpawnSubsystem.h" // 파킹 완료 시 스폰포인트 재캐시 — 아래 PollPending 주석 참고
#include "Run/FPSRStageDirectorSubsystem.h" // NextCombatArenaIndex (순환 규칙 SSOT — GetNextStageOrder 주석 참고)
#include "Core/FPSRLogChannels.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

bool UFPSRArenaStreamSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

bool UFPSRArenaStreamSubsystem::HasServerAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

void UFPSRArenaStreamSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimer);
	}
	Pending.Reset();
	Roster.Reset();
	PendingParkAfterOrder = INDEX_NONE;
	Super::Deinitialize();
}

void UFPSRArenaStreamSubsystem::RefreshRoster() const
{
	Roster.Reset();

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (ULevelStreaming* Streaming : World->GetStreamingLevels())
	{
		if (!Streaming)
		{
			continue;
		}
		// GetLoadedLevel() is non-null once the PACKAGE is loaded, which happens well before the level is made
		// visible — that gap is the whole point here. An arena that is still loading simply is not in the roster
		// yet; RefreshRoster is called again on the next query rather than caching an incomplete answer.
		const ULevel* Level = Streaming->GetLoadedLevel();
		if (!Level)
		{
			continue;
		}

		AFPSRArenaActor* Found = nullptr;
		for (AActor* Actor : Level->Actors)
		{
			if (AFPSRArenaActor* Arena = Cast<AFPSRArenaActor>(Actor))
			{
				if (Found)
				{
					// Invariant 4 (one arena = one sublevel = one mask). Two in one level makes "which mask does
					// this level carry" ambiguous, and every spatial membership answer with it. The editor
					// validator is what should have caught this; say so here rather than picking one silently.
					UE_LOG(LogFPSR, Error,
						TEXT("[ArenaStream] Level '%s' contains more than one AFPSRArenaActor (%s and %s). ADR 0012 invariant 4: one arena per sublevel. Keeping %s."),
						*Streaming->GetWorldAssetPackageName(), *Found->GetName(), *Arena->GetName(), *Found->GetName());
					break;
				}
				Found = Arena;
			}
		}
		if (!Found)
		{
			continue; // an ordinary (non-arena) sublevel — not this subsystem's business
		}

		FFPSRArenaSlot Slot;
		Slot.PackageName = Streaming->GetWorldAssetPackageFName();
		Slot.StageOrder = Found->GetStageOrder();
		Slot.Arena = Found;
		Slot.bStartsActive = Found->StartsActive();
		Slot.Role = Found->GetArenaRole();
		Roster.Add(Slot);
	}

	Roster.Sort([](const FFPSRArenaSlot& A, const FFPSRArenaSlot& B)
	{
		// Ties broken by package name so the order does not depend on the streaming list's own order. Two arenas
		// sharing a StageOrder is an authoring mistake (the duplicate check right below says so); this only
		// guarantees the answer is stable, not that it matches the tie-break AFPSRArenaActor::FindAllInWorld picks
		// (that one sorts by actor name, and the two lists exist for different jobs — this one includes arenas not
		// yet in the world).
		return A.StageOrder != B.StageOrder
			? A.StageOrder < B.StageOrder
			: A.PackageName.LexicalLess(B.PackageName);
	});

	// StageOrder 유일성 검사. 종전에는 두 곳의 주석이 "에디터 검증기가 잡는다"고 주장했지만 **그런 검사는
	// 없었다** — 그러면서 역할 기반 라우팅(FindStageOrderByRole → BeginTransition)이 이 유일성에 새로 의존하게
	// 됐다. 게다가 중복은 예외 상황이 아니라 **기본 상태**다: 아레나 레벨을 복제하면 StageOrder 까지 복제되고,
	// `Docs/BossStage_ContentGuide.md` §4 가 그걸 사람에게 고치라고 안내하는 게 유일한 방어였다.
	// 정렬 직후라 중복은 반드시 인접하므로 한 번 훑으면 끝난다. (레드팀 게이트 2026-08-29)
	if (!bWarnedDuplicateStageOrder)
	{
		for (int32 i = 1; i < Roster.Num(); ++i)
		{
			if (Roster[i].StageOrder == Roster[i - 1].StageOrder)
			{
				bWarnedDuplicateStageOrder = true; // 로스터는 사전 파킹 창에서 4Hz 로 재구축된다 — 한 번만 외친다
				UE_LOG(LogFPSR, Error,
					TEXT("[ArenaStream] Arenas '%s' and '%s' share 스테이지 순서 %d. It must be UNIQUE per arena sublevel: stage lookup picks by StageOrder alone, so a duplicate makes the boss transition (and every parked-successor query) land on whichever tied level sorts first. A duplicated arena level keeps the original's value — give the copy its own."),
					*Roster[i - 1].PackageName.ToString(), *Roster[i].PackageName.ToString(), Roster[i].StageOrder);
				break;
			}
		}
	}
}

bool UFPSRArenaStreamSubsystem::FindSlot(int32 StageOrder, FFPSRArenaSlot& OutSlot) const
{
	RefreshRoster();
	if (const FFPSRArenaSlot* Found = Roster.FindByPredicate([StageOrder](const FFPSRArenaSlot& S) { return S.StageOrder == StageOrder; }))
	{
		OutSlot = *Found;
		return true;
	}
	return false;
}

int32 UFPSRArenaStreamSubsystem::FindStartingStageOrder() const
{
	RefreshRoster();
	for (const FFPSRArenaSlot& Slot : Roster)
	{
		if (Slot.bStartsActive)
		{
			return Slot.StageOrder;
		}
	}
	// Nothing authored as the starting arena: fall back to the first COMBAT one. Skipping boss arenas here matters
	// as much as it does in GetNextStageOrder — a level that forgot bStartsActive would otherwise start the run
	// inside the boss stage, which carries no suppressor and so has no way out.
	for (const FFPSRArenaSlot& Slot : Roster)
	{
		if (Slot.Role == EFPSRArenaRole::Combat)
		{
			return Slot.StageOrder;
		}
	}
	// 여기까지 왔다 = 로스터가 비었거나 **전부 보스 아레나**다. 종전에는 마지막 폴백이 `Roster[0]` 이라
	// all-Boss 축퇴 저작에서 **보스 아레나를 시작 아레나로 돌려줬다** — 바로 위 주석("나갈 길이 없다")과
	// 정면으로 모순된다. PerformSwap 의 all-boss 중단은 순환만 막지 런 시작은 못 막는다.
	// "시작할 수 있는 아레나가 없다"는 INDEX_NONE 으로 정직하게 말하는 편이 낫다 — 조용히 나갈 길 없는
	// 스테이지에서 런을 시작하는 것보다, 호출부가 "시작 아레나 없음" 경로를 타는 쪽이 진단 가능하다.
	// (레드팀 게이트 2026-08-29 범위 밖 발견 ③)
	if (Roster.Num() > 0)
	{
		UE_LOG(LogFPSR, Error,
			TEXT("[ArenaStream] No arena can start the run: %d arena(s) in the roster and every one of them is a BOSS arena. A boss arena carries no suppressor, so a run starting there has no way out. Author at least one 아레나 역할 = 전투 arena."),
			Roster.Num());
	}
	return INDEX_NONE;
}

int32 UFPSRArenaStreamSubsystem::NextCombatStageOrder(
	TConstArrayView<int32> StageOrders, TConstArrayView<EFPSRArenaRole> Roles, int32 CurrentStageOrder)
{
	const int32 Num = StageOrders.Num();
	if (Num == 0 || Roles.Num() != Num)
	{
		return INDEX_NONE;
	}

	const int32 Index = StageOrders.IndexOfByKey(CurrentStageOrder);
	// An unknown current arena starts the scan BEFORE the first entry rather than answering "no next": the roster is
	// authored content and the caller has a transition to run either way, so advancing to a real arena beats
	// stalling the run. (-1 so the first step below lands on index 0 — NextArenaIndex(-1, N) == 0.)
	const int32 Start = (Index == INDEX_NONE) ? -1 : Index;

	// Walk the cycle until a COMBAT arena turns up (보스 스테이지, 2026-08-28 — see the header). One full lap:
	//  - a lone combat arena returns ITSELF at the last step, which is the intended single-arena behavior
	//    (UFPSRStageDirectorSubsystem::NextArenaIndex's self-cycle, unchanged);
	//  - a roster with no combat arena at all falls out with INDEX_NONE rather than handing the caller the boss
	//    stage, which the suppressor cycle must never enter.
	//
	// ⚠️ 이 walk 를 여기서 다시 구현하지 말 것 — `NextCombatArenaIndex` 를 **호출**한다. 종전에는 같은 규칙이
	//    두 곳에 각각 구현돼 있었다(그쪽 주석은 "the cycle rule stays in ONE place"라고 적고 있었다).
	const int32 NextIndex = UFPSRStageDirectorSubsystem::NextCombatArenaIndex(Start, Roles);
	return StageOrders.IsValidIndex(NextIndex) ? StageOrders[NextIndex] : INDEX_NONE;
}

int32 UFPSRArenaStreamSubsystem::GetNextStageOrder(int32 CurrentStageOrder) const
{
	RefreshRoster();

	// 로스터를 두 배열로 펴서 순수 코어에 넘긴다. 이 함수에 남는 일은 "로스터를 읽는 것"뿐이고, 판단은 전부
	// NextCombatStageOrder 안에 있다 — 그쪽은 월드 없이 호출되므로 헤드리스 테스트로 잠긴다
	// (`FPSRoguelite.Arena.StageTransition` (5c)). 종전에는 인덱스 산술·StageOrder 매핑·빈 로스터 경로가
	// 전부 이 안에 있어 테스트가 닿지 못했다. (레드팀 게이트 2026-08-29 P3-1 후속)
	TArray<int32, TInlineAllocator<8>> StageOrders;
	TArray<EFPSRArenaRole, TInlineAllocator<8>> Roles;
	StageOrders.Reserve(Roster.Num());
	Roles.Reserve(Roster.Num());
	for (const FFPSRArenaSlot& Slot : Roster)
	{
		StageOrders.Add(Slot.StageOrder);
		Roles.Add(Slot.Role);
	}

	return NextCombatStageOrder(StageOrders, Roles, CurrentStageOrder);
}

int32 UFPSRArenaStreamSubsystem::FindStageOrderByRole(EFPSRArenaRole Role) const
{
	RefreshRoster();
	// Roster is sorted by StageOrder ascending, so the first match IS the lowest — see the header on why
	// duplicates are left to the editor validator rather than warned about here.
	for (const FFPSRArenaSlot& Slot : Roster)
	{
		if (Slot.Role == Role)
		{
			return Slot.StageOrder;
		}
	}
	return INDEX_NONE;
}

bool UFPSRArenaStreamSubsystem::IsLevelVisibleLocally(FName PackageName) const
{
	const UWorld* World = GetWorld();
	if (!World || PackageName.IsNone())
	{
		return false;
	}
	for (ULevelStreaming* Streaming : World->GetStreamingLevels())
	{
		if (Streaming && Streaming->GetWorldAssetPackageFName() == PackageName)
		{
			// LoadedVisible = AddToWorld fully complete, so components (and their collision) are registered. This
			// is the same predicate UFPSRMapStreamSubsystem gates on, and the reason neither gates on the latent
			// LoadStreamLevel callback: that fires before registration finishes.
			return Streaming->GetLevelStreamingState() == ELevelStreamingState::LoadedVisible
				&& Streaming->GetLoadedLevel() != nullptr;
		}
	}
	return false;
}

void UFPSRArenaStreamSubsystem::RequestPark(int32 StageOrder)
{
	if (!HasServerAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	FFPSRArenaSlot Slot;
	if (!World || !FindSlot(StageOrder, Slot))
	{
		// Not an error. A map with one arena cycles to itself (AFPSRArenaActor cycling is intended behaviour, not
		// an authoring gap), so "there is no next arena to park" is a normal steady state.
		return;
	}

	if (IsLevelVisibleLocally(Slot.PackageName))
	{
		// Already in the world on this machine. Clients may still be catching up — that is what
		// IsReadyForEveryone answers — but there is nothing more for the server to request.
		return;
	}
	for (const FPendingPark& P : Pending)
	{
		if (P.StageOrder == StageOrder)
		{
			return; // already parking
		}
	}

	// bShouldBlockOnLoad stays FALSE. Setting it means bConsiderTimeLimit=false inside AddToWorld, i.e. the whole
	// level enters the world in one frame — a hitch by definition, and the exact cost this whole mechanism exists
	// to move out of the transition window. bMakeVisibleAfterLoad is true because visibility is the expensive
	// half we are pre-paying; loading alone would not shorten anything.
	FLatentActionInfo LatentInfo;
	LatentInfo.UUID = NextLatentUUID++;
	LatentInfo.CallbackTarget = this;
	UGameplayStatics::LoadStreamLevel(World, Slot.PackageName, /*bMakeVisibleAfterLoad*/ true,
		/*bShouldBlockOnLoad*/ false, LatentInfo);

	Pending.Add({ StageOrder, 0.0f });
	UE_LOG(LogFPSR, Log, TEXT("[ArenaStream] parking arena stage %d ('%s') — visibility requested."),
		StageOrder, *Slot.PackageName.ToString());

	if (!World->GetTimerManager().IsTimerActive(PollTimer))
	{
		World->GetTimerManager().SetTimer(PollTimer, this, &UFPSRArenaStreamSubsystem::PollPending, PollInterval, true);
	}
}

void UFPSRArenaStreamSubsystem::RequestParkAfter(int32 CurrentStageOrder)
{
	if (!HasServerAuthority() || CurrentStageOrder == INDEX_NONE)
	{
		return;
	}

	const int32 NextOrder = GetNextStageOrder(CurrentStageOrder);
	if (NextOrder != INDEX_NONE && NextOrder != CurrentStageOrder)
	{
		RequestPark(NextOrder);
		return;
	}

	// The roster cannot answer yet — either the successor's package is still loading, or this really is a
	// single-arena map. Those look identical right now, so retry on the poll timer instead of guessing.
	PendingParkAfterOrder = CurrentStageOrder;
	ParkAfterElapsed = 0.0f;
	if (UWorld* World = GetWorld())
	{
		if (!World->GetTimerManager().IsTimerActive(PollTimer))
		{
			World->GetTimerManager().SetTimer(PollTimer, this, &UFPSRArenaStreamSubsystem::PollPending, PollInterval, true);
		}
	}
}

void UFPSRArenaStreamSubsystem::PollPending()
{
	if (!HasServerAuthority())
	{
		return;
	}

	if (PendingParkAfterOrder != INDEX_NONE)
	{
		ParkAfterElapsed += PollInterval;
		const int32 NextOrder = GetNextStageOrder(PendingParkAfterOrder);
		if (NextOrder != INDEX_NONE && NextOrder != PendingParkAfterOrder)
		{
			PendingParkAfterOrder = INDEX_NONE;
			RequestPark(NextOrder);
		}
		else if (ParkAfterElapsed >= RosterResolveTimeout)
		{
			// Log, not Warning: a one-arena map is a legitimate configuration (it cycles to itself), and this is
			// the only place that can tell the reader which of the two situations they are in.
			UE_LOG(LogFPSR, Log,
				TEXT("[ArenaStream] no arena follows stage %d after %.0fs — either this map has a single arena (it will cycle to itself), or the next arena's sublevel never entered the roster. The roster needs the PACKAGE loaded, which for an authored sublevel means Levels panel > right-click > Change Streaming Method > Always Loaded (ULevelStreamingAlwaysLoaded::ShouldBeLoaded is the only authorable way to force it; bShouldBeLoaded itself is not EditAnywhere). Leave 'Visibility in Game' OFF on a reserve arena — ShouldBeVisible is NOT overridden by that class, so Always Loaded + hidden is exactly 'loaded but not visible'."),
				PendingParkAfterOrder, RosterResolveTimeout);
			PendingParkAfterOrder = INDEX_NONE;
		}
	}

	for (int32 i = Pending.Num() - 1; i >= 0; --i)
	{
		const int32 StageOrder = Pending[i].StageOrder;
		FFPSRArenaSlot Slot;
		if (!FindSlot(StageOrder, Slot))
		{
			UE_LOG(LogFPSR, Warning, TEXT("[ArenaStream] arena stage %d vanished from the roster while parking — dropped."), StageOrder);
			Pending.RemoveAt(i);
			continue;
		}

		Pending[i].Elapsed += PollInterval;

		if (IsLevelVisibleLocally(Slot.PackageName))
		{
			// It is in the world now, so hide it. The arena's own BeginPlay already did this (bStartsActive is
			// false on a reserve arena) — this is deliberately redundant, because "a parked arena ends up hidden"
			// is a property of parking, not something to infer from how the arena happens to be authored. Both
			// calls are idempotent.
			if (AFPSRArenaActor* Arena = Slot.Arena.Get())
			{
				if (!Arena->IsArenaActive())
				{
					Arena->SetArenaActive(false);
				}
			}

			// Re-cache enemy spawn points: UFPSREnemySpawnSubsystem caches them ONCE at world begin
			// (CacheSpawnPoints), and a parked arena's sublevel is not visible yet at that moment — so without
			// this its spawn points never enter the cache and the stage that arena becomes live has NO enemies.
			// UFPSRMapStreamSubsystem::HandleMapReady does the same thing at the same point in its own flow.
			//
			// Doing it HERE (at park) rather than at the swap is deliberate and safe: PassesCommonSpawnGates ends
			// with an arena-bounds test against the LIVE arena, so a reserve arena's points sit in the cache
			// ineligible until it actually goes live. Caching early costs nothing and takes one more thing out of
			// the transition window.
			if (UWorld* World = GetWorld())
			{
				if (UFPSREnemySpawnSubsystem* Spawn = World->GetSubsystem<UFPSREnemySpawnSubsystem>())
				{
					Spawn->RefreshSpawnPointCache();
				}
			}

			TArray<FString> NotReady;
			GetConnectionsNotReady(StageOrder, NotReady);
			UE_LOG(LogFPSR, Log,
				TEXT("[ArenaStream] arena stage %d ('%s') parked on the server after %.1fs; %d client(s) still catching up."),
				StageOrder, *Slot.PackageName.ToString(), Pending[i].Elapsed, NotReady.Num());

			Pending.RemoveAt(i);
			continue;
		}

		if (Pending[i].Elapsed >= ParkTimeout)
		{
			// Warn and stop polling THIS one, but do not mark it unusable: the level may still land, and the
			// transition's own readiness check is what decides whether the swap can go ahead. Silence here would
			// turn a stuck stream into a mystery stall at the next transition instead.
			UE_LOG(LogFPSR, Error,
				TEXT("[ArenaStream] arena stage %d ('%s') did not reach LoadedVisible within %.0fs. Check it is an authored streaming sublevel of the persistent map (Levels panel > Add Existing) — a level created at runtime cannot work here, because clients resolve streaming status by package name against their OWN persistent level."),
				StageOrder, *Slot.PackageName.ToString(), ParkTimeout);
			Pending.RemoveAt(i);
		}
	}

	if (Pending.Num() == 0 && PendingParkAfterOrder == INDEX_NONE)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(PollTimer);
		}
	}
}

void UFPSRArenaStreamSubsystem::GetConnectionsNotReady(int32 StageOrder, TArray<FString>& OutNames) const
{
	OutNames.Reset();

	const UWorld* World = GetWorld();
	FFPSRArenaSlot Slot;
	if (!World || !FindSlot(StageOrder, Slot))
	{
		return; // unknown arena — see IsReadyForEveryone on why that reads as "nothing to wait for"
	}

	if (!IsLevelVisibleLocally(Slot.PackageName))
	{
		OutNames.Add(TEXT("server"));
	}

	// Only the server can answer for clients, and this is the authoritative source: it is the same set
	// UNetDriver consults to decide whether an actor in a streamed level may be replicated to a connection, so a
	// name missing here means that client would genuinely receive none of the new arena's actors.
	const UNetDriver* NetDriver = World->GetNetDriver();
	if (!NetDriver)
	{
		return; // standalone — the server check above is the whole answer
	}
	for (const UNetConnection* Connection : NetDriver->ClientConnections)
	{
		if (!Connection)
		{
			continue;
		}
		if (!Connection->ClientVisibleLevelNames.Contains(Slot.PackageName))
		{
			OutNames.Add(Connection->GetName());
		}
	}
}

bool UFPSRArenaStreamSubsystem::IsReadyForEveryone(int32 StageOrder) const
{
	TArray<FString> NotReady;
	GetConnectionsNotReady(StageOrder, NotReady);
	return NotReady.Num() == 0;
}
