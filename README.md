# oc-esp32-p4-wifi-gba

**ESP32-P4가 실제로 무엇을 할 수 있는지 알아보려고 쓴 펌웨어.**

GBA 껍데기에 넣을 핸드헬드를 만들면서, 그걸 P4의 기술 검토 수단으로 삼았다. 데이터시트가
답해주지 않는 것들 — 에뮬레이션이 어디까지 되는가, 천장이 어디에 있는가, 참조 플랫폼의
경험이 이 칩에 옮겨오는가 — 을 **전부 실기에서 측정해서** 답한 기록이다.

측정하지 않은 것은 이 문서에 없다. 측정했는데 예상과 달랐던 것은 그렇게 적혀 있다.

## 하드웨어

| | |
|---|---|
| MCU | ESP32-P4, 듀얼 RISC-V 360MHz, PSRAM 32MB, 플래시 32MB |
| Wi-Fi | 모듈 위의 ESP32-C6 |
| 디스플레이 | D310N9362V0 — 3.1" 480x800 IPS, ST7701S, MIPI DSI, 회전 장착 |
| 오디오 | ES8311 코덱 + NS4150B 앰프 |
| 입력 | GBA 버튼 10개 + 후면 패들 2개, TCA9554 확장기 두 개 |
| 저장 | 모듈의 microSD, SDMMC 4비트 |
| 전원 | IP5306, LiPo, 껍데기의 원래 슬라이드 스위치 |

240x160을 3배로 올려 720x480, 패널의 800x480 안에 넣는다. 정수배라 뭉개지지 않고,
60.48 x 40.32 mm로 원본 GBA의 61.2 x 40.8과 거의 같다.

## 실측 — 전 기종, 전부 실기

`us/frame`이 비교 지표다. BUSY%는 발열로 흐르지만(한 세션에서 67%→78%) FPS도 같이
오르므로 `busyTime/ticks`만 남는다. 60fps 예산은 **16,667 us**.

| 기종 | us/frame | fps | 앱 |
|---|---|---|---|
| NES | **2,245** | 58.9 | coreloader (SD의 `nofrendo.elf`) |
| Game & Watch | 2,531 | 122.1 | retro-core |
| 게임보이 | **2,838** | 58.4 | coreloader (SD의 `gnuboy.elf`) |
| 게임보이 컬러 | 3,525 | 58.7 | coreloader |
| 게임기어 | 3,586 | 55.8 | retro-core |
| PC Engine | 5,644 | 59.0 | retro-core |
| Atari 2600 | 5,700 | 57.5 | retro-core |
| SG-1000 | 6,511 | 54.2 | retro-core |
| Supervision | 7,271 | 32.8 | retro-core |
| 마스터 시스템 | 7,565 | 52.9 | retro-core |
| 콜레코비전 | 7,895 | 53.8 | retro-core |
| 메가드라이브 | 12,466 | 57.9 | gwenesis |
| GBA | 20,106 | 47.3 | gbsp |
| NGPC | 26,915 | 36.7 | retro-core |
| SNES (sm) | 33,043 | 30.3 | sm-go |
| SNES (snes9x) | 35,038 | 27.6 | retro-core |
| MSX | 계측 없음 | 49.4 | fmsx |

12기종이 예산 안, 4기종이 초과. 롬/BIOS가 카드에 없어 못 잰 기종 10개는 그렇게 표시된다.

**이 표를 믿을 수 있는 이유**: 하네스가 부팅 배너의 `Project name:`과 실제로 읽은 롬 경로를
요청한 것과 대조하고, 맞지 않으면 숫자 대신 실패를 낸다. 그 검증이 없던 시절의 표는
17행 중 10행이 **런처 메뉴의 프레임레이트**였다.

## 무엇을 알아냈나

### 천장은 셋이었고, 셋 다 소프트웨어였다

| 천장 | 정체 | 결말 |
|---|---|---|
| OTA 파티션 16개 | ESP-IDF가 `ota_0..15`만 안다 | 코어 20개를 `retro-core` 하나로 접음 |
| 내부 RAM | 접은 코어들의 정적이 동시 상주 | **256KB 회수**, 성능 손해 0 |
| **16MB 부팅선** | 부트로더가 16MB 위를 못 읽음 | 파티션 크기가 거짓말한다 |

