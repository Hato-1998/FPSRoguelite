// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "TickableAttributeSetInterface.h" // GASM1 §5-B(G2 P2-1) — ITickableAttributeSetInterface, 구성 ③ᵀ
#include "FPSRMeasureAttributeSet.generated.h"

/** GASM1(Docs/Specs/GASM1_SwarmASCCostMeasurement.md) — 적 스웜에 GAS 로드아웃을 붙였을 때의 실비용을 재기
 *  위한 측정 전용 어트리뷰트 4종. 게임 로직 소비자 0 — 의도적이다. ADR 0013 이 거부한 것은 "죽은 데이터를
 *  프로덕션 구조에 넣는 것"이고, 이것은 프로덕션 구조가 아니다(§5-B).
 *
 *  🔴 UCLASS 는 #if 밖에 둔다 — UHT 는 UE_BUILD_SHIPPING 을 인식 목록에 갖고 있지 않아 #if 안의
 *  UCLASS/UPROPERTY 는 모든 구성에서 UHT 오류를 낸다(UhtTokenBufferReader.cs:719-733,
 *  UhtHeaderFileParser.cs:1046-1113 → Unrecognized). 선언은 무조건 컴파일하고, 실제 인스턴스가 생기는
 *  경로(AFPSREnemyBase::Activate, CVar 게이트)만 #if !UE_BUILD_SHIPPING 으로 가드한다 — 인스턴스가 생기지
 *  않으면 이 선언 자체는 무해하다. [[uht-ignores-shipping-guard]] */
// 🔁 ITickableAttributeSetInterface 상속 추가 (G2 P2-1, 구성 ③ᵀ). 이 인터페이스는
// UAbilitySystemComponent::GetShouldTick() 이 직접 검사하는 세 조건 중 하나다
// (AbilitySystemComponent_Abilities.cpp:223-247, 인터페이스 체크는 :235-243) — ShouldTick() 이 true 인
// 동안 ASC 틱이 유지된다(§10 자기해제 체인의 ③번을 "강제로"가 아니라 "합법적으로" 만족시키는 경로).
UCLASS()
class FPSROGUELITE_API UFPSRMeasureAttributeSet : public UAttributeSet, public ITickableAttributeSetInterface
{
	GENERATED_BODY()
public:
	UFPSRMeasureAttributeSet();

	//~ITickableAttributeSetInterface — 구성 ③ᵀ 에서만 true. 🔴 CVar 를 여기서 직접 읽지 않는다:
	// UAbilitySystemComponent::TickComponent/GetShouldTick() 이 매 프레임 × 최대 300 세트를 순회하며
	// 부르므로, CVar 조회 비용이 그대로 ③↔③ᵀ 델타로 새어든다(MeasureCadenceCached, FPSREnemyBase.h 와
	// 같은 이유) — 대신 AFPSREnemyBase::Activate 가 1회 캐시해 주는 아래 멤버만 본다.
	virtual bool ShouldTick() const override { return bMeasureTickable; }
	// 의도적 no-op — 재는 것은 "틱이 도는 비용"이지 틱 내용이 아니다(§10). ShouldTick() 이 true 인 한
	// TickComponent 가 이 함수를 매 프레임 부르지만, 안에서는 아무 것도 갱신하지 않는다.
	virtual void Tick(float DeltaTime) override {}

	/** AFPSREnemyBase::Activate 가 CVar(FPSR.Debug.MeasureTickable)로부터 1회 설정한다. 비-UPROPERTY —
	 *  리플렉션도 복제도 불필요하다(틱 여부는 로컬 판단이고 ShouldTick() 의 반환값 자체는 복제 대상이 아니다).
	 *  AFPSREnemyBase 가 값을 대입하므로 public 이어야 한다(다른 클래스의 직접 대입). */
	bool bMeasureTickable = false;

	// 4종 전부 ReplicatedUsing — 복제 트래픽 자체가 측정 대상이므로 COND_None(전원에게)으로 그대로 둔다
	// (GASM1 §7 복제표). Push Model 은 쓰지 않는다 — GAS 어트리뷰트는 Aggregator 경유라 애초에 POD
	// 프로퍼티 푸시모델의 대상이 아니다(FPSRHealthSet 과 동일 형태, push-model-off-in-packaged-build 와는
	// 별개 사안).
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Health, Category = "FPSR|Measure")
	FGameplayAttributeData Health;
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxHealth, Category = "FPSR|Measure")
	FGameplayAttributeData MaxHealth;
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_AttackPower, Category = "FPSR|Measure")
	FGameplayAttributeData AttackPower;
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MoveSpeedMult, Category = "FPSR|Measure")
	FGameplayAttributeData MoveSpeedMult;

	// 🔴 구현 노트 — 명세 §5-B 원문은 ATTRIBUTE_ACCESSORS 를 쓰지만, 그 매크로는 이 리포에도 엔진에도
	// #define 돼 있지 않다: AttributeSet.h:408-426 은 "프로젝트가 원하면 이렇게 직접 정의해 쓰라"는 주석
	// 예시일 뿐이고, 실제로 매크로를 정의하는 것은 그 아래 ATTRIBUTE_ACCESSORS_BASIC 뿐이다
	// (AttributeSet.h:458-469, "이름 충돌을 피하려고 다르게 명명했다"는 주석 포함). 이 리포는 처음부터
	// ATTRIBUTE_ACCESSORS_BASIC 을 써 왔다(FPSRHealthSet.h:25-33, FPSRCombatSet.h) — 둘은 완전히 동일한
	// 4개 접근자(Get/Set/Init/GetXAttribute)를 만드는 동일 계열 매크로이므로, 미정의 심볼 대신 이 리포의
	// 기존 규약을 그대로 쓴다.
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRMeasureAttributeSet, Health)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRMeasureAttributeSet, MaxHealth)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRMeasureAttributeSet, AttackPower)
	ATTRIBUTE_ACCESSORS_BASIC(UFPSRMeasureAttributeSet, MoveSpeedMult)

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 풀 재사용 시 값을 처음(Health 100 / MaxHealth 100 / AttackPower 10 / MoveSpeedMult 1.0)으로
	 *  되돌린다. 🔴 필수 — CancelAbilities()/RemoveActiveEffects()/ClearAbility() 중 무엇도 어트리뷰트
	 *  "값" 자체에는 손대지 않으므로, 이걸 부르지 않으면 이전 삶에서 누적된 Health 감소가 다음 삶으로 그대로
	 *  이어진다. AFPSREnemyBase::Activate 가 로드아웃 부착마다 부른다(GASM1 §6-A). */
	void ResetForMeasure();

protected:
	UFUNCTION() void OnRep_Health(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_MaxHealth(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_AttackPower(const FGameplayAttributeData& Old);
	UFUNCTION() void OnRep_MoveSpeedMult(const FGameplayAttributeData& Old);
};
