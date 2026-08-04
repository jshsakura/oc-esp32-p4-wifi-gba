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

---

## 진짜 천장은 16개가 아니라 16MB다 (2026-08-04, 실기)

OTA 슬롯이 16개라는 것만 보고 있었는데, **더 낮은 곳에 훨씬 단단한 한계가 있었다.
이 부트로더는 16MB 위의 파티션에서 부팅하지 못한다.** 32MB 플래시는 16MB를 넘어가면
4바이트 주소 모드가 필요한데, 2단 부트로더가 그걸 쓰지 않는다.

그래서 파티션 표의 마지막 두 앱은 **한 번도 부팅한 적이 없다**:

| 앱 | 오프셋 | 기종 |
|---|---|---|
| `fceumm-go` | `0x1030000` (16.9MB) | NES |
| `tgbdual-go` | `0x1230000` (18.9MB) | 게임보이 / 컬러 |

**조용히 실패하지 않는다. 기기를 통째로 못 쓰게 만든다.** otadata가 둘 중 하나를
가리키면 MSPI가 엉켜서 그 뒤로 **모든** 슬롯이 `invalid magic byte`로 읽힌다 —
런처(`0x10000`)까지 포함해서. 26초 캡처 하나에 부트루프가 **131회** 찍혔다. 플래시를
직접 읽어보면 네 파티션 모두 매직 `0xe9`가 멀쩡히 있다. 이미지 문제가 아니다.

**주소가 원인이라는 증거**: 같은 `tgbdual-go` 바이너리를 16MB 아래인 `0xef0000`
(sm-go 자리)에 넣으면 정상 부팅해서 ~200fps로 돈다. 바이너리는 한 바이트도 안 바꿨다.

메모리에 "`switch_ota_partition`이 ota_14에서는 안 먹더라, 이유 불명"으로 남아 있던 것이
이것이다. 이유는 있었다.

### 지금 어떻게 해뒀나

런처의 NES·GB·GBC를 `retro-core`(nofrendo 57fps, gnuboy 58fps — 둘 다 실측)로
돌려놨다. 더 좋은 코어를 포기한 것이므로 **임시 조치다.**

### 제대로 고치려면 — 3.2MB를 비워야 한다

앱 파티션 합계가 19,584KB인데 16MB 아래 쓸 수 있는 건 16,320KB다. **3,264KB 초과.**

지금 카드에 롬도 BIOS도 없어서 스윕이 전부 "NO ROM"을 내는 앱들이 정확히 그만큼 된다:

| 앱 | 크기 | 상태 |
|---|---|---|
| `prboom-go` | 1,472KB | WAD 없음 |
| `caprice32-go` | 1,408KB | 롬 없음 |
| `tamalib-go` | 960KB | 롬 없음 |

셋을 빼면 3,840KB가 나와 충분하다. **어느 기종을 버릴지는 사람이 정할 일**이라 여기서
실행하지 않았다. 정하고 나면 `rg_tool.py build-img` 후 파티션 오프셋을
`tools/sweep_systems.py`에 다시 옮겨 적을 것.

**새 앱을 추가할 때는 슬롯 수가 아니라 `0x1000000`을 먼저 볼 것.**

---

## 코어를 SD에서 런타임 로딩하는 길 (2026-08-04 조사)

리눅스 기기들이 하듯 코어를 `.so`로 SD에 두고 실행 직전에 불러 쓰는 방식이 **이 칩에서
가능하다.** 그러면 OTA 슬롯 16개 제한도, 16MB 부팅 한계도, 스무 코어 정적 동시 상주도
한꺼번에 없어진다. 복사 비용은 논점이 아니다 — 1MB면 0.1~0.3초다.

**`espressif/elf_loader`가 공식으로 있다.** ESP32-P4를 지원 목록에 명시하고 "PSRAM에서
ELF 실행"을 지원한다고 적는다. API는 `dlopen()`/`dlsym()`이고 파일 경로를 받는다(예제
경로가 `/riscv/lib.so`다). 호스트 심볼은 `tool/symbols.py`가 `esp_all_symbol.c`를
생성해 잇는다 — 재배치·심볼 바인딩을 손으로 만들 필요가 없다.

    idf.py add-dependency "espressif/elf_loader=*"

### 실기에서 부딪힌 것 둘, 둘 다 기록해 둔다

**1. 링커 프래그먼트로 `.text`를 PSRAM에 보내는 건 안 된다.** neopop에 `text ->
extern_ram`을 넣으면 링크는 통과하고 172KB가 `0x480xxxxx`에 배치되지만, 첫 호출에서
`Illegal instruction`으로 죽는다(MEPC `0x480a61c6`). **프래그먼트는 주소를 배정할 뿐,
거기에 무언가를 적재해주지 않는다.** 초기화 안 된 PSRAM을 실행한 것이다. 코드를 PSRAM에
올리는 건 부팅 시점의 일이다.

**2. `CONFIG_SPIRAM_XIP_FROM_PSRAM`은 이 트리에서 링크되지 않는다.** P4에서 지원되는
옵션이 맞고(`SPIRAM_BOOT_INIT` 의존, FETCH_INSTRUCTIONS/RODATA/FLASH_LOAD_TO_PSRAM을
select) 켜지긴 하는데, libefuse의 `.sdata.*`가 `--enable-non-contiguous-regions
discards section`으로 떨어진다. 아직 안 팠다.

