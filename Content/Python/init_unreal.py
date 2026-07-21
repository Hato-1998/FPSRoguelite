"""에디터 시작 시 자동 실행 (PythonScriptPlugin). FPSR CityGen 메뉴 등록."""
try:
    import fpsr_citygen
    fpsr_citygen.register_menu()
except Exception as e:
    import unreal
    unreal.log_warning(f"[CityGen] init 실패: {e}")
