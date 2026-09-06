// 산출 = Docs/Architecture/0016 시각 부록(아티팩트 「FPSRoguelite Arcade Pixel Concept Sheet」). 실행: 작업 폴더에서 `node gen_concept_sheet_arcade_pixel.mjs` → *.dc.html + canvas.json → design 스킬 seed-canvas.mjs 로 조립.
// 컨셉 시트 아트보드 생성기 — ASCII 픽셀맵 → SVG rect. 산출 = *.dc.html + canvas.json
import { writeFileSync } from 'node:fs';

// ---------- 팔레트 (Docs/SSOT/ArtDirection.md §A-3 그대로) ----------
const P = {
  void: '#0A0912', floorDark: '#151329', floor: '#221E3D', side: '#332B57', top: '#41386B',
  wireFar: '#1E5A66', wire: '#2A8A96', wireWide: '#39B8B0', via: '#5FE0D2',
  destrFar: '#6FA82E', destr: '#9BE33C', destrHot: '#C8FF5A',
  enemyRim: '#FF3B4E', enemyTel: '#FF6B2C', elite: '#FF1E7A',
  ally: '#4FD8FF', allyPing: '#2E9BFF', self: '#8B6BFF',
  plumDark: '#5A2E63', plum: '#9B3F86',
  uiText: '#EAF6FF', uiSub: '#8FA8C4', uiOk: '#5FE0D2', uiReward: '#FFC24A', uiWarn: '#FF4D5E',
  // 캐릭터 대역(V25-70·S30-60) 안의 적 몸통 — 파일럿 MI 색은 사용자 확정 대기(ART-후속)
  enemyBody: '#9E4560', enemyShade: '#6E2E44', tooth: '#EADFE8', eyeW: '#E8E4F0', pupil: '#1A1024', tongue: '#C8324A',
  gun: '#2A2545', gunDark: '#1B1830', gunLight: '#3B3560',
  flash: '#FFF3C4', flash2: '#FFC24A',
};

// ---------- 픽셀맵 → rect (가로 런 병합) ----------
function px(map, legend, cell, ox = 0, oy = 0, extra = '') {
  const rows = map.trim().split('\n').map(r => r.replace(/\s+$/, ''));
  let out = '';
  rows.forEach((row, y) => {
    let x = 0;
    while (x < row.length) {
      const ch = row[x];
      if (ch === '.' || !(ch in legend)) { x++; continue; }
      let x2 = x;
      while (x2 + 1 < row.length && row[x2 + 1] === ch) x2++;
      out += `<rect x="${ox + x * cell}" y="${oy + y * cell}" width="${(x2 - x + 1) * cell}" height="${cell}" fill="${legend[ch]}"${extra}></rect>`;
      x = x2 + 1;
    }
  });
  return out;
}
const dims = (map) => { const rows = map.trim().split('\n'); return [Math.max(...rows.map(r => r.trimEnd().length)), rows.length]; };

// ---------- 스프라이트 ----------
const CHOMPER_OPEN = `
......HHHH......
....HHHHHHHH....
...HHHHHHHHHH...
..HHHHHHHHHHHH..
.HHHHHHHHHHHHHH.
.HHWWWHHHHWWWHH.
HHHWPWHHHHWPWHHH
HHHWWWHHHHWWWHHH
HHHHHHHHHHHHHHHS
HHHHHHHHHHHHHHHS
HTDDDTDDDDTDDDTS
DDDDDDDCCDDDDDDD
DDDRRRDCCDRRRDDD
JTDDDTDDDDTDDDTS
JJJJJJJJJJJJJJJS
JJJJJJJJJJJJJJJS
JJ.JJJJ..JJJJ.JS
J...JJJ..JJJ...S`;
const CHOMPER_CLOSED = `
......HHHH......
....HHHHHHHH....
...HHHHHHHHHH...
..HHHHHHHHHHHH..
.HHHHHHHHHHHHHH.
.HHWWWHHHHWWWHH.
HHHWPWHHHHWPWHHH
HHHWWWHHHHWWWHHH
HHHHHHHHHHHHHHHS
HHHHHHHHHHHHHHHS
HHHHHHHHHHHHHHHS
SSSSSSSSSSSSSSSS
JJJJJJJJJJJJJJJS
JJJJJJJJJJJJJJJS
JJJJJJJJJJJJJJJS
JJJJJJJJJJJJJJJS
JJ.JJJJ..JJJJ.JS
J...JJJ..JJJ...S`;
const LEG_CH = { H: P.enemyBody, S: P.enemyShade, W: P.eyeW, P: P.pupil, D: '#2A0E18', T: P.tooth, R: P.tongue, C: P.enemyTel, J: '#8A3A52' };
const LEG_CH_ELITE = { ...LEG_CH, H: '#8C2E5A', J: '#742548', S: '#4E1A34', C: P.elite };

// 각진 비틀(각 실루엣) 18×12
const BEETLE = `
..HHHH......HHHH..
.HHHHHH....HHHHHH.
HHHHHHHHHHHHHHHHHH
HHWWHHHHHHHHHHWWHH
HHWPHHHHCCHHHHPWHH
HHHHHHHCCCCHHHHHHH
HHHHHHHHCCHHHHHHHS
SHHHHHHHHHHHHHHHHS
SSHHHHHHHHHHHHHHSS
..SS.SS.SS.SS.SS..
..SS.SS.SS.SS.SS..
.SS..SS..SS..SS..S`;
// 스파이크 글리치(침 실루엣) 10×20
const SPIKE = `
....HH....
....HH....
...HHHH...
...HHHH...
..HHHHHH..
..HWWHHH..
..HPWHHH..
.HHHHHHHH.
.HHHCCHHH.
.HHCCCCHH.
.HHHCCHHH.
HHHHHHHHHS
HHHHHHHHHS
.HHHHHHHS.
.HHHHHHHS.
..HHHHHS..
..HHHHHS..
...HHHS...
..H.HH.H..
.H..HH..H.`;

const COIN = `
..GGGG..
.GhGGGg.
GhGGGGgG
GhGGGGgG
GGGGGGgG
GGGGGggG
.GGgggg.
..gggg..`;
const LEG_COIN = { G: P.uiReward, g: '#B8801E', h: '#FFE9A8' };
const STAR = `
....Y....
...YYY...
YYYYYYYYY
.YYYYYYY.
..YYYYY..
.YYYYYYY.
.YYY.YYY.
YY.....YY`;
const HEART = `
.CC..CC.
CCCCCCCC
CCCCCCCC
CCCCCCCC
.CCCCCC.
..CCCC..
...CC...`;
const CABINET = `
.BBBBBBBBBB.
BBBBBBBBBBBB
BBSSSSSSSSBB
BBSGGGGGGSBB
BBSGGggGGSBB
BBSGGGGGGSBB
BBSSSSSSSSBB
BBBBBBBBBBBB
BBBBRRBBOOBB
BBBBBBBBBBBB
.BBBBBBBBBB.
.BBBBBBBBBB.
.BB......BB.`;
const LEG_CAB = { B: '#332B57', S: '#151329', G: '#2A8A96', g: '#5FE0D2', R: P.uiWarn, O: P.uiReward };
const JOY = `
....RR....
...RRRR...
...RRRR...
....RR....
....SS....
....SS....
....SS....
.BBBBBBBB.
BBBBBBBBBB
BBBBBBBBBB`;
const LEG_JOY = { R: P.uiWarn, S: '#8FA8C4', B: '#332B57' };
const GLITCH = `
MM..MMMM..
..MM..MMMM
MMMM..MM..
..MMMM..MM
MM..MM....
....MMMM..
MMMM..MMMM
..MM....MM`;
const LEG_GLITCH = { M: P.plum };
const CARD = `
.WWWWWWWWWW.
WWSSSSSSSSWW
WWSSYYYYSSWW
WWSSYYYYSSWW
WWSSSYYSSSWW
WWSSSSSSSSWW
WWSSSSSSSSWW
WWSSSSSSSSWW
WWSWWWWWWSWW
WWSSSSSSSSWW
.WWWWWWWWWW.`;
const LEG_CARD = { W: P.uiReward, S: '#221E3D', Y: '#FFE9A8' };
const SHIELD = `
.CCCCCCC.
CCCCCCCCC
CCCCCCCCC
CCCCCCCCC
.CCCCCCC.
..CCCCC..
...CCC...
....C....`;
const RIFLE_ICON = `
..............GGGG.......
.....GGGGGGGGGGGGGGGGGGGG
GGGGGGGGGGGGGGGGGGGGGGGGG
GGGGGGGPPPPPPGGGGGGGGGGGG
.....GGGGGGG.....GGGG....
....GGGG.........GGG.....
....GGG..................`;
const LEG_RIFLE = { G: '#8FA8C4', P: P.self };
const CROSS = `
....C....
....C....
.........
.........
C.......C
.........
.........
....C....
....C....`;

// 1P 복셀 라이플 — 뒤에서 본 시점(총구 위·개머리판 아래), 26×34
const GUN = `
...........LLLL...........
...........GGGG...........
..........LGGGGL..........
..........GGGGGG..........
..........GGGGGG..........
.........LGGGGGGL.........
.........GGEEEEGG.........
.........GGEEEEGG.........
.........GGGGGGGG.........
.........GGGGGGGG.........
........LGGGGGGGGL........
........GGGGGGGGGG........
........GGDDDDDDGG........
........GGDDDDDDGG........
........GGGGGGGGGG........
.......LGGGGGGGGGGL.......
.......GGGGGGGGGGGG.......
.......GGGEEEEEEGGG.......
.......GGGEEEEEEGGG.......
.......GGGGGGGGGGGG.......
......LGGGGGGGGGGGGL......
......GGGGGGGGGGGGGG......
......GGGGDDDDDDGGGG......
......GGGGDAAAADGGGG......
......GGGGDDDDDDGGGG......
......GGGGGGGGGGGGGG......
.....LGGGGGGGGGGGGGGL.....
.....GGGGGGGGGGGGGGGG.....
....GGGGGGGGGGGGGGGGGG....
...GGGGGGGGGGGGGGGGGGGG...
..GGGGGGGGGGGGGGGGGGGGGG..
.GGGGGGGGGGGGGGGGGGGGGGGG.
GGGGGGGGGGGGGGGGGGGGGGGGGG
DDDDDDDDDDDDDDDDDDDDDDDDDD`;
const LEG_GUN = { L: P.gunLight, G: P.gun, D: P.gunDark, E: P.self, A: P.allyPing };
const FLASH = `
.....F.....
...F.F.F...
....FFF....
.F.FFfFF.F.
..FFfffFF..
FFFfffffFFF
..FFfffFF..
.F.FFfFF.F.
....FFF....
...F.F.F...
.....F.....`;
const LEG_FLASH = { F: P.flash2, f: P.flash };