⚠️ 되돌릴 때 **생성된 `retro-core/sdkconfig`를 지워야 한다.** 타깃 sdkconfig에서 옵션을
빼도 생성본이 그대로 들고 있어서 빌드가 계속 깨진다. rg_tool의 스탬프 해시도 이 경우를
못 잡았다.

### 그런데 성능은 공짜가 아닐 수 있다 — 근거 있는 우려

P4 Kconfig 도움말이 직접 적고 있다:

> *"Because P4 flash and PSRAM are using **two separate SPI buses**, moving flash content
> to PSRAM will actually **increase the load of the PSRAM MSPI bus**... We suggest doing
> performance profiling to determine if enabling this option."*

지금 구조는 **명령어는 플래시 버스, 데이터는 PSRAM 버스**로 병렬이다. 코어를 PSRAM에서
실행하면 둘이 한 버스로 몰린다. 일반 앱이라면 대개 이득이지만 **에뮬레이터는 데이터가
무겁다** — 롬, WRAM, VRAM, 프레임버퍼가 전부 PSRAM이다. 여기서 경합이 생기면 그대로
프레임 시간이다.

참고로 데이터 배치는 이미 무의미함이 확인됐다(L2 128KB가 흡수, WRAM 실험 0.3%). 하지만
**명령어 페치는 별개 문제이고 아직 숫자가 없다.**

### 스파이크 결과 (2026-08-04, 실기) — 어디까지 되고 어디서 막히나

`sm-go`를 숙주로 `elf_loader`를 붙이고 `-fPIC -shared`로 만든 2,612바이트 벤치마크
모듈을 임베드해서(카드가 케이스 안이라 SD 대신 임베드) 돌려봤다. 소스는
`tools/elfspike/bench.c`.

**되는 것:**

- `espressif/elf_loader` 1.3.2가 IDF 5.5에서 받아지고 **RISC-V용으로 빌드된다**
  (`src/arch/esp_elf_riscv.c`가 컴파일된다). P4에서 `CONFIG_ELF_LOADER_LOAD_PSRAM`이
  기본 `y`다.
- **재배치가 성공한다.** 모듈이 PSRAM에 올라간다 — `elf->entry=0x480d2400`.

**막히는 것 — 세 겹으로 정확히 특정됐다:**

1. `.so`의 `main()`이 자기 함수를 부르면 PLT를 타는데, 그걸 푸는 코드가
   `CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT`(기본 꺼짐) 뒤에 있다. 없으면 재배치가
   `-ENOSYS`로 실패한다(`Can't find common elfspike_bench`). 실제 코어는 이런 호출
   덩어리이므로 이 옵션은 필수다. **켜면 재배치까지 통과한다.**
2. 그 다음 **모듈의 첫 명령어에서 죽는다** — `MEPC == elf->entry`, `MCAUSE 0x1f`,
   `MTVAL 0`. 명령어 페치 자체가 실패한 것이다. P4에서는 `LOAD_PSRAM`이 MMU/캐시
   옵션을 select하지 않는다(그건 S2/S3용). **PSRAM 명령어 페치는 부팅 시점에 켜져
   있어야 한다.**
3. 그걸 켜는 유일한 길이 `CONFIG_SPIRAM_XIP_FROM_PSRAM`이다
   (`SPIRAM_FETCH_INSTRUCTIONS`는 프롬프트 없는 심볼이라 직접 못 켠다). sm-go에서는
   **링크된다**(retro-core에서만 libefuse `.sdata`가 discards로 떨어진다). 그런데
   **부팅이 안 된다** — 부트로더가 sm-go를 건너뛰고 한 칸 아래 `caprice32-go`로
   떨어졌다. `XIP_FROM_PSRAM`은 부트로더가 플래시 내용을 PSRAM으로 적재해줘야 하는데
   (`SPIRAM_FLASH_LOAD_TO_PSRAM`), 기기에 구워진 부트로더에 그 기능이 없다.

**따라서 다음 한 걸음은 부트로더 재빌드·재플래시다.** 앱 하나가 아니라 기기 전체에
영향을 주는 변경이라 여기서 멈췄다. 메모리의 경고와 같은 종류다 — PSRAM 속도를 바꿀
때도 부트로더를 다시 구워야 했다.

**부수적으로 알게 된 것**: 2,612바이트 모듈 재배치에 **9,926us**가 걸렸다. 실행 시간이
아니라 로드 시간이고 코어 실행당 한 번이라 그 자체로는 문제가 아니지만, 크기에 어떻게
비례하는지는 확인해야 한다 — 1MB 코어가 선형이면 4초다.

### 아직 답이 없는 질문

**PSRAM에서 명령어를 페치하면 얼마나 느려지는가.** 부트로더를 다시 굽고 나면 가장
깔끔한 측정은 로더를 거치지 않는 쪽이다 — 같은 앱을 `XIP_FROM_PSRAM` 켜고/끄고 빌드해
`us/frame`을 비교하면 PIC도 로더도 섞이지 않은 순수한 페치 비용이 나온다. 검출기로는
NGPC(BUSY 99%, 26,681 us/frame)나 SNES(sm, 33,043)가 좋다.
