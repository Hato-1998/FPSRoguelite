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

AFPSREnemyBase* FFPSREnemyDormantPool::EvictOneFromLargestOtherBucket(UClass* ExceptClass)
{
	// Explicit TSubclassOf construction (same reasoning as Add/AcquireOfClass above) so the exclusion compare is
	// always the SAME type this map's own KeyType comparison uses.
	const TSubclassOf<AFPSREnemyBase> ExceptKey(ExceptClass);

	FFPSREnemyDormantBucket* LargestOtherBucket = nullptr;
	int32 LargestOtherSize = 0;
	for (TPair<TSubclassOf<AFPSREnemyBase>, FFPSREnemyDormantBucket>& Pair : BucketsByClass)
	{
		if (Pair.Key == ExceptKey)
		{
			continue; // never evict from the very class the caller is trying to make room FOR
		}
		const int32 BucketSize = Pair.Value.Enemies.Num();
		if (BucketSize > LargestOtherSize) // empty buckets (BucketSize == 0) never win, so they're skipped for free
		{
			LargestOtherSize = BucketSize;
			LargestOtherBucket = &Pair.Value;
		}
	}
	if (!LargestOtherBucket)
	{
		return nullptr; // no other class has a dormant occupant to sacrifice
	}

	// Same reverse-scan-with-RemoveAtSwap idiom as AcquireOfClass — prune any invalid (externally-destroyed) slot
	// encountered along the way. This function only REMOVES from the bucket; the caller owns Destroy() + the
	// TotalSpawned decrement (see this method's own header comment for why).
	TArray<TObjectPtr<AFPSREnemyBase>>& Enemies = LargestOtherBucket->Enemies;
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
	return nullptr; // the largest-by-count bucket turned out to be all invalid slots (rare — external destroy only)
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
