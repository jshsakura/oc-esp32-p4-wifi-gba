# oc-gba 로드맵

ESP32-P4 기반 `oc-gba` 보드에서 어디까지 갈 수 있는지, 그리고 어떤 순서로 갈지에 대한 문서.

- 최초 작성 2026-07-29 / 최종 갱신 2026-07-29 (브랜치 `oc-gba`)
- 근거 조사 결과는 부록에 남겨둠. **같은 걸 두 번 찾아보지 않기 위한 문서이므로, 판정을 바꿀 때는 근거도 같이 갱신할 것.**

---

## 1. 하드웨어 예산

한계를 정하는 건 클럭이 아니라 아래 항목들이다.

| 자원 | 값 | 에뮬레이션에서의 의미 |
|---|---|---|
| CPU | RISC-V 2코어 @360MHz (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=360`) | **PSP(333MHz MIPS)와 같은 체급.** gpSP가 원래 겨냥한 하드웨어 |
| ISA | `rv32imafc_zicsr_zifencei_xesppie`, ABI `ilp32f` | RV32이고 `a`(원자)·`d`(배정밀도) 확장 없음 |
| 내부 SRAM | **768KB**, L2 캐시 128KB / 라인 64B | **최대 제약.** 3절의 예산 문제 참조 |
| PSRAM | hex(x16) @200MHz, 패키지 내장 | 대역폭은 S3의 4~5배. **내장이라 증설 불가** |
| 표시 | MIPI-DSI (ST7701), PPA / 2D-DMA | 현재는 CPU가 회전·바이트스왑 담당 |
| 가속기 | JPEG 디코더(HW), H.264 **인코더** | 디코더는 JPEG뿐 — 영상 수신은 MJPEG만 실용적 |
| WiFi | 없음. ESP32-C6 동반칩 (ESP-Hosted / SDIO) | 넷플레이·웹UI는 SDIO 대역폭과 CPU를 소비 |

### 메모리 증설에 대하여

**불가능하고, 용량은 병목도 아니다.** PSRAM이 패키지 내장이라 보드에서 못 바꾼다. 그리고 병목은 L2 캐시 128KB와 접근 지연이라 용량을 늘려도 프레임레이트는 안 오른다. 늘어나는 건 "돌릴 수 있는 기종의 범위"이지 속도가 아니다.

---

## 2. 기종별 실현 티어

| 등급 | 기종 | 판정 |
|---|---|---|
| **A. 여유** | NES, GB/GBC, SMS/GG/CV, PC엔진, Lynx, MSX, Doom | 코어 존재(`retro-core`, `fmsx`, `prboom-go`). 보드 브링업만 끝나면 60fps |
| **B. 실질 최대치** | **GBA**, 메가드라이브, SNES | `gwenesis`는 무난. **GBA가 본진**이며 다이나렉이 관건 |
| **C. 애매** | 네오지오 MVS | CPU는 되지만 ROM 100MB+ SD 스트리밍이 못 따라감 |
| **D. 사실상 불가** | **PS1** | 다이나렉 인프라를 밑바닥부터 지어야 함 → [부록 A](#부록-a-다이나렉-조사-2026-07-29) |
| **E. 절대 불가** | N64 / 새턴 / DS / PSP 이상 | GPU 부재. 자릿수 초과 |

> **한 줄 요약: 16비트 2D가 천장이다.**

### 경계가 그어지는 이유

- **GBA/SNES에서 안 끊기는 이유** — 게스트 CPU가 8~17MHz대라 감당되고, PPU가 스캔라인 단위라 예측 가능하며, 워킹셋이 L2 캐시에 얹힌다.
- **PS1에서 끊기는 이유** — CPU 자체는 다이나렉만 있으면 계산상 여유가 있다. 문제는 (1) 쓸 수 있는 RV32 다이나렉 프레임워크가 없다는 것과 (2) 소프트웨어 텍스처 래스터라이저가 코어 하나를 먹으면서 코드캐시와 SRAM 자리를 다툰다는 것.
- **N64에서 완전히 끊기는 이유** — CPU 에뮬레이션이 아니라 **RSP/RDP라는 별도 프로세서 2개**를 더 흉내내야 한다.

### N64 정적 재컴파일에 대하여

[N64Recomp](https://github.com/N64Recomp/N64Recomp) / [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp)는 실재하지만 **판정을 바꾸지 않는다.**

- 에뮬레이터가 아니라 ROM을 오프라인에서 C로 번역하는 **정적 재컴파일**이다 (그래서 게임마다 별도 프로젝트).
- 렌더러 RT64가 **D3D12 / Vulkan 1.2 / Metal Argument Buffers Tier 2 GPU를 요구**한다.
- 즉 저 기술이 없앤 것은 **CPU 에뮬레이션 비용**이고, P4가 못하는 것은 **그래픽**이다. 없애준 것과 막힌 것이 겹치지 않는다.

발상 자체("번역을 오프라인으로")는 MCU에 유리하지만, GBA에 적용하려면 셀프 모디파잉 코드와 뱅킹 때문에 정적 분석이 깨지고, **게임별 빌드가 되어 "롬 골라서 실행하는 런처"라는 이 프로젝트의 전제가 무너진다.**

---

## 3. SRAM 예산 — 이 기기 고유의 진짜 문제

CPU 성능은 선례가 증명해준다(부록 B). **PSP에는 없었고 우리에게만 있는 문제는 메모리 배치다.**

GBA 메모리 선언 (`gbsp/components/gbsp-libretro/gba_memory.h:247~254`):

```c
u8 ewram[1024 * 256 * 2];   // 512KB  (256KB 실메모리 + 256KB SMC 추적)
u8 iwram[1024 * 32 * 2];    //  64KB
u8 vram[1024 * 96];         //  96KB
u16 palette_ram[512], oam_ram[512];  // 2KB
                            // ────────
                            //  674KB
```

**내부 SRAM 768KB 중 674KB를 GBA 메모리만으로 쓴다.** 여기에 JIT 코드캐시 + retro-go 프레임버퍼 + FreeRTOS + 오디오 버퍼가 더 들어가야 한다. 그대로는 성립하지 않는다.

### 배치 우선순위

| 순위 | 대상 | 크기 | 근거 |
|---|---|---|---|
| 1 | **IWRAM** | 32KB | 게임이 뜨거운 코드/데이터를 여기 둔다 |
| 2 | **JIT 코드캐시** | 튜닝 대상 | 최대 성능 손잡이 |
| 3 | **VRAM** | 96KB | PPU가 매 스캔라인 두들긴다 |
| 4 | **EWRAM** | 256KB | 승부처. 넣고 싶지만 자리가 없을 수 있음 |
| 5 | SMC 추적 배열 | 288KB | **압축 여지가 가장 큼** (페이지 단위 더티 비트 등) |

### 이 절이 걸린 미해결 질문

**Q1. P4에서 PSRAM 영역의 명령어 인출이 가능한가? → 2026-07-31 실기 측정. 답은 "된다", 단 예상과 반대의 함정이 있다.**

| 대상 | 결과 |
|---|---|
| PSRAM, 캐시 상주 | **346 MIPS** — 360MHz에서 사실상 전속력 |
| PSRAM, 스트리밍(1MB를 블록마다 한 번씩) | **8 MIPS** — 캐시를 벗어나면 **43배** 느려짐 |
| 일반 힙의 내부 RAM (`MALLOC_CAP_INTERNAL`) | **Instruction access fault** |

세 가지가 따라 나온다:

1. **힙은 실행 가능 메모리를 주지 않는다.** `MALLOC_CAP_EXEC`는 PSRAM에서도 내부에서도 NULL이다.
   PSRAM은 플래그 없이 잡아도 실행됐다.
2. **거꾸로다.** PSRAM은 실행되고 **내부 RAM이 폴트난다** — 메모리 보호가 DRAM에 실행 권한을 주지
   않기 때문이다(`CONFIG_SPIRAM_PRE_CONFIGURE_MEMORY_PROTECTION`, `targets/oc-gba/sdkconfig:203`).
   `esp_ptr_executable()`은 양쪽 다 1이라고 답하므로 믿으면 안 된다. **"뜨거운 블록은 IRAM"은
   그냥 되는 게 아니라, 실행 가능한 내부 영역을 따로 확보하는 작업이 선행돼야 한다.**
3. **설계를 가르는 숫자는 43배다.** 코드캐시를 PSRAM에 두는 것 자체는 가능하지만, L2 128KB 안에
   머무는 블록만 전속력이다. 즉 캐시 크기가 곧 성능이고, 코드캐시는 L2에 맞춰 설계하고 PSRAM은
   넘칠 때의 저장소로 보는 것이 맞다.

프로브: `components/retro-go/rg_psram_exec_test.c`, 절차는 [BRINGUP.md A1](BRINGUP.md).

**Q2. SMC 추적 배열 288KB를 줄일 수 있는가?**
성공하면 EWRAM을 내부로 올릴 여유가 생긴다.

**Q3. 코드 생성 후 캐시 유지보수 절차** — RISC-V `fence.i` + P4 L2 라이트백.

---

## 4. 단계별 계획

### Phase 0 — 보드 브링업 ← **현재 위치**

핀맵, DSI, 다중 I2C 확장칩 입력, 파티션 테이블. 브랜치 `oc-gba`의 커밋 4개.

### Phase 1 — 표시/입력/사운드 파이프라인 확정

- **CPU 블릿 제거.** `components/retro-go/drivers/display/st7701.h`의 `lcd_send_buffer()`가 회전·바이트스왑을 픽셀 단위 루프로 처리 중. Phase 2의 전제조건.
  - **2026-08-01 기종별 실측** (패널 미연결, 코어 1의 `rg_display` 태스크에서 에뮬레이터와 병렬):

    | 기종 | 소스 → 출력 | 블릿 |
    |---|---|---|
    | PICO-8 | 128×128 → 384×384 | **3.0ms** |
    | NES | 256×224 → 512×448 | **6.0ms** |
    | 메가드라이브 | 320×224 → 640×448 | **7.2ms** |
    | GBA | 240×160 → 720×480 | **7.6ms** |
    | 게임앤워치 | 320×240 → 640×480 | **8.8ms** |
    | SNES | 256×224 → 512×448 | **10.2ms** |

    출력 픽셀당 7~8사이클이 기본선이다. **SNES만 같은 출력 크기에 두 배가 든다** — 소스 서피스가
    PSRAM에 있기 때문으로 보인다(오늘의 주제 그대로다). 확인하고 내부로 올릴 것.

    그리고 **정수배 스케일링이 기본이 되면서 블릿이 크게 싸졌다.** 같은 PICO-8이 7월 30일에는
    480×480 비정수배로 13ms였는데 384×384 정수배로 3.0ms다. 출력 픽셀 수는 1.6배 줄었는데
    시간은 4.3배 줄었다 — **비정수배 스케일링은 픽셀 수에 비례하는 것 이상으로 비싸다.**
  - ⚠️ 이 줄은 한 번 틀리게 적혔었다. "블릿이 CPU의 98%"라고 썼는데, 실제로는 패널이 응답하지 않을 때
    포기하고 남겨둔 `st7701_init` 태스크가 우선순위 5로 계속 돌면서 90%를 먹고 있었다
    ([BRINGUP.md](BRINGUP.md) 참조). 그 상태에서 잰 모든 수치는 그 태스크를 잰 것이었다.
    **패널 없이 성능을 잴 때는 FreeRTOS 런타임 통계로 누가 CPU를 쓰는지부터 확인할 것.**
  - 후보 1: **PPA / 2D-DMA** (스케일·회전)
  - 후보 2: **`xesppie`** — 툴체인 `-march`에 붙어 있는 Espressif 벤더 SIMD 확장
- 오디오 버퍼·지연 확정
- **A티어 전 코어 60fps 실측**

### Phase 2 — GBA 60fps

**방침: `jit_dev`를 버리고 gpSP 본래 다이나렉을 복원한다.** 근거는 [부록 C](#부록-c-jit_dev-감사-2026-07-29).

| 단계 | 내용 |
|---|---|
| 2a | `gbsp` 인터프리터 실측 (기준선) — 실기 필요 |
| 2b | **Q1 답 확정** (PSRAM 명령어 인출 가부) — 설계 분기점 |
| 2c | gpSP 다이나렉 프레임워크 복원 (`cpu_threaded.c` 등) |
| 2d | **`riscv_emit.h` / `riscv_codegen.h` / `riscv_stub.S` 작성** |
| 2e | SRAM 예산 설계 및 튜닝 (2c~2d와 동시 진행) |

작업량 산정은 5절.

### Phase 3 — 정확도

메가드라이브/SNES 오디오·타이밍 다듬기, SNES 코어 편입.

### Phase 4 — WiFi (ESP32-C6 / ESP-Hosted)

웹UI 롬 업로드, OTA, 넷플레이, 그리고 원격 렌더 클라이언트(6절).

### Phase 5 — 보류

PS1. Phase 2가 실제로 완료된 **이후에만** 재검토한다.

---

## 5. Phase 2 작업량 산정

업스트림 [libretro/gpsp](https://github.com/libretro/gpsp) 실측 기준.

### 공짜로 얻는 것

| 파일 | 줄 수 | 성격 |
|---|---|---|
| `cpu_threaded.c` | **3,415** | 아키텍처 독립 다이나렉 구동부. **그대로 재사용** |
| `tests/` | — | 업스트림 테스트 스위트 존재 |

`cpu_threaded.c`가 백엔드에 요구하는 고유 인터페이스는 **`generate_*` 7종**뿐이다. 나머지는 백엔드 내부 사정.

### 직접 써야 하는 것 (MIPS 백엔드가 본보기)

| 파일 | 줄 수 | 정의 수 | 난이도 |
|---|---|---|---|
| `mips_emit.h` → `riscv_emit.h` | 2,813 | 매크로 341개 (`generate_*` 129개) | **핵심.** 지연 플래그·조건 실행·메모리 고속경로·블록 링킹 |
| `mips_codegen.h` → `riscv_codegen.h` | 406 | 인코더 68개 | 기계적. SLJIT RV32 인코딩과 대조 가능 |
| `mips_stub.S` → `riscv_stub.S` | 636 | 진입점 14개 | 디스패처·메모리 핸들러·컨텍스트 스위치를 RV32 ABI로 |
| **합계** | **3,855** | | |

### 일정 추정

| 작업 | 추정 |
|---|---|
| 프레임워크 복원 + 빌드 통합 (이 포크의 `cpu.cpp`는 업스트림 `cpu.cc`에서 갈라져 있어 머지 필요) | 1~2주 |
| `riscv_codegen.h` (인코더 68개) | 3~5일 |
| `riscv_emit.h` (generate_* 129개) | 3~6주 |
| `riscv_stub.S` (진입점 14개) | 1~2주 |
| SRAM 예산 설계·튜닝 | 2~3주 |
| **디버깅** | 위 전체와 맞먹음 |

**풀타임 기준 6~10주. 개인 프로젝트로는 2~4개월.**

### 위험 요소

**다이나렉은 디버깅이 본체다.** "게임마다 다르게 깨지는" 꼬리가 길고, `jit_dev`가 정확히 그 꼬리에서 죽었다(부록 C). 완화 수단 두 가지:

1. 업스트림 `tests/` 스위트를 먼저 돌릴 수 있게 만들 것
2. 인터프리터와 **한 명령씩 대조 실행**하는 하네스를 초기에 구축할 것 — 나중에 만들면 이미 늦다

---

## 6. 원격 렌더 클라이언트 (Phase 4 확장)

브라우저와 웹게임(포케로그류)을 이 기기에서 보고 싶다는 요구는 **하나의 아키텍처로 동시에 해결된다.** 서버가 진짜 브라우저를 돌리고, 기기는 렌더된 이미지와 입력만 주고받는 방식(오페라 미니 방식).

**이 보드에 맞는 이유**: P4의 **하드웨어 JPEG 디코더** 덕에 MJPEG 수신이 CPU를 거의 안 먹는다. 800×480 MJPEG는 2~5Mbps라 ESP-Hosted 대역폭에 여유가 있고, 지연 50~100ms는 턴제 게임과 웹 브라우징에 문제없다. (P4의 H.264는 **인코더**뿐이라 영상 수신에는 못 쓴다.)

retro-go는 앱마다 OTA 파티션을 쓰므로(`launcher`/`retro-core`/`gbsp`) 네 번째 앱으로 추가하면 된다. **다만 파티션 테이블이 이미 3개로 차 있어 플래시 여유부터 실측해야 한다.**

### 하지 말아야 할 것들 (기록)

- **웹/Node 그대로 포팅**: 불가능. 포케로그는 Phaser 3(WebGL) 위에 있어 필요한 건 JS 엔진이 아니라 브라우저 전체다. Node(V8)는 MMU 있는 OS와 수백 MB RAM 전제.
- **로컬 브라우저**: 불가능. 크로미움/WebKit/Gecko는 탭 하나가 100~200MB이고, 더 근본적으로 **P4에 MMU가 없다**(PMP만 존재). RAM을 늘려도 빌드조차 안 된다. GPU도 없다.
- **QuickJS/XS + 로직 이식**: Phaser 의존 제거 = 사실상 재작성. 그럴 거면 C로 쓰는 게 빠르다.
- **JS 없는 HTML 렌더러**(litehtml 등): 문서·위키·RSS에는 쓸 만하나, 요즘 웹 대부분이 JS로 DOM을 만들어 **화면이 빈 채로 뜬다.**

참고: `launcher/main/webui.c`는 **반대 방향**이다(기기가 웹서버가 되어 PC로 롬 업로드).

---

## 7. 실기 측정 방법

`rg_system.c:254`가 주기적으로 출력:

```
STACK:.., HEAP:.., BUSY:87%, FPS:60 (0+12+48), BATT:..
         └ CPU 점유율          └ 총 (스킵+부분+완전)
```

**⚠️ 함정: 오토 프레임스킵이 켜져 있다** (`rg_system.c:268~`). 속도가 96% 밑으로 떨어지면 프레임을 버려서 `FPS:60`을 유지한다. 그대로 보면 착각한다.

- **괄호 안 마지막 숫자(fullFPS)** 와 **`BUSY:%`** 를 볼 것
- 정확히 재려면 **프레임스킵 0 고정**
- 저 줄은 `RG_LOG_DEBUG`라 **로그 레벨을 DEBUG로** 올려야 보임
- 조건 고정: 같은 롬, 같은 플레이 구간(타이틀 화면 말고), 30초 이상

---

## 부록 A. 다이나렉 조사 (2026-07-29)

### A.1 GNU Lightning — RV32에서 성립 안 함

[Lightrec](https://opendingux.net/emulation/2019/10/04/introducing-lightrec.html)(PS1)은 GNU Lightning에 코드 생성을 위임한다. Lightning 2.1.3(2019)부터 RISC-V 백엔드가 있어 처음엔 "이미 있는 부품"으로 보였으나, 소스 확인 결과 **사실상 RV64 전용**이었다.

결정적 증거 — `lib/jit_riscv-sz.c`:

```
1:   #if __WORDSIZE == 64
2:   #define JIT_INSTR_MAX 116
...
591: #endif /* __WORDSIZE */
```

파일 전체가 64비트 가드 하나로 감싸여 있고 32비트 분기가 없다. Lightning이 명령어별 코드 크기를 알아야 버퍼를 잡는데, RV32에서는 `JIT_INSTR_MAX`조차 정의되지 않는다.

| 파일 | 32비트 분기 | 64비트 분기 |
|---|---|---|
| `lib/jit_riscv.c` (본체) | **0** | 5 |
| `lib/jit_riscv-cpu.c` | 3 | 8 |
| `lib/jit_riscv-sz.c` | **0** | 1 |

→ PS1은 티어 D 유지. Lightrec이 Lightning API에 묶여 있어 다른 코드 생성기로 갈아끼우는 것은 별개의 큰 작업.

### A.2 SLJIT — RV32 지원함 (Lightning과 다름)

**"RV32 다이나렉이 없다"는 A.1의 결론을 일반화하면 안 된다.** [SLJIT](https://github.com/zherczeg/sljit)은 `sljitNativeRISCV_32.c`로 RV32를 네이티브 지원하며, 이 저장소에 이미 벤더링되어 있다(`gbsp/components/jit_dev/sljit_src/`, P4 전용 설정 `sljitConfigEsp32P4.h` 포함).

다만 Phase 2에서 SLJIT을 **쓰지는 않기로 했다** — gpSP 프레임워크는 직접 인코딩 전제라 IR 계층을 끼우면 오히려 어긋난다. **RV32 명령어 인코딩 대조용 참고자료로 보존한다.**

### A.3 P4 쪽 제약

`~/esp/esp-idf` v5.5의 `tools/cmake/toolchain-esp32p4.cmake`:

```
-march=rv32imafc_zicsr_zifencei_xesppie -mabi=ilp32f
```

1. **`a` 확장 없음** — 원자 명령(LR/SC) 부재
2. **`d` 확장 없음** (`ilp32f`) — 배정밀도 하드웨어 없음
3. **`xesppie`** — Espressif 벤더 SIMD. 다이나렉과 무관하나 Phase 1 블릿 최적화 후보

### A.4 출처

- [libretro/gpsp](https://github.com/libretro/gpsp)
- [Introducing Lightrec, a MIPS-to-everything dynarec](https://opendingux.net/emulation/2019/10/04/introducing-lightrec.html)
- [GNU lightning](https://en.wikipedia.org/wiki/GNU_lightning) / 소스: `git.savannah.gnu.org/cgit/lightning.git`
- [N64Recomp](https://github.com/N64Recomp/N64Recomp), [Zelda64Recomp](https://github.com/Zelda64Recomp/Zelda64Recomp)

---

## 부록 B. gpSP 선례 — GBA 60fps가 가능한 근거

**gpSP는 원래 PSP용으로 만들어졌다.** Exophase가 333MHz MIPS R4000에서 GBA를 풀스피드로 돌리려고 쓴 에뮬레이터이고, 이후 Dingoo A320(336MHz MIPS) 등에서도 같은 MIPS 백엔드로 돌았다.

**P4는 360MHz RISC-V 2코어다. 같은 체급이며 코어가 하나 더 있다.**

즉 "이 클래스 CPU에서 GBA 60fps"는 추정이 아니라 이미 증명된 사실이고, 그것을 해낸 설계가 이 저장소에서 잘려나간 바로 그 코드다. **CPU 성능은 선례가 보증하고, 우리 고유 문제는 SRAM 예산(3절)뿐이다.**

---

## 부록 C. `jit_dev` 감사 (2026-07-29)

`gbsp/components/jit_dev/`는 최초 커밋 `26e2d14`부터 있던 SLJIT 기반 자작 JIT이다. **결론: "절반 지어진 JIT"이 아니라 "꺼놓은 JIT"이다.**

작성자 본인 주석 (`gbsp/main/main.c:19`):

```c
static bool jit_enabled = true;  /* Enable JIT, but all instructions use interpreter */
```

### C.1 Thumb이 전부 비활성

`jit_decode.c:173~227`의 Thumb 판정 함수가 모든 경우에 `false`를 반환한다:

```c
return false;  /* ❌ Thumb指令全部禁用 */   (Thumb 명령 전부 비활성화)
return false;  /* ❌ BX指令导致ROM卡死 */    (BX가 ROM을 멈추게 함)
return false;  /* ❌ 条件分支导致ROM卡死 */  (조건 분기가 ROM을 멈추게 함)
```

**GBA는 카트리지 ROM이 16비트 버스라 상용 게임 대부분이 Thumb으로 컴파일된다.** Thumb을 끈 JIT은 뜨거운 코드를 하나도 건드리지 않는다. 주석 패턴상 켜봤다가 롬이 멈춰서 하나씩 끈 것으로 보인다.

### C.2 코드캐시가 PSRAM

`sljit_src/sljitConfigEsp32P4.h:57`:

```c
#define SLJIT_MALLOC_EXEC(size, allocator_data) \
    heap_caps_malloc(size, MALLOC_CAP_SPIRAM)
```

`jit_cache.c:170,181`의 ROM/RAM 번역 캐시도 동일. **`MALLOC_CAP_EXEC`가 아니다.** SLJIT 실행 메모리 할당자 3종도 전부 `0`으로 꺼져 있다(`sljitConfigEsp32P4.h:136~146`). → Q1(3절)이 여기서 나왔다.

### C.3 블록마다 상태 전체 복사

`main.c:222~251` — 블록 하나마다 레지스터 16개 `memcpy` 왕복 + CPSR 플래그 언팩/리팩. 기본 블록 평균 5~10개 명령이므로 **이 왕복만으로 인터프리터보다 비쌀 가능성이 높다.**

원인은 구조다. JIT이 gpSP 코어 **바깥**(앱 프레임 루프)에 붙어 있다. `cpu.cpp`와 `gba_cpu.h`에 JIT 참조가 하나도 없다.

### C.4 나머지

- **L1 전용 블록만 컴파일** (`jit_execute.c:73~79`). 아니면 그 PC를 **영구 블랙리스트**
- **블록 링킹 없음.** `JIT_FEATURE_BLOCK_LINKING` 플래그는 정의만 존재
- ARM 커버리지: 데이터 처리(MUL/BX 제외), 로드/스토어, 분기만. **LDM/STM 비활성** (`jit_decode.c:156`)

### C.5 처분

빌드에서 분리하고 참고자료로 보존한다. SLJIT RV32 인코딩(A.2)과 `jit_test.c`(380줄)는 재사용 가치가 있다.

### C.6 실기 측정 시 예상

**JIT 켜나 끄나 차이가 없거나 JIT이 약간 느릴 것이다.** 확인 방법:

- `g_jit_stats.jit_hits` vs `interpreter_fallbacks` → hits가 거의 0이면 진단 확정
- `jit_enabled = false` / `true` 각각 측정

---

## 부록 D. 다음에 확인할 것

- [x] **Q1: P4에서 PSRAM 영역 명령어 인출 가부** — 2026-07-31 답 나옴. 된다(캐시 상주 346 MIPS,
      스트리밍 8 MIPS). 다만 **내부 RAM 쪽이 폴트난다.** 3절 참조
- [ ] Q2: SMC 추적 배열 288KB 축소 가능성 — Phase 2e
- [ ] Q3: 코드 생성 후 캐시 유지보수 절차 (`fence.i` + L2 라이트백) — Phase 2d
- [ ] `gbsp` 인터프리터 실측 fps (기준선) — Phase 2a, 실기 필요
- [ ] `jit_dev` 실측으로 C.6 진단 확정 — 실기 필요
- [ ] `xesppie`로 RGB565 바이트스왑·블릿 가능한지 — Phase 1
- [ ] PPA(`esp_driver_ppa`)가 DSI 프레임버퍼에 직접 쓸 수 있는지 — Phase 1
- [ ] 파티션 테이블 플래시 여유 (4번째 앱 수용 가능한지) — Phase 4
