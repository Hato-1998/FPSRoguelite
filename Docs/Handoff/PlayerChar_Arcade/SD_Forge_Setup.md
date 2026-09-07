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
