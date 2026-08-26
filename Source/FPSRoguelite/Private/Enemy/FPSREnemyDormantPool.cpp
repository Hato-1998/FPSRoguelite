// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyDormantPool.h"
#include "Enemy/FPSREnemyBase.h" // full type: Add() calls Enemy->GetClass() (needs more than the fwd-decl)

void FFPSREnemyDormantPool::Add(AFPSREnemyBase* Enemy)
{
	if (!IsValid(Enemy))
	{
		return;
	}

	// Explicit TSubclassOf construction (not a raw UClass* passed straight to FindOrAdd) so the key stored is
	// always the SAME type FindOrAdd's own KeyType comparison uses — no reliance on cross-type hash agreement.
	const TSubclassOf<AFPSREnemyBase> Key(Enemy->GetClass());
	BucketsByClass.FindOrAdd(Key).Enemies.Add(Enemy);
}

AFPSREnemyBase* FFPSREnemyDormantPool::AcquireOfClass(UClass* ClassToSpawn)
{
	if (!ClassToSpawn)
	{
		return nullptr;
	}

	const TSubclassOf<AFPSREnemyBase> Key(ClassToSpawn);
	FFPSREnemyDormantBucket* Bucket = BucketsByClass.Find(Key);
	if (!Bucket)
	{
		return nullptr;
	}

	// Same reverse-scan-with-RemoveAtSwap idiom the old flat-array AcquireEnemy loop used — LIFO reuse, pruning
	// any invalid (externally-destroyed) slot encountered along the way — just scoped to this one class's
	// bucket instead of the whole pool, so a miss/scan here never costs O(other classes' enemies).
	TArray<TObjectPtr<AFPSREnemyBase>>& Enemies = Bucket->Enemies;
	for (int32 i = Enemies.Num() - 1; i >= 0; --i)
	{
		AFPSREnemyBase* Candidate = Enemies[i].Get();
		if (!IsValid(Candidate))
		{
			Enemies.RemoveAtSwap(i);
			continue;
		}
		Enemies.RemoveAtSwap(i);
		return Candidate;
	}
	return nullptr;
}

int32 FFPSREnemyDormantPool::Num() const
{
	int32 Total = 0;
	for (const TPair<TSubclassOf<AFPSREnemyBase>, FFPSREnemyDormantBucket>& Pair : BucketsByClass)
	{
		Total += Pair.Value.Enemies.Num();
	}
	return Total;
}
