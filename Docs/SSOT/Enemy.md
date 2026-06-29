# Enemy — 몬스터 / 발사체 / 네트워크 (FPSRoguelite SSOT 분할)

> `Game.md`(SSOT 허브)의 분할 문서. **섹션 번호(§x)는 원본 그대로 보존** — 소스 주석·교차참조 호환.
> 작업 시작 전 허브 `Game.md` + `PROGRESS.md`를 먼저 읽고, 적 스웜·스폰·발사체·데미지 브릿지·네트워크 토폴로지 관련 작업 시 본 파일을 연다. 성능/플로우필드 수치는 `Performance.md`(§5) 참조.
> 담는 섹션: §2-6 몬스터(스웜 적) / §2-10 발사체·네트워크.

---

### 2-6. 몬스터 (스웜 적)
- 공격 타입 **근거리 / 원거리 / 특수 중 정확히 1개 고정** (상황 따라 전환 안 함)
- **GAS 미사용** — 경량 `UHealthComponent`(`UFPSREnemyHealthComponent`) + 비-GE 데미지 적용
- 이동: **Flow-Field 샘플링(고정맵 사전계산) + 분리(separation)**, 배치 업데이트 (P2)
- 렌더: 인스턴싱/VAT + 거리 LOD (Significance Manager)
- 풀링 필수 (`UActorPool`)
- 시간 스케일링: `UEnemyScalingProfile` DataAsset — HP/공격력 커브 (이속 불변, 스탯별 슬롯 확장 가능)
- **개체별 이속 편차**(확정 2026-05-30): 아키타입 기본속도는 고정, **스폰 시 개체마다 ±10% 무작위 편차** 부여 → 단일 Blob 밀착 방지, 스웜을 입체적·유기적으로 분산해 카이팅 재미. 비용 0(스폰 시 1회 곱셈)
- **원거리 공격 규격**(확정 2026-05-30): 기본 **투사체(Projectile)** 방식(눈으로 보고 회피 가능)으로 강제. 히트스캔 사용 시 **차징 유예 + 사전경고 인디케이터 필수**(부조리 탄막 금지)
- **공격 토큰(Attack Token)**(확정 2026-05-30): 플레이어당 **동시에 공격을 시도할 수 있는 적 개체 수 상한**(서버 권위). 수백 마리 동시 사격/특수공격의 불합리 방지 + §5 "적 공격 판정 서버 배치"의 구현 수단. (토큰 개수·FF 기본 10% 등 수치는 밸런스 후속)
- **룸 기반 점진 개방 스폰**(구현 2026-06-25): 맵=방(룸) 구성. 벽의 `AFPSRDoor`(파괴 장벽)를 사격해 부수면 통로 개방 → 플레이어가 `AFPSRSpawnRoom`(박스 트리거) 진입 시 그 방의 스폰존이 활성. 활성 존은 **누적**(지나온 방 계속 스폰; 적 총량은 레벨기반(§위)으로 불변, 방 개방은 스폰 **위치**만 추가). 스폰포인트는 방 박스가 BeginPlay에 자동 태깅(`AFPSREnemySpawnPoint.ZoneTag=RoomTag`, 수동 태그는 존중), 선택은 적격(MinPlayerDistance + 존활성) **균등 랜덤**(가중치·거리폴오프 폐지 2026-06-25, **out-of-view(시야 밖) 게이트 폐지 2026-06-29** — 스폰포인트가 플레이어 시야 안에 있어도 스폰 허용. 적이 눈앞에 등장할 수 있으므로 배치는 디자이너가 등 뒤/측면으로 의도; 단일 정면 포인트가 스폰을 굶기던 문제 해소). 시작방=`bActiveAtStart`. 서버 권위(`UFPSREnemySpawnSubsystem.ActiveSpawnZones`/`ActivateSpawnZone`/`ResetSpawnZones`/`DeactivateSpawnZone`; 리셋=OnWorldBeginPlay + StartRun). **룸 비활성화 볼륨**(2026-06-25 추가): 누적이 **기본**이지만, 디자이너가 `AFPSRSpawnRoom.TriggerMode=Deactivate`로 둔 볼륨에 플레이어가 진입하면 대상 존(같은 `RoomTag`)이 꺼진다(`DeactivateSpawnZone`=`ActiveSpawnZones.RemoveTag`, `ActivateSpawnZone`의 대칭; 플랫 RoomTag exact 제거). 즉 누적=기본 동작, 비활성화=레벨 디자이너가 특정 방 스폰을 **명시적으로** 정리(페이싱/슬라이딩)하려 할 때만 작동(Deactivate룸은 자동태깅 안 함=대상 존만 참조, `ResetSpawnZones`는 Activate 시작방만 재활성). 존 상태는 전역 서버권위(활성화와 동일)라 4인 협동에서 1인 진입=전역 토글(분리 파티 주의). 같은태그 대칭(1볼륨=1존). 설계상세 `Docs/RoomSpawnSystem_Handoff.md`.
- **파괴 장벽(`AFPSRDoor`) = 비-적 데미지 대상**: `UFPSREnemyHealthComponent`를 가져 **데미지 브릿지로 전 무기 경로 자동 피격**(신규 데미지 코드 0). 콜리전 오브젝트타입 = `ECC_FPSRPlayerPawn`(플레이어·적 모두 차단 + 대시로 통과 불가 + 모든 무기 오브젝트쿼리에 포함). **`bCountsAsKill=false`** → 부숴도 킬 크레딧·on-kill 프래그먼트·흡혈(on-damage GAS 이벤트) 미발동(데미지/`DamageDealt`/파괴는 정상; `FPSRCombat::ApplyDamage` 게이트). 메시는 BP 지정(C++ 하드코딩 금지), 파괴 연출=`OnDoorBroken`(BlueprintImplementableEvent). 문틀은 `FrameMesh`(WorldStatic=무기쿼리 비대상=무반응 벽). XP는 `AFPSREnemyBase::HandleDeath` 전용이라 문은 자동 0.

### 2-10. 발사체 / 네트워크
- 발사체: **클라 예측 + 서버 검증**
  - 스나이퍼/레이저 = 히트스캔, 바주카/유탄 = 실제 발사체(소수), 샷건 = 다중 히트스캔, 연사 = 히트스캔/경량
- 데미지: **플레이어 GAS 계산 → 적 HealthComponent.ApplyDamage 브릿지** (적 ASC 없음)
- **아군 오사(Friendly Fire)**(확정 2026-05-30): 판정은 존재하나 아군 적중 시 **몬스터 데미지의 10%만 적용**(기본값, 불쾌감 완화). 호스트가 세션 설정에서 **FF 적용 토글** 가능. 데미지 브릿지에 팀/FF 배율 경로를 둠(구현 P5)
- 토폴로지: **리슨서버 P2P** (Steam Sockets/EOS 세션, P5에서 구축)
- **개발 방법론: P1부터 Net-aware (서버 권위 + Push Model), PIE 2-client 상시 검증.** 솔로로 만들고 나중에 리플리케이션 retrofit 금지
- GAS ASC Replication Mode: 솔로 = Minimal, 협동 = Mixed
- Iris: **OFF (디폴트는 Push Model)**. P5에서 평가용으로만 검토 (Beta 리스크)
