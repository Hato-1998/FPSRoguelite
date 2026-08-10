// Copyright Epic Games, Inc. All Rights Reserved.

// §1 "로직과 분리" — this file is ONLY ImGui/ImGuizmo drawing + input->intent translation. Every actual state change
// (rebake, key edit, save, attach) goes back through FFPSRGunMotionStudio's own API; nothing here touches
// UFPSRGunMotionStudioData/UAnimSequence/UAnimMontage directly.

#include "GunMotionStudio/FPSRGunMotionStudio.h"

#include "Hero/FPSRCharacter.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Anim/FPSRGunMotionStudioData.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Controller.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/KismetMathLibrary.h"
#include "Misc/Paths.h"       // 한글 폰트 경로(EngineContentDir)
#include "Misc/ScopeExit.h"   // ON_SCOPE_EXIT — PushFont/PopFont 짝 보장
#include "Engine/Engine.h"    // GEngine->GetWorldContextFromWorld — PIE 인스턴스 id 명시 해석

THIRD_PARTY_INCLUDES_START
#include "imgui.h"
THIRD_PARTY_INCLUDES_END

#include "GunMotionStudio/ImGuizmo/ImGuizmo.h"

namespace FPSRGunMotionStudioUI
{
	// --- UE <-> ImGuizmo float[16] matrix conversion (row-major, row-vector convention throughout — self-consistent
	//     with UE's own FMatrix layout; see FPSRGunMotionStudioUI.cpp header comment in the .h-less design note
	//     above). Every matrix (view/proj/object/delta) uses the SAME convention, which is what Manipulate's internal
	//     math actually needs — never mix a transposed one in. UNVERIFIED against a live PIE session (no interactive
	//     UE editor available while writing this) — first thing to confirm in the §8 smoke test if the gizmo axes
	//     read backwards/mirrored. ---
	static void FMatrixToFloats(const FMatrix& M, float Out[16])
	{
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Col = 0; Col < 4; ++Col)
			{
				Out[Row * 4 + Col] = static_cast<float>(M.M[Row][Col]);
			}
		}
	}

	static FMatrix FloatsToFMatrix(const float In[16])
	{
		FMatrix M;
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Col = 0; Col < 4; ++Col)
			{
				M.M[Row][Col] = In[Row * 4 + Col];
			}
		}
		return M;
	}

	/** One candidate the click-picker/gizmo-target resolver considers (§5 "화면 클릭 → ... 스크린 투영 근접 판정"). */
	struct FPickCandidate
	{
		EFPSRGunMotionStudioSelection Selection = EFPSRGunMotionStudioSelection::None;
		FName PartSocket = NAME_None;
		FVector WorldLocation = FVector::ZeroVector;
	};

	static void GatherPickCandidates(AFPSRCharacter& Character, TArray<FPickCandidate>& OutCandidates)
	{
		if (UMeshComponent* Weapon = Character.GetActiveWeaponMeshComponentForStudio())
		{
			OutCandidates.Add({ EFPSRGunMotionStudioSelection::Gun, NAME_None, Weapon->GetComponentLocation() });
		}
		if (USkeletalMeshComponent* Arms = Character.GetFirstPersonArmsMeshComponent())
		{
			if (Arms->DoesSocketExist(FName(TEXT("ik_hand_l"))))
			{
				OutCandidates.Add({ EFPSRGunMotionStudioSelection::LeftHand, NAME_None, Arms->GetBoneTransform(FName(TEXT("ik_hand_l")), RTS_World).GetLocation() });
			}
			if (Arms->DoesSocketExist(FName(TEXT("ik_hand_r"))))
			{
				OutCandidates.Add({ EFPSRGunMotionStudioSelection::RightHand, NAME_None, Arms->GetBoneTransform(FName(TEXT("ik_hand_r")), RTS_World).GetLocation() });
			}
		}
		for (const TObjectPtr<UStaticMeshComponent>& Part : Character.GetWeaponPartComponentsForStudio())
		{
			if (Part)
			{
				OutCandidates.Add({ EFPSRGunMotionStudioSelection::Part, Part->GetAttachSocketName(), Part->GetComponentLocation() });
			}
		}
	}

	/** §5 "스크린 투영 근접 판정" — projects every candidate, picks whichever lands closest to MousePos within
	 *  PickRadiusPixels. Returns false when nothing is within range (leaves selection untouched). */
	static bool PickAtScreenPosition(APlayerController& PC, AFPSRCharacter& Character, const FVector2D& MousePos,
		float PickRadiusPixels, FPickCandidate& OutBest)
	{
		TArray<FPickCandidate> Candidates;
		GatherPickCandidates(Character, Candidates);

		float BestDistSq = FMath::Square(PickRadiusPixels);
		bool bFound = false;
		for (const FPickCandidate& Candidate : Candidates)
		{
			FVector2D ScreenPos;
			if (!PC.ProjectWorldLocationToScreen(Candidate.WorldLocation, ScreenPos, true))
			{
				continue;
			}
			const float DistSq = FVector2D::DistSquared(ScreenPos, MousePos);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				OutBest = Candidate;
				bFound = true;
			}
		}
		return bFound;
	}

	/** §5 whole-gun/hand/part fixed key-diamond markers for every OTHER track, drawn faint, plus the SELECTED track's
	 *  keys drawn bright + draggable, plus attach-span bars (§5 "타임라인"). Custom ImGui DrawList — no external
	 *  sequencer widget (§5 "단일 파일 유지"). */
	static void DrawTimeline(FFPSRGunMotionStudio& Session)
	{
		UFPSRGunMotionStudioData* Data = Session.GetWipStudioData();
		if (!Data)
		{
			return;
		}
		const float Length = FMath::Max(Session.GetClipLengthSeconds(), 0.01f);

		ImGui::Begin("Timeline (GunMotionStudio)");

		const bool bPlaying = Session.IsPlaying();
		if (ImGui::Button(bPlaying ? "Pause" : "Play"))
		{
			Session.SetPlaying(!bPlaying);
		}
		ImGui::SameLine();
		float Playhead = Session.GetPlayheadSeconds();
		ImGui::SetNextItemWidth(120.0f);
		if (ImGui::SliderFloat("##playhead_numeric", &Playhead, 0.0f, Length, "%.2fs"))
		{
			Session.SetPlayheadSeconds(Playhead);
		}

		const ImVec2 Origin = ImGui::GetCursorScreenPos();
		const float TimelineWidth = ImGui::GetContentRegionAvail().x;
		const float TimelineHeight = 60.0f;
		ImDrawList* DrawList = ImGui::GetWindowDrawList();

		const ImU32 RulerColor = IM_COL32(140, 140, 140, 255);
		const ImU32 PlayheadColor = IM_COL32(255, 220, 60, 255);
		const ImU32 KeyColorSelected = IM_COL32(255, 120, 40, 255);
		const ImU32 KeyColorOther = IM_COL32(120, 120, 120, 150);
		const ImU32 SpanColor = IM_COL32(60, 160, 220, 90);

		DrawList->AddRectFilled(Origin, ImVec2(Origin.x + TimelineWidth, Origin.y + TimelineHeight), IM_COL32(30, 30, 30, 255));

		auto TimeToX = [&](float Time) { return Origin.x + (Time / Length) * TimelineWidth; };
		auto XToTime = [&](float X) { return FMath::Clamp((X - Origin.x) / TimelineWidth, 0.0f, 1.0f) * Length; };

		// Attach-span bars (§5) — only meaningful when a hand is selected, drawn on the bottom strip.
		auto DrawSpans = [&](const TArray<FFPSRStudioAttachSpan>& Spans)
		{
			for (const FFPSRStudioAttachSpan& Span : Spans)
			{
				const float X0 = TimeToX(Span.Start);
				const float X1 = TimeToX(Span.End);
				DrawList->AddRectFilled(ImVec2(X0, Origin.y + TimelineHeight - 14.0f), ImVec2(X1, Origin.y + TimelineHeight - 2.0f), SpanColor);
			}
		};
		if (Session.GetSelection() == EFPSRGunMotionStudioSelection::LeftHand)
		{
			DrawSpans(Data->LeftHandAttachSpans);
		}
		else if (Session.GetSelection() == EFPSRGunMotionStudioSelection::RightHand)
		{
			DrawSpans(Data->RightHandAttachSpans);
		}

		// Ruler ticks (every 0.5s).
		for (float T = 0.0f; T <= Length + KINDA_SMALL_NUMBER; T += 0.5f)
		{
			const float X = TimeToX(T);
			DrawList->AddLine(ImVec2(X, Origin.y), ImVec2(X, Origin.y + 8.0f), RulerColor);
		}

		// Faint keys for every OTHER track (§5 "전 대상 키는 흐리게 병기").
		auto DrawFaintKeys = [&](const FFPSRStudioTrack& Track)
		{
			for (const FFPSRStudioKey& Key : Track.Keys)
			{
				const float X = TimeToX(Key.Time);
				DrawList->AddCircleFilled(ImVec2(X, Origin.y + TimelineHeight * 0.5f), 3.0f, KeyColorOther);
			}
		};
		if (Session.GetSelection() != EFPSRGunMotionStudioSelection::Gun) DrawFaintKeys(Data->GunKeys);
		if (Session.GetSelection() != EFPSRGunMotionStudioSelection::LeftHand) DrawFaintKeys(Data->LeftHandKeys);
		if (Session.GetSelection() != EFPSRGunMotionStudioSelection::RightHand) DrawFaintKeys(Data->RightHandKeys);

		// Selected track's keys — bright diamonds, draggable (frame-snapped), right-click deletes.
		FFPSRStudioTrack* SelectedTrack = nullptr;
		switch (Session.GetSelection())
		{
		case EFPSRGunMotionStudioSelection::Gun:       SelectedTrack = &Data->GunKeys; break;
		case EFPSRGunMotionStudioSelection::LeftHand:  SelectedTrack = &Data->LeftHandKeys; break;
		case EFPSRGunMotionStudioSelection::RightHand: SelectedTrack = &Data->RightHandKeys; break;
		case EFPSRGunMotionStudioSelection::Part:
			if (!Session.GetSelectedPartSocket().IsNone())
			{
				SelectedTrack = Data->PartTracks.Find(Session.GetSelectedPartSocket());
			}
			break;
		default: break;
		}

		bool bRebakeNeeded = false;
		if (SelectedTrack)
		{
			for (int32 Index = 0; Index < SelectedTrack->Keys.Num(); ++Index)
			{
				FFPSRStudioKey& Key = SelectedTrack->Keys[Index];
				const float X = TimeToX(Key.Time);
				const ImVec2 Center(X, Origin.y + TimelineHeight * 0.5f);
				const float R = 5.0f;
				DrawList->AddQuadFilled(ImVec2(Center.x, Center.y - R), ImVec2(Center.x + R, Center.y), ImVec2(Center.x, Center.y + R), ImVec2(Center.x - R, Center.y), KeyColorSelected);

				ImGui::SetCursorScreenPos(ImVec2(Center.x - R, Center.y - R));
				ImGui::PushID(Index);
				ImGui::InvisibleButton("key", ImVec2(R * 2.0f, R * 2.0f));
				if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
				{
					const float NewTime = XToTime(ImGui::GetMousePos().x);
					// Frame-snap to the clip's 30fps grid (§5 "키 드래그(프레임 스냅)").
					Key.Time = FMath::RoundToFloat(NewTime * 30.0f) / 30.0f;
					bRebakeNeeded = true;
				}
				if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
				{
					Session.SetPlayheadSeconds(Key.Time);
				}
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
				{
					SelectedTrack->Keys.RemoveAt(Index);
					bRebakeNeeded = true;
					ImGui::PopID();
					break;
				}
				ImGui::PopID();
			}
		}

		// Playhead + click-to-scrub on the ruler strip.
		ImGui::SetCursorScreenPos(Origin);
		ImGui::InvisibleButton("timeline_scrub", ImVec2(TimelineWidth, TimelineHeight));
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)))
		{
			Session.SetPlayheadSeconds(XToTime(ImGui::GetMousePos().x));
		}

		const float PlayheadX = TimeToX(Session.GetPlayheadSeconds());
		DrawList->AddLine(ImVec2(PlayheadX, Origin.y), ImVec2(PlayheadX, Origin.y + TimelineHeight), PlayheadColor, 2.0f);

		ImGui::Dummy(ImVec2(TimelineWidth, TimelineHeight + 4.0f));

		if (bRebakeNeeded)
		{
			SelectedTrack->Keys.Sort([](const FFPSRStudioKey& A, const FFPSRStudioKey& B) { return A.Time < B.Time; });
			Session.RebakeAndRestartMontage();
		}

		ImGui::End();
	}

	static void DrawAttachControls(FFPSRGunMotionStudio& Session)
	{
		const EFPSRGunMotionStudioSelection Sel = Session.GetSelection();
		if (Sel != EFPSRGunMotionStudioSelection::LeftHand && Sel != EFPSRGunMotionStudioSelection::RightHand)
		{
			ImGui::TextDisabled("IK 붙이기: 손을 선택하세요.");
			return;
		}

		static char SocketBuffer[64] = "";
		ImGui::SetNextItemWidth(160.0f);
		ImGui::InputText("소켓명", SocketBuffer, IM_ARRAYSIZE(SocketBuffer));

		if (Session.IsSelectedHandAttached())
		{
			if (ImGui::Button("떼기"))
			{
				Session.DetachSelectedHand();
			}
		}
		else
		{
			if (ImGui::Button("IK 붙이기"))
			{
				FString Notify;
				Session.AttachSelectedHand(FName(ANSI_TO_TCHAR(SocketBuffer)), Notify);
				ImGui::OpenPopup("AttachNotify");
				static FString GLastNotify;
				GLastNotify = Notify;
			}
		}
	}

	static void DrawStatePoseMode(FFPSRGunMotionStudio& Session)
	{
		ImGui::Begin("Gun Motion Studio — State Pose");

		int32 TargetIndex = static_cast<int32>(Session.GetStatePoseTarget());
		const char* Targets[] = { "슬라이드", "공중", "홀스터" };
		if (ImGui::Combo("상태", &TargetIndex, Targets, IM_ARRAYSIZE(Targets)))
		{
			Session.SetStatePoseTarget(static_cast<EFPSRGunMotionStatePoseTarget>(TargetIndex));
		}

		Session.SetSelection(EFPSRGunMotionStudioSelection::Gun);
		Session.bWholeGunModeForced = true;

		if (ImGui::Button("DA에 저장"))
		{
			Session.SaveStatePoseToDataAsset();
		}

		ImGui::End();
	}

	static void DrawSaveDialog(FFPSRGunMotionStudio& Session, bool& bOpen)
	{
		if (!bOpen)
		{
			return;
		}
		ImGui::OpenPopup("저장");
		if (ImGui::BeginPopupModal("저장", &bOpen, ImGuiWindowFlags_AlwaysAutoResize))
		{
			static bool bAssignMontage = false;
			static int32 AssignTargetIndex = 0;
			ImGui::Checkbox("무기 DA 몽타주 필드에 배정", &bAssignMontage);
			if (bAssignMontage)
			{
				const char* Targets[] = { "ArmsReloadMontage", "ArmsEquipMontage" };
				ImGui::Combo("배정 대상", &AssignTargetIndex, Targets, IM_ARRAYSIZE(Targets));
			}
			ImGui::TextWrapped("확인을 누르면 엔진 기본 에셋 이름/경로 다이얼로그가 뜹니다 (이름 충돌은 그 다이얼로그가 막습니다).");
			if (ImGui::Button("확인"))
			{
				TArray<FString> Report;
				Session.SaveClip(bAssignMontage, static_cast<uint8>(AssignTargetIndex), Report);
				for (const FString& Line : Report)
				{
					UE_LOG(LogTemp, Log, TEXT("[GunMotionStudio][Save] %s"), *Line);
				}
				bOpen = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("취소"))
			{
				bOpen = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	/** §5 기즈모: 대상 = 선택된 트랙. 뷰/프로젝션 = PIE 카메라(GetPlayerViewPoint + FOV). 커밋은 매 프레임의
	 *  deltaMatrix를 그대로 CommitGizmoDelta로 넘긴다(§5 "증분 커밋"의 가장 단순한 형태 — 드래그 종료까지 모아
	 *  한번에 커밋하는 대신 프레임마다 작은 증분을 즉시 반영해, 재베이크 비용이 낮은 WIP 클립 규모에서는 결과가
	 *  동일하면서 라이브 피드백이 더 즉각적이다. 결합법칙상 프레임별 증분의 합 = 드래그 시작~종료 총 델타). */
	static void DrawGizmo(FFPSRGunMotionStudio& Session, APlayerController& PC)
	{
		if (Session.GetSelection() == EFPSRGunMotionStudioSelection::None)
		{
			return;
		}

		FTransform TargetWorld;
		if (!Session.GetSelectionWorldTransform(TargetWorld))
		{
			return;
		}

		FVector CamLoc; FRotator CamRot;
		PC.GetPlayerViewPoint(CamLoc, CamRot);
		const float FovDeg = PC.PlayerCameraManager ? PC.PlayerCameraManager->GetFOVAngle() : 90.0f;

		const ImGuiIO& IO = ImGui::GetIO();
		const float AspectRatio = (IO.DisplaySize.y > 0.0f) ? (IO.DisplaySize.x / IO.DisplaySize.y) : 1.777f;
		const float NearClip = 10.0f;

		const FMatrix ViewMatrix = FTransform(CamRot, CamLoc).ToMatrixNoScale().Inverse();
		const FMatrix ProjMatrix = FReversedZPerspectiveMatrix(FMath::DegreesToRadians(FovDeg) * 0.5f, AspectRatio, 1.0f, NearClip);

		float ViewFloats[16]; FMatrixToFloats(ViewMatrix, ViewFloats);
		float ProjFloats[16]; FMatrixToFloats(ProjMatrix, ProjFloats);
		float ObjectFloats[16]; FMatrixToFloats(TargetWorld.ToMatrixWithScale(), ObjectFloats);
		float DeltaFloats[16];

		ImGuizmo::BeginFrame();
		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
		ImGuizmo::SetRect(0.0f, 0.0f, IO.DisplaySize.x, IO.DisplaySize.y);

		static ImGuizmo::OPERATION CurrentOp = ImGuizmo::TRANSLATE;
		if (ImGui::IsKeyPressed(ImGuiKey_R)) CurrentOp = ImGuizmo::ROTATE;
		if (ImGui::IsKeyPressed(ImGuiKey_G)) CurrentOp = ImGuizmo::TRANSLATE;

		const bool bManipulating = ImGuizmo::Manipulate(ViewFloats, ProjFloats, CurrentOp, ImGuizmo::WORLD, ObjectFloats, DeltaFloats);
		if (bManipulating && ImGuizmo::IsUsing())
		{
			const FMatrix DeltaMat = FloatsToFMatrix(DeltaFloats);
			const FVector DeltaTranslation = DeltaMat.GetOrigin();
			const FQuat DeltaRotation = DeltaMat.ToQuat().GetNormalized();
			if (!DeltaTranslation.IsNearlyZero() || !DeltaRotation.Equals(FQuat::Identity, 1e-5f))
			{
				Session.CommitGizmoDelta(DeltaTranslation, DeltaRotation);
			}
		}
	}

	void Draw(FFPSRGunMotionStudio& Session)
	{
		AFPSRCharacter* Character = Session.GetCharacter();
		APlayerController* PC = Cast<APlayerController>(Character ? Character->GetController() : nullptr);
		if (!Character || !PC)
		{
			return;
		}

		// 🚨 PIE 인스턴스 id 를 캐릭터 월드에서 **명시** 해석해야 한다 — 이 함수는 FTSTicker(월드 밖)에서 불리므로
		// 기본 인자 FScopedContext 는 GetPlayInEditorID()=INDEX_NONE 을 보고 **에디터 메인 창 전체** 컨텍스트를
		// 만들어 버린다(첫 스모크 실측: 오버레이가 뷰포트가 아니라 에디터 좌상단에 뜨고, 픽킹/기즈모 좌표계가
		// PIE 뷰포트와 어긋남). PIE id 로 받으면 플러그인이 게임 뷰포트 위젯에 오버레이를 붙인다(ImGuiModule.cpp).
		int32 PieSessionId = INDEX_NONE;
		if (const FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(Character->GetWorld()))
		{
			PieSessionId = WorldContext->PIEInstance;
		}
		if (PieSessionId == INDEX_NONE)
		{
			return; // PIE 밖(월드 소멸 중 등) — 메인 창 컨텍스트로 새는 것보다 이번 프레임을 건너뛰는 게 안전.
		}
		const ImGui::FScopedContext ScopedContext(PieSessionId);
		if (!ScopedContext)
		{
			return;
		}

		// 한글 폰트 — 플러그인 기본 아틀라스는 Roboto(ASCII)뿐이라 한글 라벨이 전부 '?' 로 깨진다(첫 스모크 실측).
		// 엔진 동봉 CJK 폴백 폰트를 컨텍스트당 1회 등록한다(시스템 폰트 의존 없음). ImGui 1.92 는 동적 아틀라스라
		// 프레임 사이 추가가 안전하고, 글리프는 필요 시점에 로드된다(glyph ranges 불요).
		static ImFont* KoreanFont = nullptr;
		static ImGuiContext* KoreanFontContext = nullptr;
		if (ImGuiContext* CurrentContext = ImGui::GetCurrentContext(); CurrentContext != KoreanFontContext)
		{
			KoreanFontContext = CurrentContext;
			KoreanFont = nullptr;
			const FString FontPath = FPaths::EngineContentDir() / TEXT("Slate/Fonts/DroidSansFallback.ttf");
			if (FPaths::FileExists(FontPath))
			{
				KoreanFont = ImGui::GetIO().Fonts->AddFontFromFileTTF(TCHAR_TO_UTF8(*FontPath), 16.0f);
			}
			else
			{
				UE_LOG(LogFPSRGunMotionStudio, Warning, TEXT("[GunMotionStudio] CJK 폴백 폰트를 찾지 못했습니다(%s) — 한글 라벨이 깨져 보입니다."), *FontPath);
			}
		}
		if (KoreanFont)
		{
			ImGui::PushFont(KoreanFont, 16.0f);
		}
		ON_SCOPE_EXIT
		{
			if (KoreanFont)
			{
				ImGui::PopFont();
			}
		};

		// §5 화면 클릭으로 대상 선택 — ImGui 윈도우가 입력을 소비 중이 아닐 때만(그렇지 않으면 타임라인/버튼 클릭이
		// 오인식된다), 좌클릭 & 기즈모를 잡고 있지 않을 때.
		if (!ImGui::GetIO().WantCaptureMouse && !ImGuizmo::IsUsing() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			FPickCandidate Best;
			const ImVec2 MousePos = ImGui::GetMousePos();
			if (PickAtScreenPosition(*PC, *Character, FVector2D(MousePos.x, MousePos.y), 40.0f, Best))
			{
				Session.SetSelection(Best.Selection, Best.PartSocket);
			}
		}

		ImGui::Begin("Gun Motion Studio");

		int32 ModeIndex = static_cast<int32>(Session.GetMode());
		const char* Modes[] = { "액션 클립", "상태 포즈" };
		if (ImGui::Combo("모드", &ModeIndex, Modes, IM_ARRAYSIZE(Modes)))
		{
			Session.SetMode(static_cast<EFPSRGunMotionStudioMode>(ModeIndex));
			if (Session.GetMode() == EFPSRGunMotionStudioMode::StatePose)
			{
				Session.SetStatePoseTarget(Session.GetStatePoseTarget()); // reload edit buffer from DA
			}
		}

		if (Session.GetMode() == EFPSRGunMotionStudioMode::ActionClip)
		{
			bool bWholeGun = Session.bWholeGunModeForced;
			if (ImGui::Checkbox("무기 전체 모드", &bWholeGun))
			{
				Session.bWholeGunModeForced = bWholeGun;
				if (bWholeGun)
				{
					Session.SetSelection(EFPSRGunMotionStudioSelection::Gun);
				}
			}

			// TCHAR_TO_UTF8 이어야 한다 — ImGui 텍스트는 UTF-8 전제, TCHAR_TO_ANSI 는 시스템 코드페이지(CP949)라
			// 한글 폰트를 얹어도 모지바케가 난다(첫 스모크 실측 계열).
			const TCHAR* SelectionNames[] = { TEXT("없음"), TEXT("총"), TEXT("왼손"), TEXT("오른손"), TEXT("파츠") };
			ImGui::Text("선택: %s", TCHAR_TO_UTF8(SelectionNames[static_cast<int32>(Session.GetSelection())]));

			ImGui::Separator();
			DrawAttachControls(Session);

			static bool bShowSaveDialog = false;
			if (ImGui::Button("저장…"))
			{
				bShowSaveDialog = true;
			}
			DrawSaveDialog(Session, bShowSaveDialog);
		}

		ImGui::End();

		if (Session.GetMode() == EFPSRGunMotionStudioMode::ActionClip)
		{
			DrawTimeline(Session);
			DrawGizmo(Session, *PC);
		}
		else
		{
			DrawStatePoseMode(Session);
			DrawGizmo(Session, *PC);
		}
	}
}
