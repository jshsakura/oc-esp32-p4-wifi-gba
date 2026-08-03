# SNES APU HLE (High-Level Emulation) snes9x 이식 타당성 조사 보고서

## 1. 개요 (Executive Summary & Background)

본 보고서는 현재 ESP32-P4 타깃의 `snes9x` 에뮬레이터 코어에 **SNES APU HLE (N-SPC 네이티브 플레이어 및 포트 프로토콜 라이브 와이어링)**를 이식하는 것의 기술적 타당성, 필요성, 커버리지 한계, 그리고 투자 대비 효과(ROI)를 검토한다.

### 1.1 현황 및 병목 분석
* **현재 performance**: ESP32-P4 실기 환경에서 SNES 에뮬레이션 속도는 **26~28 FPS** (60 FPS 타겟 16.7 ms 대비 1 frame 당 약 36 ms 소요)로, 지원 타겟 중 가장 낮은 성능을 보인다.
* **SPC700 부하 측정치**: `RG_BENCH_PROFILE_APU=1` 기준, `spc700.h` 내 scanline 단위 APU execution loop (`APU_EXECUTE()`)가 전체 벽시계 시간(Wall-clock time)의 **6% ~ 33%**를 점유한다. (opcode 단위 `APU_EXECUTE1()` 호출 부하는 측정 제외되었으므로, 이는 APU 비용의 Lower Bound에 해당함).
* **참조 프로젝트 (Game & Watch / STM32H7)**: STM32H735 (340 MHz Cortex-M7) 환경에서는 SPC700 + DSP LLE를 N-SPC 네이티브 HLE 플레이어로 교체하여 **49.2 FPS -> 57.3 FPS (+16%)** (Zelda ALttP 기준, Translator+Spin-skip과 결합 시 **73.3 FPS**)를 달성하였다.

### 1.2 핵심 결론 미리보기
> **최종 권고: 단기 미도입 (Deprioritize / Phase 2 검토)**
>
> SNES APU HLE는 이식에 필요한 Glue 코드 작성 및 포트 프로토콜 예외 처리가 매우 복잡하며, **전체 SNES 라이브러리의 27.4% 만 지원**할 뿐만 아니라 **효과음(SFX) 무음 처리**라는 치명적인 음질 퇴행을 동반한다. 또한, ESP32-P4에서 SPC700을 완전히 제거하더라도 예상 속도는 **33~34 FPS** 수준에 그쳐 60 FPS 타겟에 크게 미달한다.
> 따라서 APU HLE보다 **65816 CPU Spin-Skip** 및 **PPU 타일/라인 렌더러 최적화**를 최우선 과제로 진행해야 한다.

---

## 2. N-SPC Package 패키지 구성 요소 분석

Game & Watch 파이프라인 (`tools/nspc_audio_wire/`)의 N-SPC 패키지는 에뮬레이터 비독립적(Agnostic) 핵심 엔진과 에뮬레이터 전용 Glue 코드로 명확히 구분된다.

```
+-------------------------------------------------------------------------+
|                  N-SPC Audio HLE Engine (Emulator-Agnostic)             |
|  - SpcPlayer Sequencer (500 Hz Tick, N-SPC vcmd/dialect parser)         |
|  - Native DSP Mixer (32 kHz BRR decoding, ADSR, Pitch, Echo, Noise)     |
|  - Driver Signature Scanner (snes_driver_sigs.h, ARAM scan, DIR check)  |
+-------------------------------------------------------------------------+
                                    |
                                    | Interface Hooks
                                    v
+-------------------------------------------------------------------------+
|                    snes9x Specific Glue Layer (To Be Written)            |
|  - Memory Map: IAPU.RAM (64 KiB) snapshot & direct buffer pointer       |
|  - Port Protocol: S9xAPUWritePort() / S9xAPUReadPort() ($2140-$2143)    |
|  - Execution Loop: Bypass APUExecute() / APU_EXECUTE()                  |
|  - Audio Output: S9xMixSamples() / DMA Ringbuffer routing               |
+-------------------------------------------------------------------------+
```

### 2.1 에뮬레이터 독립적 모듈 (재사용 가능, ~80%)
이 모듈들은 SNES 코어 종류(`external/sm` vs `snes9x`)와 무관하게 C 라이브러리 형태로 바로 들여올 수 있다.

