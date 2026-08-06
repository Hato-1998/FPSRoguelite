// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assembler/FPSRWeaponAssemblerSettings.h"

UFPSRWeaponAssemblerSettings::UFPSRWeaponAssemblerSettings()
{
	// 화면비 프리셋 시드. Config 배열이라 Config/DefaultEditor.ini 에 항목이 있으면 그쪽이 이긴다 — 여기 값은
	// "아직 아무도 손대지 않은 프로젝트에서 콤보가 비어 있지 않게" 하는 초기값이다.
	//
	// 🚩 해상도가 아니라 종횡비로 묶는다: FHD(1920×1080)·QHD(2560×1440)·4K(3840×2160) 는 전부 16:9 라 구도가
	// 완전히 같다. 라벨에 해상도를 적는 건 디자이너가 자기 모니터를 찾기 쉬우라는 것뿐이다.
	auto Add = [this](const TCHAR* InLabel, float InAspect)
	{
		FFPSRAssemblerAspectPreset& P = AspectPresets.AddDefaulted_GetRef();
		P.Label = InLabel;
		P.AspectRatio = InAspect;
	};

	Add(TEXT("16:9 — FHD · QHD · 4K"), 16.0f / 9.0f);
	Add(TEXT("16:10 — 1920×1200 · 2560×1600"), 16.0f / 10.0f);
	Add(TEXT("21:9 — 울트라와이드 3440×1440"), 3440.0f / 1440.0f);
	Add(TEXT("32:9 — 슈퍼울트라와이드 5120×1440"), 32.0f / 9.0f);
	Add(TEXT("4:3 — 1600×1200"), 4.0f / 3.0f);
}
