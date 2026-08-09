// Copyright Epic Games, Inc. All Rights Reserved.

#include "GunMotionStudio/FPSRGunMotionStudio.h"
#include "GunMotionStudio/FPSRGunMotionStudioBaker.h"

#include "Hero/FPSRCharacter.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Anim/FPSRGunMotionStudioData.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "Editor.h"

#include "AnimationEditorUtils.h"
#include "ScopedTransaction.h"
#include "HAL/IConsoleManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPSRGunMotionStudio, Log, All);

// Forward declaration only — FPSRGunMotionStudioUI.cpp owns the ImGui/ImGuizmo drawing (§1 "로직과 분리"). This
// translation unit never includes imgui.h/ImGuizmo.h.
namespace FPSRGunMotionStudioUI
{
	void Draw(FFPSRGunMotionStudio& Session);
}

#define LOCTEXT_NAMESPACE "FPSRGunMotionStudio"

FFPSRGunMotionStudio& FFPSRGunMotionStudio::Get()
{
	static FFPSRGunMotionStudio Instance;
	return Instance;
}

void FFPSRGunMotionStudio::Toggle()
{
	if (bActive)
	{
		Deactivate(TEXT("콘솔 명령 토글 OFF"));
	}
	else
	{
		Activate();
	}
}

static FAutoConsoleCommand GFPSRGunMotionStudioToggleCommand(
	TEXT("FPSR.GunMotionStudio"),
	TEXT("총모션 스튜디오(인게임 ImGui) 토글 — PIE에서 로컬 캐릭터가 무기를 든 상태여야 한다 (GunMotionTool_Spec.md §5)."),
	FConsoleCommandDelegate::CreateStatic([]() { FFPSRGunMotionStudio::Get().Toggle(); })
);

UAnimInstance* FFPSRGunMotionStudio::GetArmsAnimInstance() const
{
	AFPSRCharacter* Character = CachedCharacter.Get();
	if (!Character)
	{
		return nullptr;
	}
	USkeletalMeshComponent* Arms = Character->GetFirstPersonArmsMeshComponent();
	return Arms ? Arms->GetAnimInstance() : nullptr;
}

