// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Blueprint/UserWidget.h"
#include "FPSREnemyHealthBarWidget.generated.h"

class UFPSREnemyHealthComponent;

/** 적 머리 위 체력바 위젯의 C++ 베이스. 담는 것은 **바인딩 계약 하나**뿐이다 — 바의 표현·레이아웃·애니메이션은
 *  전부 콘텐츠(WBP)의 몫이다(§6-2 데이터드리븐 경계).
 *
 *  왜 이 클래스가 필요한가: 종전 바인딩은 AFPSREnemyBase 가 OnHealthBarReady(BlueprintImplementableEvent)를 쏘면
 *  **각 적 BP 가** GetUserWidgetObject -> Cast -> InitHealthComp 를 손으로 배선하는 구조였다. 그래서 컴포넌트만
 *  네이티브로 옮겨도 배선은 여전히 아키타입마다 잊힐 수 있다. 위젯 쪽에 C++ 진입점을 두면 베이스가 직접 부를 수
 *  있고, 그때 비로소 "적이면 체력바가 묶인다"가 코드의 계약이 된다. */
UCLASS(Abstract, BlueprintType)
class FPSROGUELITE_API UFPSREnemyHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 이 바가 표시할 체력 컴포넌트를 넘긴다. AFPSREnemyBase::InitHealthBarWidget 이 위젯 생성 직후 **한 번** 부른다
	 *  (액터 수명당 1회 — 위젯은 풀 재사용을 넘어 살아남는다).
	 *
	 *  ⚠️ 이름이 `InitHealthComp` 가 아닌 것은 의도다. 현행 `WBP_EnemyHealthBar` 가 이미 그 이름의 **BP 함수**를
	 *  갖고 있어서, 부모에 동명 이벤트를 두면 재부모화 시 이름 충돌이 난다(같은 계열의 실사고 = LOD1 의
	 *  HealthBarWidget 충돌, [[cpp-uproperty-name-collides-with-bp]]). 이 이벤트는 기존 함수를 **호출만** 하면 된다.
	 *
	 *  BlueprintImplementableEvent = C++ 기본 구현 없음. 구현하지 않은 WBP 는 조용히 아무 일도 하지 않는다(크래시 아님). */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Enemy")
	void BindHealthComponent(UFPSREnemyHealthComponent* HealthComp);
};