// ---------- 픽셀 패럴랙스 배경(경계 밖) — 결정적 의사난수, 단색 실루엣만 ----------
function parallax(W, horizon, seed = 0, opts = {}) {
  const { starColor = P.side, farColor = P.plumDark, nearColor = '#3E2244', blockColor = P.plumDark, dense = 1, stars = 1 } = opts;
  let r = 1234 + seed * 7919; const rnd = () => { r = (r * 1103515245 + 12345) & 0x7fffffff; return r / 0x7fffffff; };
  let s = '';
  // 픽셀 별(2px)
  s += `<g fill="${starColor}">`;
  for (let i = 0; i < 70 * stars; i++) { const x = Math.floor(rnd() * W / 4) * 4, y = Math.floor(rnd() * (horizon - 120) / 4) * 4; s += `<rect x="${x}" y="${y}" width="${rnd() < 0.2 ? 4 : 2}" height="${2}"></rect>`; }
  s += `</g>`;
  // 먼 스카이라인(계단 실루엣, 8px 격자)
  s += `<g fill="${farColor}">`;
  for (let x = 0; x < W;) { const w = 24 + Math.floor(rnd() * 8) * 8, hgt = 40 + Math.floor(rnd() * 12 * dense) * 8; s += `<rect x="${x}" y="${horizon - hgt}" width="${w}" height="${hgt}"></rect>`; if (rnd() < 0.4) s += `<rect x="${x + 8}" y="${horizon - hgt - 16}" width="${Math.max(8, w - 16)}" height="16"></rect>`; x += w; }
  s += `</g>`;
  // 가까운 스카이라인(더 낮고 더 밝은 자두)
  s += `<g fill="${nearColor}">`;
  for (let x = -16; x < W;) { const w = 40 + Math.floor(rnd() * 10) * 8, hgt = 16 + Math.floor(rnd() * 6 * dense) * 8; s += `<rect x="${x}" y="${horizon - hgt}" width="${w}" height="${hgt}"></rect>`; x += w + Math.floor(rnd() * 4) * 8; }
  s += `</g>`;
  // 부유 픽셀 블록(단색 실루엣, 계단형)
  s += `<g fill="${blockColor}">`;
  for (let i = 0; i < 6 * dense; i++) { const x = Math.floor(rnd() * W / 8) * 8, y = 40 + Math.floor(rnd() * (horizon - 200) / 8) * 8, sz = 16 + Math.floor(rnd() * 4) * 8; s += `<rect x="${x}" y="${y}" width="${sz}" height="${sz}"></rect><rect x="${x + sz}" y="${y + sz / 2}" width="${sz / 2}" height="${sz / 2}"></rect>`; }
  s += `</g>`;
  return s;
}

// ---------- 공통 헤드 ----------
const FONTS = `<link href="https://fonts.googleapis.com/css2?family=Press+Start+2P&amp;family=Noto+Sans+KR:wght@400;500;700&amp;display=swap" rel="stylesheet">`;
const BASE_CSS = `
    body { margin: 0; background: ${P.void}; color: ${P.uiText}; font-family: 'Noto Sans KR', 'Malgun Gothic', system-ui, sans-serif; }
    a { color: ${P.uiOk}; } a:hover { color: ${P.uiReward}; }
    .px { font-family: 'Press Start 2P', 'Courier New', monospace; }
    .sub { color: ${P.uiSub}; }
    svg { display: block; }
`;
function doc(body, extraCss = '', script = '') {
  return `<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <script src="./support.js"></script>
</head>
<body>
<x-dc>
<helmet>
  ${FONTS}
  <style>${BASE_CSS}${extraCss}</style>
</helmet>
${body}
</x-dc>${script}
</body>
</html>`;
}
const h = (tag, style, inner = '', attrs = '') => `<${tag} style="${style}"${attrs ? ' ' + attrs : ''}>${inner}</${tag}>`;
const T = {
  h1: (t) => h('div', `font-family: 'Press Start 2P', monospace; font-size: 22px; line-height: 34px; color: ${P.uiText}; letter-spacing: 1px`, t),
  h2: (t) => h('div', `font-family: 'Press Start 2P', monospace; font-size: 12px; line-height: 20px; color: ${P.uiOk}; letter-spacing: 1px`, t),
  kr: (t, extra = '') => h('div', `font-size: 15px; line-height: 24px; color: ${P.uiText}; ${extra}`, t),
  sub: (t, extra = '') => h('div', `font-size: 13px; line-height: 20px; color: ${P.uiSub}; ${extra}`, t),
  tag: (t, c = P.uiOk) => h('span', `display: inline-block; padding: 4px 8px; border: 2px solid ${c}; color: ${c}; font-family: 'Press Start 2P', monospace; font-size: 9px; line-height: 12px`, t),
};
const panel = (inner, w = 'auto', extra = '') => h('div', `display: flex; flex-direction: column; gap: 12px; padding: 20px; background: ${P.floorDark}; border: 2px solid ${P.side}; width: ${w}; box-sizing: border-box; ${extra}`, inner);
const swatch = (hex, label, note = '', big = false) => h('div', `display: flex; flex-direction: column; gap: 6px; width: ${big ? 132 : 108}px`,
  h('div', `height: ${big ? 64 : 44}px; background: ${hex}; border: 2px solid ${P.side}`) +
  h('div', `font-family: 'Press Start 2P', monospace; font-size: 8px; line-height: 12px; color: ${P.uiSub}`, hex) +
  h('div', `font-size: 12px; line-height: 17px; color: ${P.uiText}`, label) +
  (note ? h('div', `font-size: 11px; line-height: 15px; color: ${P.uiSub}`, note) : ''));