void FFPSRGunMotionStudio::Activate()
{
	if (bActive)
	{
		return;
	}

	// --- §5 "PIE 로컬 AFPSRCharacter+장착 무기 필수, 진입 시... 사유 로그 후 무동작" ---
	AFPSRCharacter* Character = nullptr;
	APlayerController* PC = nullptr;
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType != EWorldType::PIE)
			{
				continue;
			}
			UWorld* World = Context.World();
			if (!World)
			{
				continue;
			}
			PC = World->GetFirstPlayerController();
			if (PC)
			{
				Character = Cast<AFPSRCharacter>(PC->GetPawn());
				if (Character)
				{
					break;
				}
			}
		}
	}
	if (!Character || !PC)
	{
		UE_LOG(LogFPSRGunMotionStudio, Warning, TEXT("[GunMotionStudio] 무동작 — PIE에서 로컬 AFPSRCharacter를 찾지 못했습니다. PIE를 켜고 캐릭터를 조종 중인지 확인하세요."));
		return;
	}

	USkeletalMeshComponent* Arms = Character->GetFirstPersonArmsMeshComponent();
	UCameraComponent* Camera = Character->GetFirstPersonCameraComponent();
	UMeshComponent* ActiveWeapon = Character->GetActiveWeaponMeshComponentForStudio();
	if (!Arms || !Camera || !ActiveWeapon || !Arms->GetSkeletalMeshAsset())
	{
		UE_LOG(LogFPSRGunMotionStudio, Warning, TEXT("[GunMotionStudio] 무동작 — 1인칭 팔/카메라/장착 무기 중 하나가 없습니다(무기를 먼저 드십시오)."));
		return;
	}
	USkeleton* ArmsSkeleton = Arms->GetSkeletalMeshAsset()->GetSkeleton();
	if (!ArmsSkeleton)
	{
		UE_LOG(LogFPSRGunMotionStudio, Warning, TEXT("[GunMotionStudio] 무동작 — 팔 메시에 스켈레톤이 없습니다."));
		return;
	}

	UAnimInstance* ArmsAnim = Arms->GetAnimInstance();
	if (!ArmsAnim)
	{
		UE_LOG(LogFPSRGunMotionStudio, Warning, TEXT("[GunMotionStudio] 무동작 — 팔 AnimInstance가 없습니다."));
		return;
	}

	// --- §3-2 라이브 M/P 캐시 (세션 시작, Idle 정지 상태 — 이후 세션 중 불변) ---
	const FQuat ArmsWorldRot = Arms->GetComponentTransform().GetRotation();
	const FQuat CamWorldRot = Camera->GetComponentTransform().GetRotation();
	CachedCamToArmsRotation = (ArmsWorldRot.Inverse() * CamWorldRot).GetNormalized();
	CachedLowerArmCompRotation = Arms->GetBoneTransform(LowerArmBoneName, RTS_Component).GetRotation();

	const FReferenceSkeleton& RefSkel = ArmsSkeleton->GetReferenceSkeleton();
	const int32 HandRIndex = RefSkel.FindBoneIndex(HandRBoneName);
	if (HandRIndex == INDEX_NONE)
	{
		UE_LOG(LogFPSRGunMotionStudio, Warning, TEXT("[GunMotionStudio] 무동작 — 팔 스켈레톤에 본 '%s'가 없습니다."), *HandRBoneName.ToString());
		return;
	}
	CachedHandRRefPose = RefSkel.GetRefBonePose()[HandRIndex];

	// --- §3-1 WIP transient 클립 생성 + 저작 데이터(AUD) 부착 ---
	FText BakeError;
	UAnimSequence* NewWipClip = FPSRGunMotionStudioBaker::CreateWipClip(ArmsSkeleton, 2.0f, BakeError);
	if (!NewWipClip)
	{
		UE_LOG(LogFPSRGunMotionStudio, Warning, TEXT("[GunMotionStudio] 무동작 — WIP 클립 생성 실패: %s"), *BakeError.ToString());
		return;
	}
	UFPSRGunMotionStudioData* NewWipData = NewObject<UFPSRGunMotionStudioData>(NewWipClip, NAME_None, RF_Transient);
	NewWipClip->AddAssetUserData(NewWipData);

	// --- §5 진입 시 상태: WIP 클립을 슬롯 다이내믹 몽타주로 재생 + 즉시 Pause ---
	UAnimMontage* NewWipMontage = ArmsAnim->PlaySlotAnimationAsDynamicMontage(NewWipClip, StudioSlotName, 0.0f, 0.0f, 1.0f, 1, -1.0f, 0.0f);
	if (!NewWipMontage)
	{
		UE_LOG(LogFPSRGunMotionStudio, Warning, TEXT("[GunMotionStudio] 무동작 — 슬롯 '%s' 다이내믹 몽타주 재생 실패(팔 AnimBP에 이 이름의 Slot 노드가 있는지 확인)."), *StudioSlotName.ToString());
		return;
	}
	ArmsAnim->Montage_Pause(NewWipMontage);

	// --- 월드 일시정지 + 팔/캐릭터만 예외적으로 계속 틱(스크럽 결과가 화면에 실제로 반영되려면 SkeletalMeshComponent가
	//     계속 갱신돼야 한다 — SetGamePaused는 게임플레이/타이머만 멈추고, 이 두 틱 플래그로 "화면=판정 화면"(§0)을 유지). ---
	bWasGamePausedBeforeSession = UGameplayStatics::IsGamePaused(Character->GetWorld());
	UGameplayStatics::SetGamePaused(Character->GetWorld(), true);
	Character->PrimaryActorTick.bTickEvenWhenPaused = true;
	Arms->PrimaryComponentTick.bTickEvenWhenPaused = true;

	bWasShowingMouseCursorBeforeSession = PC->bShowMouseCursor;
	PC->bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	PC->SetInputMode(InputMode);

	CachedCharacter = Character;
	CachedPlayerController = PC;
	if (UFPSRWeaponInventoryComponent* Inventory = Character->FindComponentByClass<UFPSRWeaponInventoryComponent>())
	{
		CachedWeaponData = Inventory->GetCurrentWeapon();
	}
	WipClip = NewWipClip;
	WipStudioData = NewWipData;
	WipMontage = NewWipMontage;
	Mode = EFPSRGunMotionStudioMode::ActionClip;
	Selection = EFPSRGunMotionStudioSelection::None;
	SelectedPartSocket = NAME_None;
	bWholeGunModeForced = false;
	PlayheadSeconds = 0.0f;
	bIsPlaying = false;

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FFPSRGunMotionStudio::Tick), 0.0f);
	bActive = true;

	UE_LOG(LogFPSRGunMotionStudio, Log, TEXT("[GunMotionStudio] 세션 시작 — 캐릭터=%s, WIP 클립 길이=2.0s."), *Character->GetName());
}

void FFPSRGunMotionStudio::Deactivate(const TCHAR* Reason)
{
	if (!bActive)
	{
		return;
	}

	if (UAnimInstance* ArmsAnim = GetArmsAnimInstance())
	{
		if (UAnimMontage* Montage = WipMontage.Get())
		{
			ArmsAnim->Montage_Stop(0.0f, Montage);
		}
	}

	if (AFPSRCharacter* Character = CachedCharacter.Get())
	{
		UGameplayStatics::SetGamePaused(Character->GetWorld(), bWasGamePausedBeforeSession);
		Character->PrimaryActorTick.bTickEvenWhenPaused = false;
		if (USkeletalMeshComponent* Arms = Character->GetFirstPersonArmsMeshComponent())
		{
			Arms->PrimaryComponentTick.bTickEvenWhenPaused = false;
		}
	}
	if (APlayerController* PC = CachedPlayerController.Get())
	{
		PC->bShowMouseCursor = bWasShowingMouseCursorBeforeSession;
		PC->SetInputMode(FInputModeGameOnly());
	}

	FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	TickerHandle.Reset();

	CachedCharacter.Reset();
	CachedPlayerController.Reset();
	CachedWeaponData.Reset();
	WipClip.Reset();
	WipStudioData.Reset();
	WipMontage.Reset();
	Selection = EFPSRGunMotionStudioSelection::None;
	SelectedPartSocket = NAME_None;
	bActive = false;

	UE_LOG(LogFPSRGunMotionStudio, Log, TEXT("[GunMotionStudio] 세션 종료 — 사유: %s"), Reason);
}

