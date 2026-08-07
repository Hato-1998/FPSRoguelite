// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UAnimSequence;
class UFPSRGunMotionAuthoringData;
class SVerticalBox;
class STextBlock;
struct FAssetData;
struct FFPSRGunMotionKey;

/**
 * 총 모션 저작 툴 탭(GunMotionTool_Spec.md §4) — 클립 선택 → [총 고정화] → 카메라 공간 키 저작 → [클립에 굽기] →
 * [PIE에서 재생] 워크플로 하나로 묶는다. 실제 수학은 전부 FPSRGunMotionBaker(순수 로직)에 있고, 이 위젯은 그
 * 호출과 저작 키(AssetUserData) 편집 UI 만 담당한다.
 *
 * 클립은 TWeakObjectPtr 로만 들고 있는다(§5: 탭 위젯이 클립 강참조를 들지 말 것) — 탭을 열어 둔 채 클립을
 * 삭제/리로드해도 위젯이 그 참조 하나 때문에 GC 를 막지 않는다.
 */
class SFPSRGunMotionTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFPSRGunMotionTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// --- 클립 선택 ---
	FString GetSequenceObjectPath() const;
	void OnSequenceChanged(const FAssetData& AssetData);
	UAnimSequence* GetSequence() const;
	UFPSRGunMotionAuthoringData* GetOrCreateAuthoringData(bool bCreateIfMissing);

	// --- 상태 줄(§4-2) ---
	FText GetStatusText() const;
	bool IsClipAdditive() const;

	/** §5: 클립 경로가 Anims_LPAMG 밖이거나 _GunLocked/_GunMotion 접미가 없으면 계속/취소 경고. true = 계속 진행
	 *  (클립이 없으면 무조건 true — 버튼 자체 핸들러가 별도로 "클립 없음"을 처리한다). */
	bool ConfirmIfOutsideConvention() const;

	void SetStatus(const FText& InText);

	// --- 액션 버튼(§4-3·5·6) ---
	FReply OnSanitizeClicked();
	FReply OnBakeClicked();
	FReply OnPlayInPIEClicked();

	// --- 키 목록(§4-4) ---
	void RebuildKeyRows();
	TSharedRef<SWidget> BuildKeyRow(int32 KeyIndex);
	FReply OnAddKeyClicked();
	FReply OnRemoveKeyClicked(int32 KeyIndex);
	FReply OnApplyDefaultTemplateClicked();

	/** 키 하나의 필드를 트랜잭션 안에서 고친다 — 6개 성분(Right/Up/Fwd/Pitch/Yaw/Roll)+Time 세터가 공유하는
	 *  단일 경로(FScopedTransaction + AssetUserData::Modify + Seq::MarkPackageDirty). */
	void MutateKey(int32 KeyIndex, const FText& TransactionText, TFunctionRef<void(FFPSRGunMotionKey&)> Mutator);

	float GetKeyTime(int32 KeyIndex) const;
	void SetKeyTime(int32 KeyIndex, float NewValue);
	float GetKeyRight(int32 KeyIndex) const;
	void SetKeyRight(int32 KeyIndex, float NewValue);
	float GetKeyUp(int32 KeyIndex) const;
	void SetKeyUp(int32 KeyIndex, float NewValue);
	float GetKeyFwd(int32 KeyIndex) const;
	void SetKeyFwd(int32 KeyIndex, float NewValue);
	float GetKeyPitch(int32 KeyIndex) const;
	void SetKeyPitch(int32 KeyIndex, float NewValue);
	float GetKeyYaw(int32 KeyIndex) const;
	void SetKeyYaw(int32 KeyIndex, float NewValue);
	float GetKeyRoll(int32 KeyIndex) const;
	void SetKeyRoll(int32 KeyIndex, float NewValue);

	// --- "풀 오프셋" 입력값(§3-3 기본 템플릿이 읽는 값) ---
	TOptional<float> GetFullOffsetRight() const { return FullOffsetRight; }
	void SetFullOffsetRight(float V) { FullOffsetRight = V; }
	TOptional<float> GetFullOffsetUp() const { return FullOffsetUp; }
	void SetFullOffsetUp(float V) { FullOffsetUp = V; }
	TOptional<float> GetFullOffsetFwd() const { return FullOffsetFwd; }
	void SetFullOffsetFwd(float V) { FullOffsetFwd = V; }
	TOptional<float> GetFullOffsetPitch() const { return FullOffsetPitch; }
	void SetFullOffsetPitch(float V) { FullOffsetPitch = V; }
	TOptional<float> GetFullOffsetYaw() const { return FullOffsetYaw; }
	void SetFullOffsetYaw(float V) { FullOffsetYaw = V; }
	TOptional<float> GetFullOffsetRoll() const { return FullOffsetRoll; }
	void SetFullOffsetRoll(float V) { FullOffsetRoll = V; }

	TWeakObjectPtr<UAnimSequence> SelectedSequence;

	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<STextBlock> ActionStatusText;
	TSharedPtr<SVerticalBox> KeyListContainer;

	// 기본값(스펙 §3-3): Right+3 / Up+6cm, Pitch12 / Roll10 도.
	float FullOffsetRight = 3.0f;
	float FullOffsetUp = 6.0f;
	float FullOffsetFwd = 0.0f;
	float FullOffsetPitch = 12.0f;
	float FullOffsetYaw = 0.0f;
	float FullOffsetRoll = 10.0f;
};
