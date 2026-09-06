# 아케이드 픽셀 키아트 — 외부 이미지 생성 프롬프트 세트 (ADR 0016 부록)

> 컨셉 시트(아티팩트 「FPSRoguelite Arcade Pixel Concept Sheet」)는 **구도·색·규칙**을 정한 도면이지 페인팅이 아니다.
> 페인팅 키아트(스토어 캡슐·로비 배경·프레젠테이션용)는 사용자가 Midjourney · Stable Diffusion · 기타 생성 도구나 아티스트에게
> 이 프롬프트를 넘겨 뽑는다. **프롬프트의 모든 조건은 `Docs/SSOT/ArtDirection.md §A·§B` 와 1:1** — 결과물이 규칙을 어기면
> 프롬프트가 아니라 결과물을 버린다.
>
> ⚠️ 롤 아케이드 스킨·전자오락수호대의 **캐릭터·의상·포즈·로고를 프롬프트에 넣지 않는다**(모티프·색감만). 생성 결과는 참고 자료지 인게임 에셋이 아니다 — 인게임은 복셀 메시·픽셀 스프라이트로 다시 저작한다(ADR 0016 D7).

## 0. 공통 블록 (모든 프롬프트 앞에 붙인다)

```
retro arcade cyberspace, dark stage with pop accents, voxel (3D pixel) forms with strict single grid per object,
flat colored voxel faces (flat albedo) with thin dark grid lines, NO photo textures, NO painted gradients on surfaces — but lit with global illumination: emissive neon panels and floor traces bounce colored light onto nearby voxel walls and floor, floor has a soft low-roughness reflection (never mirror-like),
dominant palette: deep indigo/violet substrate (#151329 #221E3D #332B57) covering 70% of frame,
bright teal circuit traces on the floor (#2A8A96 #39B8B0 #5FE0D2) as walkable paths,
outside the arena boundary: a retro-game pixel parallax backdrop — 2px pixel stars, stepped pixel skyline silhouettes and floating pixel blocks in dim plum (#5A2E63 #9B3F86), ALL flat single-color silhouettes, NO wireframes, NO circuit traces, NO neon edges on walls or blocks,
enemies ONLY in hot colors (pink/red/orange #FF3B4E #FF6B2C #FF1E7A) with one glowing core each,
allies ONLY in cold colors (cyan/blue/violet #4FD8FF #2E9BFF #8B6BFF),
destructible objects marked by small lime glowing cores (#9BE33C),
gold pixel coins and stars as pickups (#FFC24A), CRT scanlines subtle, slight chromatic aberration, vignette,
enemy glow must not tint the floor (only environment neon bounces), no pure white, no pure black, no photoreal, no smooth cel-shading, no anime characters
```

네거티브(지원하는 도구에서):
```
--no orange background, magenta background, wireframe, circuit board, tron, bright environment, photo texture, mirror floor, anime face, cel shading, smooth gradients, blur, motion blur, lens flare, text, logo, watermark
```

## 1. 키 비주얼 A — 1인칭 프레임 (스토어 캡슐 · 메인 이미지)

```
[공통 블록]
first-person view of a blocky voxel rifle held at bottom-right with a violet emissive stripe (#8B6BFF),
pixel-sprite muzzle flash in pale gold, crosshair at center,
a swarm of round voxel ghost enemies with open jaws and glowing orange cores (#FF6B2C) rushing across a dark voxel arena floor,
one large enemy in the near left, dozens receding to the horizon,
one teammate silhouette outlined in cyan (#4FD8FF) standing mid-distance to the left with a small "P2" tag,
voxel blocker walls in indigo with thin dark grid lines, one block with a small lime glowing core,
teal circuit traces on the floor converging to the horizon, a low boundary wall, beyond it a plum pixel skyline, pixel stars and floating pixel blocks as flat silhouettes,
minimal pixel HUD: segmented shield/health bars bottom-left, big ammo digits bottom-right, "STAGE 2" top center,
16:9, wide angle 90 fov, eye level
```