// =====================================================================
// 1. Main — 키 비주얼 1인칭 프레임 1440×810
// =====================================================================
function mainBoard() {
  const W = 1440, H = 810, VX = 720, VY = 380; // 소실점
  let s = '';
  // 보이드 + 경계 밖 = 픽셀 패럴랙스 배경(레트로 게임 배경 레이어: 픽셀 별 · 먼 스카이라인 실루엣 · 부유 픽셀 블록). 전부 단색 실루엣, 선 없음
  s += `<rect width="${W}" height="${H}" fill="${P.void}"></rect>`;
  s += parallax(W, VY - 60, 0);
  s += `<rect x="0" y="${VY - 60}" width="${W}" height="60" fill="${P.floor}"></rect>`;
  // 경계벽(먼 곳) — 어두운 면 + 상단 엣지 1줄
  s += `<rect x="0" y="${VY - 62}" width="${W}" height="4" fill="${P.top}"></rect>`;
  // 벽면 복셀 격자선(5cm 격자 = 벽에선 촘촘한 어두운 선)
  s += `<g stroke="${P.side}" stroke-width="1" opacity="0.9">`;
  for (let x = 0; x <= W; x += 36) s += `<line x1="${x}" y1="${VY - 60}" x2="${x}" y2="${VY}"></line>`;
  s += `<line x1="0" y1="${VY - 30}" x2="${W}" y2="${VY - 30}"></line></g>`;
  // 바닥
  s += `<rect x="0" y="${VY}" width="${W}" height="${H - VY}" fill="${P.floorDark}"></rect>`;
  // 바닥 격자(원근) — 환경 최하 대역
  s += `<g stroke="${P.floor}" stroke-width="2">`;
  for (let i = -14; i <= 14; i++) { const xb = VX + i * 160; s += `<line x1="${VX + i * 12}" y1="${VY}" x2="${xb}" y2="${H}"></line>`; }
  [400, 428, 470, 530, 615, 730, 810].forEach(y => { s += `<line x1="0" y1="${y}" x2="${W}" y2="${y}"></line>`; });
  s += `</g>`;
  // L2 배선(통행 가능 = 밝은 라인) + 비아
  s += `<g fill="none" stroke-linecap="square">`;
  s += `<path d="M${VX - 6} ${VY + 8} L 560 ${H}" stroke="${P.wireWide}" stroke-width="10"></path>`;
  s += `<path d="M${VX + 6} ${VY + 8} L 1120 ${H}" stroke="${P.wire}" stroke-width="7"></path>`;
  s += `<path d="M ${VX - 40} ${VY + 60} L 240 ${VY + 60} M ${VX + 30} ${VY + 130} L 1260 ${VY + 130}" stroke="${P.wireFar}" stroke-width="5"></path>`;
  s += `</g>`;
  [[240, VY + 60], [VX - 40, VY + 60], [1260, VY + 130], [VX + 30, VY + 130], [640, 560]].forEach(([x, y]) => { s += `<rect x="${x - 7}" y="${y - 7}" width="14" height="14" fill="${P.via}"></rect>`; });
  // 블로커(복셀 면) — 좌
  const block = (x, y, w, hgt, depth) => {
    let b = `<rect x="${x}" y="${y}" width="${w}" height="${hgt}" fill="${P.side}"></rect>`;
    b += `<polygon points="${x},${y} ${x + depth},${y - depth * 0.55} ${x + w + depth},${y - depth * 0.55} ${x + w},${y}" fill="${P.top}"></polygon>`;
    b += `<polygon points="${x + w},${y} ${x + w + depth},${y - depth * 0.55} ${x + w + depth},${y + hgt - depth * 0.55} ${x + w},${y + hgt}" fill="${P.floor}"></polygon>`;
    // 복셀 격자선
    b += `<g stroke="${P.floorDark}" stroke-width="1" opacity="0.8">`;
    for (let gx = x + 20; gx < x + w; gx += 20) b += `<line x1="${gx}" y1="${y}" x2="${gx}" y2="${y + hgt}"></line>`;
    for (let gy = y + 20; gy < y + hgt; gy += 20) b += `<line x1="${x}" y1="${gy}" x2="${x + w}" y2="${gy}"></line>`;
    b += `</g>`;
    // 상단 하이라이트 엣지(얇게)
    b += `<rect x="${x}" y="${y}" width="${w}" height="3" fill="${P.top}"></rect>`;
    return b;
  };
  s += block(60, 330, 260, 300, 40);
  s += block(1180, 350, 220, 200, 34);
  s += block(880, 372, 90, 60, 14);
  // 파괴 가능 블록(작은 연두 코어) — 중앙 우
  s += block(980, 400, 120, 110, 22);
  s += `<rect x="1026" y="436" width="28" height="28" fill="${P.destr}"></rect><rect x="1034" y="444" width="12" height="12" fill="${P.destrHot}"></rect>`;
  s += `<rect x="1016" y="426" width="48" height="48" fill="none" stroke="${P.destrFar}" stroke-width="2" opacity="0.7"></rect>`;
  // 아군(청록 아웃라인 실루엣) — 중거리 좌
  const ally = (x, y, sc) => {
    const m = `
...HH...
..HHHH..
...HH...
.HHHHHH.
HHHHHHHH
H.HHHH.H
..HHHH..
..H..H..
..H..H..
.HH..HH.`;
    return `<g>${px(m, { H: '#0F1C2A' }, sc, x, y)}${px(m, { H: P.ally }, sc, x, y, ' opacity="0.0"')}<g stroke="${P.ally}" stroke-width="2" fill="none"><rect x="${x - 2}" y="${y - 2}" width="${8 * sc + 4}" height="${10 * sc + 4}" opacity="0"></rect></g></g>`
      + `<g>${px(m, { H: P.ally }, sc, x, y)}${px(m, { H: '#123044' }, sc - 2, x + 1, y + 1)}</g>`
      + `<text x="${x + 4 * sc}" y="${y - 10}" text-anchor="middle" fill="${P.ally}" font-family="'Press Start 2P', monospace" font-size="10">P2</text>`
      + `<rect x="${x - 6}" y="${y - 8}" width="${8 * sc + 12}" height="4" fill="${P.ally}" opacity="0.85"></rect>`;
  };
  s += ally(430, 348, 6);
  // 적 스웜 — 원거리 작은 것부터
  const [cw, chh] = dims(CHOMPER_OPEN);
  const chomp = (x, y, c, open = true, elite = false) => `<g>${px(open ? CHOMPER_OPEN : CHOMPER_CLOSED, elite ? LEG_CH_ELITE : LEG_CH, c, x, y)}` +
    `<rect x="${x - 1}" y="${y + 4 * c}" width="1" height="${(chh - 4) * c}" fill="${elite ? P.elite : P.enemyRim}"></rect><rect x="${x + cw * c}" y="${y + 4 * c}" width="1" height="${(chh - 4) * c}" fill="${elite ? P.elite : P.enemyRim}"></rect></g>`;
  // 원거리 열
  [[560, 356], [610, 352], [790, 350], [830, 356], [930, 352], [700, 350]].forEach(([x, y]) => { s += chomp(x, y, 2, false); });
  [[660, 372], [880, 380], [520, 384]].forEach(([x, y]) => { s += chomp(x, y, 3, x === 880); });
  s += chomp(760, 400, 5, true);
  s += chomp(1230, 480, 7, true, true);
  s += chomp(330, 460, 8, true);
  s += chomp(560, 520, 11, true);
  // 적 림(가까운 것만 1px 발광 테두리 강조) — 가장 가까운 개체 눈 위치에 텔레그래프 코어 글로우
  s += `<rect x="${560 + 7 * 11}" y="${520 + 11 * 11}" width="22" height="22" fill="${P.enemyTel}" opacity="0.35"></rect>`;
  // 픽업 — 코인·별
  s += px(COIN, LEG_COIN, 5, 610, 640);
  s += px(COIN, LEG_COIN, 3, 470, 600);
  s += px(STAR, { Y: P.uiReward }, 3, 1160, 610);
  // 1P 총(우하단) + 총구 화염(픽셀 스프라이트)
  s += px(GUN, LEG_GUN, 12, 860, 440);
  s += px(FLASH, LEG_FLASH, 9, 968, 372);
  // HUD
  const hud = `<g font-family="'Press Start 2P', monospace" fill="${P.uiText}">
    <text x="${VX}" y="44" text-anchor="middle" font-size="14">STAGE 2</text>
    <text x="${VX}" y="66" text-anchor="middle" font-size="9" fill="${P.uiSub}">WAVE 07   04:12</text>
    <text x="40" y="44" font-size="8" fill="${P.ally}">P2</text><rect x="70" y="36" width="120" height="8" fill="${P.floor}"></rect><rect x="70" y="36" width="88" height="8" fill="${P.ally}"></rect>
    <text x="40" y="64" font-size="8" fill="${P.ally}">P3</text><rect x="70" y="56" width="120" height="8" fill="${P.floor}"></rect><rect x="70" y="56" width="40" height="8" fill="${P.uiWarn}"></rect>
    <text x="40" y="84" font-size="8" fill="${P.ally}">P4</text><rect x="70" y="76" width="120" height="8" fill="${P.floor}"></rect><rect x="70" y="76" width="112" height="8" fill="${P.ally}"></rect>
    <g transform="translate(40 700)">
      <text x="0" y="0" font-size="8" fill="${P.uiSub}">SHIELD</text>
      ${Array.from({ length: 10 }, (_, i) => `<rect x="${i * 26}" y="8" width="22" height="14" fill="${i < 6 ? P.uiOk : P.floor}"></rect>`).join('')}
      <text x="0" y="44" font-size="8" fill="${P.uiSub}">HP</text>
      ${Array.from({ length: 10 }, (_, i) => `<rect x="${i * 26}" y="52" width="22" height="18" fill="${i < 8 ? P.uiText : P.floor}"></rect>`).join('')}
    </g>
    <g transform="translate(1160 690)">
      ${px(RIFLE_ICON, LEG_RIFLE, 3, 0, 0)}
      <text x="238" y="66" text-anchor="end" font-size="30">24</text>
      <text x="238" y="86" text-anchor="end" font-size="10" fill="${P.uiSub}">/ 30</text>
      <rect x="0" y="40" width="10" height="10" fill="${P.uiOk}"></rect><rect x="16" y="40" width="10" height="10" fill="${P.side}"></rect><rect x="32" y="40" width="10" height="10" fill="${P.side}"></rect>
    </g>
    <g transform="translate(1210 640)"><rect x="0" y="0" width="14" height="14" fill="${P.uiReward}"></rect><text x="22" y="12" font-size="9" fill="${P.uiReward}">+1 CARD</text></g>
  </g>`;
  s += hud;
  // 크로스헤어(절차 SDF 4선)
  s += `<g fill="${P.uiText}"><rect x="${VX - 1}" y="${VY + 10 - 18}" width="3" height="10"></rect><rect x="${VX - 1}" y="${VY + 10 + 8}" width="3" height="10"></rect><rect x="${VX - 18}" y="${VY + 9}" width="10" height="3"></rect><rect x="${VX + 8}" y="${VY + 9}" width="10" height="3"></rect></g>`;
  // 스캔라인 + 비네트(PP_Arcade 재현: 4px 주기 · 0.15)
  s += `<defs><pattern id="scan" width="4" height="4" patternUnits="userSpaceOnUse"><rect width="4" height="2" fill="#000" opacity="0.15"></rect></pattern><radialGradient id="vig" cx="50%" cy="50%" r="72%"><stop offset="60%" stop-color="#000" stop-opacity="0"></stop><stop offset="100%" stop-color="#000" stop-opacity="0.55"></stop></radialGradient></defs>`;
  s += `<rect width="${W}" height="${H}" fill="url(#scan)"></rect><rect width="${W}" height="${H}" fill="url(#vig)"></rect>`;
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}" shape-rendering="crispEdges">${s}</svg>`;
  return doc(h('div', `width: ${W}px; height: ${H}px; background: ${P.void}; overflow: hidden`, svg));
}

// =====================================================================
// 2. Palette — 레퍼런스 → 대역 재매핑
// =====================================================================
function paletteBoard() {
  const refRow = (refHex, refName, arrowTo) => h('div', `display: flex; flex-direction: row; gap: 18px; align-items: flex-start`,
    h('div', `display: flex; flex-direction: column; gap: 6px; width: 120px`,
      h('div', `height: 56px; background: ${refHex}; border: 2px solid ${P.side}`) +
      h('div', `font-size: 12px; line-height: 17px; color: ${P.uiText}`, refName) +
      h('div', `font-size: 11px; line-height: 15px; color: ${P.uiSub}`, '레퍼런스 원색')) +
    h('div', `width: 40px; padding-top: 20px; font-family: 'Press Start 2P', monospace; font-size: 12px; color: ${P.uiSub}`, '&gt;') +
    h('div', `display: flex; flex-direction: row; gap: 14px; flex-wrap: wrap`, arrowTo.map(a => swatch(a[0], a[1], a[2])).join('')));
  const left = panel(
    T.h2('REFERENCE HUE  &gt;  BAND REMAP') +
    T.kr('롤 아케이드 스킨은 아래 색을 배경 전면에 최대 채도로 쓴다. 이 게임에서는 <b>색상(hue)만 가져오고 명도·채도를 A-1 대역에 다시 매핑</b>한다. 뜨거운 색은 적 예약 대역이라 환경에서 쫓아낸다.') +
    refRow('#FF7A1A', '주황 (스킨 배경·에너지)', [[P.enemyTel, '적 공격 텔레그래프', '🔒 A-3-4 적 대역'], [P.uiReward, 'UI 보상·카드', '스크린 공간에서만']]) +
    refRow('#FF2FA8', '마젠타 (픽셀 파티클)', [[P.elite, '엘리트·보스', '🔒 A-3-4'], [P.plum, '경계벽 밖 배경', '플레이 공간 금지'], [P.plumDark, '바닥 실크스크린', '≤45cm 평면 장식']]) +
    refRow('#39FF6A', '연두 (에너지·콘솔 LED)', [[P.destr, '파괴 가능 코어', '작게·발광만'], [P.destrHot, '파괴 임계 직전', '펄스 상한']]) +
    refRow('#2FE8FF', '시안 (전자·홀로)', [[P.ally, '아군 아웃라인', '🔒 A-3-5'], [P.wireWide, '넓은 통로 배선', '환경 상단'], [P.via, '정션·UI 긍정', '']]) +
    refRow('#7A3CFF', '보라 (하늘·그림자)', [[P.floor, '기판 바닥', '화면 70%'], [P.side, '블로커 측면', ''], [P.self, '자기 자신', '아군과 구분']]) +
    refRow('#FFD23F', '금색 (코인·별)', [[P.uiReward, '코인·별·카드 픽업', '월드에선 작게'], ['#B8801E', '코인 음영', '']]),
    900);

  // 대역 사다리(명도 V 0-100)
  const band = (name, v0, v1, s0, s1, sw, note) => h('div', `display: flex; flex-direction: row; gap: 14px; align-items: center`,
    h('div', `width: 96px; font-size: 12px; line-height: 16px; color: ${P.uiText}`, name) +
    h('div', `position: relative; width: 300px; height: 22px; background: ${P.floor}; border: 2px solid ${P.side}`,
      h('div', `position: absolute; left: ${v0}%; width: ${v1 - v0}%; top: 0; bottom: 0; background: ${sw}`)) +
    h('div', `width: 70px; font-family: 'Press Start 2P', monospace; font-size: 8px; color: ${P.uiSub}`, `V ${v0}-${v1}`) +
    h('div', `width: 70px; font-family: 'Press Start 2P', monospace; font-size: 8px; color: ${P.uiSub}`, `S ${s0}-${s1}`) +
    h('div', `font-size: 11px; line-height: 15px; color: ${P.uiSub}; width: 150px`, note));
  const right = panel(
    T.h2('A-1 VALUE LADDER') +
    T.sub('가로축 = 명도 V 0→100. 대역이 넓을수록 시선을 끈다. 0·100 은 어디에도 없다.') +
    band('환경', 6, 40, 15, 40, P.side, '바닥·블로커·벽 — 화면 70%+') +
    band('L2 배선', 35, 60, 45, 70, P.wire, '통행 가능 = 밝은 선') +
    band('캐릭터', 25, 70, 30, 60, P.enemyBody, '적 몸통·1P 총') +
    band('VFX', 15, 95, 20, 90, P.enemyTel, '가장 넓다 — 헤드룸') +
    band('UI', 65, 95, 35, 80, P.uiText, '스크린 공간') +
    h('div', `height: 2px; background: ${P.side}; margin: 6px 0`) +
    T.h2('SCREEN AREA BUDGET') +
    h('div', `display: flex; flex-direction: row; height: 34px; border: 2px solid ${P.side}`,
      h('div', `width: 70%; background: ${P.floor}; display: flex; align-items: center; justify-content: center; font-size: 11px; color: ${P.uiSub}`, '환경 substrate 70%') +
      h('div', `width: 12%; background: ${P.wire}; display: flex; align-items: center; justify-content: center; font-size: 10px; color: ${P.void}`, '배선') +
      h('div', `width: 10%; background: ${P.enemyBody}; display: flex; align-items: center; justify-content: center; font-size: 10px; color: ${P.uiText}`, '적') +
      h('div', `width: 5%; background: ${P.enemyTel}`) +
      h('div', `width: 3%; background: ${P.uiReward}`)) +
    T.sub('팝한 색(주황·금·연두·마젠타)은 합쳐서 <b>화면의 10% 미만</b>. 어두운 무대 위에 액센트로만 얹는다 — 사용자 결정 2026-09-06.') +
    h('div', `height: 2px; background: ${P.side}; margin: 6px 0`) +
    T.h2('LOCKED') +
    h('div', `display: flex; flex-direction: row; gap: 10px; flex-wrap: wrap`,
      T.tag('ENEMY = HOT', P.enemyRim) + T.tag('ALLY = COLD', P.ally) + T.tag('NO 0% / 100%', P.uiSub) + T.tag('DESTRUCTIBLE = LIME', P.destr)),
    480);
  const body = h('div', `display: flex; flex-direction: column; gap: 20px; padding: 32px; width: 1440px; box-sizing: border-box; background: ${P.void}`,
    h('div', `display: flex; flex-direction: row; gap: 20px; align-items: baseline`, T.h1('PALETTE REMAP') + T.sub('ArtDirection.md §A-1 · §A-3 — 수치·hue 무변. 이 시트는 레퍼런스가 어느 칸으로 가는지만 정한다.')) +
    h('div', `display: flex; flex-direction: row; gap: 20px; align-items: flex-start`, left + right));
  return doc(body);
}