마지막 것이 가장 비쌌다. `fceumm-go`(NES)와 `tgbdual-go`(게임보이)는 파티션이 16MB 위라
**한 번도 부팅한 적이 없었고**, 조용히 실패하지도 않았다 — otadata가 거길 가리키면 MSPI가
엉켜 런처 포함 전 슬롯이 `invalid magic byte`로 읽히고 부트루프에 빠진다(26초 캡처에 131회).

**앱의 실제 여유는 파티션 크기가 아니라 `0x1000000 − offset`이다.** `sm-go` 슬롯은
1376K로 적혀 있지만 1088K만 닿는다. 자세한 것은
[docs/APP_PARTITION_CEILING.md](docs/APP_PARTITION_CEILING.md).

### 그래서 코어를 파일로 만들었다

`coreloader`는 **에뮬레이터를 하나도 안 가진 앱**이다. 코어는 `/sd/retro-go/cores/*.elf`에
있고, `espressif/elf_loader`가 실행 시점에 적재·재배치하고, 호스트가 모듈이 넘겨준 함수
테이블로 구동한다. 런처의 선택은 `retro-core`와 똑같이 `configNs`로 도착한다.

**그리고 공짜다:**

| | us/frame |
|---|---|
| 정적 링크 gnuboy | 3,002 |
| **SD 파일에서 로드** | **2,940** |

재배치는 51KB 코어에 6.3ms, 실행당 한 번. 프레임 시간에 안 나타난다.

기종 추가가 이제 파티션 문제가 아니다 — `project_elf()`로 코어를 빌드하고, 드라이버를
쓰고, 카드에 `.elf`를 복사한다. 이미지 재패킹도, 내부 RAM 예산 재협상도 없다.
과정과 함정은 [tools/elfspike/README.md](tools/elfspike/README.md).

### 메모리 배치는 이 칩에서 레버가 아니다

참조 플랫폼(STM32H7, 754KB 내부 RAM)에서는 배치가 전부였다. **여기서는 넷 다 0이다:**

| 실험 | 결과 |
|---|---|
| 게스트 WRAM 128KB를 PSRAM → 내부 RAM | 0.3% |
| 앱 전체를 PSRAM에서 실행 (XIP) | 0.0% |
| 인터프리터 `.text`를 플래시 → 내부 SRAM | 0.5% |
| `-O2` → `-O3` | 0.2% |

L2 캐시가 128KB라 게스트 워킹셋을 흡수하고, I-캐시가 11KB짜리 opcode switch를 담는다.
**남는 설명은 인터프리터 구현 하나뿐이다** — 참조의 46fps는 손으로 쓴 Thumb-2 어셈블리이고
우리는 컴파일러가 낸 C다. 클럭은 1.29배 유리한데 1.52배 느리다. 근거는
[docs/SNES_BUDGET.md](docs/SNES_BUDGET.md).

### SNES 60fps는 이 구성으로 불가능하다

프레임 33,043 us의 내역(PPU는 **제거로** 측정):

    65816 인터프리터   22,396 us  (68%)
    SPC700 (APU)        8,200 us  (25%)
    PPU 렌더링          2,447 us  ( 7%)

APU를 통째로 없애도 40fps, APU+PPU를 다 없애도 45fps. **인터프리터 하나만으로 이미
예산을 34% 넘는다.** 참조 포트도 어셈블리와 사운드 대체를 다 하고 55.4fps다.

렛저가 "가장 큰 레버"라 적은 컬러 매스는 여기서 7% 안의 일부다. **남의 플랫폼 퍼센트를
옮겨 쓰면 안 된다.**

## 무엇이 돌아가나

런처 탭 32개, 앱 파티션 13개. 카드에 롬이 없는 탭은 숨는다.

| 파티션 | 담긴 것 |
|---|---|
| `retro-core` | 20기종 (`configNs`로 분기) |
| `coreloader` | **코어 없음** — SD의 `.elf`를 런타임 로드. NES, 게임보이/컬러 |
| `gbsp` | GBA |
| `gwenesis` | 메가드라이브 |
| `picodrive-go` | Sega CD (32X는 컴파일되나 미제공) |
| `sm-go` | SNES 대체 코어 |
| `fmsx` · `fake08` · `caprice32-go` · `tamalib-go` · `fceumm-go` · `tgbdual-go` | 각 1기종 |

