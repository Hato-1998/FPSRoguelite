// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assembler/FPSRWeaponAssemblerHelpers.h"

#include "Assembler/FPSRWeaponAssemblerSettings.h"
#include "Hero/FPSRCharacter.h"   // GetFirstPersonViewSetup — 구도 규칙은 캐릭터가 갖고 툴이 그걸 부른다
#include "Weapon/FPSRWeaponDataAsset.h"

#include "Engine/LocalPlayer.h"   // ULocalPlayer::AspectRatioAxisConstraint — 게임이 실제로 쓰는 축 제약(상수로 박지 않는다)
#include "Engine/World.h"

#include "FileHelpers.h"   // UEditorLoadingAndSavingUtils::SavePackages (UnrealEd — avoids the EditorScriptingUtilities plugin)
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/Skeleton.h"   // USkeleton::Sockets/GetReferenceSkeleton — BakeWeaponSocket may own a socket on the skeleton, not the mesh (P1)
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/Guid.h"

namespace FPSRWeaponAssemblerHelpers
{
	FString MakePartDisplayName(const TSoftObjectPtr<UStaticMesh>& Part, int32 Index)
	{
		if (Part.IsNull())
		{
			return FString::Printf(TEXT("Part%d"), Index);
		}
		FString Name = Part.GetAssetName();
		Name.RemoveFromStart(TEXT("SM_Wep_Mod_A_"));
		Name.RemoveFromStart(TEXT("SM_Wep_Mod_"));
		int32 UnderscorePos = INDEX_NONE;
		if (Name.FindLastChar(TEXT('_'), UnderscorePos))
		{
			const FString Tail = Name.Mid(UnderscorePos + 1);
			if (!Tail.IsEmpty() && Tail.IsNumeric())
			{
				Name = Name.Left(UnderscorePos);
			}
		}
		return Name.IsEmpty() ? FString::Printf(TEXT("Part%d"), Index) : Name;
	}

	FTransform RootBoneComponentSpace(const USkeletalMesh* Mesh)
	{
		if (Mesh && Mesh->GetRefSkeleton().GetRefBonePose().Num() > 0)
		{
			return Mesh->GetRefSkeleton().GetRefBonePose()[0];
		}
		return FTransform::Identity;
	}