// =====================================================================
// 3. Form — 형태 언어
// =====================================================================
function formBoard() {
  // 격자 계층 시각화: 1m 자 위에 5 / 7.5 / 2.5cm 눈금
  const ruler = (cm, color, label, note) => {
    const n = Math.round(100 / cm); const cw = 360 / n;
    let ticks = '';
    for (let i = 0; i < n; i++) ticks += `<rect x="${i * cw}" y="0" width="${Math.max(1, cw - 1)}" height="22" fill="${i % 2 ? color : P.floor}"></rect>`;
    return h('div', `display: flex; flex-direction: row; gap: 16px; align-items: center`,
      h('div', `width: 110px; font-family: 'Press Start 2P', monospace; font-size: 10px; line-height: 14px; color: ${color}`, label) +
      `<svg width="360" height="22" viewBox="0 0 360 22" shape-rendering="crispEdges">${ticks}</svg>` +
      h('div', `font-size: 12px; line-height: 17px; color: ${P.uiSub}; width: 330px`, note));
  };
  const grid = panel(
    T.h2('VOXEL GRID HIERARCHY  (1 m)') +
    ruler(5, P.wireWide, 'ENV 5cm', '환경(블로커·벽·프롭). 콜리전 셀 100cm·문턱 45cm·60cm 이 <b>전부 정수배</b>(20·9·12). 면은 평평, 격자는 머티리얼 선으로.') +
    ruler(7.5, P.enemyRim, 'ENEMY 7.5cm', '적·픽업(쩝쩝이). 시트 스프라이트 18층 = 135cm. 실루엣이 먼 거리에서도 덩어리로 읽히는 굵기.') +
    ruler(2.5, P.self, '1P GUN 2.5cm', '1인칭 총. 카메라 30~60cm 앞이라 적 격자의 1/3. 화면상 픽셀 크기는 적과 비슷해진다.') +
    T.sub('규칙 ① 한 오브젝트 안에서 격자는 하나(믹셀 금지). ② 클래스 사이 격자 차이는 허용 — 거리가 화면 픽셀 크기를 맞춰 준다. ③ 수치는 제안 초기값이며 PIE 육안으로 조정한다.'),
    '100%');

  // 선 vs 면
  const lineface = panel(
    T.h2('FACE INSIDE  ·  PARALLAX OUTSIDE') +
    `<svg width="600" height="230" viewBox="0 0 600 230" shape-rendering="crispEdges">
      <rect width="600" height="230" fill="${P.void}"></rect>
      <g>${parallax(600, 215, 3, { dense: 0.8 })}</g>
      <rect x="160" y="10" width="14" height="210" fill="${P.floor}"></rect><rect x="160" y="10" width="14" height="3" fill="${P.top}"></rect>
      <rect x="426" y="10" width="14" height="210" fill="${P.floor}"></rect><rect x="426" y="10" width="14" height="3" fill="${P.top}"></rect>
      <rect x="174" y="10" width="252" height="210" fill="${P.floorDark}"></rect>
      <g stroke="${P.floor}" stroke-width="1">${Array.from({ length: 12 }, (_, i) => `<line x1="${174 + i * 21}" y1="10" x2="${174 + i * 21}" y2="220"></line>`).join('')}${Array.from({ length: 10 }, (_, i) => `<line x1="174" y1="${10 + i * 21}" x2="426" y2="${10 + i * 21}"></line>`).join('')}</g>
      <path d="M174 130 H 426" stroke="${P.wire}" stroke-width="5"></path><rect x="294" y="124" width="12" height="12" fill="${P.via}"></rect>
      <rect x="210" y="60" width="70" height="52" fill="${P.side}"></rect><polygon points="210,60 222,52 292,52 280,60" fill="${P.top}"></polygon><polygon points="280,60 292,52 292,104 280,112" fill="${P.floor}"></polygon>
      <g stroke="${P.floorDark}" stroke-width="1"><line x1="228" y1="60" x2="228" y2="112"></line><line x1="246" y1="60" x2="246" y2="112"></line><line x1="264" y1="60" x2="264" y2="112"></line><line x1="210" y1="78" x2="280" y2="78"></line><line x1="210" y1="96" x2="280" y2="96"></line></g>
      <rect x="340" y="150" width="46" height="46" fill="${P.side}"></rect><rect x="356" y="166" width="14" height="14" fill="${P.destr}"></rect>
      ${px(CHOMPER_CLOSED, LEG_CH, 3, 360, 60)}
      <rect x="0" y="215" width="600" height="15" fill="${P.void}"></rect>
      <g font-family="'Press Start 2P', monospace" font-size="8" fill="${P.uiSub}"><text x="14" y="226">OUTSIDE = PARALLAX</text><text x="200" y="226">PLAY SPACE = VOXEL FACE</text><text x="452" y="226">OUTSIDE = PARALLAX</text></g>
    </svg>` +
    T.kr('<b>플레이 공간 = 면.</b> 복셀 평면 채색 + 어두운 격자선(같은 대역 안, 명도차 작게). 통행 정보는 오직 <b>바닥 배선(밝은 선)</b>이 든다 — 사용자 결정 2026-09-06 "배선은 유지".') +
    T.kr('<b>경계벽 밖 = 픽셀 패럴랙스 배경.</b> 레트로 게임 배경 레이어 — 픽셀 별 · 먼 스카이라인 · 부유 픽셀 블록, <b>전부 단색 실루엣</b>(선·와이어프레임·회로 없음). 마젠타는 여기서만. 구역마다 레이어를 바꿔 무드 변주. 콜리전 없음.'),
    620);

  // 실루엣 3분류
  const sil = (map, leg, c, name, note) => h('div', `display: flex; flex-direction: column; gap: 8px; align-items: center; width: 150px`,
    `<svg width="130" height="120" viewBox="0 0 130 120" shape-rendering="crispEdges"><rect width="130" height="120" fill="${P.floorDark}"></rect>${px(map, leg, c, (130 - dims(map)[0] * c) / 2, (120 - dims(map)[1] * c) / 2)}</svg>` +
    h('div', `font-family: 'Press Start 2P', monospace; font-size: 9px; line-height: 13px; color: ${P.enemyRim}; text-align: center`, name) +
    h('div', `font-size: 11px; line-height: 15px; color: ${P.uiSub}; text-align: center`, note));
  const silhouette = panel(
    T.h2('SILHOUETTE = 3 FAMILIES') +
    h('div', `display: flex; flex-direction: row; gap: 14px; justify-content: space-between`,
      sil(CHOMPER_OPEN, LEG_CH, 5, 'ROUND', '구 — 유령·쩝쩝이. 기본 일반') +
      sil(BEETLE, LEG_CH, 5, 'BLOCK', '각 — 비틀·탱커. 느리고 단단') +
      sil(SPIKE, LEG_CH, 5, 'SPIKE', '침 — 글리치. 빠르고 얇음')) +
    T.kr('덩어리 실루엣이 먼저, 디테일은 <b>복셀 1칸</b> 단위로만. 읽힘점 = <b>발광 코어 1개</b>(공격 텔레그래프 색 `#FF6B2C`). 엘리트는 같은 실루엣에 크기 ×1.3 + 림 `#FF1E7A`.') +
    T.kr('<b>텍스처 없음.</b> 색 = 요소ID LUT(쩝쩝이 12요소 방식) + 격자선 + 이미시브. "복셀 1칸 = 색 1개." 사진·노이즈 텍스처는 금지(§A-5).'),
    '100%');
  const body = h('div', `display: flex; flex-direction: column; gap: 20px; padding: 32px; width: 1440px; box-sizing: border-box; background: ${P.void}`,
    h('div', `display: flex; flex-direction: row; gap: 20px; align-items: baseline`, T.h1('FORM LANGUAGE') + T.sub('복셀 = 3D 픽셀. 격자·선/면·실루엣·텍셀 네 규칙.')) +
    grid +
    h('div', `display: flex; flex-direction: row; gap: 20px; align-items: stretch`, lineface + h('div', `flex-grow: 1`, silhouette)));
  return doc(body);
}

