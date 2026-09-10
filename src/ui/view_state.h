#pragma once
#include "engine.h"

namespace replay::ui {
struct SaveState {
    bool enabled{};
    std::wstring explanation;
};
inline SaveState saveState(Status const &s, int seconds, Settings const &settings) {
    if (seconds < 1 || seconds > settings.retention)
        return {false, L"저장 길이는 1초 이상, 보관 시간 이하여야 합니다."};
    if (s.state != L"녹화 중")
        return {false, s.state == L"일시정지" ? L"기록이 비워졌습니다. 재개하면 새로 쌓습니다."
                                              : L"녹화를 시작하면 최근 구간을 저장할 수 있습니다."};
    if (s.available <= 0)
        return {false, L"첫 기록을 준비하고 있습니다."};
    if (s.available < seconds)
        return {!settings.requireFull,
                settings.requireFull ? L"전체 길이를 기다리는 중입니다. 확보 시간을 확인하세요."
                                     : L"아직 요청 길이보다 기록이 짧습니다. 지금 확보된 구간만 저장합니다."};
    return {true, L"저장할 준비가 되었습니다. 저장 중에도 녹화는 계속됩니다."};
}
} // namespace replay::ui
