# pm-board 서브에이전트 권한 경계 — PreToolUse 훅
#
# 무엇을 막나: pm-board 에이전트가 Notion 행의 기존 값을 덮어쓰는 것.
#   `notion-update-page` 하나가 `insert_content`(로그 추가)와 `update_properties`(속성 덮어쓰기)를
#   같은 도구로 처리하기 때문에, 도구 화이트리스트만으로는 "추가만 허용"을 강제할 수 없다.
#   이 훅이 tool_input.command 를 보고 갈라 준다.
#
# 누구에게: agent_type == 'pm-board' 일 때만. 훅 입력의 agent_type/agent_id 는
#   서브에이전트 안에서 훅이 발화할 때만 채워지므로, 메인 세션의 정당한 클레임·상태 갱신은 그대로 통과한다.
#
# 배선(공유되지 않음): .claude/settings.json 은 .gitignore 대상(머신 로컬)이라 이 스크립트만 커밋된다.
#   새 클론에서 경계를 켜려면 settings.json 의 hooks.PreToolUse 에 이 스크립트를 물려야 한다 — Workflow.md §6-9 (7).

$ErrorActionPreference = 'Stop'

function Write-Decision {
    param([string]$Decision, [string]$Reason)
    $payload = @{
        hookSpecificOutput = @{
            hookEventName          = 'PreToolUse'
            permissionDecision     = $Decision
            permissionDecisionReason = $Reason
        }
    }
    $payload | ConvertTo-Json -Depth 5 -Compress | Write-Output
    exit 0
}

$raw = [Console]::In.ReadToEnd()

try {
    $hook = $raw | ConvertFrom-Json
} catch {
    # 누가 부른 건지 판정할 수 없다. 조용히 통과시키면 경계가 새고,
    # 무조건 막으면 메인 세션의 정당한 갱신까지 죽는다 → 사용자에게 묻는다.
    Write-Decision 'ask' 'pm-board 가드: 훅 입력을 파싱하지 못해 호출 주체를 확인할 수 없습니다. 직접 확인해 주세요.'
}

# 서브에이전트 밖(메인 세션)이거나 다른 에이전트면 관여하지 않는다.
if ($hook.agent_type -ne 'pm-board') { exit 0 }

$command = $hook.tool_input.command

# 로그 append 는 허용된 유일한 쓰기.
if ($command -eq 'insert_content') { exit 0 }

$shown = if ([string]::IsNullOrEmpty($command)) { '(없음)' } else { $command }
Write-Decision 'deny' @"
pm-board 에이전트는 기존 행을 덮어쓸 수 없습니다 (요청한 command = $shown).
허용: insert_content(로그 append) · notion-create-pages(신규 행).
상태·담당·브랜치·우선순위·완료커밋 변경은 출력의 '## 실행 필요(메인)' 에 도구 인자로 적어 메인 세션에 넘기세요. (Workflow.md §6-9 (7))
"@