bool FFPSRGunMotionStudio::ValidateSessionAlive()
{
	if (!GEngine)
	{
		Deactivate(TEXT("GEngine 없음"));
		return false;
	}
	bool bPieStillRunning = false;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE && Context.World())
		{
			bPieStillRunning = true;
			break;
		}
	}
	if (!bPieStillRunning)
	{
		Deactivate(TEXT("PIE 세션 종료"));
		return false;
	}
	if (!CachedCharacter.IsValid() || !CachedPlayerController.IsValid() || !WipClip.IsValid() || !WipStudioData.IsValid() || !WipMontage.IsValid())
	{
		Deactivate(TEXT("세션 참조 무효화(캐릭터/몽타주/WIP 데이터 소실)"));
		return false;
	}
	return true;
}

bool FFPSRGunMotionStudio::Tick(float DeltaTime)
{
	if (!bActive)
	{
		return false; // ticker already being torn down by Deactivate; don't reschedule.
	}
	if (!ValidateSessionAlive())
	{
		return false;
	}

	if (bIsPlaying)
	{
		const float Length = GetClipLengthSeconds();
		float NewTime = PlayheadSeconds + DeltaTime;
		if (Length > 0.0f && NewTime >= Length)
		{
			NewTime = 0.0f; // loop
		}
		SetPlayheadSeconds(NewTime);
	}

	FPSRGunMotionStudioUI::Draw(*this);
	return true;
}

// --- §6 state pose target / edit-buffer sync ---------------------------------------------------------------------

void FFPSRGunMotionStudio::SetStatePoseTarget(EFPSRGunMotionStatePoseTarget NewTarget)
{
	StatePoseTarget = NewTarget;
	const UFPSRWeaponDataAsset* WeaponData = CachedWeaponData.Get();
	if (!WeaponData)
	{
		StatePoseOffset = FVector::ZeroVector;
		StatePoseTilt = FRotator::ZeroRotator;
		return;
	}
	const FFPSRWeaponStatePose* SourcePose = nullptr;
	switch (NewTarget)
	{
	case EFPSRGunMotionStatePoseTarget::Slide:    SourcePose = &WeaponData->ProceduralWeaponMotion.SlidePose; break;
	case EFPSRGunMotionStatePoseTarget::Airborne: SourcePose = &WeaponData->ProceduralWeaponMotion.AirbornePose; break;
	case EFPSRGunMotionStatePoseTarget::Holster:  SourcePose = &WeaponData->ProceduralWeaponMotion.HolsterPose; break;
	}
	StatePoseOffset = SourcePose ? SourcePose->Offset : FVector::ZeroVector;
	StatePoseTilt = SourcePose ? SourcePose->Tilt : FRotator::ZeroRotator;
}

// --- Selection / track plumbing -------------------------------------------------------------------------------

void FFPSRGunMotionStudio::SetSelection(EFPSRGunMotionStudioSelection NewSelection, FName PartSocket)
{
	Selection = bWholeGunModeForced ? EFPSRGunMotionStudioSelection::Gun : NewSelection;
	SelectedPartSocket = (Selection == EFPSRGunMotionStudioSelection::Part) ? PartSocket : NAME_None;
}

FFPSRStudioTrack* FFPSRGunMotionStudio::GetSelectedTrack() const
{
	UFPSRGunMotionStudioData* Data = WipStudioData.Get();
	if (!Data)
	{
		return nullptr;
	}
	switch (Selection)
	{
	case EFPSRGunMotionStudioSelection::Gun:       return &Data->GunKeys;
	case EFPSRGunMotionStudioSelection::LeftHand:  return &Data->LeftHandKeys;
	case EFPSRGunMotionStudioSelection::RightHand: return &Data->RightHandKeys;
	case EFPSRGunMotionStudioSelection::Part:
		if (SelectedPartSocket.IsNone())
		{
			return nullptr;
		}
		return &Data->PartTracks.FindOrAdd(SelectedPartSocket);
	default:
		return nullptr;
	}
}