// =====================================================================
// 4. Motifs — 모티프 + 로그라이트 매핑
// =====================================================================
function motifsBoard() {
  const icon = (map, leg, c, name, maps) => h('div', `display: flex; flex-direction: row; gap: 14px; align-items: center; padding: 12px; background: ${P.floorDark}; border: 2px solid ${P.side}`,
    `<svg width="72" height="72" viewBox="0 0 72 72" shape-rendering="crispEdges"><rect width="72" height="72" fill="${P.floor}"></rect>${px(map, leg, c, (72 - dims(map)[0] * c) / 2, (72 - dims(map)[1] * c) / 2)}</svg>` +
    h('div', `display: flex; flex-direction: column; gap: 4px; width: 250px`,
      h('div', `font-family: 'Press Start 2P', monospace; font-size: 9px; line-height: 13px; color: ${P.uiReward}`, name) +
      h('div', `font-size: 12px; line-height: 17px; color: ${P.uiText}`, maps)));
  const icons = h('div', `display: grid; grid-template-columns: repeat(3, minmax(0, 1fr)); gap: 12px`,
    icon(COIN, LEG_COIN, 6, 'COIN', 'XP·재화 픽업. 월드에선 8px 스프라이트 크기, 자석 흡수') +
    icon(STAR, { Y: P.uiReward }, 6, 'STAR', '카드 희귀도 표시(★1~3). 레벨업 3택 상단') +
    icon(HEART, { C: P.uiOk }, 7, '1UP', 'DBNO 부활 완료 · 회복 픽업. 차가운 쪽(아군 계열)') +
    icon(CARD, LEG_CARD, 5, 'POWER-UP CARD', '레벨업 카드 = 아케이드 파워업 캡슐. 금색 테두리') +
    icon(CABINET, LEG_CAB, 5, 'CABINET', '억제기 = 스테이지 보스 캐비닛. 화면이 얼굴, 부수면 STAGE CLEAR') +
    icon(GLITCH, LEG_GLITCH, 6, 'GLITCH', '적 스폰 지점·버그 픽션. 자두색 = 경계 밖 대역') +
    icon(JOY, LEG_JOY, 6, 'JOYSTICK', '핑·상호작용 프롬프트 아이콘') +
    icon(SHIELD, { C: P.uiOk }, 7, 'SHIELD', '실드 세그먼트. 체력 위는 실드(VIT1 2층)') +
    icon(CROSS, { C: P.uiText }, 7, 'CROSSHAIR', '절차 SDF 4선 유지(U12). 픽셀 스냅만 추가'));
  const screen = (title, inner) => h('div', `display: flex; flex-direction: column; gap: 8px`,
    T.h2(title) + h('div', `width: 440px; height: 248px; background: ${P.void}; border: 2px solid ${P.side}; position: relative; overflow: hidden`, inner));
  const scan = `<div style="position: absolute; left: 0; top: 0; right: 0; bottom: 0; background: repeating-linear-gradient(#000 0 2px, transparent 2px 4px); opacity: 0.15"></div>`;
  const stageClear = screen('STAGE CLEAR  (스테이지 전환 = Ultimate)',
    h('div', `position: absolute; left: 0; right: 0; top: 70px; text-align: center; font-family: 'Press Start 2P', monospace; font-size: 26px; color: ${P.destrHot}`, 'STAGE CLEAR') +
    h('div', `position: absolute; left: 0; right: 0; top: 122px; text-align: center; font-family: 'Press Start 2P', monospace; font-size: 10px; color: ${P.uiSub}`, 'BONUS  x4 PLAYERS   +2000') +
    h('div', `position: absolute; left: 0; right: 0; top: 176px; text-align: center; font-family: 'Press Start 2P', monospace; font-size: 10px; color: ${P.uiOk}`, 'NEXT: STAGE 3') + scan);
  const cont = screen('CONTINUE?  (DBNO 협동 부활 창)',
    h('div', `position: absolute; left: 0; right: 0; top: 60px; text-align: center; font-family: 'Press Start 2P', monospace; font-size: 22px; color: ${P.uiText}`, 'CONTINUE?') +
    h('div', `position: absolute; left: 0; right: 0; top: 104px; text-align: center; font-family: 'Press Start 2P', monospace; font-size: 46px; color: ${P.uiWarn}`, '7') +
    h('div', `position: absolute; left: 0; right: 0; top: 180px; text-align: center; font-family: 'Press Start 2P', monospace; font-size: 9px; color: ${P.ally}`, 'P3 IS COMING  ·  HOLD [E] TO REVIVE') + scan);
  const insert = screen('INSERT COIN  (로비·대기)',
    h('div', `position: absolute; left: 0; right: 0; top: 40px; text-align: center; font-family: 'Press Start 2P', monospace; font-size: 18px; color: ${P.uiText}`, 'FPSROGUELITE') +
    h('div', `position: absolute; left: 0; right: 0; top: 120px; text-align: center; font-family: 'Press Start 2P', monospace; font-size: 12px; color: ${P.uiReward}`, 'INSERT COIN') +
    h('div', `position: absolute; left: 0; right: 0; top: 160px; text-align: center; font-family: 'Press Start 2P', monospace; font-size: 8px; color: ${P.uiSub}`, 'P1 READY   P2 READY   P3 ---   P4 ---') +
    h('div', `position: absolute; left: 0; right: 0; top: 206px; text-align: center; font-family: 'Press Start 2P', monospace; font-size: 8px; color: ${P.uiSub}`, 'CREDIT 03') + scan);
  const fiction = panel(
    T.h2('FICTION HOOK') +
    T.kr('<b>게임 세계 다이브</b>(Concept.md §1-C-9) + <b>"게임 세계를 지키는 수호자"</b>(전자오락수호대 모티프). 플레이어 4인 = 접속한 히어로(차가운 색). 적 = 세계를 갉아먹는 <b>버그·글리치·해적판 몬스터</b>(뜨거운 색). 억제기 = 스테이지를 지배하는 캐비닛 코어.') +
    T.kr('로그라이트 어휘를 아케이드 어휘로 번역한다: 런 = 1코인 플레이 · 레벨업 카드 = 파워업 · 스테이지 전환 = STAGE CLEAR · DBNO = CONTINUE? 카운트다운(동료가 오면 멈춤) · 런 종료 = GAME OVER + 하이스코어.') +
    T.sub('레퍼런스에서 가져오지 않는 것: 롤 캐릭터 디자인·의상·포즈·특정 게임 로고. 웹툰의 캐릭터·서사. 모티프와 색감만.'),
    '100%');
  const body = h('div', `display: flex; flex-direction: column; gap: 20px; padding: 32px; width: 1440px; box-sizing: border-box; background: ${P.void}`,
    h('div', `display: flex; flex-direction: row; gap: 20px; align-items: baseline`, T.h1('MOTIFS  &amp;  ROGUELITE MAP') + T.sub('픽셀 스프라이트 = 8~13px 격자, 포인트 필터, 팔레트 제한. 아이콘은 UI 대역·픽업은 VFX 대역.')) +
    h('div', `display: flex; flex-direction: row; gap: 20px; align-items: flex-start`,
      h('div', `width: 960px`, icons) +
      h('div', `display: flex; flex-direction: column; gap: 16px`, stageClear + cont)) +
    h('div', `display: flex; flex-direction: row; gap: 20px; align-items: flex-start`, h('div', `width: 460px`, insert) + h('div', `flex-grow: 1`, fiction)));
  return doc(body);
}

// =====================================================================
// 5. HUD — 픽셀 HUD 모크 1440×810
// =====================================================================
function hudBoard() {
  const W = 1440, H = 810;
  let s = `<rect width="${W}" height="${H}" fill="${P.void}"></rect><rect x="0" y="380" width="${W}" height="430" fill="${P.floorDark}"></rect>`;
  s += `<g stroke="${P.floor}" stroke-width="2">${Array.from({ length: 29 }, (_, i) => `<line x1="${720 + (i - 14) * 12}" y1="380" x2="${720 + (i - 14) * 160}" y2="810"></line>`).join('')}</g>`;
  s += `<rect x="0" y="320" width="${W}" height="60" fill="${P.floor}"></rect>`;
  s += px(CHOMPER_OPEN, LEG_CH, 6, 900, 330) + px(CHOMPER_CLOSED, LEG_CH, 3, 600, 350) + px(CHOMPER_CLOSED, LEG_CH, 3, 660, 356);
  s += px(GUN, LEG_GUN, 12, 860, 440);
  const seg = (x, y, n, on, c, w = 22, hh = 14, gap = 4) => Array.from({ length: n }, (_, i) => `<rect x="${x + i * (w + gap)}" y="${y}" width="${w}" height="${hh}" fill="${i < on ? c : P.floor}" stroke="${P.side}" stroke-width="1"></rect>`).join('');
  s += `<g font-family="'Press Start 2P', monospace" fill="${P.uiText}">
    <!-- 상단 중앙: 스테이지·웨이브·타이머 -->
    <rect x="600" y="20" width="240" height="60" fill="${P.void}" opacity="0.6"></rect>
    <text x="720" y="46" text-anchor="middle" font-size="16">STAGE 2</text>
    <text x="720" y="70" text-anchor="middle" font-size="9" fill="${P.uiSub}">WAVE 07     04:12</text>
    <!-- 좌상단: 파티 -->
    <g transform="translate(40 36)">
      <text x="0" y="10" font-size="8" fill="${P.ally}">P2  NOVA</text>${seg(110, 0, 8, 6, P.ally, 14, 10, 2)}
      <text x="0" y="34" font-size="8" fill="${P.ally}">P3  BIT</text>${seg(110, 24, 8, 2, P.uiWarn, 14, 10, 2)}<text x="245" y="34" font-size="7" fill="${P.uiWarn}">DOWN 7</text>
      <text x="0" y="58" font-size="8" fill="${P.ally}">P4  RAM</text>${seg(110, 48, 8, 7, P.ally, 14, 10, 2)}
    </g>
    <!-- 좌하단: 실드 / HP -->
    <g transform="translate(40 690)">
      <text x="0" y="0" font-size="8" fill="${P.uiOk}">SHIELD</text>${seg(0, 8, 10, 6, P.uiOk)}
      <text x="0" y="46" font-size="8" fill="${P.uiSub}">HP</text>${seg(0, 54, 10, 8, P.uiText, 22, 18)}
      <text x="270" y="70" font-size="12">80</text>
    </g>
    <!-- 우하단: 무기·탄약·슬롯 -->
    <g transform="translate(1140 680)">
      ${px(RIFLE_ICON, LEG_RIFLE, 3, 0, 0)}
      <text x="258" y="60" text-anchor="end" font-size="34">24</text>
      <text x="258" y="80" text-anchor="end" font-size="10" fill="${P.uiSub}">/ 30   RELOAD [R]</text>
      <g transform="translate(0 44)"><rect x="0" y="0" width="12" height="12" fill="${P.uiOk}"></rect><rect x="18" y="0" width="12" height="12" fill="${P.side}"></rect><rect x="36" y="0" width="12" height="12" fill="${P.side}"></rect><text x="0" y="26" font-size="7" fill="${P.uiSub}">1  2  3</text></g>
    </g>
    <!-- 우상단: 코인·카드 -->
    <g transform="translate(1250 36)">
      ${px(COIN, LEG_COIN, 3, 0, 0)}<text x="34" y="20" font-size="10" fill="${P.uiReward}">1 240</text>
      ${px(STAR, { Y: P.uiReward }, 3, 0, 34)}<text x="34" y="54" font-size="10" fill="${P.uiReward}">LV 6</text>
      <rect x="0" y="66" width="150" height="6" fill="${P.floor}"></rect><rect x="0" y="66" width="96" height="6" fill="${P.uiReward}"></rect>
    </g>
    <!-- 픽업 토스트 -->
    <g transform="translate(640 560)"><text x="0" y="0" font-size="9" fill="${P.uiReward}">+CARD  ★★</text></g>
    <!-- 적 체력바(월드 위 UI = 뜨거운 색 금지 → 흰/회 세그먼트) -->
    <g transform="translate(920 318)">${seg(0, 0, 6, 4, P.uiText, 14, 5, 2)}</g>
    <!-- 억제기 방향 마커 -->
    <g transform="translate(1000 300)"><rect x="0" y="0" width="10" height="10" fill="${P.destr}"></rect><text x="16" y="9" font-size="7" fill="${P.destr}">CORE 62m</text></g>
  </g>`;
  s += `<g fill="${P.uiText}"><rect x="719" y="372" width="3" height="10"></rect><rect x="719" y="398" width="3" height="10"></rect><rect x="702" y="389" width="10" height="3"></rect><rect x="728" y="389" width="10" height="3"></rect></g>`;
  s += `<defs><pattern id="scan2" width="4" height="4" patternUnits="userSpaceOnUse"><rect width="4" height="2" fill="#000" opacity="0.15"></rect></pattern></defs><rect width="${W}" height="${H}" fill="url(#scan2)"></rect>`;
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}" shape-rendering="crispEdges">${s}</svg>`;
  return doc(h('div', `width: ${W}px; height: ${H}px; background: ${P.void}; overflow: hidden`, svg));
}

