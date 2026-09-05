# 저작 시트 쓰기 (write-back) — 설정과 사용

> **왜 있는가**: 종전 저작 사슬은 `구글 시트(사람이 손으로 편집) → sync → Content/Authoring/*.csv → 임포터` 였다.
> 첫 칸이 사람이라 **자동화가 원천적으로 불가능**했고, 카드 물량이 30→70 으로 가는 로드맵(`Roadmap.md` §7-6)에서
> 그 칸이 병목이다. 이 문서는 그 칸을 없애는 도구의 설정법이다.
>
> 관련: `Docs/SSOT/Localization.md` L-5(동기화 규약) · `Docs/SSOT/CombatWeaponCard.md` §2-3-10(카드 CSV 파이프라인)

---

## 1. 마스터 규칙 (먼저 읽을 것)

**리포 CSV 가 마스터고, 구글 시트는 미러다.** (2026-09-05 전환)

```
                 ┌──────────────────────────── 사람이 시트에서 편집했을 때만 ───┐
                 │                                                              │
자동화/Claude ──apply──▶ Content/Authoring/*.csv ──push──▶ 구글 시트            │
                              │      ▲                                          │
                              │      └──────── sync-authoring-csv.ps1 ──────────┘
                        임포터 │                (로컬이 앞서 있으면 거부)
                              ▼
                         DA_Card_* (파생물)
```

- 둘 다 마스터로 두면 **lost update** 가 난다. 그래서 방향마다 가드가 있다:
  - `sync-authoring-csv.ps1`(시트→리포)은 로컬 CSV 가 매니페스트와 다르면 **덮어쓰기를 거부**한다(`-Force` 로만 강제).
  - `authoring_sheet.py push`(리포→시트)는 시트의 해당 탭을 **통째로 갈아끼운다** — 그 사이 사람이 시트에서 한 편집은 사라진다. 그래서 시트에서 편집했으면 **먼저 sync 로 당겨오고**, 그 다음에 push 한다.
- 어느 쪽이 앞서 있는지는 언제나 이걸로 본다:

```bash
python Scripts/authoring_sheet.py status
```

---

## 2. 일상 사용 (설정 없이 지금 바로 되는 것)

카드 추가·수정·삭제는 **변경셋 JSON 1개**로 한다. 사람이 행을 하나씩 만지지 않는다.

```bash
python Scripts/authoring_sheet.py apply Content/Authoring/changesets/<파일>.json --dry-run
python Scripts/authoring_sheet.py apply Content/Authoring/changesets/<파일>.json
```

변경셋 형식(실물 예시 = `Content/Authoring/changesets/20260905-crit1-rifle-crit-build.json`):

```json
{
  "description": "왜 이 변경인가 — 감사 흔적으로 남는다",
  "changes": [
    { "sheet": "Cards",
      "upsert": [ { "CardId": "DA_Card_Foo", "Group": "Weapon", "E1_Attr": "weapon.firerate", "E1_Tiers": "C:0.03" } ],
      "delete": [ "DA_Card_Retired" ] }
  ]
}
```

- **키 = 헤더의 첫 컬럼**(Cards=`CardId` · CardCatalog=`AttrId` · ST_*=`Key`). `"key"` 로 덮어쓸 수 있다.
- `upsert` 는 **병합**이다 — 준 컬럼만 덮어쓰고 나머지는 보존한다(새 행이면 빈 문자열). 22개 컬럼을 다 적을 필요가 없고, **콤마 세는 실수가 원천적으로 없다**.
- 헤더는 `Config/AuthoringSheets.json` 의 `expectedHeader` 와 대조해 **어긋나면 즉시 죽는다**. 조용히 잘못된 열에 쓰는 것보다 낫다.
- **멱등**: 결과가 이미 목표와 같으면 파일을 건드리지 않는다(2회 연속 실행 = diff 0).

적용 후에는 임포터를 돌려 DA 를 갱신한다(Tools > FPSR > 카드 CSV 임포트, 또는 `-run=FPSRImportCards`).

---

## 3. 시트 미러링 설정 (1회, 약 3분)

리포만으로도 게임은 완결된다 — 이 절은 **시트를 계속 보고 싶을 때만** 필요하다.

### 왜 Apps Script 인가
시트에 쓰려면 인증이 필요하다. 서비스 계정(GCP 프로젝트 + API 활성화 + 키 파일 + 공유)은 설정이 무겁고 **키 파일을 리포 근처에 들여야** 한다. Apps Script 웹앱은 시트 안에서 끝나고, 자격증명이 gitignore 된 설정 파일 하나에만 산다.

### 절차 (시트마다 1회 — Cards, CardCatalog)

1. 시트 열기 → **확장 프로그램 > Apps Script**
2. `Scripts/AppsScript/AuthoringSheetWriter.gs` **전체를 붙여넣기**
3. 맨 위 `SHARED_TOKEN` 을 길고 무작위한 문자열로 바꾼다 (두 시트 모두 **같은 값**)
4. **배포 > 새 배포 > 유형: 웹 앱**
   - 실행: **나**
   - 액세스 권한: **링크가 있는 모든 사용자**
     > ⚠️ "나만"으로 두면 OAuth 를 요구해 자동화가 불가능하다. **토큰이 문지기**다 — 그래서 3번의 토큰은 반드시 길고 무작위여야 한다.
5. **배포 > URL 복사**
6. `Config/AuthoringSheets.writeback.example.json` 을 **`Config/AuthoringSheets.writeback.json`** 으로 복사하고 토큰·URL 을 채운다
   (실제 파일은 `.gitignore` 에 있다 — **토큰은 커밋하지 않는다**)

### 사용

```bash
python Scripts/authoring_sheet.py push                # 전체
python Scripts/authoring_sheet.py push --sheet Cards  # 하나만
```

설정 파일이 없으면 **무엇을 해야 하는지 출력하고 종료코드 2로 죽는다** — 조용히 성공한 척하지 않는다.

---

## 4. 갓차

- **시트 탭은 스프레드시트당 1개**를 유지한다. 여러 개면 export `gid` 가 흔들려 sync 가 깨진다(L-5, 2026-08-13 실측).
- **서식·수식은 push 로 보존되지 않는다.** 이 시트들은 순수 데이터라 의도된 동작이다. 수식을 쓰고 싶으면 별도 탭이 아니라 별도 스프레드시트로.
- **한국어 Excel 로 CSV 를 직접 열어 저장 금지** — CP949 로 저장돼 ko/ja 가 파손된다(L-4). 편집은 이 도구나 시트로.
- push 는 `clearContents()` 를 쓴다 — 열 너비·고정행 같은 표시 설정은 남고 **값만** 갈린다.
- 동시 쓰기는 Apps Script `LockService` 로 직렬화된다(20초 대기 후 실패 보고).

---

## 5. 아직 안 되는 것

- **Claude 가 시트에 직접 쓰기** — 세션의 Drive 커넥터는 `update_file` 이 **메타데이터(제목·부모 폴더)만** 바꾼다. 셀 내용 쓰기 API 가 없다(2026-09-05 실측). 그래서 위 3절의 1회 설정이 필요하고, 그 뒤로는 `push` 가 사람 손 없이 돈다.
- **시트→리포 자동 감지** — 사람이 시트에서 편집한 것은 여전히 `sync-authoring-csv.ps1` 을 **누군가 돌려야** 알 수 있다. 폴링이 필요해지면 별도 행.
