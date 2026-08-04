# 인계 — 2026-08-03

이 문서 하나만 읽으면 이어서 할 수 있게 쓴다. 순서대로 읽을 것.

## 0. 먼저 읽을 것

1. **`~/app/game-and-watch-retro-go-sd/docs/OPTIMIZATION_LEDGER.md`** — 사용자의 STM32H7
   포트다. 기종별로 무엇을 시도했고 무엇이 닫혔는지가 다 있다. **성능 작업 전에 반드시.**
   닫힌 길을 다시 열려면 "무엇이 달라졌는지"를 대야 한다. 재유도는 이유가 아니다.
2. **`docs/APP_PARTITION_CEILING.md`** — 왜 앱을 더 못 만드는가, 코어를 접을 때의 규칙.
3. **`docs/BRINGUP.md`** — 하드웨어 계측과 함정. 0.2가 버튼 없이 롬 띄우는 법, 0.3-2가
   "discards section"의 뜻.
4. 메모리의 `oc-gba-measurement-rig` — 계측 방법과 오늘까지 틀린 것들.

## 1. 이 기기의 물리적 제약

**패널 없음, 버튼 없음, SD 카드는 케이스 안.** 그래서 기종을 손으로 고를 수 없고,
`boot.json`도 못 고친다(게다가 `rg_system_rom_load_failed()`가 실패 시 그걸 지운다).

대신 **컴파일 플래그로 고른다** — `rg_system.c`의 `romPath`를 정하는 단 한 줄 옆에 훅이 있다:

    RG_BENCH_ROM_DIR=snes RG_BENCH_ROM_MATCH=. python3 rg_tool.py build retro-core --target oc-gba-devkit

디렉터리 이름이 시스템 이름을 겸한다(`app.configNs`도 이것으로 설정되므로 retro-core의
스무 기종이 이걸로 갈린다). **두 플래그는 `components/retro-go/CMakeLists.txt`에 선언돼
있어야 한다** — 그 컴포넌트는 `rg_setup_compile_options()`를 안 쓰므로 다른 데 넣으면
말없이 사라진다.

**전 기종 자동 스윕**: `python3 tools/sweep_systems.py [기종…]` — 빌드·플래시·부팅·캡처를
기종마다 반복하고 결과를 표로 낸다. 파티션 오프셋이 그 안에 박혀 있으니 앱 구성이 바뀌면
`rg_tool.py build-img` 후 다시 읽어 갱신할 것.

⚠️ **2026-08-04 이전 스윕 결과는 믿지 말 것.** OTA 전환이 전 기종에서 조용히 실패하고
있었다 — `sudo` 아래서 `~`는 `/root`라 `export.sh`가 없고, `IDF_PATH`가 비어
`otatool.py`가 아무 말 없이 exit 1 했다. 아무도 종료 코드를 안 봤다. 7ed5682에 커밋된
표는 17행 중 **10행이 런처 메뉴의 프레임레이트**였고(1600~2500fps가 그것이다), 앱이 맞게
뜬 행들도 *직전 패스의 롬*을 돌리고 있었다. `nes` 56.6fps는 실은 Atari 2600 숫자다.
지금은 부팅 배너의 `Project name:`과 `BENCH: using` 경로를 대조해 확인되지 않으면 숫자를
내지 않는다.

**시리얼은 sudo가 필요하다.** 계정은 `dialout`에 있지만 그 전에 열린 셸은 옛 그룹 집합을
쓴다. `otatool.py`는 추가로 `IDF_PATH`를 sudo로 넘겨야 한다.

## 2. 측정 규칙 — 오늘 이걸로 두 번 틀렸다

- **비교 지표는 `us/frame`이다** (`BUSY% * 10000 / FPS`). BUSY는 발열로 흐른다 — 한 세션에서
  변경 없는 바이너리가 67% → 78%로 올랐다. FPS도 같이 오르므로 `busyTime/ticks`만 남는다.