// =====================================================================
// 6. Lineup — 적 라인업 + 3구역 무드
// =====================================================================
function lineupBoard() {
  // 스케일 라인업: 1px = 1cm → 캐릭터 180cm = 180px. 복셀 7.5cm = 7.5px... 스프라이트 셀 = 7.5px 대신 c=8(≈)
  const cell = 7.5;
  const stage = (map, leg, name, note, c = cell, extra = '') => {
    const [w, hh] = dims(map);
    return h('div', `display: flex; flex-direction: column; gap: 8px; align-items: center`,
      `<svg width="${Math.max(120, w * c + 20)}" height="260" viewBox="0 0 ${Math.max(120, w * c + 20)} 260" shape-rendering="crispEdges">${px(map, leg, c, (Math.max(120, w * c + 20) - w * c) / 2, 250 - hh * c)}${extra}</svg>` +
      h('div', `font-family: 'Press Start 2P', monospace; font-size: 9px; line-height: 13px; color: ${P.uiText}; text-align: center`, name) +
      h('div', `font-size: 11px; line-height: 15px; color: ${P.uiSub}; text-align: center; width: 150px`, note));
  };
  const PLAYER = `
...HHHH...
..HHHHHH..
..HHHHHH..
...HHHH...
.HHHHHHHH.
HHHHHHHHHH
HHHHHHHHHH
H.HHHHHH.H
H.HHHHHH.H
..HHHHHH..
..HHHHHH..
..HH..HH..
..HH..HH..
..HH..HH..
..HH..HH..
..HH..HH..
..HH..HH..
..HH..HH..
..HH..HH..
..HH..HH..
..HH..HH..
..HH..HH..
.HHH..HHH.
.HHH..HHH.`;
  const BOSS = `
..BBBBBBBBBBBBBBBBBBBBBBBBBB..
.BBBBBBBBBBBBBBBBBBBBBBBBBBBB.
BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
BBSSSSSSSSSSSSSSSSSSSSSSSSSSBB
BBSSSSSSSSSSSSSSSSSSSSSSSSSSBB
BBSSSWWWWSSSSSSSSSSSWWWWSSSSBB
BBSSSWPPWSSSSSSSSSSSWPPWSSSSBB
BBSSSWWWWSSSSSSSSSSSWWWWSSSSBB
BBSSSSSSSSSSSSSSSSSSSSSSSSSSBB
BBSSSSSSSSSCCCCCCCCSSSSSSSSSBB
BBSSSSSSSSCCCCCCCCCCSSSSSSSSBB
BBSSSSSSSSSCCCCCCCCSSSSSSSSSBB
BBSSSSSSSSSSSSSSSSSSSSSSSSSSBB
BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
BBBBBBBRRBBBBOOBBBBGGBBBBBBBBB
BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
BBBBBBBBBBBBBBBBBBBBBBBBBBBBBB
.BBB......................BBB.
.BBB......................BBB.`;
  const LEG_BOSS = { B: '#3A2748', S: '#120A1A', W: P.eyeW, P: P.pupil, C: P.elite, R: P.uiWarn, O: P.uiReward, G: P.destr };
  const lineup = panel(
    T.h2('LINEUP  (1px = 1cm)') +
    h('div', `display: flex; flex-direction: row; gap: 24px; align-items: flex-end; justify-content: space-between`,
      stage(PLAYER, { H: P.ally }, 'PLAYER 180', '아군 = 청록 아웃라인. 실루엣만 — 3P 바디는 NEON-V 트랙') +
      stage(CHOMPER_OPEN, LEG_CH, 'CHOMPER 135', '일반 · 구 실루엣 · 18층(7.5cm) · 입 개폐 공격 · 시트 스프라이트 = 메시 정본') +
      stage(BEETLE, LEG_CH, 'BEETLE 90', '일반 · 각 실루엣 · 낮고 넓음 · ≤45cm 넘어감 아님(적)') +
      stage(SPIKE, LEG_CH, 'GLITCH 150', '일반 · 침 실루엣 · 빠름') +
      stage(CHOMPER_OPEN, LEG_CH_ELITE, 'ELITE 175', '×1.3 · 림 #FF1E7A · 코어 2개 · GAS(ADR 0013)', cell * 1.3) +
      stage(BOSS, LEG_BOSS, 'CABINET CORE 240', '억제기/보스 = 캐비닛. 화면 = 얼굴. 파괴 = STAGE CLEAR')) +
    T.sub('라인업의 형태는 방향 제시다. 쩝쩝이 외 3종은 실루엣 패밀리 예시이며 실제 메시는 후속 행에서 저작한다.'),
    '100%');
  const mood = (name, bg, floor, wire, accent, note, extraSvg = '') => h('div', `display: flex; flex-direction: column; gap: 8px; width: 440px`,
    `<svg width="440" height="180" viewBox="0 0 440 180" shape-rendering="crispEdges">
      <rect width="440" height="180" fill="${bg}"></rect>
      <rect x="0" y="90" width="440" height="90" fill="${floor}"></rect>
      <rect x="0" y="84" width="440" height="6" fill="${P.side}"></rect>
      <g stroke="${P.floor}" stroke-width="1">${Array.from({ length: 15 }, (_, i) => `<line x1="${220 + (i - 7) * 6}" y1="90" x2="${220 + (i - 7) * 60}" y2="180"></line>`).join('')}</g>
      <path d="M216 92 L 150 180 M224 92 L 300 180" stroke="${wire}" stroke-width="4"></path>
      ${extraSvg}
      <rect x="40" y="60" width="60" height="50" fill="${P.side}"></rect><rect x="40" y="60" width="60" height="3" fill="${P.top}"></rect>
      <rect x="330" y="70" width="70" height="40" fill="${P.side}"></rect><rect x="330" y="70" width="70" height="3" fill="${P.top}"></rect>
      ${px(CHOMPER_CLOSED, LEG_CH, 2, 250, 66)}${px(CHOMPER_CLOSED, LEG_CH, 2, 290, 70)}
      <rect x="0" y="0" width="440" height="180" fill="url(#scan3)"></rect>
    </svg>` +
    h('div', `font-family: 'Press Start 2P', monospace; font-size: 9px; line-height: 13px; color: ${accent}`, name) +
    h('div', `font-size: 12px; line-height: 17px; color: ${P.uiSub}`, note));
  const defs = `<svg width="0" height="0" style="position: absolute"><defs><pattern id="scan3" width="4" height="4" patternUnits="userSpaceOnUse"><rect width="4" height="2" fill="#000" opacity="0.15"></rect></pattern></defs></svg>`;
  const moods = panel(
    T.h2('3 ZONES  (같은 규칙 · 다른 변주)') +
    h('div', `display: flex; flex-direction: row; gap: 20px`,
      mood('L_MAP_1  ARCADE FLOOR', P.void, P.floorDark, P.wireWide, P.via, '기본. 청보라 복셀 면 + 시안 배선. 경계 밖 = 자두 픽셀 스카이라인·별. 밝기·밀도 중간.',
        parallax(440, 84, 11, { dense: 0.6 })) +
      mood('L_MAP_2  GLITCH SECTOR', '#0C0A16', '#141226', P.wire, P.plum, '위험. 배선은 어둡고 경계 밖 스카이라인이 높고 빽빽해지며 글리치 블록 스폰이 늘어남.',
        parallax(440, 84, 22, { dense: 1.2, nearColor: '#4A2650', blockColor: P.plum }) + `${px(GLITCH, LEG_GLITCH, 3, 150, 20)}${px(GLITCH, LEG_GLITCH, 3, 380, 40)}`) +
      mood('L_MAP_BOSS  CABINET HALL', '#08070F', '#100E1E', P.wireFar, P.destr, '가장 어둡다. 배경은 별만, 배선 최소, 중앙 억제기 연두 맥동이 유일한 광원 = Ultimate 자리.',
        parallax(440, 84, 33, { dense: 0.3, stars: 0.6, farColor: '#1A1026', nearColor: '#1A1026' }) + `${px(CABINET, LEG_CAB, 4, 196, 30)}<rect x="212" y="46" width="16" height="12" fill="${P.destrHot}"></rect><rect x="196" y="30" width="48" height="52" fill="none" stroke="${P.destr}" stroke-width="2" opacity="0.6"></rect>`)),
    '100%');
  const body = h('div', `display: flex; flex-direction: column; gap: 20px; padding: 32px; width: 1440px; box-sizing: border-box; background: ${P.void}; position: relative`,
    defs +
    h('div', `display: flex; flex-direction: row; gap: 20px; align-items: baseline`, T.h1('ENEMY LINEUP  &amp;  ZONES') + T.sub('적 = 뜨거운 쪽(🔒). 크기·실루엣·코어 수로 티어를 읽게 한다(ADR 0013).')) +
    lineup + moods);
  return doc(body);
}

// ---------- 출력 ----------
const out = (name, html) => { writeFileSync(name, html); console.log('wrote', name, html.length); };
out('Main.dc.html', mainBoard());
out('Palette.dc.html', paletteBoard());
out('Form.dc.html', formBoard());
out('Motifs.dc.html', motifsBoard());
out('HUD.dc.html', hudBoard());
out('Lineup.dc.html', lineupBoard());

const canvas = {
  artboards: [
    { file: 'Main.dc.html', title: '1 · 키 비주얼 (1P 프레임)', x: 0, y: 0, w: 1440, h: 810 },
    { file: 'HUD.dc.html', title: '5 · HUD 픽셀 모크', x: 1560, y: 0, w: 1440, h: 810 },
    { file: 'Palette.dc.html', title: '2 · 팔레트 재매핑', x: 0, y: 960, w: 1440, h: 1000 },
    { file: 'Form.dc.html', title: '3 · 형태 언어', x: 1560, y: 960, w: 1440, h: 1000 },
    { file: 'Motifs.dc.html', title: '4 · 모티프 · 로그라이트 매핑', x: 0, y: 2100, w: 1440, h: 980 },
    { file: 'Lineup.dc.html', title: '6 · 적 라인업 · 3구역', x: 1560, y: 2100, w: 1440, h: 980 },
  ],
  annotations: [
    { id: 'brief', x: 0, y: -170, w: 700, text: 'FPS · 아케이드 · 픽셀 · 로그라이트 — 메인 비주얼 컨셉 시트 (2026-09-06)\n레퍼런스 = 롤 아케이드 스킨(컨셉·픽셀 요소·색감만) + 웹툰 전자오락수호대(게임 세계 수호 모티프).\n결정: 픽셀 = 복셀+스프라이트+CRT(전체 픽셀화 PP 기각) · 환경 = 어두운 무대 + 팝 액센트 · Tron 네온선·와이어프레임·회로기판 픽션 폐기(바닥 배선만 유지).\n색 규칙은 Docs/SSOT/ArtDirection.md §A 그대로, 이 시트는 §B(모티프·형태)의 시각 부록. ADR 0016.' },
    { id: 'main-note', x: 760, y: -110, w: 640, text: '키 비주얼 읽는 법: 화면의 70%는 청보라 복셀 면. 밝은 것은 바닥 배선(통로)·적 코어·픽업·총구화염·HUD 뿐. 경계벽 너머 자두색 픽셀 스카이라인·별·부유 블록 = 패럴랙스 배경 = "플레이 불가" 신호. 스캔라인 4px·0.15 = PP_Arcade 현행값.' },
  ],
  launch: { view: 'canvas' },
};
writeFileSync('canvas.json', JSON.stringify(canvas, null, 2));
console.log('wrote canvas.json');

