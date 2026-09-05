/**
 * 저작 시트 쓰기 엔드포인트 (FPSRoguelite).
 *
 * 왜 Apps Script 인가 — 시트에 쓰려면 인증이 필요한데, 서비스 계정(GCP 프로젝트 + 키 파일 + 공유 설정)은
 * 설정이 무겁고 리포에 키를 들여야 한다. 웹앱은 시트 안에서 3분이면 끝나고, 자격증명이 리포에 안 들어온다
 * (URL·토큰은 gitignore 된 Config/AuthoringSheets.writeback.json 에만 산다).
 *
 * 계약: POST { token, sheet, csv } → { ok, rows } 또는 { ok:false, error }.
 * 첫 번째 탭의 내용 전체를 CSV 로 갈아끼운다(우리 저작 시트는 스프레드시트당 탭 1개가 원칙 —
 * 탭이 여럿이면 export gid 가 흔들려 sync 가 깨진다. Localization.md L-5).
 *
 * ⚠️ 서식·수식은 보존되지 않는다. 이 시트들은 순수 데이터라 의도된 동작이다.
 *
 * 설정:
 *   1) 시트에서 확장 프로그램 > Apps Script > 이 파일 전체를 붙여넣기
 *   2) 아래 SHARED_TOKEN 을 길고 무작위한 값으로 바꾸고, 같은 값을 writeback.json 에 적는다
 *   3) 배포 > 새 배포 > 유형=웹 앱 / 실행=나 / 액세스=링크가 있는 모든 사용자 > 배포 > URL 복사
 *      (액세스를 "나"로 두면 스크립트가 OAuth 를 요구해 자동화가 불가능하다. 대신 토큰이 문지기다.)
 */

var SHARED_TOKEN = 'CHANGE-ME-TO-A-LONG-RANDOM-STRING';

function doPost(e) {
  try {
    if (!e || !e.postData || !e.postData.contents) {
      return respond({ ok: false, error: 'empty body' });
    }
    var payload = JSON.parse(e.postData.contents);

    if (SHARED_TOKEN === 'CHANGE-ME-TO-A-LONG-RANDOM-STRING') {
      return respond({ ok: false, error: 'SHARED_TOKEN not configured in the Apps Script' });
    }
    if (payload.token !== SHARED_TOKEN) {
      return respond({ ok: false, error: 'bad token' });
    }
    if (typeof payload.csv !== 'string' || payload.csv.length === 0) {
      return respond({ ok: false, error: 'no csv' });
    }

    // Utilities.parseCsv 는 인용된 콤마·줄바꿈을 규약대로 처리한다(직접 split 하면 설명문의 콤마에서 깨진다).
    var rows = Utilities.parseCsv(payload.csv);
    if (!rows.length) {
      return respond({ ok: false, error: 'csv parsed to 0 rows' });
    }

    // 열 수를 최대치에 맞춰 패딩 — setValues 는 직사각형만 받는다(마지막 열이 빈 행에서 짧아지는 걸 방지).
    var width = 0;
    for (var i = 0; i < rows.length; i++) {
      if (rows[i].length > width) { width = rows[i].length; }
    }
    for (var j = 0; j < rows.length; j++) {
      while (rows[j].length < width) { rows[j].push(''); }
    }

    var sheet = SpreadsheetApp.getActiveSpreadsheet().getSheets()[0];

    // 락: 같은 시트에 두 요청이 겹치면 절반만 쓰인 상태가 남는다.
    var lock = LockService.getScriptLock();
    if (!lock.tryLock(20000)) {
      return respond({ ok: false, error: 'another write is in progress' });
    }
    try {
      // 필요한 만큼 격자를 넓힌 뒤 지우고 쓴다. clear() 가 아니라 clearContents() 인 이유 =
      // 사람이 넣어 둔 열 너비·고정행 같은 표시 설정까지 날리지 않기 위해서다.
      if (sheet.getMaxRows() < rows.length) {
        sheet.insertRowsAfter(sheet.getMaxRows(), rows.length - sheet.getMaxRows());
      }
      if (sheet.getMaxColumns() < width) {
        sheet.insertColumnsAfter(sheet.getMaxColumns(), width - sheet.getMaxColumns());
      }
      sheet.clearContents();
      sheet.getRange(1, 1, rows.length, width).setValues(rows);
      SpreadsheetApp.flush();
    } finally {
      lock.releaseLock();
    }

    return respond({ ok: true, rows: rows.length, sheet: sheet.getName() });
  } catch (err) {
    return respond({ ok: false, error: String(err) });
  }
}

/** GET 은 배포 확인용 — 토큰 없이 아무것도 노출하지 않는다. */
function doGet() {
  return respond({ ok: true, service: 'FPSRoguelite authoring sheet writer', method: 'POST only' });
}

function respond(object) {
  return ContentService
    .createTextOutput(JSON.stringify(object))
    .setMimeType(ContentService.MimeType.JSON);
}
