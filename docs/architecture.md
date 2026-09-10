# 구현 구조

## 구성

| 모듈 | 책임 |
|---|---|
| `src/main.cpp` | 한국어 Win32 GUI, 트레이, 전역 단축키, 폴더 선택, 작업 관리, 잠금·절전 이벤트 |
| `src/settings.*` | JSON 읽기·검증·원자적 교체, 사용자별 저장 경로 |
| `src/media.*` | WGC, D3D11 영상 변환, WASAPI, Media Foundation 인코더·MP4 다중화 |
| `src/buffer.*` | 키프레임을 유지하는 압축 순환 버퍼와 참조 공유 스냅샷 |
| `src/engine.*` | 캡처 worker, 저장 요청 시간 고정, 비동기 저장 worker, 작업·메모리 상한 |

사용자 프로그램은 `ReplayCapture.exe` 하나입니다. CLI나 외부 제어 서버는 없습니다. 테스트 실행 파일은 개발용이며 배포 ZIP에 포함하지 않습니다.

## 데이터 흐름

WGC의 GPU 표면을 D3D11 video processor로 NV12·목표 크기로 변환합니다. 첫 구현은 NV12를 CPU staging buffer로 읽어 인코더에 전달합니다. 하드웨어 H.264 MFT를 우선 열거하며 하드웨어 인코더가 없으면 Windows 소프트웨어 인코더를 사용합니다. 진단 화면에서 선택된 인코더를 확인할 수 있습니다.

WASAPI loopback의 QPC 시각을 48kHz 스테레오 PCM 시간축으로 변환합니다. 장치 데이터 도착에 120ms 여유를 두고 없는 구간은 무음으로 채웁니다. AAC 인코더는 1024샘플 블록을 처리합니다. 영상과 소리는 같은 단조 시계 원점을 사용하며 컴퓨터 시각 변경의 영향을 받지 않습니다.

압축 샘플은 시간·바이트 제한을 받는 메모리 버퍼로 들어갑니다. 영상 삭제는 키프레임 경계를 유지합니다. 저장 요청은 접수 시점 T를 고정하고 인코더 처리 여유 뒤 T 이전 샘플을 선택합니다. 보관 시간 외 2초의 처리 여유를 두며 바이트 상한이 우선합니다. 스냅샷은 데이터 복사 대신 참조를 공유하고, 저장 중 참조 메모리도 총 예산에 포함합니다.

저장 worker는 압축 H.264/AAC 샘플을 공통 원점으로 이동해 MP4에 기록합니다. `.partial` 파일을 finalize한 뒤 최종 이름으로 변경합니다. 실패·취소 시 해당 임시 파일을 정리합니다. 완료 파일은 덮어쓰지 않습니다.

## 계획서 대비 확정한 구현 선택

- 모듈을 파일 단위로 구성했습니다. 향후 규모가 커지면 계획서의 세부 디렉터리로 나눌 수 있습니다.
- 모든 설정 적용은 일관되게 새 캡처 세션을 시작합니다. 보관 시간만 바꿀 때도 버퍼 초기화를 GUI에서 안내합니다.
- 캡처 프레임은 QPC 기반 고정 FPS 스케줄로 샘플링합니다. 입력 프레임 변동과 인코더 수용 불가를 진단의 누락 수로 확인합니다.
- GPU→CPU NV12 복사가 있는 구현입니다. GPU 메모리 직접 전달 최적화는 현재 구현의 성능 결과를 기준으로 후속 검토합니다.
- 오디오 입력은 mono/stereo로 제한하며 surround 장치는 스테레오 설정을 안내합니다. HDR은 잘못된 색으로 저장하지 않도록 감지 후 거절합니다.
- 자동 실행은 앱 시작만 의미합니다. 로그인 직후 자동으로 녹화하지 않습니다.
- 설정 JSON은 플랫 스키마를 사용합니다. GUI가 모든 사용자 설정을 제공하므로 파일 직접 편집은 필요하지 않습니다.

## 공식 API 참고

- [Windows 화면 캡처](https://learn.microsoft.com/en-us/windows/uwp/audio-video-camera/screen-capture)
- [WASAPI loopback](https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording)
- [WASAPI 시간 정보](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudiocaptureclient-getbuffer)
- [H.264 Media Foundation 인코더](https://learn.microsoft.com/en-us/windows/win32/medfound/h-264-video-encoder)
- [Sink Writer: 동일 압축 형식의 재다중화](https://learn.microsoft.com/en-us/windows/win32/medfound/using-the-sink-writer)