// =====================================================================
// 7. Map — 팩맨 미로 기반 탑다운 (Scripts/gen_pacmaze_proto.py MAZE 그대로) + 구석 특수 에리어 4곳(제안)
// =====================================================================
const MAZE = [
  '############################', '#............##............#', '#.####.#####.##.#####.####.#',
  '#.####.#####.##.#####.####.#', '#.####.#####.##.#####.####.#', '#..........................#',
  '#.####.##.########.##.####.#', '#.####.##.########.##.####.#', '#......##....##....##......#',
  '######.##### ## #####.######', '     #.##### ## #####.#     ', '     #.##          ##.#     ',
  '     #.## ######## ##.#     ', '######.## #      # ##.######', '      .   #      #   .      ',
  '######.## #      # ##.######', '     #.## ######## ##.#     ', '     #.##          ##.#     ',
  '     #.## ######## ##.#     ', '######.## ######## ##.######', '#............##............#',
  '#.####.#####.##.#####.####.#', '#.####.#####.##.#####.####.#', '#...##................##...#',
  '###.##.##.########.##.##.###', '###.##.##.########.##.##.###', '#......##....##....##......#',
  '#.##########.##.##########.#', '#.##########.##.##########.#', '#..........................#',
  '############################'];
const MCOLS = 28, MROWS = 31;
// 구석 특수 에리어(제안): 3×3 타일을 파낸다
const ROOMS = [
  { r: 1, c: 1, key: 'A', name: 'POWER-UP BOOTH', tint: '#1F1A3A', icon: () => px(CARD, LEG_CARD, 4, 0, 0), iw: 12 * 4, col: P.uiReward, note: '레벨업 카드 제단 — 아케이드 파워업 캡슐. 카드 3택을 여기서 뽑게 하면 "가서 받는" 동선이 생긴다' },
  { r: 1, c: 24, key: 'B', name: 'CONTINUE BOOTH', tint: '#171F36', icon: () => px(HEART, { C: P.uiOk }, 5, 0, 0), iw: 8 * 5, col: P.uiOk, note: 'DBNO 부활·회복 부스. CONTINUE? 카운트다운의 물리적 자리(차가운 쪽 = 아군 계열)' },
  { r: 27, c: 1, key: 'C', name: 'BONUS ZONE', tint: '#241D2E', icon: () => px(COIN, LEG_COIN, 5, 0, 0), iw: 8 * 5, col: P.uiReward, note: '보너스 스테이지 — 미션 HoldZone/CollectOrbs 가 붙는 자리. 코인 러시·시간제 보상' },
  { r: 27, c: 24, key: 'D', name: 'WEAPON CABINET', tint: '#1D1F38', icon: () => px(CABINET, LEG_CAB, 3, 0, 0), iw: 12 * 3, col: P.uiOk, note: '무기 언락·교체 캐비닛(UnlockableFeatures 경로). 아케이드 캐비닛 = 상호작용 오브젝트' },
];
const inRoom = (r, c) => ROOMS.find(R => r >= R.r && r < R.r + 3 && c >= R.c && c < R.c + 3);
const cellAt = (r, c) => (r < 0 || r >= MROWS || c < 0 || c >= MCOLS) ? ' ' : MAZE[r][c];
const isHouse = (r, c) => r >= 13 && r <= 15 && c >= 11 && c <= 16;
const isWalk = (r, c) => { if (inRoom(r, c)) return true; if (isHouse(r, c)) return false; const ch = cellAt(r, c); return ch === '.' || (ch === ' ' && r >= 9 && r <= 19 && c >= 0 && c < MCOLS); };

function mapBoard() {
  const c = 24, W = MCOLS * c, H = MROWS * c;
  let s = `<rect width="${W}" height="${H}" fill="${P.void}"></rect>`;
  for (let r = 0; r < MROWS; r++) for (let col = 0; col < MCOLS; col++) {
    const x = col * c, y = r * c, ch = cellAt(r, col), room = inRoom(r, col);
    if (isHouse(r, col)) { s += `<rect x="${x}" y="${y}" width="${c}" height="${c}" fill="${P.floor}"></rect>`; continue; }
    if (room) { s += `<rect x="${x}" y="${y}" width="${c}" height="${c}" fill="${room.tint}"></rect>`; continue; }
    if (ch === '#') {
      s += `<rect x="${x}" y="${y}" width="${c}" height="${c}" fill="${P.side}"></rect><rect x="${x + 0.5}" y="${y + 0.5}" width="${c - 1}" height="${c - 1}" fill="none" stroke="${P.floorDark}" stroke-width="1"></rect>`;
      if (cellAt(r - 1, col) !== '#') s += `<rect x="${x}" y="${y}" width="${c}" height="3" fill="${P.top}"></rect>`;
    } else if (isWalk(r, col)) {
      s += `<rect x="${x}" y="${y}" width="${c}" height="${c}" fill="${P.floorDark}"></rect>`;
    }
  }
  // 통로 가이드 라인(배선) — 열린 이웃 쪽으로 반칸씩, 정션 비아
  for (let r = 0; r < MROWS; r++) for (let col = 0; col < MCOLS; col++) {
    if (!isWalk(r, col) || inRoom(r, col)) continue;
    const cx = col * c + c / 2, cy = r * c + c / 2, w = 3;
    const nb = [[0, -1], [0, 1], [-1, 0], [1, 0]].map(([dr, dc]) => isWalk(r + dr, col + dc));
    const tunnel = r === 14 && (col <= 5 || col >= 22);
    const colr = tunnel ? P.wireFar : P.wire;
    if (nb[0]) s += `<rect x="${cx - c / 2}" y="${cy - w / 2}" width="${c / 2}" height="${w}" fill="${colr}"></rect>`;
    if (nb[1]) s += `<rect x="${cx}" y="${cy - w / 2}" width="${c / 2}" height="${w}" fill="${colr}"></rect>`;
    if (nb[2]) s += `<rect x="${cx - w / 2}" y="${cy - c / 2}" width="${w}" height="${c / 2}" fill="${colr}"></rect>`;
    if (nb[3]) s += `<rect x="${cx - w / 2}" y="${cy}" width="${w}" height="${c / 2}" fill="${colr}"></rect>`;
    if (nb.filter(Boolean).length >= 3) s += `<rect x="${cx - 4}" y="${cy - 4}" width="8" height="8" fill="${P.via}"></rect>`;
  }
  s += `<g font-family="'Press Start 2P', monospace" font-size="7" fill="${P.uiSub}"><text x="4" y="${14 * c - 6}">WARP</text><text x="${23 * c + 6}" y="${14 * c - 6}">WARP</text></g>`;
  // 중앙 고스트 하우스 = 캐비닛 코어(억제기) + 글리치 스폰
  s += `<rect x="${10 * c}" y="${12 * c}" width="${8 * c}" height="${5 * c}" fill="none" stroke="${P.destr}" stroke-width="2" opacity="0.7"></rect>`;
  s += `<g transform="translate(${12.5 * c} ${12.6 * c})">${px(CABINET, LEG_CAB, 6, 0, 0)}</g>`;
  s += `<rect x="${13.5 * c + 8}" y="${13 * c + 4}" width="14" height="10" fill="${P.destrHot}"></rect>`;
  s += px(GLITCH, LEG_GLITCH, 2, 10.4 * c, 13.4 * c) + px(GLITCH, LEG_GLITCH, 2, 16.6 * c, 13.4 * c);
  ROOMS.forEach(R => {
    const x = R.c * c, y = R.r * c;
    s += `<rect x="${x + 1}" y="${y + 1}" width="${3 * c - 2}" height="${3 * c - 2}" fill="none" stroke="${R.col}" stroke-width="2"></rect>`;
    s += `<g transform="translate(${x + (3 * c - R.iw) / 2} ${y + 10})">${R.icon()}</g>`;
    s += `<text x="${x + 3 * c / 2}" y="${y + 3 * c - 5}" text-anchor="middle" font-family="'Press Start 2P', monospace" font-size="9" fill="${R.col}">${R.key}</text>`;
  });
  const START = `
..HH..
.HHHH.
..HH..
.HHHH.
.H..H.`;
  s += `<g transform="translate(${13.5 * c - 8} ${23 * c + 4})">${px(START, { H: P.ally }, 3, 0, 0)}</g><text x="${13.5 * c + 14}" y="${23 * c + 16}" font-family="'Press Start 2P', monospace" font-size="7" fill="${P.ally}">START</text>`;
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}" shape-rendering="crispEdges">${s}</svg>`;

  const row = (k, col, name, note) => h('div', `display: flex; flex-direction: row; gap: 12px; align-items: flex-start`,
    h('div', `width: 28px; height: 28px; border: 2px solid ${col}; color: ${col}; font-family: 'Press Start 2P', monospace; font-size: 11px; display: flex; align-items: center; justify-content: center; flex-shrink: 0`, k) +
    h('div', `display: flex; flex-direction: column; gap: 3px`, h('div', `font-family: 'Press Start 2P', monospace; font-size: 9px; line-height: 13px; color: ${col}`, name) + h('div', `font-size: 12px; line-height: 17px; color: ${P.uiText}`, note)));
  const legend = panel(
    T.h2('PAC-MAZE  28 x 31 TILES') +
    T.kr('<b>통로 = 1타일 폭.</b> 프로토 값(2026-09-03 사용자): 타일 <b>10 m</b> · 벽 <b>12 m</b>. 벽은 복셀 면(§A-3-1) + 상단 엣지 1줄, 통로 바닥은 가운데 배선 1줄(가이드 라인, 정션 = 비아). 배선은 통행 판독 규칙이라 유지.') +
    T.kr('<b>중앙 고스트 하우스 = 캐비닛 코어(억제기) + 글리치 스폰.</b> 팩맨의 유령 집이 이 게임에선 스테이지 보스 코어 자리다 — 부수면 STAGE CLEAR. 좌우 <b>워프 터널</b>은 유지(양쪽 끝 연결).') +
    h('div', `height: 2px; background: ${P.side}`) +
    T.h2('CORNER SPECIAL AREAS  (제안 — 지금은 없음)') +
    T.sub('팩맨의 파워 펠릿 자리. 구석 블록 3×3 을 파내 방으로 만든다. 내용은 사용자 결정 — 아래는 기존 시스템에 붙는 후보 4종.') +
    ROOMS.map(R => row(R.key, R.col, R.name, R.note)).join('') +
    T.sub('방 바닥 색은 substrate 안에서 살짝 다른 면(각 방 고유 틴트), 프레임·아이콘만 UI 대역. 방 안 적 스폰 여부·안전지대 여부는 결정 대상.') +
    h('div', `height: 2px; background: ${P.side}`) +
    T.h2('FLAGS') +
    T.kr('① <b>규모</b>: 타일 10 m 면 280 × 310 m — ADR 0012 상한(160 × 160 m · 25,600 셀)의 <b>약 3.4배</b>(86,800 셀). 타일 5 m(140 × 155 m)면 맞는다. 컨셉은 비율만 정한다 — <b>타일 크기는 ADR 0012 개정 또는 5 m 로 별도 결정</b>.') +
    T.kr('② <b>위상</b>: ADR 0010 D1(다중 코어 교차 동선)이 통로 미로로 바뀐다 — 스타일이 아니라 위상 결정이라 별도 ADR(0017) 대상.'),
    640);
  const body = h('div', `display: flex; flex-direction: column; gap: 20px; padding: 32px; width: 1440px; box-sizing: border-box; background: ${P.void}`,
    h('div', `display: flex; flex-direction: row; gap: 20px; align-items: baseline`, T.h1('ARENA MAP  ·  PAC-MAZE') + T.sub('Scripts/gen_pacmaze_proto.py 의 MAZE 그대로. 구석 3×3 만 제안으로 파냈다.')) +
    h('div', `display: flex; flex-direction: row; gap: 24px; align-items: flex-start`, h('div', `flex-shrink: 0; border: 2px solid ${P.side}`, svg) + legend));
  return doc(body);
}