- **A/B는 교차로**(A→B→A). 드리프트를 효과로 착각하지 않으려면 그 방법뿐이다.
- **이전에 구운 바이너리는 대조군이 아니다.** 변경 말고도 다른 게 다르다. 같은 트리에서
  기능만 토글해 굽는다.
- **일을 없애지 않은 프로브는 아무것도 재지 않았다.** 오늘 SNES에서 셋이 그랬다.
- **크래시는 덤프를 디코딩해서 진단한다.** 스택의 주소를
  `addr2line -pfiaC -e <app>.elf`에 넣으면 한 번에 나온다. 추론부터 하면 오늘처럼 가설
  셋을 버리게 된다.

## 3. 지금 상태

**기종 32개, 앱 13개.** ESP-IDF는 OTA 파티션을 16개까지만 안다.

`retro-core` 하나가 20기종을 담는다(`app->configNs`로 분기). 나머지는 코어가 큰 것들이다 —
`gbsp`(GBA), `gwenesis`(메가드라이브), `picodrive-go`(Sega CD/32X), `sm-go`(SNES 대체 코어),
`caprice32-go`, `fmsx`, `prboom-go`, `fake08`, `fceumm-go`, `tgbdual-go`, `tamalib-go`.

⚠️ **`fceumm-go`와 `tgbdual-go`는 빌드·플래시는 되지만 부팅이 불가능하다.** 파티션이
16MB 위(`0x1030000`, `0x1230000`)에 있고 부트로더가 거기서 못 뜬다. 게다가 조용히
실패하지 않고 MSPI를 엉키게 해 **런처 포함 전 슬롯이 안 읽히는 부트루프**가 된다.
그래서 NES/GB/GBC는 지금 retro-core(nofrendo, gnuboy)로 돌려놨다. 자세한 것과
제대로 고치는 법은 `docs/APP_PARTITION_CEILING.md` 마지막 절.

### 실측 (전부 실기, `us/frame`)

**2026-08-04 전 기종 스윕. 크래시 0, 부팅 실패 0.** 아래는 전부 부팅 배너와 롬 경로가
확인된 행이다. 정렬은 `us/frame` 오름차순 = 빠른 순.

| 기종 | us/frame | fps | 앱 |
|---|---|---|---|
| NES | **2,689** | 57.4 | retro-core (nofrendo) |
| 게임보이 | **2,982** | 57.9 | retro-core (gnuboy) |
| Game & Watch | 3,250 | 122.7 | retro-core |
| 게임보이 컬러 | 3,327 | 54.9 | retro-core |
| 게임기어 | 3,952 | 55.4 | retro-core |
| SG-1000 | 4,964 | 57.8 | retro-core |
| Atari 2600 | 6,648 | 57.0 | retro-core |
| PC Engine | 6,864 | 31.1 | retro-core |
| 마스터 시스템 | 7,228 | 55.1 | retro-core |
| Supervision | 7,244 | 32.8 | retro-core |
| 콜레코비전 | 7,333 | 55.0 | retro-core |
| 메가드라이브 | 12,575 | 57.6 | gwenesis |
| GBA | 20,118 | 47.3 | gbsp |
| NGPC | 26,948 | 36.7 | retro-core |
| **SNES (sm)** | **32,986** | 30.3 | sm-go |
| SNES (snes9x) | 34,943 | 27.6 | retro-core |
| MSX | 계측 없음 | 49.4 | fmsx |

롬/BIOS가 카드에 없어 못 잰 기종: `a78`, `poke`, `wsc`, `vb`, `videopac`, `zxs`,
`gamecom`(BIOS), `segacd`(BIOS), `cpc`, `tama`.

**이전 기록과 다른 세 줄, 전부 확인했다:**

- **GBA 20,118 (이전 12,342).** 회귀 아니다 — `jit_enabled`는 여전히 false다
  (`gbsp/main/main.c:36`). 스윕은 `/sd/roms/gba/gba.gba`를 집고 12,342는 다른
  게임에서 잰 값이다. **롬이 다르면 비교하지 말 것.**
- **PC Engine 31.1fps (2026-08-02엔 58-60).** BUSY가 21%뿐이라 CPU 한계가 아니다.
  틱 레이트 쪽으로 보이고, 아직 안 팠다.