TArray<FFPSRStudioAttachSpan>* FFPSRGunMotionStudio::GetSelectedHandAttachSpans() const
{
	UFPSRGunMotionStudioData* Data = WipStudioData.Get();
	if (!Data)
	{
		return nullptr;
	}
	if (Selection == EFPSRGunMotionStudioSelection::LeftHand)
	{
		return &Data->LeftHandAttachSpans;
	}
	if (Selection == EFPSRGunMotionStudioSelection::RightHand)
	{
		return &Data->RightHandAttachSpans;
	}
	return nullptr;
}

// --- Playback / rebake ------------------------------------------------------------------------------------------

float FFPSRGunMotionStudio::GetClipLengthSeconds() const
{
	const UAnimSequence* Clip = WipClip.Get();
	return Clip ? Clip->GetPlayLength() : 0.0f;
}

void FFPSRGunMotionStudio::SetPlayheadSeconds(float Time)
{
	PlayheadSeconds = FMath::Clamp(Time, 0.0f, FMath::Max(GetClipLengthSeconds(), 0.0f));
	if (UAnimInstance* ArmsAnim = GetArmsAnimInstance())
	{
		if (UAnimMontage* Montage = WipMontage.Get())
		{
			ArmsAnim->Montage_SetPosition(Montage, PlayheadSeconds);
		}
	}
}

void FFPSRGunMotionStudio::SetPlaying(bool bPlay)
{
	bIsPlaying = bPlay;
	if (UAnimInstance* ArmsAnim = GetArmsAnimInstance())
	{
		if (UAnimMontage* Montage = WipMontage.Get())
		{
			if (bPlay)
			{
				ArmsAnim->Montage_Resume(Montage);
			}
			else
			{
				ArmsAnim->Montage_Pause(Montage);
			}
		}
	}
}

void FFPSRGunMotionStudio::RebakeAndRestartMontage()
{
	UAnimSequence* Clip = WipClip.Get();
	UFPSRGunMotionStudioData* Data = WipStudioData.Get();
	UAnimInstance* ArmsAnim = GetArmsAnimInstance();
	if (!Clip || !Data || !ArmsAnim)
	{
		return;
	}

	TArray<FFPSRStudioKey> SortedGunKeys = Data->GunKeys.Keys;
	SortedGunKeys.Sort([](const FFPSRStudioKey& A, const FFPSRStudioKey& B) { return A.Time < B.Time; });

	FText BakeError;
	if (!FPSRGunMotionStudioBaker::BakeGunTrackToHandR(Clip, SortedGunKeys, CachedCamToArmsRotation, CachedLowerArmCompRotation, CachedHandRRefPose, HandRBoneName, BakeError))
	{
		UE_LOG(LogFPSRGunMotionStudio, Warning, TEXT("[GunMotionStudio] 재베이크 실패(본 트랙): %s"), *BakeError.ToString());
		return;
	}
	if (!FPSRGunMotionStudioBaker::BakeCurveChannels(Clip, Data, BakeError, nullptr))
	{
		UE_LOG(LogFPSRGunMotionStudio, Warning, TEXT("[GunMotionStudio] 재베이크 실패(커브): %s"), *BakeError.ToString());
		return;
	}

	// §5 "몽타주 재시작(블렌드 0.05) 후 위치 복원" — WIP 재생성 없이 같은 트랜지언트 클립 데이터를 갱신했으므로
	// 몽타주 자체는 그대로 유지해도 되지만, 슬롯 플레이어가 이미 평가한 세그먼트 캐시를 새로 반영하도록 짧게
	// 블렌드 재시작한다.
	const float RestorePosition = PlayheadSeconds;
	if (UAnimMontage* Montage = WipMontage.Get())
	{
		ArmsAnim->Montage_Play(Montage, 1.0f, EMontagePlayReturnType::MontageLength, RestorePosition, false);
		ArmsAnim->Montage_Pause(Montage);
		ArmsAnim->Montage_SetPosition(Montage, RestorePosition);
	}
	if (!bIsPlaying)
	{
		SetPlayheadSeconds(RestorePosition);
	}
}

// --- IK attach -----------------------------------------------------------------------------------------------

bool FFPSRGunMotionStudio::IsSelectedHandAttached() const
{
	const TArray<FFPSRStudioAttachSpan>* Spans = GetSelectedHandAttachSpans();
	if (!Spans)
	{
		return false;
	}
	for (const FFPSRStudioAttachSpan& Span : *Spans)
	{
		if (PlayheadSeconds >= Span.Start && PlayheadSeconds <= Span.End)
		{
			return true;
		}
	}
	return false;
}