// =====================================================================
// 8. Corridor — 1인칭 통로 뷰 (10 m 폭 · 12 m 벽) + 구석 특수 에리어 입구
// =====================================================================
function corridorBoard() {
  const W = 1440, H = 810, VX = 720, VY = 400;
  const fw = 120, fwallTop = VY - 120, fwallBot = VY + 12, nearTop = -200;
  let s = `<rect width="${W}" height="${H}" fill="${P.void}"></rect>`;
  s += parallax(W, fwallTop + 40, 5, { dense: 0.7 });
  s += `<polygon points="0,${H} ${W},${H} ${VX + fw / 2},${fwallBot} ${VX - fw / 2},${fwallBot}" fill="${P.floorDark}"></polygon>`;
  s += `<polygon points="0,${H} ${VX - fw / 2},${fwallBot} ${VX - fw / 2},${fwallTop} 0,${nearTop}" fill="${P.side}"></polygon>`;
  s += `<polygon points="${W},${H} ${VX + fw / 2},${fwallBot} ${VX + fw / 2},${fwallTop} ${W},${nearTop}" fill="${P.side}"></polygon>`;
  s += `<polygon points="0,${nearTop} ${VX - fw / 2},${fwallTop} ${VX - fw / 2},${fwallTop + 3} 0,${nearTop + 5}" fill="${P.top}"></polygon>`;
  s += `<polygon points="${W},${nearTop} ${VX + fw / 2},${fwallTop} ${VX + fw / 2},${fwallTop + 3} ${W},${nearTop + 5}" fill="${P.top}"></polygon>`;
  s += `<g stroke="${P.floorDark}" stroke-width="1" opacity="0.9">`;
  const depths = [0.04, 0.09, 0.15, 0.22, 0.3, 0.4, 0.52, 0.66, 0.82, 1.0];
  depths.forEach(t => {
    const xl = (VX - fw / 2) * t, xr = W - (W - VX - fw / 2) * t;
    const yb = H - (H - fwallBot) * t, yt = nearTop + (fwallTop - nearTop) * t;
    s += `<line x1="${xl}" y1="${yb}" x2="${xl}" y2="${yt}"></line><line x1="${xr}" y1="${yb}" x2="${xr}" y2="${yt}"></line><line x1="${xl}" y1="${yb}" x2="${xr}" y2="${yb}"></line>`;
  });
  [0.15, 0.3, 0.45, 0.6, 0.75, 0.9].forEach(k => {
    const y0 = H - (H - nearTop) * k, y1 = fwallBot - (fwallBot - fwallTop) * k;
    s += `<line x1="0" y1="${y0}" x2="${VX - fw / 2}" y2="${y1}"></line><line x1="${W}" y1="${y0}" x2="${VX + fw / 2}" y2="${y1}"></line>`;
  });
  s += `</g>`;
  // 먼 끝 T자 교차로
  s += `<rect x="${VX - fw / 2}" y="${fwallTop}" width="${fw}" height="${fwallBot - fwallTop}" fill="${P.floor}"></rect><rect x="${VX - fw / 2}" y="${fwallTop}" width="${fw}" height="2" fill="${P.top}"></rect>`;
  s += `<rect x="${VX - fw / 2 - 40}" y="${fwallBot - 6}" width="${fw + 80}" height="6" fill="${P.floorDark}"></rect>`;
  // 가이드 라인 + 펄스 + 비아
  s += `<polygon points="${VX - 12},${H} ${VX + 12},${H} ${VX + 2},${fwallBot} ${VX - 2},${fwallBot}" fill="${P.wire}"></polygon>`;
  [0.12, 0.34, 0.62].forEach(t => { const y = H - (H - fwallBot) * t, w = 26 - 20 * t; s += `<rect x="${VX - w / 2}" y="${y - w / 2}" width="${w}" height="${w / 2}" fill="${P.wireWide}"></rect>`; });
  s += `<rect x="${VX - 6}" y="${fwallBot - 4}" width="12" height="8" fill="${P.via}"></rect>`;
  // 우측 벽 — 구석 특수 에리어(A) 입구
  const t0 = 0.36, t1 = 0.5;
  const xr0 = W - (W - VX - fw / 2) * t0, xr1 = W - (W - VX - fw / 2) * t1;
  const yb0 = H - (H - fwallBot) * t0, yb1 = H - (H - fwallBot) * t1;
  const yt0 = yb0 - 220 * (1 - t0), yt1 = yb1 - 220 * (1 - t1);
  s += `<polygon points="${xr0},${yb0} ${xr1},${yb1} ${xr1},${yt1} ${xr0},${yt0}" fill="#1F1A3A"></polygon>`;
  s += `<polygon points="${xr0},${yb0} ${xr1},${yb1} ${xr1},${yt1} ${xr0},${yt0}" fill="none" stroke="${P.uiReward}" stroke-width="3"></polygon>`;
  s += `<g transform="translate(${(xr0 + xr1) / 2 - 24} ${(yt0 + yt1) / 2 + 10})">${px(CARD, LEG_CARD, 4, 0, 0)}</g>`;
  s += `<text x="${(xr0 + xr1) / 2}" y="${yt1 - 12}" text-anchor="middle" font-family="'Press Start 2P', monospace" font-size="8" fill="${P.uiReward}">POWER-UP</text>`;
  const chompS = (x, y, c, open, elite = false) => px(open ? CHOMPER_OPEN : CHOMPER_CLOSED, elite ? LEG_CH_ELITE : LEG_CH, c, x, y);
  s += chompS(VX - 22, fwallBot - 30, 2, false) + chompS(VX + 6, fwallBot - 32, 2, false) + chompS(VX - 60, fwallBot + 6, 3, false) + chompS(VX + 30, fwallBot + 20, 4, true);
  s += chompS(VX - 200, fwallBot + 60, 6, true) + chompS(VX + 120, fwallBot + 110, 8, true, true);
  s += chompS(VX - 520, fwallBot + 140, 11, true);
  const ALLY = `
...HH...
..HHHH..
...HH...
.HHHHHH.
HHHHHHHH
H.HHHH.H
..HHHH..
..H..H..
..H..H..
.HH..HH.`;
  s += `<g transform="translate(${VX + 300} ${fwallBot + 50})">${px(ALLY, { H: P.ally }, 5, 0, 0)}<text x="20" y="-8" text-anchor="middle" font-family="'Press Start 2P', monospace" font-size="9" fill="${P.ally}">P3</text></g>`;
  s += px(GUN, LEG_GUN, 12, 860, 440);
  s += px(FLASH, LEG_FLASH, 9, 968, 372);
  s += `<g font-family="'Press Start 2P', monospace" fill="${P.uiText}"><text x="${VX}" y="44" text-anchor="middle" font-size="14">STAGE 1</text><text x="${VX}" y="66" text-anchor="middle" font-size="9" fill="${P.uiSub}">WAVE 03   01:48</text></g>`;
  s += `<g fill="${P.uiText}"><rect x="${VX - 1}" y="${VY - 8}" width="3" height="10"></rect><rect x="${VX - 1}" y="${VY + 18}" width="3" height="10"></rect><rect x="${VX - 18}" y="${VY + 9}" width="10" height="3"></rect><rect x="${VX + 8}" y="${VY + 9}" width="10" height="3"></rect></g>`;
  s += `<defs><pattern id="scan4" width="4" height="4" patternUnits="userSpaceOnUse"><rect width="4" height="2" fill="#000" opacity="0.15"></rect></pattern><radialGradient id="vig4" cx="50%" cy="50%" r="72%"><stop offset="60%" stop-color="#000" stop-opacity="0"></stop><stop offset="100%" stop-color="#000" stop-opacity="0.55"></stop></radialGradient></defs>`;
  s += `<rect width="${W}" height="${H}" fill="url(#scan4)"></rect><rect width="${W}" height="${H}" fill="url(#vig4)"></rect>`;
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}" shape-rendering="crispEdges">${s}</svg>`;
  return doc(h('div', `width: ${W}px; height: ${H}px; background: ${P.void}; overflow: hidden`, svg));
}

out('Map.dc.html', mapBoard());
out('Corridor.dc.html', corridorBoard());
canvas.artboards.push(
  { file: 'Map.dc.html', title: '7 · 아레나 맵 (팩맨 미로 + 구석 특수 에리어)', x: 0, y: 3200, w: 1440, h: 900 },
  { file: 'Corridor.dc.html', title: '8 · 통로 1P 뷰 + 특수 에리어 입구', x: 1560, y: 3200, w: 1440, h: 810 });
canvas.annotations.push({ id: 'map-note', x: 0, y: 3110, w: 900, text: '맵 = 팩맨 미로 기반(사용자 2026-09-06). 통로 1타일 폭, 중앙 고스트 하우스 = 캐비닛 코어, 구석 4곳 = 특수 에리어(지금은 없음 → 제안 A~D, 내용은 사용자 결정). 타일 10 m 면 ADR 0012 규모 상한 3.4배 — 5 m 또는 ADR 개정 필요.' });
writeFileSync('canvas.json', JSON.stringify(canvas, null, 2));
console.log('wrote canvas.json (8 artboards)');
