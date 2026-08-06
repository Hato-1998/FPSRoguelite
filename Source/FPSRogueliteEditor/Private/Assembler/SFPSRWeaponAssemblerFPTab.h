// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Types/SlateEnums.h"   // ESelectInfo — 콤보 선택 콜백 시그니처

class FAdvancedPreviewScene;
class SBox;
class SFPSRWeaponAssemblerFPViewport;
class SFPSRWeaponAssemblerTab;

/**
 * FPSR 무기 어셈블러 — **1인칭 뷰** 탭 (Tools > FPSR > "무기 1인칭 뷰…").
 *
 * 조립기 탭과 **같은 프리뷰 씬**을 그리되, 카메라를 플레이어 눈 위치에 잠그고 화면비를 게임과 같게 고정한다.
 * 조립기 뷰포트에서 기즈모로 그립을 만지면 이 탭에 즉시 반영된다 — 한쪽에서 만지고 다른 쪽에서 판정하는 구조다.
 *
 * ## 왜 별도 탭인가
 * 도킹된 조립기 뷰포트는 화면비가 게임과 달라 FOV 가 같아도 세로 구도가 다르다(실측: 세로 82.3° vs 게임 58.7°,
 * 1.40배). 창을 따로 띄워야 원하는 화면비를 온전히 줄 수 있다 — 사용자 요청.
 *
 * ## 🚨 수명주기 — 여기가 이 탭에서 제일 위험한 곳
 * 프리뷰 씬의 주인은 조립기 탭이고, FEditorViewportClient 는 씬을 **raw 참조**로 받는다. 씬이 먼저 죽으면 매달린다.
 *  - 이 탭이 씬을 `TSharedPtr` 로 **공동 소유**한다 → 조립기 탭이 닫혀도 매달리지 않는다.
 *  - 조립기 탭 자체는 `TWeakPtr` 로만 본다 → 닫힌 것을 감지할 수 있다.
 *  - 🚩 조립기 탭은 노매드 탭이라 닫았다 다시 열면 **새 씬**이 생긴다. 그때 우리가 쥔 씬은 아무도 안 쓰는 낡은
 *    씬이라, 그대로 그리면 **빈 화면을 정상인 것처럼 보여주게 된다.** 그래서 Tick 에서 "지금 조립기 탭의 씬"과
 *    비교해 다르면 뷰포트를 **내려 버리고**(렌더 중지) 안내 + [다시 연결] 버튼을 띄운다.
 */
class SFPSRWeaponAssemblerFPTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFPSRWeaponAssemblerFPTab) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** 조립기 탭이 살아 있는지 / 그 씬이 우리가 쥔 씬과 같은지 감시한다(위 수명주기 주석). */
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	/** 화면비 콤보 한 줄. 설정(UFPSRWeaponAssemblerSettings::AspectPresets)에서 읽어 채운다 — 목록은 데이터다. */
	struct FAspectOption
	{
		FText Label;
		float Aspect = 16.0f / 9.0f;
	};

	/** 지금 살아 있는 조립기 탭의 씬에 붙어 뷰포트를 새로 세운다. 최초 구성과 [다시 연결] 둘 다 이 경로를 쓴다 —
	 *  "처음 연결"과 "다시 연결"은 같은 일이므로 절차가 갈리면 안 된다. */
	void RebuildViewport();

	/** 뷰포트를 내리고(= 렌더 중지) 사유 + [다시 연결] 버튼을 띄운다. 낡은 씬을 계속 그리느니 멈추는 쪽이 옳다. */
	void ShowDisconnected(const FText& Reason);

	/** 우리가 쥔 씬이 지금 조립기 탭의 씬과 같은가. 조립기 탭이 없으면 false. */
	bool IsSceneCurrent() const;

	// --- 화면비 콤보 ----------------------------------------------------------------------------------------------
	TSharedRef<SWidget> MakeAspectOptionWidget(TSharedPtr<FAspectOption> Option) const;
	void OnAspectSelectionChanged(TSharedPtr<FAspectOption> Option, ESelectInfo::Type SelectInfo);
	FText GetSelectedAspectLabel() const;

	/** "구도 다시 읽기" — 캐릭터 BP 를 고친 뒤 에디터를 껐다 켜지 않고 반영하기 위한 것. */
	FReply OnRefreshCompositionClicked();

	/** 상단 숫자 계기. 🚩 v1 은 읽어온 값을 어디에도 안 보여줘서 맞는지 확인할 방법이 툴 밖에 있었다 —
	 *  "판정은 숫자로"를 툴 안에서 할 수 있게 하는 것이 이 표시의 목적이다. */
	FText GetReadoutText() const;
	FText GetStatusText() const;

	/** 뷰포트(또는 안내 문구)가 들어가는 자리. 내용 교체로 렌더를 멈춘다. */
	TSharedPtr<SBox> ViewportContainer;

	/** 살아 있는 동안의 3D 뷰포트. 끊긴 상태에선 null. */
	TSharedPtr<SFPSRWeaponAssemblerFPViewport> FPViewport;

	/** 🚨 공동 소유 — 이것 때문에 조립기 탭이 먼저 죽어도 뷰포트 클라이언트가 매달리지 않는다. */
	TSharedPtr<FAdvancedPreviewScene> HeldScene;

	/** 씬·팔의 주인. 노매드 탭이라 언제든 닫힐 수 있어 약참조. */
	TWeakPtr<SFPSRWeaponAssemblerTab> AssemblerTab;

	TArray<TSharedPtr<FAspectOption>> AspectOptions;
	TSharedPtr<FAspectOption> SelectedAspect;

	/** 지금 뷰포트가 올라가 있는가(= 연결됨). Tick 이 상태 전이일 때만 위젯을 갈아 끼우게 하는 판정용 —
	 *  매 프레임 SetContent 를 부르면 위젯 트리를 계속 헐고 짓는다. */
	bool bConnected = false;
};