bool FFPSRGunMotionStudio::AttachSelectedHand(FName AttachSocket, FString& OutNotify)
{
	AFPSRCharacter* Character = CachedCharacter.Get();
	TArray<FFPSRStudioAttachSpan>* Spans = GetSelectedHandAttachSpans();
	if (!Character || !Spans || AttachSocket.IsNone())
	{
		OutNotify = TEXT("손이 선택되지 않았거나 소켓명이 비어 있습니다.");
		return false;
	}

	// §5 "무기 메시(+파츠)에 소켓 존재 검증" — receiver 소켓이거나, 현재 부착된 파츠 중 하나의 부착 소켓이어야 한다.
	bool bSocketValid = false;
	if (USkeletalMeshComponent* WeaponMesh = Character->GetWeaponMeshComponentForStudio())
	{
		bSocketValid = WeaponMesh->DoesSocketExist(AttachSocket);
	}
	if (!bSocketValid)
	{
		for (const TObjectPtr<UStaticMeshComponent>& Part : Character->GetWeaponPartComponentsForStudio())
		{
			if (Part && Part->GetAttachSocketName() == AttachSocket)
			{
				bSocketValid = true;
				break;
			}
		}
	}
	if (!bSocketValid)
	{
		OutNotify = FString::Printf(TEXT("소켓 '%s'를 무기/파츠에서 찾지 못했습니다."), *AttachSocket.ToString());
		return false;
	}

	FFPSRStudioAttachSpan NewSpan;
	NewSpan.Start = PlayheadSeconds;
	NewSpan.End = GetClipLengthSeconds();
	NewSpan.PartSocket = AttachSocket;
	Spans->Add(NewSpan);

	RebakeAndRestartMontage();
	OutNotify = FString::Printf(TEXT("부착됨: %s (t=%.2f부터)"), *AttachSocket.ToString(), PlayheadSeconds);
	return true;
}

void FFPSRGunMotionStudio::DetachSelectedHand()
{
	TArray<FFPSRStudioAttachSpan>* Spans = GetSelectedHandAttachSpans();
	if (!Spans)
	{
		return;
	}
	for (FFPSRStudioAttachSpan& Span : *Spans)
	{
		if (PlayheadSeconds >= Span.Start && PlayheadSeconds <= Span.End)
		{
			Span.End = PlayheadSeconds;
			RebakeAndRestartMontage();
			return;
		}
	}
}

// --- Gizmo transform / commit ----------------------------------------------------------------------------------

bool FFPSRGunMotionStudio::GetSelectionWorldTransform(FTransform& OutWorld) const
{
	AFPSRCharacter* Character = CachedCharacter.Get();
	if (!Character)
	{
		return false;
	}

	switch (Selection)
	{
	case EFPSRGunMotionStudioSelection::Gun:
	{
		UMeshComponent* Weapon = Character->GetActiveWeaponMeshComponentForStudio();
		if (!Weapon)
		{
			return false;
		}
		if (Mode == EFPSRGunMotionStudioMode::StatePose)
		{
			// §6: no runtime consumer in this spec's scope to preview through, so the gizmo shows the weapon's
			// CURRENT live base transform with the edit-buffer's offset/tilt composed on top, in the weapon's own
			// local frame ("그립 기준").
			OutWorld = FTransform(StatePoseTilt, StatePoseOffset) * Weapon->GetComponentTransform();
			return true;
		}
		OutWorld = Weapon->GetComponentTransform();
		return true;
	}
	case EFPSRGunMotionStudioSelection::LeftHand:
	case EFPSRGunMotionStudioSelection::RightHand:
	{
		// §0 "저작 화면 = 판정 화면" — the studio reads the LIVE ik_hand_l/ik_hand_r bone transform (already the
		// result of the arms AnimBP consuming the currently-baked FPGM_H* curves), never a parallel analytic
		// reconstruction of the target.
		USkeletalMeshComponent* Arms = Character->GetFirstPersonArmsMeshComponent();
		if (!Arms)
		{
			return false;
		}
		const FName IkBone = (Selection == EFPSRGunMotionStudioSelection::LeftHand) ? FName(TEXT("ik_hand_l")) : FName(TEXT("ik_hand_r"));
		if (!Arms->DoesSocketExist(IkBone))
		{
			return false;
		}
		OutWorld = Arms->GetBoneTransform(IkBone, RTS_World);
		return true;
	}
	case EFPSRGunMotionStudioSelection::Part:
	{
		for (const TObjectPtr<UStaticMeshComponent>& Part : Character->GetWeaponPartComponentsForStudio())
		{
			if (Part && Part->GetAttachSocketName() == SelectedPartSocket)
			{
				OutWorld = Part->GetComponentTransform();
				return true;
			}
		}
		return false;
	}
	default:
		return false;
	}
}

