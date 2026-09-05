"""Content/Authoring/Cards.csv 에 BuildTags 컬럼을 삽입한다 (CRIT2, 1회용 마이그레이션).

왜 전용 스크립트인가 — `authoring_sheet.py` 는 헤더가 `expectedHeader` 와 다르면 즉시 죽고(그게 옳다:
조용히 엉뚱한 열에 쓰는 것보다 낫다) **컬럼을 추가하는 명령이 없다**. 익스포터로 재생성하는 방법도 있지만
행 정렬·정규화 부작용이 있어 diff 가 커진다. 그래서 삽입만 하는 최소 도구를 따로 둔다.

삽입 위치 = index 7(`Family` 바로 뒤). 코드 쪽 파서가 DisplayName/Description 을 8..13,
EffectBaseColumn 을 14 로 이미 밀어 뒀으므로 이 위치여야만 맞는다.

csv 모듈로 읽고 쓴다 — 설명문에 콤마가 들어 있어(예 "재장전을 마치면 5초간 ...") 문자열 split 으로는 깨진다.
멱등: 이미 BuildTags 가 있으면 아무것도 하지 않는다.
"""

import csv
import io
import os
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGET = os.path.join(REPO_ROOT, "Content", "Authoring", "Cards.csv")
INSERT_AT = 7
COLUMN_NAME = "BuildTags"

with io.open(TARGET, encoding="utf-8", newline="") as f:
    rows = list(csv.reader(f))

if not rows:
    sys.exit("ERROR: Cards.csv is empty")

header = rows[0]
if COLUMN_NAME in header:
    print("no-op: '%s' already present at index %d" % (COLUMN_NAME, header.index(COLUMN_NAME)))
    sys.exit(0)

if header[INSERT_AT - 1] != "Family":
    sys.exit("ERROR: expected 'Family' at index %d, found '%s' — refusing to guess"
             % (INSERT_AT - 1, header[INSERT_AT - 1]))

out = []
for index, row in enumerate(rows):
    # 짧은 행은 삽입 지점까지 패딩해야 셀이 엉뚱한 자리에 들어가지 않는다.
    while len(row) < INSERT_AT:
        row.append("")
    row.insert(INSERT_AT, COLUMN_NAME if index == 0 else "")
    out.append(row)

buffer = io.StringIO()
writer = csv.writer(buffer, lineterminator="\n")
writer.writerows(out)
data = buffer.getvalue().encode("utf-8")   # 인코딩을 먼저 끝낸다 (open(w) 이 실패 전에 파일을 비우는 사고 방지)
with io.open(TARGET, "wb") as f:
    f.write(data)

print("migrated: %d rows, '%s' inserted at index %d (header now %d columns)"
      % (len(out) - 1, COLUMN_NAME, INSERT_AT, len(out[0])))
