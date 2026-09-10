# ReplayCapture

Windows에서 모니터 화면과 시스템 소리의 최근 기록을 보관하고, GUI 버튼 또는 사용자 지정 키보드 단축키로 영상 파일을 저장하는 프로그램입니다.

## 실행

Windows 11 x64용 네이티브 GUI 프로그램입니다. 배포 ZIP의 압축을 푼 뒤 **ReplayCapture.exe**를 실행하십시오. 설정에서 모니터·소리 장치·저장 폴더를 확인하고 **녹화 시작**을 누릅니다. 사용자용 CLI는 없습니다.

기본 저장 단축키는 **Ctrl+Shift+F9**입니다. GUI의 **최근 N초 저장** 버튼으로도 저장할 수 있습니다. 단축키 탭에서 원하는 키로 변경할 수 있습니다.

## 주요 기능

- 백그라운드에서 선택한 모니터 화면과 시스템 소리 캡처
- 최근 60초 기본 보관 및 사용자 지정 보관·저장 시간
- GUI 버튼과 사용자 지정 전역 단축키를 통한 최근 기록 저장
- 시작·일시정지·재개·중지·설정·저장 작업 관리 등 모든 기능의 GUI 조작
- 단축키 변경, 충돌 확인, 테스트 및 재시작 후 설정 유지
- H.264 영상과 AAC 오디오를 포함하는 MP4 저장
- 저장 중에도 녹화를 유지하는 압축 순환 버퍼

빠른 저장은 키프레임 경계 때문에 요청한 시간보다 약 1초 일찍 시작할 수 있습니다. 실제 저장 길이와 기록 부족 여부는 GUI에 표시합니다. 기본 출력은 1080p30, H.264 8Mbps + AAC 192kbps이며 최근 60초를 보관합니다.

## 문서와 지원 범위

- [GUI·단축키 사용 안내](docs/gui-and-hotkeys.md)
- [문제 해결](docs/troubleshooting.md)
- [구현 구조와 계획 대비 변경](docs/architecture.md)
- [검증 보고서](docs/validation-report.md)
- [초기 상세 계획서](WINDOWS_REPLAY_RECORDER_PLAN.ko.md)

SDR 모니터 하나와 mono/stereo 출력 장치의 시스템 소리를 지원합니다. 마이크, HDR 저장, 다중 모니터 합성, 정밀 재인코딩 편집은 포함하지 않습니다. 다른 GPU·드라이버와 장시간 상주 검증 범위는 검증 보고서를 확인하십시오.

## 개발자 빌드

필요 도구: Visual Studio 2022 C++ Build Tools, Windows SDK, CMake 3.25 이상. 이 저장소에서 확인한 도구는 MSVC 19.44.35222와 Windows SDK 10.0.26100.0입니다. C++ 런타임을 정적으로 연결하며 사용자 PC에 Python·.NET·FFmpeg 설치가 필요하지 않습니다.

```powershell
# Release 빌드 + 단위 시험 + 배포 ZIP
./scripts/build.ps1 -Package
```

결과는 `build/Release/ReplayCapture.exe`와 `dist/ReplayCapture-0.1.0-windows-x64.zip`입니다. 배포 패키지는 개발용 테스트 실행 파일과 개인 녹화 데이터를 포함하지 않습니다.

```powershell
# 개별 개발 작업
cmake --preset windows-x64
cmake --build --preset release
ctest --preset release

# 실제 화면·소리를 사용하는 통합 시험 (로그인된 Windows 데스크톱 필요)
./build/Release/replay_integration.exe artifacts/integration 70
```

GUI 자동 시험은 `tests/ui_probe.py`, 독립 영상 디코딩 시험은 `tests/verify_media.py`입니다. 시험에만 Python·Pillow·NumPy·PyAV가 필요합니다. GUI 시험은 `REPLAYCAPTURE_DATA_DIR` 환경 변수로 설정 저장 위치를 격리합니다. 일반 사용 시 이 변수는 설정할 필요가 없습니다.

## 저장소 구성

| 파일 | 내용 |
|---|---|
| `WINDOWS_REPLAY_RECORDER_PLAN.ko.md` | 상세 개발 계획과 출시 기준 |
| `src/` | GUI, 설정, 캡처·오디오·인코딩, 순환 버퍼와 저장 엔진 |
| `tests/` | 단위·실제 캡처·GUI·독립 디코딩 시험 |
| `scripts/` | 빌드·시험·패키징 스크립트 |
| `docs/` | 사용법·구조·검증·문제 해결 |
| `README.md` | 프로젝트 개요와 현재 상태 |
| `.gitignore` | 빌드 결과, 로컬 설정, 녹화 산출물 제외 규칙 |

## 프로젝트 정보

- 저장소: [RyuGernwoo/ReplayCapture](https://github.com/RyuGernwoo/ReplayCapture)
- 관리자: RyuGernwoo
- 연락처: qesadgun@gmail.com

공개 배포용 코드 서명과 별도 오픈소스 라이선스는 아직 설정하지 않았습니다.