- **MSX는 숫자가 없다.** fmsx가 `ShowVideo()`에서 `rg_system_tick(0)`을 매 프레임
  더 부르고 `Keyboard()`가 곧바로 `FrameStartTime`을 리셋해서, 프레임의 작업이 아니라
  인접한 두 호출 사이의 빈 구간을 잰다. 스윕은 이제 0을 결과로 찍지 않고 "계측 없음"이라
  적는다. 고치려면 fmsx의 루프를 봐야 한다.

**GBA가 오늘 사정권에 들어왔고, 원인은 에뮬레이터가 아니었다.** `jit_enabled`가 true로
출하돼 있었고 주석이 스스로 "Enable JIT, but all instructions use interpreter"라고 적고
있었다 — 한 명령도 번역하지 않으면서 매 프레임 시도 전체를 돌려 **벽시계의 53%**를 썼다.
인터프리터 본체는 47%였다.

## 4. 열려 있는 것 — 값어치 순

### (1) SNES: 코어 교체는 6%였다 — 2026-08-04에 실기로 결론남

**측정 끝. 답은 "교체만으로는 안 된다"이다.** A/B/A 교차, 두 앱 모두 같은 트리에서
`RG_BENCH_ROM_DIR=snes`로 빌드해 **같은 `/sd/roms/snes/snes.smc`** 를 읽게 하고, OTA
파티션만 바꿔가며 쟀다:

| 코어 | us/frame | fps |
|---|---|---|
| snes9x (retro-core) | 35,038 / 34,990 | 27.6 |
| sm (sm-go) | **33,101 / 33,101** | 30.2 |

**5.5% 빠르다. 1.7배가 아니다.** 전제(참조 기기가 46fps니까 격차는 에뮬레이터다)는 이
보드에서 성립하지 않았다 — 둘 다 포화 상태이고 6% 안에 있다. 참조의 46fps는 RV32에
올릴 것이 없는 **손으로 쓴 ARM 인터프리터**로 낸 숫자라는 점도 같이 봐야 한다.

**애초에 sm-go는 게임을 돌린 적이 없었다.** `sm`은 에뮬레이터이기 전에 슈퍼 메트로이드
디컴파일이고, `cpu_runOpcode`를 부르는 곳은 트리 전체에서 `sm_cpu_infra.c` 하나뿐이다
(디컴파일된 게임이 네이티브라 PC로 CPU를 몬다). `snes_runFrame()`은 선언만 있고 정의가
없다. main.c가 NMI를 기다려 프레임을 끊고 있었는데, CPU가 안 도니 게스트가 NMI를 켤 리
없어 무한루프였다. 참조에는 포트가 **둘**이고(`porting/sm/` = 디컴파일, `porting/snes/`
= 범용 에뮬레이터) 46fps는 후자의 것이다. 그 드라이버를 옮겨왔다(c5a9570).

**남은 레버는 sm 쪽에 있다.** spin-skip은 `SNES_SPIN_SKIP` 뒤에 배선해뒀고 **꺼져 있다** —
켜면 4% 느리고, `SNES_SPIN_SKIP_DEFAULT=false`로 학습기만 꺼도 회복이 안 된다. 비용이
`cpu_read`/`cpu_write`마다 박히는 훅이라 런타임 게이트로 환불이 안 되기 때문이다.
숫자는 `sm-go/components/sm/CMakeLists.txt`에 적어뒀다. 다른 롬에서는 갚을 수도 있으니
가정하지 말고 다시 잴 것.

`docs/SNES_APU_HLE_FEASIBILITY.md`도 읽을 것. 결론(APU HLE는 저ROI)은 맞지만 **1순위 권고에
확인이 필요하다** — snes9x엔 이미 스핀 스킵이 켜져 있다(`memmap.c:617`). 그리고 그 문서가
근거로 든 65816 정적 번역기는 **렛저가 닫은 길**이다. 리뷰 노트가 문서 끝에 붙어 있다.

### (2) 32X — 컴파일은 되지만 런처에 없다