	int32 BakeSockets(UFPSRWeaponDataAsset* DA, USkeletalMeshComponent* BodyComp, const TArray<UStaticMeshComponent*>& PartComps)
	{
		USkeletalMesh* Body = BodyComp ? BodyComp->GetSkeletalMeshAsset() : nullptr;
		if (!DA || !Body)
		{
			return 0;
		}

		// The body's world transform in the preview scene — parts are captured RELATIVE TO THIS (not the world
		// origin), so the bake stays correct even when the whole assembly was dragged via "전체 이동".
		const FTransform BodyWorld = BodyComp->GetComponentTransform();

		const FReferenceSkeleton& RefSkel = Body->GetRefSkeleton();
		const FName RootBone = RefSkel.GetNum() > 0 ? RefSkel.GetBoneName(0) : NAME_None;
		// Sockets are BONE-relative. This pack's root bone carries a 90° roll, so convert the designer's component-
		// space placement through the root bone's component-space transform (bone 0 ref pose) — else the captured part
		// lands rotated at runtime.
		const FTransform RootBoneCS = RootBoneComponentSpace(Body);

		Body->Modify();
		DA->Modify();

		// 🚨 예전엔 여기서 바디 메시의 SOCKET_Mount_* 를 **전부 지우고** 이 무기 것만 다시 구웠다. 그런데 이 팩은
		// 무기 7종이 바디 메시(SK_Wep_Mod_A_Body_01) 하나를 공유하므로, 그건 곧 **다른 무기 6종의 마운트를 이 무기
		// 값으로 덮어쓰는** 짓이었다. 실사고: SMG 를 베이크하자 Rifle 의 핸드가드가 10.47cm, 개머리판이 10.58cm
		// 밀려났다(변종 메시 크기가 달라 마운트 위치가 달라야 했기 때문).
		//
		// 런타임은 이미 올바른 모델을 갖고 있다 — 소켓 = **고정 마운트**, 무기별 차이 = **DA 의 Offset**:
		//   FPSRWeaponPartSelector.cpp:91  ResolvedEntry.Socket = Entry.Socket; // FIXED mount
		//   FPSRCharacter.cpp:2015         PartComp->SetRelativeTransform(PartDef.Offset);
		// 진화 단계도 그 분리에 의존한다(단계는 Offset 만 갈아끼우고 Socket 은 고정). 그래서 베이크도 그 계약을
		// 따른다: **이미 있는 소켓은 건드리지 않고**, 이 무기만의 차이를 Offset 에 쓴다. 소켓을 새로 만드는 건
		// 그 슬롯에 마운트가 아직 없을 때뿐이고, 그때는 다른 무기와 절대 겹치지 않도록 GUID id 로 발급한다.
		int32 n = 0;
		bool bBodyDirty = false;
		const int32 Count = FMath::Min(PartComps.Num(), DA->WeaponParts.Num());
		for (int32 i = 0; i < Count; ++i)
		{
			UStaticMeshComponent* PC = PartComps[i];
			if (!PC)
			{
				continue;
			}

			// Part transform relative to the BODY COMPONENT (robust to the body being moved — was previously the
			// part's world transform, which only matched when the body sat at the world origin).
			const FTransform PartRelBody = PC->GetComponentTransform().GetRelativeTransform(BodyWorld);

			const FName ExistingSocketName = DA->WeaponParts[i].Socket;
			USkeletalMeshSocket* Socket = ExistingSocketName.IsNone() ? nullptr : Body->FindSocket(ExistingSocketName);

			if (Socket)
			{
				// 마운트는 그대로 두고 이 무기만의 차이만 기록한다. SetWeapon 의 배치식
				// (Init = Offset * (SocketRel * RootBoneCS))의 정확한 역연산이라 왕복이 보존된다.
				const FTransform SocketRel(Socket->RelativeRotation, Socket->RelativeLocation, Socket->RelativeScale);
				const FTransform MountRelBody = SocketRel * RootBoneCS;
				DA->WeaponParts[i].Offset = PartRelBody.GetRelativeTransform(MountRelBody);
			}
			else
			{
				// 이 슬롯엔 아직 마운트가 없다(새 파츠, 또는 소켓이 지워진 DA) — 이 무기 전용 GUID 로 새로 만든다.
				// 메시/컴포넌트 이름과 무관한 안정 id라 메시를 교체해도 리네임되지 않는다(사용자 표시명은 DisplayLabel).
				const FName SocketName(*FString::Printf(TEXT("SOCKET_Mount_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Base36Encoded)));
				const FTransform SocketRel = PartRelBody.GetRelativeTransform(RootBoneCS);

				USkeletalMeshSocket* NewSocket = NewObject<USkeletalMeshSocket>(Body);
				NewSocket->SocketName = SocketName;
				NewSocket->BoneName = RootBone;
				NewSocket->RelativeLocation = SocketRel.GetLocation();
				NewSocket->RelativeRotation = SocketRel.GetRotation().Rotator();
				NewSocket->RelativeScale = FVector(1.0f);
				Body->AddSocket(NewSocket, false);
				bBodyDirty = true;

				DA->WeaponParts[i].Socket = SocketName;
				DA->WeaponParts[i].Offset = FTransform::Identity;
			}
			++n;
		}

		DA->MarkPackageDirty();

		// 바디 메시는 **새 마운트를 만들었을 때만** 더럽힌다 — 공유 애셋이라, 이 무기 하나를 저장하는 일이 다른
		// 무기까지 건드리는 일이 되어선 안 된다(위 사고의 재발 방지선).
		TArray<UPackage*> PackagesToSave;
		PackagesToSave.Add(DA->GetOutermost());
		if (bBodyDirty)
		{
			Body->RebuildSocketMap();
			Body->MarkPackageDirty();
			PackagesToSave.Add(Body->GetOutermost());
		}
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, /*bOnlyDirty=*/false);
		return n;
	}

	USkeletalMeshSocket* FindWeaponSocket(USkeletalMesh* Mesh, FName SocketName, UObject*& OutOwner)
	{
		OutOwner = nullptr;
		if (!Mesh || SocketName.IsNone())
		{
			return nullptr;
		}

		// FindSocket 은 메시 -> 스켈레톤 순으로 찾는다(엔진 SkeletalMesh.cpp FindSocketAndIndex:4993, WITH_EDITOR
		// 경로 — 캐시 없이 매번 배열을 선형 탐색한다. SocketMap 캐시는 쿡 빌드 전용). 반환된 소켓이 메시의
		// GetMeshOnlySocketList() 배열 소속이면 주인은 메시, 아니면(스켈레톤에서 찾힌 것) 주인은 스켈레톤이다 —
		// 소켓의 GetOuter() 로 판별할 수도 있지만, 배열 소속으로 확인하는 쪽이 "지금 이 배열을 고치면 되는가"에
		// 더 직접적으로 대응한다.
		USkeletalMeshSocket* Socket = Mesh->FindSocket(SocketName);
		if (!Socket)
		{
			return nullptr;
		}

		if (Mesh->GetMeshOnlySocketList().Contains(Socket))
		{
			OutOwner = Mesh;
		}
		else
		{
			OutOwner = Mesh->GetSkeleton();
		}
		return Socket;
	}

	FBakeHandResult BakeWeaponSocket(USkeletalMeshComponent* ArmsComp, const USkeletalMeshComponent* BodyComp, FName SocketName, FName BoneName)
	{
		FBakeHandResult Result;
		Result.SocketName = SocketName;
		Result.BoneName = BoneName;   // 실패 경로에서도 호출자가 무엇을 요청했는지는 남긴다.

		USkeletalMesh* Arms = ArmsComp ? ArmsComp->GetSkeletalMeshAsset() : nullptr;
		if (!Arms || !BodyComp)
		{
			Result.Message = TEXT("팔 또는 무기 프리뷰가 없다");
			return Result;
		}
		if (SocketName.IsNone())
		{
			Result.Message = TEXT("무기 DA 의 WeaponAttachSocket 이 비어 있다");
			return Result;
		}

		UObject* Owner = nullptr;
		USkeletalMeshSocket* Socket = FindWeaponSocket(Arms, SocketName, Owner);
		USkeleton* Skeleton = Arms->GetSkeleton();   // 신규 생성 + 뼈 재배치 검증 양쪽에 쓴다(레퍼런스 스켈레톤 조회).

		if (!Socket)
		{
			// 소켓이 없다(P5) — 스켈레톤에 새로 만든다. 엔진 정식 경로 FEditableSkeleton::HandleAddSocket
			// (EditableSkeleton.cpp:542) 그대로: Outer=스켈레톤인 소켓을 새로 만들어 Skeleton->Sockets 에 추가한다.
			// 🚨 USkeletalMesh::AddSocket(Socket, /*bAddToSkeleton=*/true) 는 절대 쓰지 않는다 — 그 구현은
			// 메시에도 사본을 만들어 버리고, FindSocketAndIndex 는 메시를 먼저 보므로 그 메시 사본이 방금 만든
			// 스켈레톤 사본을 영원히 가린다(계약 2번 근거, 헤더 주석 참조).
			if (BoneName.IsNone())
			{
				Result.Message = TEXT("소켓이 없고 기준 뼈도 지정되지 않아 새로 만들 수 없다 — 먼저 뼈를 골라라");
				return Result;
			}
			if (!Skeleton || Skeleton->GetReferenceSkeleton().FindBoneIndex(BoneName) == INDEX_NONE)
			{
				Result.Message = FString::Printf(TEXT("뼈 '%s' 가 팔 스켈레톤의 레퍼런스 스켈레톤에 없어 소켓을 만들 수 없다"),
					*BoneName.ToString());
				return Result;
			}

			Skeleton->Modify();
			Socket = NewObject<USkeletalMeshSocket>(Skeleton);   // Outer = 스켈레톤(메시가 아니다)
			Socket->SocketName = SocketName;
			Socket->BoneName = BoneName;
			Skeleton->Sockets.Add(Socket);

			Owner = Skeleton;
			Result.bCreatedSocket = true;
		}

		if (!Owner)
		{
			// FindWeaponSocket 이 소켓은 찾았는데 주인을 못 정했다 — 메시/스켈레톤 둘 중 하나에서 찾은 것이라
			// 이론상 불가능하지만, 저장 대상이 없는 채로 진행하지 않도록 방어적으로 막는다.
			Result.Message = TEXT("소켓의 소유 애셋(팔 메시/스켈레톤)을 판별하지 못했다");
			return Result;
		}

		// 호출자가 뼈를 지정하지 않았으면(None) 기존 소켓의 뼈를 그대로 쓴다 — "지정 안 함"을 "뼈 없음/루트로
		// 옮겨라"로 해석하지 않는다. 방금 새로 만든 경우는 BoneName 이 이미 채워져 있어 이 줄은 값을 바꾸지 않는다.
		const FName ResolvedBoneName = BoneName.IsNone() ? Socket->BoneName : BoneName;
		const bool bWantsMove = !Result.bCreatedSocket && Socket->BoneName != ResolvedBoneName;
		if (bWantsMove)
		{
			// 신규 생성과 같은 기준으로 검증한다 — 없는 뼈로 옮기면 GetSocketTransform 이 조용히 컴포넌트 트랜스폼
			// (또는 엉뚱한 값)으로 폴백하고, 그 값 그대로 저장까지 성공해 버려서 신규 생성 실패보다 눈치채기 더
			// 어렵다(이 파일의 다른 실패들과 같은 "성공한 것처럼 보이는 실패" 패턴).
			if (!Skeleton || Skeleton->GetReferenceSkeleton().FindBoneIndex(ResolvedBoneName) == INDEX_NONE)
			{
				Result.Message = FString::Printf(TEXT("뼈 '%s' 가 팔 스켈레톤에 없어 소켓을 옮길 수 없다 — 기존 뼈(%s)를 유지한다"),
					*ResolvedBoneName.ToString(), *Socket->BoneName.ToString());
				return Result;
			}
			Result.bMovedBone = true;
		}
		Result.BoneName = ResolvedBoneName;

		// 기준 프레임 = 대상 **뼈**의 현재(포즈된) 월드 트랜스폼(GetSocketTransform 은 뼈 이름도 푼다 — 엔진 사실
		// 1). 소켓 자신을 기준으로 재면 지금 값이 상쇄돼 항상 항등이 나온다.
		const FTransform BoneWorld = ArmsComp->GetSocketTransform(ResolvedBoneName, RTS_World);
		const FTransform Rel = BodyComp->GetComponentTransform().GetRelativeTransform(BoneWorld);

		Owner->Modify();    // 주인 패키지(메시 또는 스켈레톤) — 실행취소가 소켓 배열/필드 변경을 잡도록.
		Socket->Modify();   // 소켓 자신 — 예전엔 이걸 안 해서, 이미 있던 소켓을 값만 재베이크하면(새로 만들지
		                    // 않으면) 실행취소가 필드 변경을 못 잡았다.
		Socket->BoneName = ResolvedBoneName;
		Socket->RelativeLocation = Rel.GetLocation();
		Socket->RelativeRotation = Rel.GetRotation().Rotator();
		Socket->RelativeScale = FVector(1.0f);   // 스케일은 런타임 WeaponAttachScale 이 소유 — 불변식 I-B, 헤더 주석 참조

		Owner->MarkPackageDirty();

		// RebuildSocketMap() 은 USkeletalMesh 전용이다 — USkeleton 엔 그런 함수 자체가 없다(엔진 전체에 SkeletalMesh.h/
		// .cpp 두 곳에만 존재). 게다가 메시 쪽 구현도 #if !WITH_EDITOR 로 막혀 있어 에디터 안에서는 완전 no-op이다
		// (SkeletalMesh.cpp:5136-5166): 그 함수가 갱신하는 SocketMap 캐시는 쿡된(비-에디터) 빌드에서만 쓰이고,
		// 에디터의 FindSocketAndIndex(USkeletalMesh WITH_EDITOR 분기 · USkeleton 쪽은 애초에 분기 없이 매번)는
		// 항상 Sockets 배열을 선형 탐색해 최신 값을 본다. 엔진 자신의 FEditableSkeleton::HandleAddSocket
		// (EditableSkeleton.cpp:542)도 Sockets.Add() 뒤에 그런 리빌드를 부르지 않는다 — 그래서 주인이 스켈레톤일
		// 땐 여기서도 아무것도 더 부르지 않는다. 주인이 메시일 때만(기존 BakeSockets 와 동일하게) 불러서, 캐시를
		// 실제로 쓰는 쿡 빌드까지 챙긴다.
		USkeletalMesh* OwnerMesh = Cast<USkeletalMesh>(Owner);
		if (OwnerMesh)
		{
			OwnerMesh->RebuildSocketMap();
		}
		const TCHAR* OwnerLabel = OwnerMesh ? TEXT("팔 메시") : TEXT("팔 스켈레톤");

		TArray<UPackage*> PackagesToSave;
		PackagesToSave.Add(Owner->GetOutermost());
		const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, /*bOnlyDirty=*/false);

		if (bSaved)
		{
			Result.SavedPackages.Add(Owner->GetOutermost()->GetName());
			Result.bOk = true;
			Result.Message = FString::Printf(TEXT("%s (뼈 %s) <- 위치 %s / 회전 %s · %s '%s' 에 저장됨%s"),
				*SocketName.ToString(), *ResolvedBoneName.ToString(),
				*Socket->RelativeLocation.ToString(), *Socket->RelativeRotation.ToString(),
				OwnerLabel, *Owner->GetName(),
				Result.bCreatedSocket ? TEXT(" (새로 생성)") : (Result.bMovedBone ? TEXT(" (뼈 재배치)") : TEXT("")));
		}
		else
		{
			// "소켓 만들었다"가 아니라 "무엇이 저장됐다"가 핵심(불변식 I-E) — 메모리 값은 바뀌었지만 저장은
			// 실패했다고 정직하게 알린다. SavedPackages 는 비워 둔다.
			Result.bOk = false;
			Result.Message = FString::Printf(TEXT("%s '%s' 에 값은 반영됐지만 저장에 실패했다 — 직접 저장하라"),
				OwnerLabel, *Owner->GetName());
		}

		return Result;
	}

	float ComputeFloorOffsetToRest(const USkeletalMeshComponent* BodyComp, const TArray<UStaticMeshComponent*>& PartComps,
	                               const USkeletalMeshComponent* ArmsComp)
	{
		// Accumulate a world-space box over the body + every part (+ arms, if standing in). CalcBounds() computes
		// on-demand from the component's current transform, so this doesn't depend on the cached Bounds being
		// finalized right after AddComponent.
		FBox AssemblyBox(ForceInit);
		auto Accumulate = [&AssemblyBox](const UPrimitiveComponent* Comp)
		{
			if (Comp)
			{
				AssemblyBox += Comp->CalcBounds(Comp->GetComponentTransform()).GetBox();
			}
		};

		if (BodyComp && BodyComp->GetSkeletalMeshAsset())
		{
			Accumulate(BodyComp);
		}
		for (const UStaticMeshComponent* PC : PartComps)
		{
			if (PC && PC->GetStaticMesh())
			{
				Accumulate(PC);
			}
		}
		// 팔 보기가 켜져 있을 때만 호출자가 넘긴다(기본 null). 포함하지 않으면 무기 기준으로만 바닥을 맞춰서, 팔이
		// 바닥 아래로 파고들거나 붕 떠 보인다.
		if (ArmsComp && ArmsComp->GetSkeletalMeshAsset())
		{
			Accumulate(ArmsComp);
		}

		if (!AssemblyBox.IsValid)
		{
			return 0.0f;
		}

		// Floor mesh sits at Z = -(offset). We want it at the assembly's minimum Z (its underside), i.e.
		// Center.Z - Extent.Z, so offset = -Center.Z + Extent.Z. Matches SStaticMeshEditorViewport / SMaterialEditorViewport.
		const FVector Center = AssemblyBox.GetCenter();
		const FVector Extent = AssemblyBox.GetExtent();
		return static_cast<float>(-Center.Z + Extent.Z);
	}

	// --- 1인칭 구도 -----------------------------------------------------------------------------------------------

	bool ReadFirstPersonSetup(UWorld* World, FFPSRFirstPersonSetup& OutSetup, FString& OutIssue)
	{
		const UFPSRWeaponAssemblerSettings* Settings = GetDefault<UFPSRWeaponAssemblerSettings>();
		if (!Settings || Settings->PreviewCharacterClass.IsNull())
		{
			OutIssue = TEXT("프로젝트 설정 > FPSR > 무기 어셈블러 에 '프리뷰 캐릭터 클래스'가 비어 있어 1인칭 구도를 읽을 수 없습니다.");
			return false;
		}

		UClass* CharClass = Settings->PreviewCharacterClass.LoadSynchronous();
		if (!CharClass || !World)
		{
			OutIssue = TEXT("프리뷰 캐릭터 클래스를 불러오지 못했습니다.");
			return false;
		}

		// 🚨 CDO 가 아니라 **인스턴스**라야 컴포넌트 월드 트랜스폼이 의미를 갖는다. 엔진도 FBlueprintEditor 가
		// 프리뷰 씬에 BP 를 그대로 스폰한다(BlueprintEditor.cpp). 프리뷰 월드는 BeginPlay 를 걸지 않으므로
		// AFPSRCharacter::BeginPlay/PossessedBy 같은 게임플레이 초기화는 돌지 않는다 — 값만 읽고 즉시 파괴한다.
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags |= RF_Transient;
		AFPSRCharacter* Probe = World->SpawnActor<AFPSRCharacter>(CharClass, FTransform::Identity, SpawnParams);
		if (!Probe)
		{
			OutIssue = TEXT("프리뷰 캐릭터를 프리뷰 씬에 스폰하지 못했습니다.");
			return false;
		}

		const bool bOk = Probe->GetFirstPersonViewSetup(OutSetup.CameraRelativeToArms, OutSetup.View);
		Probe->Destroy();

		if (!bOk)
		{
			OutIssue = TEXT("프리뷰 캐릭터에 1인칭 카메라 또는 1인칭 팔 컴포넌트가 없어 구도를 읽지 못했습니다.");
		}
		return bOk;
	}

	float ComputeVerticalFOV(float HorizontalFOVDegrees, float Aspect)
	{
		const float HalfX = FMath::DegreesToRadians(FMath::Max(0.001f, HorizontalFOVDegrees) * 0.5f);
		const float HalfY = FMath::Atan(FMath::Tan(HalfX) / FMath::Max(UE_KINDA_SMALL_NUMBER, Aspect));
		return FMath::RadiansToDegrees(HalfY * 2.0f);
	}

	bool TickPreviewWorldOnce(UWorld* World, float DeltaSeconds)
	{
		if (!World)
		{
			return false;
		}

		// 월드별 "마지막으로 틱한 프레임". 씬은 보통 하나뿐이지만, 조립기 탭을 닫았다 열면 새 씬이 생기고 옛 씬이
		// 잠시 남을 수 있어 월드를 키로 둔다. 죽은 월드의 항목은 그때그때 걷어낸다(표가 커질 일이 없다).
		static TMap<TWeakObjectPtr<UWorld>, uint64> LastTickedFrame;
		for (auto It = LastTickedFrame.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid())
			{
				It.RemoveCurrent();
			}
		}

		if (const uint64* Found = LastTickedFrame.Find(World))
		{
			if (*Found == GFrameCounter)
			{
				return false;   // 이 프레임엔 이미 다른 탭이 돌렸다
			}
		}
		LastTickedFrame.Add(World, GFrameCounter);

		World->Tick(LEVELTICK_All, DeltaSeconds);
		return true;
	}

