// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "FPSREnemyHealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FFPSREnemyDeathSignature, AActor*, DeadActor, AActor*, Killer);

/** Lightweight, non-GAS health for swarm enemies. Server-authoritative; damage applied via the GAS->bridge. */
UCLASS(ClassGroup = (FPSR), meta = (BlueprintSpawnableComponent))
class FPSROGUELITE_API UFPSREnemyHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSREnemyHealthComponent();

	/** Server: apply damage and handle death. */
	void ApplyDamage(float DamageAmount, AActor* DamageInstigator);

	/** Server: reset health/dead flag for pooled reuse. */
	void ResetForReuse();

	UFUNCTION(BlueprintPure, Category = "FPSR|Enemy")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "FPSR|Enemy")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "FPSR|Enemy")
	bool IsDead() const { return bDead; }

	UPROPERTY(BlueprintAssignable, Category = "FPSR|Enemy")
	FFPSREnemyDeathSignature OnDeath;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Health();

	UPROPERTY(EditAnywhere, Category = "FPSR|Enemy")
	float MaxHealth = 50.0f;

	UPROPERTY(ReplicatedUsing = OnRep_Health)
	float Health = 50.0f;

	UPROPERTY(Replicated)
	bool bDead = false;
};