void FFPSRGunMotionStudio::CommitGizmoDelta(const FVector& WorldDeltaTranslation, const FQuat& WorldDeltaRotation)
{
	AFPSRCharacter* Character = CachedCharacter.Get();
	if (!Character)
	{
		return;
	}

	// §6 state-pose mode: Gun selection writes directly into the session's edit buffer (no baked track — see
	// GetSelectionWorldTransform's Mode branch above), in the WEAPON's own base rotation space ("그립 기준"), never
	// the camera space §5 action-clip Gun keys use.
	if (Mode == EFPSRGunMotionStudioMode::StatePose)
	{
		if (Selection != EFPSRGunMotionStudioSelection::Gun)
		{
			return;
		}
		UMeshComponent* Weapon = Character->GetActiveWeaponMeshComponentForStudio();
		if (!Weapon)
		{
			return;
		}
		const FQuat WeaponBaseRot = Weapon->GetComponentTransform().GetRotation().GetNormalized();
		const FQuat WeaponBaseRotInv = WeaponBaseRot.Inverse();
		StatePoseOffset += WeaponBaseRotInv.RotateVector(WorldDeltaTranslation);
		const FQuat LocalDeltaRot = (WeaponBaseRotInv * WorldDeltaRotation * WeaponBaseRot).GetNormalized();
		StatePoseTilt = (LocalDeltaRot * FQuat(StatePoseTilt)).GetNormalized().Rotator();
		return;
	}

	FFPSRStudioTrack* Track = GetSelectedTrack();
	if (!Track)
	{
		return;
	}

	// §5 "공간별 역산": 월드 델타를 대상 공간(회전)으로 되돌린다 — 총=카메라 공간(M), 손=총(무기) 공간, 파츠=소켓
	// 프레임(저작 오프셋 회전은 기준에서 배제, §4-2 합성과 정합).
	FQuat SpaceRotation = FQuat::Identity; // world-to-authoring-space basis rotation.
	switch (Selection)
	{
	case EFPSRGunMotionStudioSelection::Gun:
	{
		if (UCameraComponent* Camera = Character->GetFirstPersonCameraComponent())
		{
			SpaceRotation = Camera->GetComponentTransform().GetRotation();
		}
		break;
	}
	case EFPSRGunMotionStudioSelection::LeftHand:
	case EFPSRGunMotionStudioSelection::RightHand:
	{
		if (UMeshComponent* Weapon = Character->GetActiveWeaponMeshComponentForStudio())
		{
			SpaceRotation = Weapon->GetComponentTransform().GetRotation();
		}
		break;
	}
	case EFPSRGunMotionStudioSelection::Part:
	{
		// Socket frame WITHOUT the authored per-part Offset rotation (§5 "저작 오프셋 회전은 기준에서 배제") — the
		// weapon mesh's own world rotation composed with the socket's own relative rotation only.
		if (USkeletalMeshComponent* WeaponMesh = Character->GetWeaponMeshComponentForStudio())
		{
			if (WeaponMesh->DoesSocketExist(SelectedPartSocket))
			{
				SpaceRotation = WeaponMesh->GetSocketTransform(SelectedPartSocket, RTS_World).GetRotation();
			}
		}
		break;
	}
	default:
		return;
	}
	SpaceRotation.Normalize();
	const FQuat SpaceRotationInv = SpaceRotation.Inverse();

	const FVector LocalDeltaTranslation = SpaceRotationInv.RotateVector(WorldDeltaTranslation);
	FQuat LocalDeltaRotation = (SpaceRotationInv * WorldDeltaRotation * SpaceRotation).GetNormalized();

	// §5 "증분 커밋(드래그 시작 스냅샷 대비 델타를 기존 평가값에 가산)" — find/create the key at the current
	// playhead (±1 frame reuses the nearest existing key) and add the delta onto whatever it currently evaluates to.
	constexpr float FrameSnapSeconds = 1.0f / 30.0f;
	FFPSRStudioKey* ExistingKey = Track->Keys.FindByPredicate([this, FrameSnapSeconds](const FFPSRStudioKey& K)
	{
		return FMath::Abs(K.Time - PlayheadSeconds) <= FrameSnapSeconds * 0.5f;
	});

	if (!ExistingKey)
	{
		FFPSRStudioKey NewKey;
		NewKey.Time = PlayheadSeconds;
		TArray<FFPSRStudioKey> Sorted = Track->Keys;
		Sorted.Sort([](const FFPSRStudioKey& A, const FFPSRStudioKey& B) { return A.Time < B.Time; });
		FVector EvalLoc; FQuat EvalRot;
		FPSRGunMotionStudioBaker::EvalStudioKeys(Sorted, PlayheadSeconds, EvalLoc, EvalRot);
		NewKey.Loc = EvalLoc;
		NewKey.Rot = EvalRot.Rotator();
		Track->Keys.Add(NewKey);
		ExistingKey = &Track->Keys.Last();
	}

	ExistingKey->Loc += LocalDeltaTranslation;
	ExistingKey->Rot = (LocalDeltaRotation * FQuat(ExistingKey->Rot)).GetNormalized().Rotator();

	RebakeAndRestartMontage();
}

// --- §6 state pose mode ------------------------------------------------------------------------------------------