	float DeriveFOVForAspect(const FMinimalViewInfo& View, float TargetAspect)
	{
		const float SourceFOV = FMath::Max(0.001f, View.FOV);
		const float SourceAspect = View.AspectRatio;
		TargetAspect = FMath::Max(UE_KINDA_SMALL_NUMBER, TargetAspect);

		// 게임이 실제로 쓰는 축 제약. 카메라가 덮었으면(bOverrideAspectRatioAxisConstraint) 그 값이,
		// 아니면 ULocalPlayer 의 config 기본값이 진짜다 — 상수로 박지 않는다(BaseEngine.ini 를 프로젝트가 덮을 수 있다).
		const EAspectRatioAxisConstraint GameConstraint =
			View.AspectRatioAxisConstraint.Get(GetDefault<ULocalPlayer>()->AspectRatioAxisConstraint);

		// CameraStackTypes.cpp:287 과 같은 판정. MajorAxisFOV 는 "긴 축을 고정"이므로 가로가 긴 프리셋에선 가로 고정.
		const bool bGameMaintainsX =
			(GameConstraint == AspectRatio_MaintainXFOV) ||
			(GameConstraint == AspectRatio_MajorAxisFOV && TargetAspect >= 1.0f);

		if (bGameMaintainsX || SourceAspect <= UE_KINDA_SMALL_NUMBER)
		{
			// 게임이 가로를 고정하는 설정이면 종횡비가 바뀌어도 가로 FOV 는 그대로다 — 재계산하면 오히려 틀린다.
			return SourceFOV;
		}

		// 세로 고정(MaintainYFOV): 원본 종횡비에서 세로 FOV 를 구해 그대로 유지하고, 목표 종횡비의 가로를 되푼다.
		// CameraStackTypes.cpp:329-331 의 역연산이다.
		const float HalfX = FMath::DegreesToRadians(SourceFOV * 0.5f);
		const float HalfY = FMath::Atan(FMath::Tan(HalfX) / SourceAspect);
		const float NewHalfX = FMath::Atan(FMath::Tan(HalfY) * TargetAspect);
		return FMath::RadiansToDegrees(NewHalfX * 2.0f);
	}
}
