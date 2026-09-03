"""에디터 시작 시 자동 실행 (PythonScriptPlugin). BP 노드 정리 메뉴 등록.

CityGen / CityLayout 메뉴는 도시 트랙 폐기(2026-09-03)와 함께 제거됐다 —
도시맵은 ADR 0010 에서 아레나 위상으로 대체됐고, 그 도구가 스캔하던
/Game/PolygonCyberCity · /Game/PolygonScifi 콘텐츠 팩도 이미 삭제됐다.
"""
try:
    import fpsr_bp_layout
    fpsr_bp_layout.register_menu()
except Exception as e:
    import unreal
    unreal.log_warning(f"[BP 노드 정리] init 실패: {e}")