## 빌드

```sh
. ~/esp/esp-idf/export.sh          # ESP-IDF v5.5
python3 rg_tool.py --target oc-gba-devkit build-img
python3 rg_tool.py --target oc-gba-devkit --port /dev/ttyACM0 install
```

버튼도 화면도 없는 보드에서 특정 기종을 띄우려면 컴파일 플래그를 쓴다:

```sh
RG_BENCH_ROM_DIR=snes RG_BENCH_ROM_MATCH=. python3 rg_tool.py build retro-core --target oc-gba-devkit
python3 tools/sweep_systems.py            # 전 기종 빌드·플래시·부팅·캡처
```

배선·SD 배치·복구는 [docs/FLASHING.md](docs/FLASHING.md), 계측 방법과 함정은
[docs/BRINGUP.md](docs/BRINGUP.md), 이어서 작업하려면 [docs/HANDOFF.md](docs/HANDOFF.md).

## 핀 정의는 사람이 아니라 보드에서 온다

```sh
python3 tools/pinmap_from_kicad.py <board.kicad_pcb> --check
```

제작된 `.kicad_pcb`를 읽어 헤더 패드 뒤의 GPIO를 매핑하고, 보드와 `config.h`가 어긋나면
실패한다. 의례가 아니다 — 패들 버튼이 다른 확장기에 붙은 것과 디스플레이 라인 하나가
지워진 것을 이미 잡았고, 둘 다 실기에서는 "버튼 하나가 죽었다"나 "패널이 까맣다"로만
나타났을 것이다.

## 베이스와 달라진 것

- **부트루프 탈출.** 15초를 못 버틴 부팅이 세 번이면 저장된 설정과 부팅 대상을 건너뛴다.
- **패널이 없어도 멈추지 않는다.** DSI 쓰기는 리본 반대편이 비면 돌아오지 않으므로,
  패널 초기화를 마감시한 있는 별도 태스크에서 돌린다.
- **타이머 자동 저장.** 하드 전원 스위치는 저장할 틈을 주지 않는다.
- **PICO-8** ([FAKE-08](https://github.com/jtothebell/fake-08)), 4bpp 화면을 8비트 팔레트
  서피스로 넘겨 색 변환이 없다.
- **베이스에 없던 코어 다수** — Atari 2600/7800, NGPC, Supervision, Pokémon Mini,
  WonderSwan, Virtual Boy, ZX Spectrum, Game.com, Videopac, Amstrad CPC, 타마고치, Sega CD.
- **한국어**, 번역에서 뽑은 서브셋 폰트를 빌드가 재생성.
- **ES8311 오디오**, **오버클럭**, 베이스가 꺼둔 셋(DOOM, 메가드라이브, MSX).

## 상태

데브 보드에서 전 기종 스윕 통과 — **크래시 0, 부팅 실패 0.** 부팅 384ms, SD, 오디오,
런처, 폰트, 복구, 자동 저장·재개.

패널과 캐리어 보드는 아직 대기 중이다. 블릿은 측정됐지만(3.0–10.2ms/frame) 실제 패널을
구동한 적이 없다. 클럭은 300–400MHz 밖을 거부한다 — CPU PLL이 429MHz에서 포화하는데
480을 요청하면 조용히 429로 돌면서 480이라고 보고했다.

## 계보

[ximzi/retro-go-esp32p4](https://github.com/ximzi/retro-go-esp32p4)에서 포크했고, 그쪽은
[ducalex/retro-go](https://github.com/ducalex/retro-go)에 P4 타깃과 GBA 코어를 더한 것이다.
Retro-Go는 GPL — [COPYING](COPYING) 참조.

오버클럭은 [DynaMight1124/retro-go](https://github.com/DynaMight1124/retro-go)에서 이식.
한국어 폰트는 Noto Sans CJK 서브셋(SIL OFL) —
[licenses/](components/retro-go/fonts/licenses/).
베이스의 원래 문서는 [docs/upstream/](docs/upstream/)에 그대로 뒀다.
