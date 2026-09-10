# ReplayCapture 아키텍처 🛠️

이 문서는 외부 개발자가 현재 구현의 경계와 데이터 흐름을 빠르게 이해하기 위한 요약입니다. 최초 요구사항과 상세 설계 판단은 [초기 구현 계획](../WINDOWS_REPLAY_RECORDER_PLAN.ko.md)에 보존되어 있습니다.

## 시스템 개요

ReplayCapture는 Windows 11 x64용 네이티브 데스크톱 앱입니다. 화면과 시스템 출력 오디오를 수집해 메모리 순환 버퍼에 유지하고, GUI·전역 단축키·트레이 메뉴의 저장 요청을 독립된 MP4 내보내기 작업으로 처리합니다.

~~~mermaid
flowchart LR
    subgraph Input[입력]
        V[Windows.Graphics.Capture]
        A[WASAPI Loopback]
    end

    subgraph Core[녹화 코어]
        C[Engine]
        B[ReplayBuffer]
        Q[Export Queue]
    end

    subgraph Output[출력]
        E[Media Foundation]
        M[H.264 + AAC MP4]
    end

    UI[Win32 GUI · Tray · Hotkey] --> C
    V --> C
    A --> C
    C --> B
    UI --> Q
    B --> Q
    Q --> E --> M
    S[Settings · Auto Start] --> UI
    S --> C
~~~

## 모듈 지도

| 파일 | 책임 |
|---|---|
| <code>src/main.cpp</code> | Win32 창, 트레이, 메시지 루프, 전역 단축키, 설정 화면 연결 |
| <code>src/engine.*</code> | 캡처 수명 주기, 상태, 저장 요청 큐와 작업 스레드 |
| <code>src/media.*</code> | 장치 열거, 화면·오디오 수집, H.264/AAC 인코딩과 MP4 작성 |
| <code>src/buffer.*</code> | 시간·메모리 한도 기반 샘플 보관과 저장 스냅샷 |
| <code>src/settings.*</code> | 로컬 설정 검증·직렬화와 기본값 관리 |
| <code>src/common.h</code> | 공통 형식, 런타임 초기화와 오류 처리 보조 |
| <code>src/resource.h</code>, <code>src/app.rc</code> | 실행 파일과 창·트레이 아이콘 리소스 |
| <code>tests/</code> | 순환 버퍼, 설정, 내보내기 핵심 동작 검증 |

## 저장 흐름

~~~mermaid
sequenceDiagram
    participant UI as GUI·Hotkey·Tray
    participant RC as Engine
    participant RB as ReplayBuffer
    participant EQ as Export Queue
    participant MF as Media Foundation

    RC->>RB: 인코딩된 영상·오디오 샘플 추가
    UI->>RC: 최근 N초 저장 요청
    RC->>RB: 요청 시점의 불변 스냅샷 생성
    RB-->>EQ: 키프레임부터 시작하는 샘플 묶음
    EQ->>MF: 시간축을 0부터 재기준화해 MP4 작성
    MF-->>UI: 완료 또는 오류 알림
~~~

저장 요청은 현재 버퍼의 스냅샷을 사용하므로 내보내기 중에도 새 샘플을 계속 수집할 수 있습니다. 영상은 디코딩 가능한 H.264 키프레임부터 시작하며 오디오는 같은 기준 시각에 맞춰 잘립니다. 그 결과 파일 길이는 요청값보다 최대 한 키프레임 간격 정도 길 수 있습니다.

## 상태와 복구

~~~mermaid
stateDiagram-v2
    [*] --> Stopped
    Stopped --> Recording: 녹화 시작
    Recording --> Paused: 일시 정지
    Paused --> Recording: 다시 시작
    Recording --> Stopped: 녹화 중지
    Paused --> Stopped: 녹화 중지
    Recording --> Recovering: 장치·세션 변경
    Recovering --> Recording: 재초기화 성공
    Recovering --> Stopped: 재초기화 실패
~~~

잠금·절전과 캡처 대상 종료 시에는 오래된 버퍼를 그대로 재사용하지 않습니다. 설정 변경이 캡처 형식에 영향을 주면 세션을 다시 만들고 버퍼를 초기화합니다.

## 기술 스택

<div>
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white" alt="C++20" />
  <img src="https://img.shields.io/badge/Win32-GUI-0078D4?logo=windows&logoColor=white" alt="Win32 GUI" />
  <img src="https://img.shields.io/badge/C%2B%2FWinRT-Windows_Runtime-0078D4" alt="C++/WinRT" />
  <img src="https://img.shields.io/badge/Direct3D-11-76B900" alt="Direct3D 11" />
  <img src="https://img.shields.io/badge/Audio-WASAPI-6A5ACD" alt="WASAPI" />
  <img src="https://img.shields.io/badge/Media-Media_Foundation-FF6F00" alt="Media Foundation" />
  <img src="https://img.shields.io/badge/Build-CMake-064F8C?logo=cmake&logoColor=white" alt="CMake" />
</div>

## 설계 제약

- 대상은 Windows 11 x64이며 단일 SDR 모니터를 녹화합니다.
- 시스템 출력 오디오는 mono 또는 stereo PCM을 AAC로 인코딩합니다.
- 프레임 단위 길이 보정보다 저장 중 캡처가 멈추지 않는 것을 우선합니다.
- 설정과 영상은 로컬에 저장하며 네트워크 업로드 경로가 없습니다.
- 마이크, HDR, 여러 모니터 합성, 보호 콘텐츠 캡처는 현재 범위 밖입니다.

빌드 방법은 [README 개발자 가이드](../README.md#-외부-개발자-가이드), 실제 확인 범위는 [검증 보고서](validation-report.md)를 참고하세요.