의도된 것이다. 참조 포트가 성능으로 제거했다: **프레임당 ~24,000,000 사이클 대 5,197,920
예산**, 4.6배 초과. 우리는 더 빠르고 그쪽에 없던 2D-DMA(PPA)가 있지만 4.6배는 저절로
닫히지 않는다. **숫자가 나오면 항목을 만든다.** `picodrive-go`에 이미 링크돼 있으니 재기만
하면 된다.

### (3) Sega CD — 빌드되지만 BIOS가 없다

`/bios/bios_CD_U.bin`(또는 `_E`/`_J`)이 카드에 필요하다. 없으면 코어가 그렇게 말한다.
MP3 CDDA는 미지원(로그로 알림), SVP 가속 경로는 ARM 어셈이라 C 인터프리터만 쓴다.

### (4) 롬이 없어 못 재는 기종

`a78`, `tama`, `cpc`, `zxs`, `vb`, `poke`, `wsc`, `videopac`, `lnx`, `doom`, `sm`, `segacd`.
`gamecom`은 롬은 있으나 BIOS가 없다. 롬 라이브러리는 Pi에 있다 — `ssh rpi5`,
`/media/pi/EXTERNAL/miyoo-library/public/roms`.

### (5) 80MHz PSRAM이 retro-core에서만 컴파일 방식에 따라 부팅이 뒤집힌다

`-Os` 5/5 정상, `-O2` 6/6 부트루프. 인라이닝·크기·UART 지연은 실기에서 배제했다. 마진이
없다는 그림이고, 20MHz로는 디스플레이가 예산을 넘어 오디오가 굶는다. **우회가 아니라 해결이
필요하다.**

## 5. 코어를 접을 때 (파티션이 모자라면)

`retro-core`가 패턴이다. `git show 30e5783`(Atari 7800)과 `git show b3684fb`(아홉 개)를 볼 것.

**규칙: 한 번에 하나만 돈다.** 그러므로 접은 코어의 정적 상태는 **예외 없이** 링커
프래그먼트로 PSRAM에 내보낸다. "프래그먼트 없이 빌드가 통과했다"는 안전의 증거가 아니다 —
potator가 그 경우였고 그래서 터졌다.

접을 때 나오는 것들, 전부 조용하다:

- `rg_surface_create(..., MEM_FAST)`는 **실패하지 않는다.** 경고만 남기고 PSRAM을 주고,
  한참 뒤 `heap_caps_free` 어서트로 터진다. 접은 코어는 `MEM_SLOW`를 쓴다.
- 프래그먼트에 **`legacy_bss`** 를 빠뜨리면 다른 게 자리를 필요로 할 때까지 잘 된다.
- **스택은 힙과 별개 예산**이다. C++ 코어는 `app_main` 프레임 위에서 돌아 8KB를 넘긴다 —
  `CONFIG_ESP_MAIN_TASK_STACK_SIZE`.
- **심볼·헤더 충돌은 합쳐야만 나온다.** `#include "cpu.h"`가 gnuboy 것으로 조용히
  해결됐고, neopop의 `pc`/`bios`/`sound_init`이 smsplus와 부딪혔다. 코어 접두어로 개명하고,
  include는 경로를 명시한다.

## 6. 다른 레인

GLM(opencode, tmux `mobile-d62ccd47ac23`)과 Antigravity(agy, `mobile-9946c3bf66f8`)가 같은
저장소에 있다. `tmux send-keys`로 붙일 수 있고, 브리프는 `docs/handoff/LANE_*.md`에 있다.
그 형식이 통했다 — 베낄 템플릿을 지목하고, 건드리면 안 되는 파일을 명시하고, 못 하겠으면
스텁 만들지 말고 멈추라고 적을 것.

**워크트리 격리 서브에이전트는 오래된 브랜치에서 시작하고 미커밋 작업을 못 본다.** 오늘 둘
다 커밋된 tip으로 리셋하고 진행했고, 공유 파일(`rg_tool.py`, `applications.c`) 수정은 병합
때 손으로 다시 얹어야 했다.
