# ENE 삼면도 — Stable Diffusion (Forge) 경로 셋업 · 프롬프트

> 2026-09-08. 사용자가 로컬에 **SD WebUI Forge** 를 띄워 둔 상태에서 이 트랙을 SD 로도 시험한다.
> 제미나이 경로(`ImagePrompt_v4.txt`)는 **폐기하지 않는다** — 둘 중 나은 쪽을 고른다.

## 0. 실측한 환경 (2026-09-08)

| 항목 | 값 |
|---|---|
| 프론트엔드 | **SD WebUI Forge** (`F:\StableDiffusion\stable-diffusion-webui-forge`), `python launch.py`, 포트 **7860** |
| 체크포인트 | **`sd_xl_base_1.0.safetensors` 하나뿐** (6.5 GB) |
| LoRA / VAE / 확장 | **없음** |
| ControlNet | 확장은 Forge 내장, **모델 0개**(`/controlnet/model_list` → `["None"]`) |
| API | **꺼져 있었음** → `webui-user.bat` 의 `COMMANDLINE_ARGS=--api` 로 켜 둠(백업 `.bak` 생성). **재시작 필요** |
| GPU | RTX 4070 Ti 12 GB — SDXL 계열 구동에 충분 |

## 1. 🔴 착수 전 판정 — 지금 상태로는 제미나이보다 나쁘다

**SDXL base 1.0 은 애니/셀 화풍을 못 그린다.** 파운데이션 모델이라 애니 파인튜닝이 없고,
이 트랙이 요구하는 **굵은 외곽선 · 평면 채색 · 데포르메 비례**가 아예 안 나온다.
"SD 가 스타일에서 이긴다"는 판단은 **애니 파인튜닝 체크포인트를 전제**한 것이었다.

→ **체크포인트 1개 다운로드가 이 트랙의 선행 조건이다.** 없이 돌리면 v4 제미나이 경로보다 못하다.

### 1-1. 받아야 할 것 (사용자, Civitai 계정 필요)

| 우선 | 무엇 | 왜 |
|---|---|---|
| **필수** | **Illustrious XL 계열**(또는 그 파생 NoobAI-XL) SDXL 체크포인트 ~6.5 GB | 현시점 애니/게임아트 최강 계열. **danbooru 태그 네이티브**라 아래 §2 프롬프트가 그대로 먹는다. 굵은 외곽선·평면 채색이 기본값에 가깝다 |
| 대안 | **Pony Diffusion V6 XL** | 스타일라이즈드 캐릭터에 강하다. 태그 체계가 조금 다르고 `score_9, score_8_up...` 접두어가 필요하다 |
| 선택 | ControlNet **OpenPose**(SDXL용) ~2.5 GB | A포즈를 강제한다. 없으면 포즈가 매번 흔들린다 |
| 선택 | ControlNet **IP-Adapter**(SDXL용) | 레퍼런스 이미지의 스타일/디자인을 물린다. 제미나이의 "레퍼런스 첨부"에 해당하는 기능 |

> 💡 **모델 없이 되는 것 하나** — ControlNet 의 **`reference_only`** 모드는 **전처리기만 쓰므로 모델 다운로드가 필요 없다.**
> 정면을 확정한 뒤 그걸 `reference_only` 로 물려 측면·후면을 뽑으면, 모델 0개 상태에서도 뷰 일관성을 상당히 끌어올릴 수 있다.