void FFPSRGunMotionStudio::SaveStatePoseToDataAsset()
{
	UFPSRWeaponDataAsset* WeaponData = CachedWeaponData.Get();
	if (!WeaponData)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SaveStatePose", "FPSR Gun Motion: Save State Pose"));
	WeaponData->Modify();

	FFPSRWeaponStatePose* TargetPose = nullptr;
	switch (StatePoseTarget)
	{
	case EFPSRGunMotionStatePoseTarget::Slide:    TargetPose = &WeaponData->ProceduralWeaponMotion.SlidePose; break;
	case EFPSRGunMotionStatePoseTarget::Airborne: TargetPose = &WeaponData->ProceduralWeaponMotion.AirbornePose; break;
	case EFPSRGunMotionStatePoseTarget::Holster:  TargetPose = &WeaponData->ProceduralWeaponMotion.HolsterPose; break;
	}
	if (!TargetPose)
	{
		return;
	}

	// §6: the edit buffer (StatePoseOffset/StatePoseTilt, accumulated by CommitGizmoDelta in the weapon's own base
	// rotation space — "그립 기준") IS the value the DA field stores; no further conversion.
	TargetPose->Offset = StatePoseOffset;
	TargetPose->Tilt = StatePoseTilt;

	WeaponData->MarkPackageDirty();
	UE_LOG(LogFPSRGunMotionStudio, Log, TEXT("[GunMotionStudio] 상태 포즈 저장: Offset=%s Tilt=%s"),
		*TargetPose->Offset.ToString(), *TargetPose->Tilt.ToString());
}

// --- Save (§3-1/§3-4) --------------------------------------------------------------------------------------------

