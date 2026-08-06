// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Hero/FPSRCharacter.h"   // TSoftClassPtr<AFPSRCharacter> — 1인칭 구도를 읽어올 프리뷰 캐릭터
#include "FPSRWeaponAssemblerSettings.generated.h"

/**
 * 1인칭 뷰 탭의 화면비 프리셋 한 줄. **표시는 해상도 이름으로, 동작은 종횡비로** 묶는다 — 1920×1080 과 2560×1440 은
 * 둘 다 16:9 라 구도가 완전히 같다(가장자리 구도를 바꾸는 건 해상도가 아니라 종횡비다). 해상도를 따로 안 받는 이유도
 * 그것이다: 픽셀 크기는 구도에 영향을 주지 않는다.
 */
USTRUCT()
struct FFPSRAssemblerAspectPreset
{
	GENERATED_BODY()

	/** 콤보 박스에 뜨는 이름. 같은 종횡비를 쓰는 해상도를 한 줄에 몰아 적어 두면 디자이너가 헷갈리지 않는다. */
	UPROPERTY(EditAnywhere, Config)
	FString Label;

	/** 가로/세로. 16:9 = 1.777778. 이 값 하나가 구도를 결정한다. */
	UPROPERTY(EditAnywhere, Config, meta = (ClampMin = "0.1", UIMin = "0.5", UIMax = "4.0"))
	float AspectRatio = 1.777778f;
};

/**
 * Weapon Part Assembler tool configuration (editor-only). Config = Editor + DefaultConfig so the value lives in
 * Config/DefaultEditor.ini — checked in, shared by designers, and changeable in Project Settings > FPSR with NO C++
 * rebuild. The default mirrors the current content layout; a later content reorg (e.g. U22 Synty) can repoint it
 * without touching C++. Editor module only — nothing here is read at runtime.
 */
UCLASS(Config = Editor, DefaultConfig, meta = (DisplayName = "FPSR Weapon Assembler"))
class UFPSRWeaponAssemblerSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Groups this under the "FPSR" section in Project Settings, alongside the other FPSR editor tools. */
	virtual FName GetCategoryName() const override { return FName(TEXT("FPSR")); }

	/** Name of the sibling folder (next to a weapon's own parts folder) that holds shared attachments — sights, grips,
	 *  muzzle devices, lasers, and a Scopes/ subfolder. The assembler's catalog scan looks in "<weaponFolder>/../<this>"
	 *  recursively. Default "Attachments" matches the current layout; change it here (no rebuild) if content is reorganised. */
	UPROPERTY(EditAnywhere, Config, Category = "Catalog")
	FString AttachmentsFolderName = TEXT("Attachments");

	/** First-person arms shown in the assembler's "팔 보기" mode, so a weapon's grip can be judged **in the hand**
	 *  instead of floating at the origin. Empty = the toggle stays off and the tool behaves exactly as before.
	 *  Soft ref + Config so swapping the arms mesh (this project has already changed it twice) needs no rebuild.
	 *  The arms mesh is also where the "손 위치 저장" bake starts from — but the socket it writes may land on THIS
	 *  mesh or on its Skeleton asset, whichever actually owns the weapon attach socket
	 *  (FPSRWeaponAssemblerHelpers::FindWeaponSocket resolves it; this pack's SOCKET_Weapon lives on the Skeleton). */
	UPROPERTY(EditAnywhere, Config, Category = "First Person Preview", meta = (AllowedClasses = "/Script/Engine.SkeletalMesh"))
	TSoftObjectPtr<USkeletalMesh> PreviewArmsMesh;

	/** Pose/animation played on the preview arms. Judging a grip against the REFERENCE pose is misleading — the arms
	 *  stand spread-eagled and the weapon lands nowhere near where it does in play. Empty = reference pose.
	 *
	 *  무기는 이제 팔의 무기 부착 소켓(또는 지정한 뼈)에 실제로 **attach** 되어 그 뼈를 계속 따라간다(런타임
	 *  AttachWeaponMeshes 와 동일한 부착 규칙 — AFPSRCharacter::ResolveWeaponAttachSocket 참조). 그래서 여기 넣은
	 *  애니가 정지 포즈일 필요가 없다 — 재장전처럼 움직이는 애니를 넣어도 총이 손을 그대로 따라간다. 툴에서
	 *  재생/스크럽하며 애니가 진행되는 동안 그립이 미끄러지지 않는지 눈으로 판정하는 것이 오히려 이 필드의 목적. */
	UPROPERTY(EditAnywhere, Config, Category = "First Person Preview", meta = (AllowedClasses = "/Script/Engine.AnimationAsset"))
	TSoftObjectPtr<UAnimationAsset> PreviewArmsPose;

	/** "1인칭 뷰" 모드가 구도를 읽어올 플레이어 캐릭터 BP. 자유 시점으로는 "플레이어 눈에 총이 어떻게 걸리는가"를
	 *  판정할 수 없다는 지적에서 나온 기능이다(사용자 요청 2026-08-05·08-06).
	 *
	 *  툴은 이 클래스를 프리뷰 씬에 **잠깐 스폰해서** AFPSRCharacter::GetFirstPersonViewSetup 으로 카메라↔팔 상대
	 *  트랜스폼과 FOV 를 읽고 **곧바로 파괴한다**. 상수로 박지 않는 이유는 부착 위상·수치가 BP 쪽에서 바뀔 수 있기
	 *  때문이고, 근사(역행렬 계산)를 쓰지 않는 이유는 그게 "팔이 카메라에 직접 붙어 있다"는 가정을 안고 있기
	 *  때문이다 — 에디터 툴은 정확성이 우선이다(사용자 지시).
	 *
	 *  비어 있으면 "1인칭 뷰" 토글이 켜지지 않고 툴은 자유 시점 그대로 동작한다. */
	UPROPERTY(EditAnywhere, Config, Category = "First Person Preview")
	TSoftClassPtr<AFPSRCharacter> PreviewCharacterClass;

	/** 1인칭 뷰 탭의 화면비 프리셋 목록(콤보 박스 항목). 여기 값을 고치면 재빌드 없이 반영된다.
	 *
	 *  기본값은 아래 생성자에서 채운다 — 코드에 박아 두는 게 아니라 "처음 한 번의 시드"이고, ini 에 항목이 있으면
	 *  그쪽이 이긴다. 새 종횡비(예: 폴더블·세로 모니터)는 여기에 한 줄 추가하면 끝이고 C++ 은 손대지 않는다. */
	UPROPERTY(EditAnywhere, Config, Category = "First Person Preview")
	TArray<FFPSRAssemblerAspectPreset> AspectPresets;

	UFPSRWeaponAssemblerSettings();
};