설치 위치: 체크포인트 → `models\Stable-diffusion\` · ControlNet → `models\ControlNet\` · LoRA → `models\Lora\`

### 1-2. 체크포인트 후보 (2026-09-08 정리 · 지식 기준 2026-05, **버전은 사이트에서 최신으로 확인할 것**)

선정 기준 = 이 프로젝트가 요구하는 것: **굵은 외곽선 · 평면 채색 · 데포르메 비례 · 태그로 색을 통제할 수 있을 것.**

| 순위 | 계열 | 맞는 이유 | 주의 |
|---|---|---|---|
| **1** | **Illustrious XL**(OnomaAI) | **danbooru 태그 네이티브** — `thick outline`·`flat color`·`from behind` 가 그대로 먹는다. 선이 깨끗하고 평면 채색이 기본값에 가깝다. §3 프롬프트가 이 계열 기준으로 쓰였다 | 파생이 워낙 많아 **베이스보다 파생이 나은 경우가 흔하다** |
| **2** | **NoobAI-XL**(Illustrious 기반) | 태그 이해도가 더 넓고 구도 지시를 잘 받는다 | 🔴 **V-Pred 변형은 설정이 까다롭다**(`v_prediction` + ZSNR config 필요) → **EPS 버전을 받을 것** |
| **3** | **Animagine XL**(Cagliostro) | "공식 애니 일러스트" 톤, 색이 차분하고 대비가 안정적 | 데포르메·과장 실루엣은 Illustrious 계열보다 약하다 |
| 대안 | **Pony Diffusion V6 XL** | 스타일라이즈드·장난감 형태에 강하고 LoRA 생태계가 가장 크다 | 🟡 `score_9, score_8_up, score_7_up` **접두어 필수**. 고유 화풍 편향이 있어 "깨끗한 셀"은 Illustrious 쪽이 낫다 |

**고르는 요령** — 베이스보다 잘 튜닝된 **파생**이 대개 낫다. `Illustrious` 필터 + 다운로드순 상위에서, **샘플 이미지가 "굵은 선 + 평면 채색"인 것**을 고른다. 샘플이 부드러운 그라데이션·사실적 음영 위주면 우리 방향과 정반대다.

### 1-3. ControlNet 후보 — 다 받을 필요 없다

| 우선 | 모델 | 용량 | 용도 |
|---|---|---|---|
| **1순위** | **xinsir ControlNet-Union SDXL (ProMax)** | ~2.5 GB | **한 모델로 openpose·depth·canny·lineart·tile 커버.** 개별로 받으면 2.5 GB × 5 |
| 개별 | xinsir **OpenPose SDXL** | ~2.5 GB | A포즈 강제만 필요할 때 |
| **레퍼런스** | **IP-Adapter SDXL**(`ip-adapter-plus_sdxl_vit-h`) | ~1 GB + CLIP vision 인코더 | 제미나이의 "레퍼런스 첨부"에 해당 — 레퍼런스 4장의 스타일·디자인을 물린다 |
| 경량 대안 | **T2I-Adapter SDXL**(TencentARC) | **~300 MB** | openpose/canny/lineart. 정밀도는 낮지만 10배 가볍다 |
| **무료** | `reference_only` | 0 | 전처리기 전용. 정면 → 측면·후면 일관성(§2 전략) |

→ **최소 조합 = Union ProMax + IP-Adapter ≈ 3.5 GB.** A포즈 강제 + 레퍼런스 물리기 + 뷰 일관성이 전부 커버된다.

### 1-4. LoRA — 이름이 자주 바뀌므로 검색 키워드로

- **`flat color`** / **`cel shading`** / **`thick outline`** / **`vector art`** — 우리가 원하는 화풍의 핵심
- **`chibi`** / **`deformed`** / **`minigirl`** — 등신 데포르메 강제
- ★ **`character sheet`** / **`turnaround`** / **`multiple views`** — **삼면도 자체를 학습한 LoRA. 이 프로젝트에 가장 값어치 있다**

⚠️ 체크포인트와 **베이스가 맞아야** 한다(Illustrious용 LoRA를 Pony 에 쓰면 잘 안 먹는다).

### 1-5. 🔑 삼면도를 기하학적으로 강제하는 트릭 (OpenPose/Union 을 받으면)

**컨트롤 이미지 한 장에 OpenPose 스켈레톤을 3개 나란히** 넣는다(정면·측면·후면 포즈).
그러면 SD 가 세 뷰를 **같은 캔버스에, 같은 높이로, 지정한 포즈로** 그린다.

→ "3뷰 배율·높이 일치"와 "A포즈"가 **프롬프트 부탁이 아니라 기하학적 구속**이 된다.
이 트랙에서 반복적으로 깨진 두 항목이 한 번에 닫힌다. §2 의 "정면 확정 → reference_only 파생"
전략보다 이쪽이 더 강하므로, Union 을 받으면 **이 방법을 우선한다.**


## 2. 🔑 전략 — 한 장에 3뷰를 욕심내지 않는다

제미나이 경로는 "한 장에 3뷰"였다. 일관성을 AI 의 선의에 맡기는 방식이라 v1 에서 실제로 깨졌다.
**SD 에서는 그럴 필요가 없다** — Meshy 는 어차피 낱장으로 받는다.

1. **정면을 먼저 뽑아 확정**한다(시드 고정).
2. 그 이미지를 **`reference_only` ControlNet** 에 물리고 프롬프트만 `from side` / `from behind` 로 바꿔 측면·후면을 뽑는다.
3. 세 장의 **키·배율을 후처리로 맞춘다**(같은 캔버스 높이로 리사이즈).

이게 "세 뷰 일치"를 **희망이 아니라 절차**로 만든다 — 이 트랙에서 반복적으로 깨진 항목이 바로 그것이다.

## 3. 프롬프트 (Illustrious / NoobAI 계열 기준 · danbooru 태그)

### 3-1. Positive — 정면
```
masterpiece, best quality, very aesthetic, official art, character reference sheet,
1girl, solo, full body, standing, a-pose, arms slightly away from body, open relaxed hands, empty hands,
front view, facing viewer, simple background, grey background,