void FFPSRGunMotionStudio::SaveClip(bool bAssignMontage, uint8 AssignTargetIndex, TArray<FString>& OutReport)
{
	UAnimSequence* Wip = WipClip.Get();
	UFPSRGunMotionStudioData* WipData = WipStudioData.Get();
	AFPSRCharacter* Character = CachedCharacter.Get();
	if (!Wip || !WipData || !Character)
	{
		OutReport.Add(TEXT("저장 실패 — 세션이 유효하지 않습니다."));
		return;
	}
	USkeletalMeshComponent* Arms = Character->GetFirstPersonArmsMeshComponent();
	USkeleton* ArmsSkeleton = Arms ? Arms->GetSkeletalMeshAsset()->GetSkeleton() : nullptr;
	if (!ArmsSkeleton)
	{
		OutReport.Add(TEXT("저장 실패 — 팔 스켈레톤을 찾지 못했습니다."));
		return;
	}

	const float LengthSeconds = GetClipLengthSeconds();
	const FQuat CamToArms = CachedCamToArmsRotation;
	const FQuat LowerArm = CachedLowerArmCompRotation;
	const FTransform HandRRef = CachedHandRRefPose;
	const FName HandRBone = HandRBoneName;
	UFPSRWeaponDataAsset* WeaponData = CachedWeaponData.Get();

	// Snapshot the studio keys by VALUE (not a pointer into the transient WipData) so the async name-dialog callback
	// below doesn't depend on the session still being active when the user confirms the save dialog.
	const UFPSRGunMotionStudioData* SourceData = WipData;
	FFPSRStudioTrack GunKeysCopy = SourceData->GunKeys;
	FFPSRStudioTrack LeftKeysCopy = SourceData->LeftHandKeys;
	FFPSRStudioTrack RightKeysCopy = SourceData->RightHandKeys;
	TMap<FName, FFPSRStudioTrack> PartTracksCopy = SourceData->PartTracks;
	TArray<FFPSRStudioAttachSpan> LeftSpansCopy = SourceData->LeftHandAttachSpans;
	TArray<FFPSRStudioAttachSpan> RightSpansCopy = SourceData->RightHandAttachSpans;

	TArray<TSoftObjectPtr<UObject>> SkeletonArray;
	SkeletonArray.Add(TSoftObjectPtr<UObject>(ArmsSkeleton));

	TSharedPtr<TArray<FString>> ReportPtr = MakeShared<TArray<FString>>();

	AnimationEditorUtils::CreateAnimationAssets(SkeletonArray, UAnimSequence::StaticClass(), TEXT("_GunMotion"),
		FAnimAssetCreated::CreateLambda([GunKeysCopy, LeftKeysCopy, RightKeysCopy, PartTracksCopy, LeftSpansCopy, RightSpansCopy,
			LengthSeconds, CamToArms, LowerArm, HandRRef, HandRBone, WeaponData, bAssignMontage, AssignTargetIndex, ReportPtr](TArray<UObject*> NewAssets) -> bool
		{
			UAnimSequence* NewClip = (NewAssets.Num() > 0) ? Cast<UAnimSequence>(NewAssets[0]) : nullptr;
			if (!NewClip)
			{
				ReportPtr->Add(TEXT("저장 취소됨(다이얼로그에서 이름/경로를 선택하지 않았거나 실패)."));
				return false;
			}

			NewClip->AdditiveAnimType = AAT_LocalSpaceBase;
			NewClip->RefPoseType = ABPT_RefPose;
			{
				IAnimationDataController& Controller = NewClip->GetController();
				IAnimationDataController::FScopedBracket Bracket(Controller, FText::FromString(TEXT("FPSR Gun Motion: Init Saved Clip")));
				NewClip->Modify();
				Controller.SetFrameRate(FFrameRate(30, 1));
				Controller.SetNumberOfFrames(FFrameNumber(FMath::Max(1, FMath::RoundToInt(LengthSeconds * 30.0f))));
			}

			UFPSRGunMotionStudioData* SavedData = NewObject<UFPSRGunMotionStudioData>(NewClip);
			SavedData->GunKeys = GunKeysCopy;
			SavedData->LeftHandKeys = LeftKeysCopy;
			SavedData->RightHandKeys = RightKeysCopy;
			SavedData->PartTracks = PartTracksCopy;
			SavedData->LeftHandAttachSpans = LeftSpansCopy;
			SavedData->RightHandAttachSpans = RightSpansCopy;
			NewClip->AddAssetUserData(SavedData);

			TArray<FFPSRStudioKey> SortedGunKeys = GunKeysCopy.Keys;
			SortedGunKeys.Sort([](const FFPSRStudioKey& A, const FFPSRStudioKey& B) { return A.Time < B.Time; });

			FText BakeError;
			if (!FPSRGunMotionStudioBaker::BakeGunTrackToHandR(NewClip, SortedGunKeys, CamToArms, LowerArm, HandRRef, HandRBone, BakeError))
			{
				ReportPtr->Add(FString::Printf(TEXT("본 트랙 베이크 실패: %s"), *BakeError.ToString()));
				return false;
			}
			TArray<FString> MontageReport;
			if (!FPSRGunMotionStudioBaker::BakeCurveChannels(NewClip, SavedData, BakeError, &MontageReport))
			{
				ReportPtr->Add(FString::Printf(TEXT("커브 베이크 실패: %s"), *BakeError.ToString()));
				return false;
			}
			ReportPtr->Append(MontageReport);
			ReportPtr->Add(FString::Printf(TEXT("저장됨: %s"), *NewClip->GetPathName()));

			// §3-4 몽타주 배정 — 체크 시 AM_<클립명> 생성/배정, 대상 필드 = ArmsReloadMontage/ArmsEquipMontage.
			if (bAssignMontage && WeaponData)
			{
				const FString MontageName = FString::Printf(TEXT("AM_%s"), *NewClip->GetName());
				const FString PackagePath = FPackageName::GetLongPackagePath(NewClip->GetOutermost()->GetName());
				const FString PackageName = PackagePath / MontageName;

				UPackage* MontagePackage = CreatePackage(*PackageName);
				UAnimMontage* NewMontage = NewObject<UAnimMontage>(MontagePackage, FName(*MontageName), RF_Public | RF_Standalone);
				NewMontage->SetSkeleton(NewClip->GetSkeleton());
				NewMontage->Modify();

				FSlotAnimationTrack SlotTrack;
				SlotTrack.SlotName = FName(TEXT("DefaultSlot"));
				FAnimSegment Segment;
				Segment.SetAnimReference(NewClip);
				Segment.AnimStartTime = 0.0f;
				Segment.AnimEndTime = LengthSeconds;
				Segment.AnimPlayRate = 1.0f;
				Segment.StartPos = 0.0f;
				SlotTrack.AnimTrack.AnimSegments.Add(Segment);
				NewMontage->SlotAnimTracks.Add(SlotTrack);
				// UAnimMontage's composite length is derived from its segments, not the IAnimationDataController
				// model (that system only backs the curve/bone-track DATA of an UAnimSequence — a montage's slot
				// structure is a plain UPROPERTY composite) — CalculateSequenceLength reads the segments just added.
				NewMontage->SetCompositeLength(NewMontage->CalculateSequenceLength());
				NewMontage->MarkPackageDirty();
				FAssetRegistryModule::AssetCreated(NewMontage);

				if (AssignTargetIndex == 0)
				{
					WeaponData->Modify();
					WeaponData->ArmsReloadMontage = NewMontage;
					WeaponData->MarkPackageDirty();
					ReportPtr->Add(FString::Printf(TEXT("몽타주 배정: %s -> ArmsReloadMontage"), *NewMontage->GetName()));
				}
				else if (AssignTargetIndex == 1)
				{
					WeaponData->Modify();
					WeaponData->ArmsEquipMontage = NewMontage;
					WeaponData->MarkPackageDirty();
					ReportPtr->Add(FString::Printf(TEXT("몽타주 배정: %s -> ArmsEquipMontage"), *NewMontage->GetName()));
				}
				else
				{
					ReportPtr->Add(FString::Printf(TEXT("몽타주 생성만: %s (배정 없음)"), *NewMontage->GetName()));
				}
			}

			return true;
		}),
		/*NameBaseObject=*/nullptr, /*bDoNotShowNameDialog=*/false, /*bAllowReplaceExisting=*/false);

	OutReport = *ReportPtr;
}

#undef LOCTEXT_NAMESPACE
