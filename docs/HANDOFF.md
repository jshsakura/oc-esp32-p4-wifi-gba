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

### 실측 (전부 실기, `us/frame`)

| 기종 | 결과 |
|---|---|
| GBA | **12,342** — 60fps, 여유 38% (아침엔 29,294) |
| 메가드라이브 | **12,556** — 57fps |
| Atari 2600 | 56.6fps |
| NGPC | 33fps |
| Supervision | 33fps |
| SNES (snes9x) | 26–28fps |

**GBA가 오늘 사정권에 들어왔고, 원인은 에뮬레이터가 아니었다.** `jit_enabled`가 true로
출하돼 있었고 주석이 스스로 "Enable JIT, but all instructions use interpreter"라고 적고
있었다 — 한 명령도 번역하지 않으면서 매 프레임 시도 전체를 돌려 **벽시계의 53%**를 썼다.
인터프리터 본체는 47%였다.

## 4. 열려 있는 것 — 값어치 순

### (1) SNES: 코어 교체가 답이다, 최적화가 아니라

snes9x는 **고른 적이 없다.** `git log -- retro-core/components/snes9x`가 한 줄이다: ximzi
베이스의 초기 커밋. 사용자 본인의 `sm` 코어가 같은 게임을 280MHz 단일코어 M7에서 46fps로
돌린다. 그 코어는 `sm-go/`로 들어와 있고 빌드된다.

**다음 할 일**: `sm-go`에 SNES 롬을 물려 실기에서 재고, snes9x와 A/B해서 고른다. 롬은
`/sd/roms/sm/`에 넣어야 한다(현재 비어 있음).

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