large head, short neck, narrow shoulders, wide hair silhouette, head wider than shoulders,
chibi-like proportions, rounded body, thick rounded limbs, toy-like, inflated shapes, oversized shoes,

blunt bangs, chin-length bob cut, very dark blue hair, single pale streak in bangs, blue hair ornament,
tinted goggles, cyan tinted goggles, over-ear headphones, cable,

off-white cropped jacket, puffy long sleeves, chest harness, strap, emblem,
black bodysuit, black leggings, covered legs, thigh strap,
oversized platform sneakers, chunky sneakers,
cyan glowing trim, glowing accents, back panel,

thick outline, bold lineart, flat color, cel shading, hard shadow, limited palette
```

### 3-2. Negative (여기가 SD 의 진짜 강점이다 — 문장이 아니라 억제 메커니즘)
```
orange, red, magenta, pink, amber, yellow, warm colors, orange tint, red glow,
pure white, pure black, white background,

photorealistic, realistic, 3d render, photo, gradient, soft shading, airbrush, blurry, noise, texture, film grain,
thin limbs, skinny, slender, tall, long legs, realistic anatomy, fashion illustration, elegant proportions,

weapon, gun, sword, grenade, holding, ribbon, scarf, tentacle, squid, ink, hat, cap, backpack,
bare legs, bare thighs, thighs, exposed skin on legs,

text, watermark, signature, logo, dimension lines, arrows, border,
multiple girls, 2girls, extra limbs, bad hands, bad anatomy, worst quality, low quality, jpeg artifacts
```
> 🔴 네거티브 첫 줄이 **`§A-3-4` 적 예약 대역 차단**이다. `#FF3B4E`·`#FF6B2C`·`#FF1E7A` 가 화면에
> 들어오면 4인 협동 × 적 200~300 에서 아군이 적으로 읽힌다(ADR 0016 **I1**, 복구 불가 등급).
> 특히 레퍼런스 [4] 헤드기어가 **원본이 주황**이라 여기가 가장 새기 쉽다.

### 3-3. 측면 / 후면 (정면 확정 후, `reference_only` 물린 상태에서)
`front view, facing viewer` 만 아래로 교체하고 **나머지는 한 토큰도 바꾸지 않는다**:
- 측면 → `from side, profile, facing right`
- 후면 → `from behind, back view, facing away from viewer`

### 3-4. 설정 초기값
| 항목 | 값 | 비고 |
|---|---|---|
| 해상도 | **832 × 1216** (인물 세로) | SDXL 은 총 화소가 1024² 근처여야 한다. 3뷰를 한 장에 욕심내지 않으므로 세로 인물 비율이 맞다 |
| Sampler | `Euler a` 또는 `DPM++ 2M SDE Karras` | |
| Steps | 28~32 | |
| CFG | **4~6** | Illustrious/NoobAI 계열은 CFG 를 낮게 쓴다. 7 이상이면 색이 타고 대비가 깨진다 |
| Hires fix | 1.5× · `4x-UltraSharp` 또는 `R-ESRGAN 4x+ Anime6B` · denoise 0.35 | 선택 |
| Seed | **정면이 통과하면 고정** | 측면·후면 생성 시 같은 시드 + `reference_only` |

## 4. 판정
`ImagePrompt_v4.txt` 의 체크리스트를 그대로 쓴다(색 4항목 최우선 → 형태 언어 4항목 → 레퍼런스 분담 → 비례 → 일관성).
**세 뷰 일치 항목은 3장을 나란히 놓고** 부위별로 대조한다.

## 5. 이 문서가 안 하는 것
모델 다운로드(사용자 계정) · Forge 재시작(사용자) · 게임/에디터 기동. `--api` 는 켜 뒀으나 **재시작해야 적용된다.**