## 2. 키 비주얼 B — 3인칭 단체 샷 (4인 협동 · 로비 배경)

```
[공통 블록]
four voxel hero figures seen from behind and slightly above, standing back-to-back in a square formation at the center of a dark voxel arena,
each outlined in a cold color (cyan, blue, violet, teal), holding blocky voxel guns with blue emissive accents,
surrounded on all sides by a ring of hot-colored voxel ghost enemies (pink/red) with glowing orange cores, hundreds receding into darkness,
teal circuit traces radiating from the center, one giant arcade-cabinet-shaped boss core in the far background pulsing lime green,
plum pixel skyline and floating pixel block silhouettes outside the arena boundary,
16:9, dramatic but readable — the four cold silhouettes must pop against the hot swarm
```

## 3. 키 비주얼 C — 억제기(캐비닛 코어) 파괴 순간 (STAGE CLEAR · Ultimate)

```
[공통 블록]
a giant voxel arcade cabinet (indigo body, dark screen with two white pixel eyes and a magenta pixel mouth) cracking apart,
its lime-green core (#C8FF5A) bursting into square pixel shards — the brightest object in the frame,
four cold-outlined voxel heroes small in the foreground facing it, hot-colored ghost enemies dissolving into pixel dust,
pixel text "STAGE CLEAR" floating above in lime green, CRT scanlines,
16:9, the explosion is the only high-value area; everything else stays in the dark substrate band
```

## 4. 무드 스트립 3종 (구역별 · 컨셉 시트 「3 ZONES」 대응)

- **L_Map_1 아케이드 플로어**: `[공통 블록] + wide establishing shot of the arena from a high corner, teal traces medium density, medium brightness, calm`
- **L_Map_2 글리치 섹터**: `[공통 블록] + darker floor traces, a taller and denser magenta pixel skyline beyond the boundary, glitch-block spawn points along the wall, corrupted pixel-noise patches on the floor in plum, tense`
- **L_Map_Boss 캐비닛 홀**: `[공통 블록] + almost no floor traces, only sparse pixel stars beyond the boundary, the only light source is a giant lime-pulsing arcade cabinet core at the center, darkest of the three`

## 5. 적 라인업 시트 (컨셉 시트 「LINEUP」 페인팅판)

```
[공통 블록]
character lineup sheet on a flat indigo background, left to right at consistent scale:
a cyan-outlined voxel hero (180cm) for scale,
a round voxel ghost with a wide jaw and glowing orange core (165cm),
a low wide voxel beetle with a single core on its back (90cm),
a tall thin voxel spike creature (150cm),
an elite version of the ghost 1.3x larger with a magenta rim (#FF1E7A) and two cores,
a giant voxel arcade cabinet boss (240cm) with a screen for a face,
all enemies strictly pink/red body colors within the character value band, one 7.5cm voxel grid per figure, no textures
```

## 6. 결과물 판정 체크리스트 (§A·§B 대조)

- [ ] 환경이 어둡다 — 화면 70% 이상이 청보라 substrate 인가. 주황·마젠타 **배경** 이면 폐기.
- [ ] 뜨거운 색은 적·텔레그래프·엘리트에만, 차가운 색은 아군·배선·UI에만 있는가(🔒 A-3-4/A-3-5).
- [ ] 팝한 색(금·주황·연두·마젠타) 합계가 화면 10% 미만인가.
- [ ] 복셀이 **한 오브젝트 한 격자**인가 — 매끈한 실루엣에 표면만 계단이면 폐기(가짜 픽셀).
- [ ] 사진·노이즈 텍스처·부드러운 그라데이션이 표면에 없는가.
- [ ] 순백·순흑이 없는가.
- [ ] 레퍼런스 IP 의 캐릭터·로고가 섞이지 않았는가.