1. **`SpcPlayer` (N-SPC 시퀀서 및 드라이버 방언 파서)**
   * ARAM 스냅샷 기반으로 500 Hz 타이머 틱에 맞추어 N-SPC 트랙 커맨드(vcmd)를 해석.
   * `std`, `earlier`, `YI`, `gd3` 등 다양한 N-SPC 커맨드 디코딩 로직 내장.
2. **네이티브 DSP 믹서**
   * 32 kHz 16-bit 스테레오 샘플 생성 (BRR 디코딩, ADSR 엔벨로프, 피치 변환, Echo 가우시안 필터, 노이즈 제너레이터).
   * 1 sample step 당 32 CPU 사이클 단위 호스트 처리로 LLE 대비 처리 루프 대폭 축소.
3. **드라이버 시그니처 감지기 (`snes_driver_sigs.h`)**
   * ARAM 바이트 패턴 스캔을 통한 N-SPC 드라이버 버전, 곡 음악 파라미터 테이블, DIR(DSP 샘플 디렉토리) 포인터 추출.
   * DIR 페이지 유효성(Non-zero) 및 안정성(2회 연속 동일) 검증 알고리즘.

### 2.2 snes9x 전용 Glue 코드 (전면 신규 작성 필요)
참조 프로젝트의 코어는 `external/sm` (`src/snes/apu.c`, `src/dsp.c`, `apu_run()`, `dsp_cycle`)인 반면, 본 프로젝트는 `snes9x` (`spc700.c`, `apu.c`, `soundux.c`)를 사용하므로 모든 접점 코드를 snes9x 구조에 맞게 새로 설계해야 한다.

