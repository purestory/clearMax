# Agent Instructions (AI 행동 지침)

**항상 다음 지침을 준수하여 사용자를 지원하세요.**
1. **지식 업데이트:** 향후 개발을 진행하면서 특정 프로젝트에 국한되지 않는 **일반적인 Win32 C++ 개발, 성능 최적화, 혹은 UI/UX 개선 패턴**을 새롭게 발견하거나 구현하게 되면, AI는 스스로 판단하여 **이 문서(GEMINI.md)의 '개발 규칙' 항목에 해당 내용을 자동으로 요약 및 추가**해야 합니다.
2. 단, 너무 지엽적이거나 특정 상황에만 해당하는 특수한 예외 사항은 억지로 추가하지 않습니다.

---

# 빌드 규칙 (Build Rules)

환경 변수에 `cmake`와 `msbuild`가 등록되어 있지 않은 Windows 환경에서 C++ 프로젝트를 빌드할 때는 다음 규칙을 따릅니다.

1. PowerShell에서 `cmake --build`나 `msbuild` 명령어를 직접 실행하지 않습니다.
2. 빌드 시 반드시 **Visual Studio 2022 Community의 MSBuild 절대 경로**를 사용해야 합니다.
3. `&` 연산자를 사용하여 다음과 같이 솔루션을 빌드합니다. (경로는 현재 프로젝트에 맞게 수정)
   `& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" build\<SolutionName>.sln /p:Configuration=Release`

---

# 개발 규칙 (Development Rules)

Win32 C++ 프로젝트 개발 시 소프트웨어의 완성도를 높이기 위해 다음 원칙을 항상 준수하세요.

1. **동적 레이아웃 및 리사이징 (WM_SIZE 처리)**
   - `.rc` 파일에 정의된 컨트롤 크기 및 위치는 픽셀이 아닌 **DLU(Dialog Units)** 기반이므로, 시스템 DPI나 시스템 폰트에 따라 렌더링되는 크기가 달라집니다.
   - 따라서 `WM_SIZE` 핸들러에서 하드코딩된 픽셀 값(예: `width - 90`)을 사용한 위치/크기 계산을 피해야 합니다.
   - 항상 `GetWindowRect`로 컨트롤의 실제 렌더링 크기(width/height)를 구하거나, `MapDialogRect`를 통해 DLU를 픽셀로 변환한 후, 부모 클라이언트 영역(`GetClientRect`)에 비례하여 동적으로 배치(우측/하단 정렬 등)하세요.

2. **깜빡임 없는 UI 업데이트 (In-place Update)**
   - 리스트뷰(`ListView`) 등의 아이템 상태가 일부 변경되었을 때, 전체 리스트를 지우고 새로고침(`DeleteAllItems` 후 재로드)하지 마세요.
   - 화면 깜빡임과 스크롤 초기화를 방지하기 위해 `ListView_SetItemText` 등을 사용하여 **변경된 특정 행(Row)만 부분 업데이트(In-place Update)** 해야 합니다.

3. **데이터 수집 시 레지스트리 유연한 파싱 (Fallback 구현)**
   - Windows 레지스트리를 통해 프로그램 정보 등을 조회할 때, `InstallLocation` 같은 표준 키는 비어있거나 누락된 경우가 매우 많습니다.
   - 항상 특정 키 값이 비어있을 것을 가정하고, `UninstallString`이나 `DisplayIcon` 같은 다른 문자열에서 `.exe` 경로와 디렉토리를 역추적해내는 **Fallback(예비) 로직**을 반드시 구현하여 데이터 수집의 정확도와 안정성을 높이세요.

4. **Win32 Dialog 내 Custom Draw 처리 (DWLP_MSGRESULT)**
   - DialogBox 기반의 윈도우(DialogProc)에서 `WM_NOTIFY` 메시지의 `NM_CUSTOMDRAW`를 처리할 때, 직접 return 값을 반환하면 시스템이 인식하지 못하고 무시하는 경우가 있습니다.
   - 반드시 `SetWindowLongPtr(hWnd, DWLP_MSGRESULT, 반환값)`을 호출하여 결과를 명시한 후, `return (INT_PTR)TRUE;`를 반환해야 정상적으로 렌더링을 가로채거나(`CDRF_NOTIFYPOSTPAINT`) 넘길 수 있습니다.
   - 기존 컨트롤의 아이콘이나 체크박스 등을 보존하면서 텍스트만 다르게 그리고 싶다면, `CDRF_SKIPDEFAULT` 대신 `CDRF_NOTIFYPOSTPAINT`를 사용하여 기본 렌더링이 끝난 후 `CDDS_ITEMPOSTPAINT` 단계에서 덮어쓰는 방식을 사용해야 합니다.

5. **TreeView 폴더 확장 시 Auto-Scroll(화면 튐) 방지**
   - `SysTreeView32` 컨트롤에서 사용자가 폴더(+)를 확장하여 다수의 자식 노드를 삽입(`TVN_ITEMEXPANDING`)할 때, 컨트롤이 자식들을 화면에 욱여넣으려고 자동 스크롤하면서 부모 노드가 화면 최상단으로 튕겨 올라가는 불쾌한 UX가 발생합니다.
   - 이를 방지하기 위해 노드 삽입 직전 `GetScrollPos(hTree, SB_VERT)`로 스크롤 위치를 기억하고, `PostMessage` 등을 이용해 비동기로(즉시) 해당 스크롤 위치를 복구(`WM_VSCROLL`)하여 화면이 그 상태 그대로 유지되도록 처리해야 합니다.

6. **PowerShell 텍스트 파일 조작 시 UTF-8 인코딩 강제**
   - Windows PowerShell에서 `Add-Content`나 `Out-File` 등의 명령어로 텍스트 파일에 내용을 덧붙이거나 작성할 때, 기본 인코딩(ANSI 등)이 적용되어 기존의 UTF-8(특히 한글)이 통째로 깨지는 현상이 발생합니다.
   - 따라서 파일 I/O를 수행하는 셸 스크립트를 작성할 때는 반드시 `-Encoding UTF8` 파라미터를 명시하여 인코딩 깨짐을 원천 차단해야 합니다.

