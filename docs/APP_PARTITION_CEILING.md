# 앱 하나에 파티션 하나는 22기종에서 끝난다

2026-08-03, 신규 코어 넷(Atari 7800, 타마고치, Amstrad CPC, picodrive/Sega CD)과
GLM 레인의 둘(ZX Spectrum, Game.com)을 올린 뒤 전체 이미지를 말다가 막혔다.

## 두 개의 벽, 하나의 원인

**1. ESP-IDF는 OTA 앱 파티션을 16개까지만 안다.**

```
Error at line 20: Value 'ota_16' is not valid.
Known keywords: factory, test, ota_0 ... ota_15
```

앱이 22개다. `factory`를 더해도 17이 상한이고, 부트로더의 OTA 선택 로직 자체가
`ota_0..15`를 전제한다. 커스텀 서브타입으로 이름을 만들어 붙일 수는 있어도 부트로더가
그것을 부팅 대상으로 고르지는 않는다.

**2. 앱 총합이 27.75MB, 플래시는 32MB다.**

## 원인은 코어가 아니라 프레임워크 사본이다

빌드된 바이너리 크기다.

| 앱 | 크기 | 코어 자체의 크기 |
|---|---|---|
| tamalib-go | 953 KB | 수십 KB (타마고치는 4비트 MCU 하나다) |
| supervision-go | 961 KB | 작다 |
| prosystem-go | 964 KB | 작다 |
| vb-go | 974 KB | 중간 |
| zxs-go | 986 KB | 작다 |
| gamecom-go | 990 KB | 작다 |

**바닥이 전부 ~950KB로 같다.** 그 950KB는 코어가 아니라 retro-go 프레임워크 —
디스플레이 파이프라인, 오디오, 입력, GUI, 폰트, 설정, 파일 시스템 — 이고, 앱마다 한 벌씩
들어간다. 코어가 30KB든 300KB든 앱은 1MB가 된다.

그래서 22기종 = 22MB가 아니라 **22 × (프레임워크 + 코어)** 이고, 파티션 한계와 플래시
한계에 동시에 부딪힌 것이다. 기종을 더 넣을수록 낭비가 선형으로 늘어난다.

## 답은 이 저장소가 이미 쓰고 있다

`retro-core` **하나가 8개 기종을 담는다** — NES, SNES, GB/GBC, SMS/GG, PCE, G&W,
ColecoVision, Lynx. `retro-core/main/main.c`가 `app->configNs`로 갈라서
`main_nes.c` / `main_snes.c` / `main_gbc.c` … 를 부르고, 프레임워크는 한 벌만 들어간다.
런처 항목은 여덟 개지만 파티션은 하나다.

나머지 14개 앱은 전부 1앱 = 1기종이다.

## 그러므로

작은 코어들을 `retro-core` 방식으로 묶어야 한다. 묶을 후보는 프레임워크 대비 코어가
작은 것들 — supervision, pkmini, videopac, tamalib, gamecom, zxs, prosystem, stella,
wswan, ngpocket. 열 개를 한 앱으로 묶으면 파티션 열 개가 하나가 되고, 프레임워크 사본
아홉 벌(약 8.5MB)이 사라진다.

**묶으면 안 되는 것**은 코어 자체가 큰 것들이다 — picodrive(1.5MB), gbsp(1.4MB),
caprice32(1.3MB), fceumm, fmsx, prboom. 이들은 한 앱에 둘씩 넣어도 내부 RAM이
버티지 못한다(각자 다른 정적 상태를 갖는다).

이 문서는 "왜 앱을 더 못 만드나"에 대한 답이다. 다음에 기종을 추가할 때 파티션을 하나 더
만들기 전에 여기를 읽을 것.

---

## 접고 나서 배운 것 (2026-08-03, 실기)

파티션 천장을 풀면 **내부 RAM 천장이 온다.** 스무 기종이 42KB를 나눠 쓰게 되고, 그
경계에서 나오는 고장은 셋 다 조용하다.

### 규칙: 한 번에 하나만 돈다

부팅당 한 기종만 실행된다. 그러므로 **접어 넣은 코어의 정적 상태가 내부 RAM에 동시에
있을 이유가 없다.** 참조 포트가 같은 원칙을 그대로 적는다 — *"Every core is an overlay
at the same RAM address."* 여기서 그 overlay는 PSRAM이다. 접는 코어는 **예외 없이**
`.bss`를 링커 프래그먼트로 내보내고, 내부 RAM은 프레임워크와 **지금 도는** 코어의 런타임
할당에만 남긴다.

**"프래그먼트 없이도 빌드가 통과했다"는 안전의 증거가 아니다.** potator가 정확히 그
경우였고, 그래서 위험했다. 시작 시 0으로 지워진 `screenBuffers[]`가 나중에
`0x80828d19` 같은 값이 되어 `free()`에서 죽었다 — 정적이 힙과 부딪히고 있었다.

### 세 가지 조용한 고장

1. **`rg_surface_create(..., MEM_FAST)`** 는 실패하지 않는다. `rg_alloc`이 "CAPS not
   fully met"를 로그에 남기고 PSRAM을 돌려준 뒤, 한참 뒤에 `heap_caps_free`
   어서트로 터진다.
2. **`legacy_bss`** 를 빠뜨린 프래그먼트는 작동하는 것처럼 보인다. RISC-V의
   `.sbss`/`.scommon`은 베이스 링커 스크립트가 무조건 내부로 끌어간다.
3. **스택은 힙과 별개다.** C++ 코어(stella, iostream)는 이제 자기 태스크 바닥이 아니라
   `app_main` 프레임 위에서 돌아 8KB 기본값을 넘긴다 — `CONFIG_ESP_MAIN_TASK_STACK_SIZE`.

### 진단은 주소를 디코딩해서 한다

Supervision 건에서 세운 가설 셋이 전부 틀렸다: `.ext_ram.bss`가 안 지워진다(→
`esp_psram_bss_init()`이 지운다), `SB_MAX` 불일치(→ 일치했다), 헤더 충돌(→ 없었다).
크래시 덤프의 스택에서 주소를 뽑아 `addr2line`에 넣자 한 줄로 나왔다:

    supervision_load → supervision_reset → gpu_set_ghosting → heap_caps_free

그리고 포인터 값을 직접 찍고 나서야 "정적이 나중에 덮어써진다"가 보였다. **덤프가 있는데
추론부터 하지 말 것.**