| 구분 | `external/sm` (참조용) | `snes9x` (본 프로젝트) | 이식 필요 작업 |
|---|---|---|---|
| **ARAM 버퍼** | `apu->ram` (64 KiB) | [IAPU.RAM](file:///home/jshsakura/app/oc-esp32-p4-wifi-gba/retro-core/components/snes9x/src/apu.c#L15) (`uint8_t*`, heap) | `IAPU.RAM` 버퍼 포인터를 `SpcPlayer`에 직접 바인딩 |
| **APU 실행** | `apu_run(apu, cycles)` | [APU_EXECUTE()](file:///home/jshsakura/app/oc-esp32-p4-wifi-gba/retro-core/components/snes9x/src/spc700.h#L90) / `APUExecute()` | HLE 활성화 시 `APUExecute()` 사이클 드레인 완전 비활성화 |
| **포트 쓰기** | `apu_write(snes, adr, val)` | [S9xAPUWritePort(Address, Byte)](file:///home/jshsakura/app/oc-esp32-p4-wifi-gba/retro-core/components/snes9x/src/apu.c#L125) | Port 0 메일박스 전달, Port 1~3 Instant ACK 수신 |
| **포트 읽기** | `apu_read(snes, adr)` | [S9xAPUReadPort(Address)](file:///home/jshsakura/app/oc-esp32-p4-wifi-gba/retro-core/components/snes9x/src/apu.c#L95) | `APU.OutPorts` 대신 HLE 플레이어의 동기화된 outPort 반환 |
| **오디오 믹싱** | `dsp_cycle()` 매 샘플 | [S9xMixSamples(buffer, samples)](file:///home/jshsakura/app/oc-esp32-p4-wifi-gba/retro-core/components/snes9x/src/soundux.c#L757) | `soundux.c` 믹서를 우회하여 `SpcPlayer_GenerateSamples` 직접 오디오 버퍼로 출력 |

---

## 3. snes9x 인터페이스 및 훅(Hook) 설계 Detail

snes9x 코어 내에 HLE 스왑 메커니즘을 이식하기 위해 필요한 3가지 접점 상세는 다음과 같다.

```
                    +-----------------------------+
                    |  S9xMainLoop / Frame End   |
                    +-----------------------------+
                                   |
                                   v
                    +-----------------------------+
                    | wire_try_swap() (Every 60f) |
                    | Scan IAPU.RAM & DIR stability|
                    +-----------------------------+
                             /           \
                 (Not Ready / Fail)    (Matched & Stable)
                           /               \
                          v                 v
            +-------------------+     +-------------------------+
            | Stay LLE Mode     |     | g_wire_on = 1           |
            | APU_EXECUTE() run |     | Freeze SPC700           |
            | Standard Snes9x   |     | Init SpcPlayer(IAPU.RAM)|
            +-------------------+     +-------------------------+
                                                |
                                                v
                               +----------------------------------+
                               | Intercept Ports & Audio          |
                               | - S9xAPUWritePort() -> Inbox/ACK |
                               | - S9xAPUReadPort()  -> HLE Out   |
                               | - S9xMixSamples()   -> SpcPlayer |
                               +----------------------------------+
```

### 3.1 ARAM 및 메모리 접근
* snes9x는 `S9xInitAPU()`에서 `IAPU.RAM = (uint8_t*) malloc(0x10000);` 으로 64 KiB를 할당한다.
* LLE 부팅 단계에서 65816 CPU가 IPL 통신을 통해 ARAM으로 드라이버 코드와 음원 데이터를 전송하는 동안 `IAPU.RAM`은 실시간으로 업데이트된다. HLE 스왑 시 별도 메모리 복사 없이 `IAPU.RAM`의 주소를 Zero-copy로 `SpcPlayer`에 전달할 수 있다.

### 3.2 포트 트래픽 제어 ($2140–$2143)
* **CPU 메모리 매핑**: snes9x는 `$2140–$217F` 영역에 대한 65816 CPU의 읽기/쓰기를 [ppu.c:L608](file:///home/jshsakura/app/oc-esp32-p4-wifi-gba/retro-core/components/snes9x/src/ppu.c#L608) 과 [ppu.c:L854](file:///home/jshsakura/app/oc-esp32-p4-wifi-gba/retro-core/components/snes9x/src/ppu.c#L854) 에서 각각 `S9xAPUWritePort()` 및 `S9xAPUReadPort()`로 라우팅한다.
* **HLE 포트 핸들링 규칙**:
  * **Port 0 ($2140)**: 곡 변경/재생 명령 메일박스. 쓰기 시 N-SPC 커맨드로 인큐하며, Idle-zero(00h 입력 시 이전 곡과 다르면 Stop으로 오인하는 문제)를 필터링해야 함.
  * **Ports 1~3 ($2141–$2143)**: 타원형/외부 SFX 통신 포트. Generic N-SPC HLE에서는 SFX 프로토콜을 다루지 않으므로, 65816 CPU의 무한 대기(Hang)를 방지하기 위해 쓰기 값을 그대로 에코하거나 카운터를 시뮬레이션하는 **Instant ACK** 처리를 수행함.

### 3.3 드라이버 감지 및 스왑 훅 (Detect & Swap Hook)
* **감지 시점**: 게임 시작 직후 LLE로 부팅 후, 매 60 프레임마다 `wire_try_swap()` 호출.
* **감지 조건**:
  1. `IAPU.RAM` 내 N-SPC 시그니처 패턴 스캔 (chOK >= 6 인지 검증).
  2. DIR(샘플 디렉토리 페이지)이 0으로 채워져 있지 않고, 60 프레임 전 스냅샷과 완전히 일치하는지 확인 (EarthBound 등 부팅 후 2차 뱅크 전송 게임 대응).
  3. 위 조건이 2회 연속 만족하면 `g_wire_on = 1` 로 전환.
* **스왑 실행**:
  * `IAPU.APUExecuting = false` 설정 및 `APU_EXECUTE()` 매크로 내 사이클 소진 무효화.
  * `SpcPlayer` 스태틱 구조체 초기화 및 LLE 포트0 최종 입력 명령으로 재생 개시.

---

## 4. 현실적인 한계선 및 라이브러리 커버리지 (The Honest Boundary)

Game & Watch 팀이 2,497개 SNES ROM 전체를 대상으로 수행한 자동화 스위프 DB (`docs/SNES_COMPATIBILITY.md`) 데이터를 분석한 결과, N-SPC HLE가 커버할 수 있는 게임의 범위는 다음과 같이 명확히 한정된다.

### 4.1 SNES 전체 라이브러리 호환성 통계

| 단계 / 분류 | ROM 수 | 전체 대비 비율 | 비고 |
|---|---:|---:|---|
| **전체 검사 대상 ROM** | **2,497** | **100.0%** | SQL survey DB 기준 (`rpi5-2504`) |
| **정상 부팅 & 화면 렌더링 (`lit > 0`)** | 1,816 | 72.7% | 나머지 27.3%는 부팅 실패/특수칩 미지원/화면 미출력 |
| **N-SPC 드라이버 계열 탑재** | 1,391 | 55.7% | 렌더링 성공작 중 76.6% 가 N-SPC 기반 |
| **[A] Generic HLE 호환 (`std` + `YI`)** | **683** | **27.4%** | **실기 증명 완료된 HLE 지원 대상** |
| **[B] SMW 전용 HLE (`smw_exact_wire`)** | 537 | 21.5% | SMW 전용 단일 패치 (오프셋 복구 수동 매핑) |
| **[C] GD3 (Konami) 방언** | 15 | 0.6% | 드라이버 응답 포트 대기 교착(Hang) 발생으로 LLE 고정 |
| **[D] Tose / 파라미터 복구 실패 N-SPC** | 156 | 6.2% | 드라이버 변형으로 인해 HLE 적용 불가 |
| **[E] 타사 커스텀 드라이버 (Capcom/Rare/Falcom 등)** | 425 | 17.0% | N-SPC 규격이 아니므로 100% LLE 동작 |

```
[SNES 전체 2,497 ROMs]
├─ 부팅/렌더링 실패/특수칩 미지원: 681 ROMs (27.3%)
└─ 정상 렌더링: 1,816 ROMs (72.7%)
    ├─ 커스텀/비 N-SPC 드라이버: 425 ROMs (17.0%)
    └─ N-SPC 드라이버 계열: 1,391 ROMs (55.7%)
        ├─ Tose / 미복구 N-SPC: 156 ROMs (6.2%)
        ├─ Konami GD3 (동작 불가): 15 ROMs (0.6%)
        ├─ SMW 계열 (SMW 전용 플레이어 필요): 537 ROMs (21.5%)
        └─ ★ Generic N-SPC HLE 적용 가능 (std + YI): 683 ROMs (27.4%)
```

### 4.2 Generic N-SPC HLE의 제약 및 기능적 손실
1. **효과음 (SFX) 완전 상실**
   * Generic `nspc_wire`는 BGM(음악) 시퀀서만 재생한다.
   * 효과음 포트(Ports 1~3)는 Instant ACK로 넘기므로, 점프음, 공격음, 메뉴 효과음 등이 **전혀 출력되지 않고 무음(Silent) 처리**된다.
2. **동적 뱅크 재업로드(Mid-game Re-upload) 대응 불가**
   * 게임 플레이 도중 APU 뱅크를 새로 전송하는 타이틀은 HLE 상태가 파괴된다. (LLE 복귀/재감지 무효화 로직 미구현).
3. **프로토콜 타임스탬프 차이로 인한 루프 이상**
   * 예: *Zelda: A Link to the Past* 타이틀 테마곡이 LLE에서는 무한 루프되지만, HLE 환경에서는 ~19초 후 곡이 정지되는 문제 존재.

---

## 5. 종합 평가 및 최종 권고사항 (ESP32-P4 최적화 전략)

### 5.1 ESP32-P4 성능 개선 예측 및 Gap 분석

* **현재 ESP32-P4 SNES 프레임 시간**: ~36 ms / frame (**26 ~ 28 FPS**)
* **APU 점유율**: 전체의 6% ~ 33% (평균 약 15~20% 내외)
* **APU HLE 적용 시 절감 가능 시간**: 약 5 ~ 7 ms 절감 예측
* **APU HLE 단독 적용 후 예상 프레임**: ~29 ~ 30 ms / frame (**33 ~ 34 FPS**)

> **핵심 한계**: APU 부하를 0%로 만든다 하더라도, 60 FPS 타겟(16.7 ms)과의 격차는 여전히 **13 ms 이상** 존재한다.
> STM32H7 참조 플랫폼이 73.3 FPS를 달성할 수 있었던 이유는 **65816 CPU 정적 번역기(Translator)** + **PPU 라인/타일 어셈블리 렌더러** + **CPU Spin-Skip**이 이미 조합되어 있었기 때문이며, 오디오 HLE 단독으로 60 FPS를 만든 것이 아니다.

### 5.2 ROI (투자 대비 효과) 및 리스크 평가

| 평가 항목 | APU HLE 이식 | 65816 CPU Spin-Skip | PPU 타일/라인 렌더러 최적화 |
|---|---|---|---|
| **예상 속도 향상** | +5 ~ 6 FPS (33 FPS) | **+10 ~ 15 FPS (40+ FPS)** | **+8 ~ 12 FPS (38+ FPS)** |
| **라이브러리 적용률** | **27.4%** (683 ROMs) | **52.5%** (Pure Spin ≥50%) | **100%** (모든 게임) |
| **부작용/부작용 리스크** | **SFX 효과음 무음** (치명적) | Hint-gate 미적용 시 타이틀에 따라 오버헤드 | 없음 (비트 일치 유지) |
| **구현 난이도** | **Very High** (포트 프로토콜/스왑) | Medium (NMI loop detection) | High (RISC-V SIMD/PPA) |
| **ROI (투자 대비 효과)** | **LOW** | **HIGH** | **VERY HIGH** |

### 5.3 최종 최적화 로드맵 제안

1. **1단계 (최우선 과제): 65816 CPU Spin-Skip (`snes_spin`) 도입**
   * SNES 게임의 52.5% (941/1,792 ROMs)는 CPU 시간의 50% 이상을 NMI 대기 루프(Spin)에서 소모한다.
   * NMI spin replay / skip 인터셉트를 구현하여 부하를 대폭 삭감 (부작용 방지를 위한 hint-gate 필수).
2. **2단계 (핵심 최적화): PPU 타일 및 라인 렌더러 최적화**
   * ESP32-P4 (RISC-V Dual Core @ 360 MHz)의 L2 캐시 및 PPA 하드웨어, SIMD 확장 명령을 활용하여 `gfx.c` / `tile.c` 렌더링 파이프라인 가속.
3. **3단계 (보커스/선택 과제): APU HLE 검토**
   * 1~2단계를 통해 SNES 에뮬레이션 속도가 **45 ~ 50 FPS** 대에 진입한 후, 60 FPS 마무리를 위한 2차 레버로 APU HLE 도입 여부를 재검토한다.

---

## 6. 리뷰 노트 (Claude, 2026-08-03) — 권고 1순위에 대한 확인 필요 사항

본 보고서의 분석과 결론(APU HLE 후순위)에 동의한다. 특히 5.1의 지적 — *APU 부하를
0으로 만들어도 60fps와 13ms 이상 벌어진다* — 는 실기 측정과 맞는다. 다만 **1순위
권고인 "65816 CPU Spin-Skip 도입"에는 확인이 필요하다.**

**우리 snes9x에는 이미 스핀 스킵이 있고, 기본으로 켜져 있다.**
`retro-core/components/snes9x/src/memmap.c:617`이 `Settings.Shutdown = true`로 두고,
1630행 부근에서 문제가 알려진 소수 타이틀(Clay Fighter, Madden, NHL, WeaponLord 등)만
끈다. 동작 지점은 `cpuops.c:2377` — `CPU.PC == CPU.WaitAddress`일 때 남은 사이클을
버린다.

이것이 보고서가 말하는 gnw의 `snes_spin`(NMI 대기 루프 replay/skip)과 **같은 것은
아니다.** snes9x 쪽은 자기 자신으로 분기하는 고전적 shutdown이고, gnw 쪽이 더
공격적이다. 따라서 권고가 틀렸다는 뜻이 아니라, **"+10~15 FPS"라는 추정치가 우리가
이미 가진 이득을 포함해 계산되었을 가능성**이 있다는 뜻이다.

**이건 추정이 아니라 계측으로 가릴 수 있다.** `Settings.Shutdown = false`로 두고
재면, 지금 이미 있는 스핀 스킵이 얼마를 벌어주고 있는지가 바로 나온다.
- 크게 느려지면: 스핀 스킵은 이미 작동 중이고, gnw 기법의 추가 여유는 그 차이만큼이다
- 변화가 없으면: snes9x의 shutdown이 이 게임들에서 발화하지 않는다는 뜻이고, 보고서의
  추정치가 그대로 살아 있다

**측정 없이 2단계(PPU 렌더러)로 넘어가면 안 되는 이유**도 여기 있다. 오늘 SNES에서
"그럴듯한데 아무것도 측정하지 않은" 프로브가 이미 셋 나왔다 — 프론트엔드 오디오
토글(믹싱만 막음), `cpuexec.c`의 blargg APU 경로(`USE_BLARGG_APU` 미정의로 죽은 코드),
그리고 SPC700 제거(게스트가 APU 핸드셰이크를 기다려 워치독 사망). 같은 함정이 여기에도
있다.

측정 방법은 `docs/`가 아니라 메모리의 `oc-gba-measurement-rig`에 적혀 있다. 요약하면
`busyPercent`는 발열로 흐르므로 비교 지표는 `us/frame`(= `BUSY% * 10000 / FPS`)이고,
A/B는 반드시 교차(A→B→A)로 잰다.
