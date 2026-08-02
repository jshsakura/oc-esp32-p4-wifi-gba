# 실기 검증 체크리스트

코드로는 끝났고 실기에서 확인해야 하는 것들. 순서는 "지금 가진 하드웨어로 되는 것"부터다.

- 최초 작성 2026-07-30 (브랜치 `oc-gba`)
- [ROADMAP.md](ROADMAP.md) 부록 D와 겹치는 항목은 링크로 표시했다. 로드맵은 **성능·설계 질문**이고
  이 문서는 **기능 검증 절차**다.
- 보유 하드웨어: Waveshare ESP32-P4-WIFI6 개발보드(`/dev/ttyACM0`) + 32GB SD. 패널·버튼·캐리어보드 없음.

---

## 0. 준비 — 없는 하드웨어를 우회하는 두 가지 방법

여기서 시간을 아껴야 A절 전체가 열린다.

### 0.1 버튼: 점퍼선 한 개로 충분하다

`RG_GAMEPAD_GPIO_MAP`(`targets/oc-gba-devkit/config.h:25`)이 내부 풀업 + 액티브 로우라, **핀과 GND를
선 하나로 짧게 잇는 것이 버튼 한 번 누르는 것**이다. 10개를 다 배선할 필요는 없다.

| 목적 | 필요한 핀 |
|---|---|
| 런처 이동/실행 | DOWN=21, A=26 (선 1개를 옮겨 써도 됨) |
| MENU 코드 | START=28 + SELECT=29 |
| 리커버리 코드 | 28 + 29 + L=30 + R=31 **동시 4개** |

패널이 없으면 화면을 못 보므로 런처 조작은 눈이 아니라 로그로 확인해야 한다. 그래서 롬을 띄우는
데는 아래 0.2가 더 낫다.

### 0.2 롬: 버튼도 화면도 없이 게임을 부팅시키는 법

부팅 대상은 두 곳에 나뉘어 저장된다 — **어느 앱 파티션인지는 OTA 데이터**, **어느 롬인지는 SD의
JSON**(`rg_system.c:129` `update_boot_config`, `rg_settings.c:38`). 둘 다 PC에서 직접 쓸 수 있다.

1. SD에 `/retro-go/config/boot.json`:

   ```json
   {"BootName": "gba", "BootArgs": "/sd/roms/gba/rom.gba", "BootSlot": -1, "BootFlags": 0}
   ```

   `BootName`은 `applications_init()`의 short_name, `BootArgs`는 롬 전체 경로, `BootSlot`은 −1이
   "새 게임" 0 이상이 "그 슬롯 로드", `BootFlags`의 0x01이 `RG_BOOT_RESUME`이다.

2. OTA 부팅 파티션을 그 앱으로 돌린다:

   ```sh
   . ~/esp/esp-idf/export.sh
   python3 $IDF_PATH/components/app_update/otatool.py --port /dev/ttyACM0 \
       switch_ota_partition --name gbsp
   ```

| 기종 | `BootName` | 파티션 |
|---|---|---|
| GBA | `gba` | `gbsp` |
| GB/GBC/NES/SNES/SMS/SG-1000/GG/PCE/Lynx/G&W/ColecoVision | `gb` `gbc` `nes` `snes` `sms` `sg1` `gg` `pce` `lnx` `gw` `col` | `retro-core` |
| 메가드라이브 | `md` | `gwenesis` |
| DOOM | `doom` | `prboom-go` |
| MSX | `msx` | `fmsx` |
| PICO-8 | `p8` | `fake08` |
| 아타리 2600 | `a26` | `stella-go` |
| 네오지오 포켓 컬러 | `ngp` | `ngpocket-go` |
| 슈퍼비전 | `supervision` | `supervision-go` |
| 포켓몬 미니 | `poke` | `pkmini-go` |
| 원더스완 | `wsc` | `wswan-go` |
| 버추얼 보이 | `vb` | `vb-go` |

런처로 돌아오려면 `switch_ota_partition --name launcher`.

### 0.3 카드를 빼지 않고 카드에 파일 넣기

앞의 두 방법으로도 남는 마지막 물리적 제약이 "카트를 넣으려면 SD를 빼야 한다"인데, 이것도 우회된다.
**펌웨어는 `/sd`를 쓰기 가능하게 마운트하고 있다.** 그러니 파일을 바이너리에 실어 보내고 부팅 때
떨어뜨리면 된다:

```cmake
# <app>/main/CMakeLists.txt
set(COMPONENT_EMBED_FILES "celeste.p8.png")
```

```c
extern const uint8_t start[] asm("_binary_celeste_p8_png_start");
extern const uint8_t end[]   asm("_binary_celeste_p8_png_end");
if (!rg_storage_exists(path)) {
    rg_storage_mkdir(RG_BASE_PATH_ROMS "/p8");
    rg_storage_write_file(path, start, end - start, 0);
}
```

2026-07-31에 셀레스테를 이렇게 넣어 실측했다. 카드가 리더에 갈 때까지 기다리지 않아도 되지만,
**남의 카트를 펌웨어 이미지에 넣은 채로 커밋하면 안 된다** — 벤치 위에서는 괜찮고 저장소에서는
아니다. 넣고, 재고, 지울 것.

**⚠️ 앱이 커지면 파티션을 넘긴다.** 카트 35KB를 넣었더니 `fake08.bin`이 파티션보다 커졌는데,
`rg_tool.py build <app>`의 크기 검사는 더미 3MB 테이블로 하기 때문에 **통과한다.** 부트로더가
그 이미지를 거부하고 **다른 앱으로 조용히 떨어졌고**, 그게 왜 gbsp가 뜨는지 한참 헤맸다.
파티션 크기는 `build-img`가 실제 바이너리 크기에서 계산하므로, **앱 하나만 굽지 말고 이미지를 다시
말아서 통째로 구울 것.**

### 0.3-1 sdkconfig를 고쳤는데 아무 일도 안 일어난다면

**esp-idf는 `SDKCONFIG_DEFAULTS`를 앱의 `sdkconfig`를 만들 때 딱 한 번만 적용한다.** 이미 있는
파일에는 다시 적용하지 않는다. 그래서 `targets/<타겟>/sdkconfig`를 고쳐도 **생성된
`<앱>/sdkconfig`가 남아 있으면 아무 효과가 없다** — 옵션이 적용되지 않은 채로 같은 링크 에러가
계속 나온다. 실제로 이걸로 네 번 연속 똑같은 실패를 보고 원인을 엉뚱한 데서 찾았다.

`rg_tool.py`가 이제 **defaults 파일 내용의 해시를 `sdkconfig.rgtarget`에 같이 적어두고**, 달라지면
재생성한다. 옛 체크아웃에서 온 파일이 섞였다면 `rm -f */sdkconfig` 한 번이면 된다.

### 0.3-2 "discarded sections"는 메모리 부족이라는 뜻이다

링커가 이렇게 말한다:

```
ld: error: --enable-non-contiguous-regions discards section `.sdata.s_panic_uart' ...
ld: error: Total discarded sections size is 775 bytes
```

**내부 RAM이 모자란다는 뜻이고, 모자란 양이 마지막 줄의 숫자다.** 그런데 `idf.py size`는
DIRAM 56% 사용에 252KB 남았다고 말한다 — 둘 다 맞다. 진짜 한계는 **`sram_low` 하나**다:

| 영역 | 크기 | 들어가는 것 |
|---|---|---|
| `sram_low` | 0x4FF00000~0x4FF2CBD0 = **183KB** | `.iram0.text` + `.dram0.data`(여기에 `.sdata`) |
| `sram_high` | 0x4FF40000~ = 384KB (L2 128KB 제외) | `.dram1.data` / `.dram1.bss` |

`.sdata`는 `sram_low`에만 규칙이 있어서 넘치면 `sram_high`로 못 흘러가고 **버려진다.** 그리고
retro-core의 `.iram0.text`가 이미 **176KB** — 여유가 6KB뿐이다. 누가 쓰는지는
`python3 -m esp_idf_size --archives build/<앱>.map`:

```
libsnes9x.a   IRAM0 .text  41408      libesp_hw_support.a  20856
libnofrendo.a              20240      libfreertos.a        16738
```

**에뮬레이터 코어가 핫루프를 IRAM에 올린 것은 의도된 것이니 건드리지 말 것.** 갚을 곳은 시스템
쪽이다. PPA 드라이버(디스플레이 블릿)를 링크하느라 775바이트가 필요했을 때는 힙 코드 7.9KB를
플래시로 내려서 갚았다(`CONFIG_HEAP_PLACE_FUNCTION_INTO_FLASH=y`). `CONFIG_FREERTOS_IN_IRAM`은
**Kconfig에서 prompt가 없는 invisible 옵션이라 sdkconfig에 써도 무시된다** — 시도해봤다.

### 0.4 로그 읽기

```sh
python3 rg_tool.py --target oc-gba-devkit --port /dev/ttyACM0 monitor   # 사람이 볼 때
python3 tools/serial_capture.py 25 > /tmp/boot.log                      # 스크립트로 잴 때
```

이 문서의 모든 수치는 두 번째 것으로 쟀다. `monitor`는 대화형이라 자동화가 안 된다.
`serial_capture.py`는 esptool과 같은 방식으로 리셋하고 정해진 시간만큼 읽는다 — 빌드·플래시·측정을
한 줄로 묶을 수 있고, **리셋을 걸 수 있다는 게 핵심**이다. 코어를 하나씩 자동 순회시킨 감사(A9)도
런처가 매 부팅 다음 코어로 넘어가게 해놓고 이걸로 리셋만 반복한 것이다.

- 개발보드에는 버튼 확장칩이 없어서 **0x20/0x21 NACK이 2초마다 정상적으로 찍힌다.** 걸러서 볼 것:
  `... | grep -v "0x2[01] failed"`
- FPS 줄은 `RG_LOG_DEBUG`라서 **릴리스 빌드에서는 안 보인다.** 성능 측정은 dev 빌드로.

---

## A. 지금 당장 — 추가 하드웨어 없이, 로그만으로 판정

### A1. PSRAM 영역에서 명령어를 인출할 수 있는가 ★최우선

[ROADMAP 3절 Q1](ROADMAP.md) — **이 답 하나로 Phase 2 다이나렉 설계가 갈린다.** 패널도 버튼도
필요 없는데 로드맵의 최우선 미해결 질문이라, 지금 가장 값싸게 가장 큰 걸 확정할 수 있는 항목이다.

프로브는 `components/retro-go/rg_psram_exec_test.c`에 있고 런처에서 호출된다
(`launcher/main/main.c`). **SD에 빈 파일 `/retro-go/psram_exec_test`를 두고 부팅하면 한 번
실행되고, 결과는 콘솔과 `/retro-go/psram_exec_test.log` 양쪽에 남는다.**

```sh
touch /media/…/retro-go/psram_exec_test     # SD를 PC에 꽂아서
```

하는 일은 네 단계다.

1. `heap_caps_aligned_alloc(64, …, MALLOC_CAP_SPIRAM | MALLOC_CAP_EXEC)` — NULL이면 힙이
   실행 가능 PSRAM을 제공하지 않는다는 뜻이고, 그때는 평범한 PSRAM으로 잡아서 **그래도 시도한다.**
   힙의 의견과 하드웨어의 의견은 다른 질문이고 중요한 건 두 번째다.
2. `addi a0, a0, 1` 256개 + `ret`을 써넣고, 데이터 캐시 라이트백 → 명령어 캐시 무효화 →
   `fence.i`로 인출 경로에 보이게 만든다.
3. 호출한다. 0을 넣어 256이 돌아오면 **쓴 그대로 실행된 것**이다(단순히 안 죽은 것과 구분된다).
4. 성공하면 숫자 세 개를 뽑는다 — PSRAM warm, 내부 RAM warm, 그리고 **PSRAM 스트리밍**
   (1MB 블록을 각각 한 번씩 = L2 128KB를 넘겨서 식은 인출). 다이나렉이 실제로 겪는 건 세 번째다.

읽는 법:

| 로그 마지막 줄 | 의미 |
|---|---|
| `VERDICT: instruction fetch from PSRAM WORKS` | 하이브리드 코드캐시 가능. 스트리밍 MIPS와 내부 RAM MIPS의 비율이 곧 설계 여유 |
| `about to call into PSRAM at …`에서 끝남 | **인출 폴트. 이게 답이다.** 코드캐시 전체가 내부 SRAM 예산 안에 들어와야 한다 → ROADMAP 3절 |
| `WRONG RESULT` | 실행은 됐는데 쓴 것과 다르다. 캐시 유지보수(Q3) 문제 |

> **죽는 쪽도 정상적인 결과다.** 폴트는 패닉이고 `/sd/crash.log`가 남는다. 프로브는 호출 **전에**
> 마커 파일을 지우므로 다음 부팅에 다시 죽지 않는다 — 답이 브릭이 되면 안 되니까.

**2026-07-31 실행됨. 답은 [ROADMAP 3절 Q1](ROADMAP.md)에 기록했다** — 요약하면 PSRAM은 실행되고
(캐시 상주 346 MIPS, 스트리밍 8 MIPS로 43배 차이), 일반 힙의 내부 RAM은 실행이 **폴트난다**.
`esp_ptr_executable()`은 양쪽 다 1이라고 답하니 믿으면 안 된다.

관련 설정은 `targets/oc-gba/sdkconfig:203`
(`CONFIG_SPIRAM_PRE_CONFIGURE_MEMORY_PROTECTION=y`).

답이 나오면 **ROADMAP 부록 D의 Q1에 기록하고 프로브는 지운다.** 기능이 아니라 질문이다.

### A2. 오토세이브 무장 — ✅ 2026-08-01 확인

```
A2: AutoSaveSecs set to 15
rg_emu_save_state: Saving state to '/sd/retro-go/saves/nes/nes.nes.sav'
Auto-save armed: an unexpected reboot will resume from slot 0   ← 한 번만
rg_emu_save_state: Saving state to '...'                        ← 이후엔 저장만
```

사양대로다. 무장 로그는 한 번, 저장은 주기마다.

기본값이 **꺼짐**(`AutoSaveSecs` 기본 0, `rg_system.c:560`)이라 아무것도 안 하면 검증할 게 없다.
SD의 `/retro-go/config/global.json`에 `{"AutoSaveSecs": 15}`를 넣고 0.2로 롬을 띄운다.

기대 로그 (게임 시작 15초 후, 한 번만):

```
Auto-save armed: an unexpected reboot will resume from slot 0
```

- "한 번만"이 사양이다(`autoSaveBootUpdated`, `rg_system.c:857`). 매 주기마다 찍히면 버그다.
- 저장은 매 주기 일어나야 한다. `/sd/retro-go/saves/<기종>/`의 mtime으로 확인.
- 런처에서는 저장이 일어나지 않는다(`!app.isLauncher`). 런처만 띄워놓고 안 된다고 하면 안 된다.
- 새 게임(`BootSlot: -1`)으로 시작해도 슬롯 0으로 저장돼야 한다 — 그게 `rg_system.c:855`의 요지다.

### A3. 재개 — ✅ 2026-08-01 확인

리셋 후 부팅하면 런처를 거치지 않고 바로 그 게임으로 들어가서:

```
configNs=nes
rg_emu_load_state: Loading state from '/sd/retro-go/saves/nes/nes.nes.sav'
```

오토세이브가 쓴 부팅 설정을 다음 부팅이 실제로 읽는다. 고리가 닫혔다.

### A3-1. 재개 — 스위치를 꺼서 검증한다

A2가 무장 로그를 찍은 다음, **USB를 뽑아서** (정상 종료 경로를 타지 않게) 전원을 끊고 다시 연결.

- `boot.json`이 `{"BootSlot": 0, "BootFlags": 1}`로 바뀌어 있어야 한다 (SD를 PC에 꽂아 확인).
- 재부팅하면 같은 롬이 그 슬롯에서 뜨는지 로그로 확인.
- `rg_system_restart()`로 재부팅한 경우와 구분할 것: 정상 종료는 `shutdown_cleanup()`에서
  세이브 기회를 따로 갖는다.

### A4. 오버클럭 — ✅ 2026-08-01 확인. **절반이 거짓말이었다**

−6~+6을 전부 훑고 실측값과 대조한 결과:

| 레벨 | 요청 | 실측 | |
|---|---|---|---|
| +1 / +2 | 380 / 400 | 379 / 399 | ✓ |
| +3 / +4 / +5 / +6 | 420 / 440 / 460 / 480 | **409 / 429 / 411 / 430** | ✗ |
| −1 / −2 / −3 | 340 / 320 / 300 | 339 / 319 / 299 | ✓ |
| −4 / −5 / −6 | 280 / 260 / 240 | **429 / 399 / 429** | ✗ |

**CPU PLL이 429MHz에서 포화한다.** 480을 요청하면 429가 되고, 460은 429×23/24=411, 420은
429×21/22=409 — 실측과 소수점까지 맞는다. 아래로는 300MHz 밑에서 추적을 멈춘다.
**실제로 되는 범위는 300~400MHz, 레벨로는 −3~+2뿐이다.**

문제는 옛 코드가 **요청값을 그대로 찍었다**는 것이다. "480Mhz applied"라고 말하면서 429로 돌았다.
이제 측정값과 2%+2MHz 이상 어긋나면 **거부하고 스톡으로 되돌린다.** 믿을 수 없는 오버클럭은
없느니만 못하다.

옛 절차(참고):

레벨 −6~+6, 20MHz 단위로 360MHz 기준(`rg_system.c:1234~`). 기대 로그:

```
Overclock level 3 applied: 420Mhz (measured: 419Mhz)
```

- 지시값과 measured가 어긋나면 PLL 계산이나 chip revision 분기(`rg_system.c:1278`)를 의심한다.
- **재부팅하면 반드시 0으로 돌아와야 한다.** 설계상 저장하지 않는다 — 남아 있으면 부팅 불가
  기기를 만들 수 있으므로 이건 기능이 아니라 안전장치다.
- 위쪽 한계(+6=480MHz)에서 죽는 것은 정상 가능. 아래쪽(−6=240MHz)도 확인해야 분수 분주기 경로
  (`target_freq % 40 != 0`, `rg_system.c:1268`)가 검증된다. 380/420/460이 그 경로다.
- 메뉴로 조작하려면 버튼이 필요하다. 버튼이 없으면 임시로 `app_main`에서 직접 호출해 스윕.

### A5. 부트 스플래시 — 소리만으로 검증 가능

`/boot/boot.wav`만 넣으면 화면 없이도 경로 전체가 돌아간다(16비트 PCM, 모노/스테레오).

- 파일이 하나도 없을 때 **부팅이 전혀 지연되지 않아야** 한다 (`bootsplash.c:202`).
- `/boot/boot.cfg`의 `duration`(기본 2500)이 소리보다 길면 그만큼 기다려야 한다.
- 포맷이 틀리면 `boot.wav is not 8/16-bit PCM, skipping`이 찍히고 부팅은 계속돼야 한다.
- 게임에서 런처로 돌아올 때는 재생되지 않는다(콜드 부트만).

### A6. gbsp 인터프리터 기준선 fps

[ROADMAP 4절 Phase 2a](ROADMAP.md). 0.2로 GBA 롬을 띄우고 `BUSY:%`와 괄호 안 마지막 숫자를 읽는다.

> **패널이 없는 상태의 수치는 CPU 상한이지 최종 성능이 아니다.** 현재 회전·바이트스왑이 CPU
> 픽셀 루프(`st7701.h`의 `lcd_send_buffer`, ROADMAP 107줄)라서, 패널을 붙이면 반드시 느려진다.
> 지금 재는 값은 "블릿을 0으로 만들었을 때의 천장"으로 기록해 둘 것 — 그것도 유용한 기준선이다.

같은 롬, 타이틀 화면이 아닌 같은 구간, 30초 이상. 프레임스킵 0 고정 (ROADMAP 7절의 함정).

### A7. jit_dev 진단 확정

[ROADMAP 부록 C.6](ROADMAP.md). `g_jit_stats.jit_hits` vs `interpreter_fallbacks`. hits가 거의
0이면 "꺼놓은 JIT"이라는 판정이 실측으로 확정된다. `jit_enabled` false/true 각각 A6와 같은 조건으로.

### A8. PICO-8 (FAKE-08)

**2026-07-30 실기에서 돌았다.** 개발보드(패널 없음)에서 BIOS 카트까지 확인했다. 아래는 그 결과와,
같은 자리에서 얻은 이 기기 전체에 대한 측정이다.

### 그날 고친 것 — 전부 같은 뿌리

이 툴체인에서 **`int32_t`가 `int`가 아니라 `long`**이라는 사실이 세 곳에서 터졌다. 다른 포팅
대상에서도 같은 성질을 가진 툴체인이면 똑같이 터진다:

| 증상 | 원인 | 조치 |
|---|---|---|
| 컴파일 불가, `fix32(int)` 모호 | z8lua가 `#ifdef _3DS`로만 가드 | 타입 동일성 기반 생성자로 교체 (포크에 커밋) |
| 부팅 직후 `bad conversion number->int` abort | `lua_Unsigned`에 맞는 캐스트가 없어 `operator double()`로 샘 | `lua_number2int/integer/unsigned`를 `int32_t` 경유로 고정 |
| 카트 로드 중 스택 보호기 | Lua 파서 재귀에 8KB main 태스크는 부족 | VM을 48KB 내부 RAM 태스크로 분리 |

### 성능 — 범인은 우리가 만든 디스플레이 타임아웃이었다

패널이 3초 안에 응답하지 않으면 초기화 태스크를 **포기하고 넘어간다**. 그 태스크는 DSI 락을 쥔 채
드라이버 안에서 블로킹돼 있다고 가정하고 지웠다 — 실제로는 **우선순위 5로 계속 돌고 있었다.**

FreeRTOS 런타임 통계가 이름으로 말해줬다:

```
st7701_init     90%      ← 포기했다고 믿었던 그 태스크
p8_vm            2%      ← 에뮬레이터
```

지울 수는 없으니(락) **최저 우선순위로 강등**했다(`st7701.h`). 남는 시간에만 돈다. 결과:

| 프레임당 | 강등 전 | 강등 후 |
|---|---|---|
| Lua (`Step`) | 820ms | **8.6~19ms** |
| 오디오 합성 | 238ms | **1.8~3.6ms** |
| 디스플레이 블릿 (코어 1, 병렬) | — | 13ms |
| `p8_vm` CPU 점유 | 2% | **54%** |

**60fps 예산 16.6ms에 프레임 작업 11~23ms.** 코어는 처음부터 충분히 빨랐고 전부 굶고 있었을 뿐이다.

### 여기서 세 번 헛짚었다 — 그래서 적어둔다

1. **오디오 신스의 double** — 실재했고(오브젝트 미정의 심볼 `__adddf3`/`__divdf3` 등으로 확인)
   float으로 전부 걷어냈지만 **프레임타임이 1ms도 안 변했다.**
2. **PSRAM 스택** — 실재했고(48KB > `SPIRAM_MALLOC_ALWAYSINTERNAL` 16KB) 내부 RAM으로 옮겼지만
   **측정값이 그대로였다.**
3. **디스플레이 블릿** — `rg_display_submit`을 빼면 60배 빨라지길래 블릿이 범인이라고 적었다.
   실제로는 제출이 없으면 저 태스크와 덜 부딪혔을 뿐이다. **ROADMAP에 틀린 수치를 한 번 썼다가
   되돌렸다.**

셋 다 그럴듯했고 앞의 둘은 고칠 가치도 있는 진짜 문제였다. 그러나 셋 다 원인이 아니었다.
**패널 없이 성능을 잴 때는 `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS`를 켜고 누가 CPU를 쓰는지
이름으로 확인하는 것이 먼저다.** 벽시계 시간만 보면 굶는 쪽을 범인으로 지목하게 된다.

### 한글 파일명 — 기기 안에서는 문제없다

빌드가 `CONFIG_FATFS_CODEPAGE_437`(US ASCII)이라 한글 이름이 깨질 것으로 봤는데, **2026-07-31
실측 결과 기기 안에서는 온전히 왕복한다.** 펌웨어가 `알타입.p8.png`, `테라리아 디메이크.png`
(공백 포함)을 쓰고 `readdir`로 읽으면 쓴 그대로 돌아온다.

**다만 이것이 증명하는 것은 "기기가 자기 자신과 일관적이다"까지다.** 쓰기와 읽기가 같은 API를
지나므로, 인코딩이 잘못돼 있어도 왕복은 성립한다. 확인되지 않은 방향은 **PC에서 복사한 한글 이름의
롬을 기기가 여는 것** — 그쪽이 어긋나면 목록에는 뜨는데 안 열린다. 카드가 리더에 갈 일이 생기면
그때 확인할 것. `tools/prepare_sd.py`가 복사 시 경고하는 이유다.

### 진짜 카트 실측 (2026-07-31)

카드를 빼지 않고 0.3의 방법으로 카트를 심어서 쟀다. 프레임당, 60fps 예산은 16.67ms:

| 카트 | 프레임 작업 | Lua | 신스 | 제출 |
|---|---|---|---|---|
| 셀레스테 클래식 | **15.4ms** | 8.1 | 6.0 | 1.1 |
| 알타입 (슈팅) | **16.6~21.7ms** | 12.5~18 | 3.6~3.9 | ~0 |

**30fps 카트는 여유, 60fps 카트는 걸친다.** 참고로 PicoPico가 ESP32에서 셀레스테 9ms/프레임
(렌더링은 다른 코어)이라고 보고했으니 같은 체급이다.

여기서 아침의 판단 하나가 뒤집힌다. **오디오 신스의 double → float 전환은 값을 했다.** BIOS
카트가 조용해서 차이가 안 보였을 뿐이고, 음악이 도는 셀레스테에서는 신스가 **프레임의 39%**다.
조용한 화면으로 오디오를 재면 안 된다.

### 남은 것

위는 타이틀/초반 구간이다. 입력이 들어가는 실제 플레이 구간은 더 무거울 수 있다:

SD에 `/roms/p8/`을 만들고 카트(`.p8` 또는 `.p8.png`)를 넣은 뒤, 0.2의 프리시드로
`{"BootName": "p8", "BootArgs": "/sd/roms/p8/카트.p8.png", "BootSlot": -1, "BootFlags": 0}` +
`switch_ota_partition --name fake08`.

| 보는 것 | 판정 |
|---|---|
| `FAKE-08 host ready: 128x128 paletted, 22050Hz, carts in /sd/roms/p8` | 백엔드 초기화 성공 |
| `Loading cart: ...` 이후 패닉 없음 | z8lua가 카트를 파싱하고 실행까지 갔다 |
| `BUSY:%`와 fullFPS (ROADMAP 7절) | **핵심 수치.** 30fps 카트인지 60fps 카트인지 먼저 확인할 것 |
| 카트를 지정하지 않고 부팅 | FAKE-08의 BIOS 카트(자체 카트 브라우저)가 뜬다 |

의도적으로 안 한 것, 실기에서 확인할 것:

- **오디오 신스가 double을 쓴다** (`external/fake-08/source/Audio.cpp:600`, 채널마다 샘플마다).
  P4는 배정밀도 하드웨어가 없어서 소프트플로트다. PicoPico가 같은 자리를 fix32로 바꿔 25ms →
  2ms를 얻었으니, `BUSY:%`가 높으면 여기가 첫 번째 용의자다.
- **`drawMode` 미구현** — PICO-8의 64x64·미러 화면 모드를 쓰는 카트는 좌상단 1/4만 그려진다.
- 키보드·마우스 입력(`KBkey`, `mouseX/Y`)은 연결하지 않았다. 버튼 7개만.
- 세이브스테이트는 없다(설계). PICO-8은 `cartdata()`로 저장하고 그건 코어가 직접 쓴다.

### A9. 전체 코어 감사 (2026-07-31)

코어마다 실게임 롬 하나씩, 패널 없이 로그로만. **부팅마다 다음 코어로 넘어가게** 해서 호스트는
리셋만 반복했다 — 롬은 0.3의 방법으로 심고, 런처가 `RG_BOOT_ONCE`로 코어를 띄우면 그 다음 부팅은
저절로 런처로 돌아온다. 에뮬레이터는 한 줄도 고치지 않았다.

| 코어 | 롬 | FPS | BUSY | 판정 |
|---|---|---|---|---|
| NES (nofrendo) | 갤럭시안 16KB | **59.2** | 12% | 여유 |
| GB (gnuboy) | 32KB | **57.8** | 17% | 여유 |
| GBC (gnuboy) | 128KB | **57.5** | 17% | 여유 |
| SMS (smsplus) | 256KB | **54.6** | 41% | 무난 |
| Game Gear | 32KB | **54.6** | 39% | 무난 |
| ColecoVision | 12KB | **54.9** | 41% | 무난 |
| MSX (fmsx) | 16KB | **49.5** | 0%* | 무난 |
| SNES (snes9x) | 768KB | **27.2** | 96% | **절반 속도** |
| 메가드라이브 (gwenesis) | 256KB | **15.6** | 100% | **1/4 속도** |
| GBA (gbsp 인터프리터) | 1.9MB | **16.4** | 100% | 예상대로 — Phase 2a 기준선 |
| PC엔진 (pce-go) | 256KB | **58~60** | 34% | 여유 (아래 수정 후) |

\* fmsx는 `rg_system_tick`에 busy를 안 넘기는 것으로 보인다. FPS만 유효.

**A티어 전 코어 60fps"라는 Phase 1 목표는 절반만 맞다.** NES/GB/GBC는 이미 거기 있고
SMS/GG/CV/MSX도 근접하지만, **SNES와 메가드라이브는 티어 A가 아니라 B다**(ROADMAP 2절 갱신 대상).
이 수치들은 모두 디스플레이 블릿(코어 1, 13ms/프레임)을 포함한 값이다.

#### 감사가 잡아낸 진짜 버그

**PC엔진이 아무 말 없이 재부팅하던 문제** — `pce_sound` 태스크의 스택 오버플로였다. 2KB로 잡혀
있는데 RISC-V 프레임이 더 커서 롬 로딩이 끝나는 순간 넘친다. 4KB로 올려 해결. **원래 Xtensa용
코드라, 2KB로 잡힌 다른 오디오 태스크들(`snes_audio`, `doom_sound`)도 같은 여유밖에 없다** —
지금은 안 넘치지만 롬에 따라 넘칠 수 있으니 비슷한 증상이 보이면 여기부터 볼 것.

**앱 전환 중 종료 경로에서 죽는 문제** — `assert failed: uxQueueMessagesWaiting`. 태스크가
종료하면 `task_wrapper`가 큐를 지우고 슬롯을 0으로 미는데, 만든 쪽은 같은 포인터를 계속 들고 있다.
시스템 모니터 태스크는 종료 중에도 돌면서 "앱이 틱을 멈췄다"고 판단해 화면에 알리려 하고, 그때
디스플레이 큐는 이미 없다. **게임을 띄울 때마다, 런처로 돌아올 때마다 지나는 길이라 간헐적으로
터진다.** `rg_task_send/peek/receive/messages_waiting`이 죽은 큐를 견디도록 고쳤다.

#### 남은 것

- **메가드라이브·SNES 최적화** — 지금은 실사용 속도가 아니다.
- DOOM(WAD 없음)과 Lynx(롬 없음)는 감사에서 빠졌다.

### A10. 게임앤워치 — 롬 포맷을 헷갈리지 말 것

**2026-08-01 실기 확인. 100 FPS, BUSY 30%.** 48개 라이브러리 전부 카드에 올렸다.

가는 길에 한 번 헛짚었으니 적어둔다. **확장자가 비슷한 두 포맷이 있고, 서로 완전히 다르다.**

| | `.gw` (이 코어가 받는 것) | `.mgw` (받지 못하는 것) |
|---|---|---|
| 정체 | SM510/SM511 **CPU 롬** + 세그먼트 아트워크 | 마드리갈 계열 **시뮬레이터 자산 묶음** |
| 컨테이너 | LZ4(`04 22 4d 18`) 또는 zlib/LZMA 또는 raw(`SM510`) | bzip2로 감싼 tar |
| 내용물 | 실행할 코드 | 기기 외형 사진, 눌린 버튼 그림, 상태별 스프라이트, PCM 녹음, 그리고 독자 바이트코드(`main.bs`) |

`.mgw`에는 **CPU 롬이 없다.** 낙하산 게임 하나에 낙하산 그림이 x1y1부터 x3y7까지 위치별로 들어
있는 게 결정적인 증거다 — 에뮬레이터라면 세그먼트 하나면 되는데 이건 상태를 전부 그림으로 갖고
있다. 지원하려면 그쪽 스크립트 VM을 새로 구현해야 하고, 그건 다른 에뮬레이터를 하나 더 만드는
일이다. 압축을 푸는 게 문제가 아니다.

`.gw` 롬은 MAME 롬셋에서 lcd-game-shrinker류 도구로 만든다. 이 프로젝트에서는 이미 만들어둔 것을
`ganda-rpi`에서 가져왔다.

### A11. 새로 이식된 코어 4종 (2026-08-01)

다른 세션이 이식한 것을 받아서 실기에 태웠다. 전부 돈다.

| 코어 | 앱 | FPS | BUSY |
|---|---|---|---|
| SG-1000 | retro-core (smsplus) | 52.6~56.3 | ~50% |
| Atari 2600 | stella-go | 55~58 | 45~58% |
| Neo Geo Pocket Color | ngpocket-go | 42~44 | **100%** |
| Watara Supervision | supervision-go | 32.4 | 22% |

봐둘 것 둘.

- **NGPC가 100%를 쓰면서 42fps다.** 여유가 없다. 화면이 붙으면 더 떨어진다.
- **stella-go가 내부 RAM 76KB를 요청하는데 31KB밖에 없어서 PSRAM으로 떨어진다**
  (`rg_alloc: CAPS not fully met!`). 지금도 56fps는 나오지만, 오늘 배운 대로 PSRAM에 놓인
  뜨거운 데이터는 비싸다. 줄이거나 나눌 여지가 있다.

### A12. 크래시 복구

`RG_PANIC`을 일부러 한 번 일으켜서:

- `/sd/crash.log`가 생기는지
- 다음 부팅이 **크래시한 앱이 아니라 런처로** 돌아오는지
- 패닉 트레이스가 로그에 남는지 (`begin_panic_trace`, `rg_system.c:109`)

임시 코드가 필요하다. dev 빌드에서만.

### A13. 디스플레이 블릿 — ✅ 2026-08-02. **그동안 절반만 재고 있었다**

**패널 없는 보드에서 잰 블릿 수치는 전부 낮게 나온 값이었다.** `st7701_ctx.framebuffer`가
NULL이면 `lcd_send_buffer()`가 첫 줄에서 돌아가고, **논리 행을 패널 열로 바꾸는 전치가 통째로
안 돈다.** 스케일·팔레트 변환만 재고 회전은 안 잰 것이다. 이제 패널이 없으면 스크래치
프레임버퍼(768KB PSRAM)를 잡아서 끝까지 돌린다 — 보이지 않을 뿐 일은 다 한다.

`rg_display_bench.c`가 기종 크기별 8비트 팔레트 서피스를 60프레임씩 밀어넣고 디스플레이 태스크의
busy 카운터를 나눈다. **롬도 버튼도 패널도 필요 없다.** `/retro-go/display_bench`를 만들어두면
돌고, 카드를 못 뺄 때를 위해 **패널이 응답하지 않으면 그냥 돈다.**

| 기종 | 소스 → 출력 | CPU | PPA |
|---|---|---|---|
| PICO-8 | 128×128 → 384×384 | 9.97ms | **3.29ms** |
| GBA | 240×160 → 720×480 | 24.87ms | **7.73ms** |
| NES/SNES | 256×224 → 512×448 | 18.37ms | **9.66ms** |
| 메가드라이브 | 320×224 → 640×448 | 23.53ms | **12.25ms** |
| 게임앤워치 | 320×240 → 640×480 | 24.11ms | **13.09ms** |

**PPA 열만 60fps 예산 16.6ms 안에 전부 들어온다.** CPU 열은 GBA·메가드라이브·게임앤워치가 모두
바깥이다. 둘 다 필요하다는 뜻이다.

여기 오기까지 **가설 두 개가 계측에 뒤집혔다. 그래서 적어둔다.**

**틀린 가설 1 — "회전이 비싸다."** DMA가 세로로 쓰면 픽셀마다 캐시라인을 하나씩 건드리니까
비쌀 것이라고 했다. 회전을 끄고 같은 크기를 재보니 **10.08 → 9.70ms, 4% 차이였다.** 회전은
공짜에 가깝다.

**틀린 가설 2 — "팔레트 확장이 비싸다."** 변환과 DMA를 나눠 재보니 DMA 쪽이 지배적이었다
(PICO-8 기준 0.64 대 9.42ms). 그런데 그 DMA 처리량이 15Mpx/s, 쓰기로 치면 30MB/s였다 —
**200MHz 옥탈 PSRAM이라면 말이 안 되는 숫자다.** 그래서 부팅 로그를 봤다:

```
I (250) esp_psram: Speed: 20MHz
```

**진짜 원인은 PSRAM이 10분의 1 속도로 돌고 있던 것이었다.** 아래 A14.

가는 길에 걸린 것 둘.

- PPA 목적지는 **캐시라인 정렬**이어야 한다. 아니면 매 프레임 `out.buffer addr or out.buffer_size
  not aligned to cache line size`가 찍히고 조용히 CPU로 폴백한다. 스크래치 프레임버퍼를
  `heap_caps_aligned_alloc`으로 잡아 해결.
- **PPA는 8비트 팔레트를 못 받는다** — 실리콘에 CLUT 모드가 없다. 팔레트는 CPU가 편다. 대신
  **출력이 아니라 소스 한 번**이라 최대 9분의 1이다.



### A14. PSRAM이 20MHz로 돌고 있었다 — ✅ 2026-08-02

`sdkconfig`에는 `CONFIG_SPIRAM_SPEED_200M=y`가 있었다. 실제로는 **20MHz**였다.

`SPIRAM_SPEED_200M`은 ESP32-P4에서 `CONFIG_IDF_EXPERIMENTAL_FEATURES` 뒤에 숨어 있다. 그게 없으면
그 choice 항목은 **선택 자체가 불가능**하고, Kconfig는 에러를 내는 대신 **목록의 최저값으로 조용히
떨어뜨린다.** 아무도 안 봤을 뿐 로그에는 처음부터 찍혀 있었다.

| | 요청 | 실제 |
|---|---|---|
| PSRAM | 200MHz | **20MHz** |
| 플래시 | 120MHz | **80MHz** |

**200MHz는 이 보드에서 안 된다.** experimental을 켜고 구우면 부트로더의 MSPI 타이밍 튜닝
(`Flash Delay: tuning success` 다음)에서 `CHIP_LP_WDT_RESET`으로 부트루프에 빠진다.
**80MHz는 멀쩡하다.** HEX 모드에서 experimental 없이 고를 수 있는 건 20과 80뿐이다.

플래시 120MHz도 포기해야 한다 — 이 보드의 GD25Q256이
`High performance mode of this flash model hasn't been supported`라고 답한다.

20 → 80MHz의 값(디스플레이 블릿, PPA 경로):

| 기종 | 20MHz | 80MHz |
|---|---|---|
| PICO-8 | 10.07ms | **3.29ms** |
| GBA | 23.44ms | **7.73ms** |
| NES/SNES | 27.06ms | **9.66ms** |
| 메가드라이브 | 34.46ms | **12.25ms** |
| 게임앤워치 | 36.97ms | **13.09ms** |

**2.8~3.1배다. 그리고 이건 디스플레이만의 이야기가 아니다** — 롬도 코어 워킹셋도 프레임버퍼도 전부
PSRAM에 있다. **이 날 이전에 이 기기에서 잰 모든 수치는 메모리가 10분의 1로 묶인 상태의 값이다.**
ROADMAP 부록 A1의 "PSRAM 스트리밍 8 MIPS"도 다시 재야 한다.

⚠️ **PSRAM 속도를 바꾸면 부트로더도 다시 구워야 한다**(`CONFIG_SPIRAM_BOOT_HW_INIT`). 앱 파티션만
구우면 부트로더가 잡아둔 속도와 앱이 기대하는 속도가 어긋나 워치독으로 부트루프에 빠진다.
`esptool.py write_flash 0x2000 <앱>/build/bootloader/bootloader.bin`.

### A15. 80MHz PSRAM — 된다. 반나절 동안 "안 된다"고 적혀 있었다

**A14에서 재본 3배는 진짜고, 쓸 수 있다.** 그리고 선택이 아니라 **이 기기가 동작하는 조건**이다:

| | 20MHz | 80MHz |
|---|---|---|
| 메가드라이브 블릿 | 34.5ms | **12.25ms** |
| 60fps 예산 | 16.6ms | 16.6ms |

20MHz에서는 블릿이 예산의 두 배라 **디스플레이가 에뮬레이터를 55fps로 붙잡는다.** 그러면 초당
28,000샘플만 나오는데 코덱은 32,000을 먹으므로 주기적으로 굶고, 스피커에서 끊긴다. **오디오 경로를
아무리 고쳐도 안 잡히는 소리다** — 증상이지 원인이 아니다. 디스플레이 제출만 빼면 즉시 59~62fps에
31,000~32,700샘플이 된다(실측).

#### 왜 "못 쓴다"고 적혔었나 — 부트로더와 앱의 속도가 어긋나 있었다

`CONFIG_SPIRAM_BOOT_HW_INIT`이라 **PSRAM을 초기화하는 것은 부트로더**고, 클럭 전환 시 MSPI
캐시 안전 모드 전환은 부트로더와 앱 **양쪽에서** `CONFIG_SPIRAM_SPEED`를 보고 갈린다
(`esp_hw_support/clk_utils.c`). 두 쪽이 어긋나면 **로그가 올라오기 전에** 죽는다:

```
부트로더 정상 → 세그먼트 로드 정상 → entry → (아무것도 안 찍힘) → CHIP_LP_WDT_RESET
```

그리고 `rg_tool.py`의 `build_image()`가 **부트로더가 없을 때만** 새로 만들고 있었다. 그래서 옛
20MHz 부트로더가 80MHz 이미지에 계속 박혀 나갔다. **`rg_tool.py`를 매번 다시 만들도록 고쳤다.**

> **PSRAM 속도를 건드렸으면 부트로더를 반드시 새로 굽고 함께 플래시할 것.** 앱만 굽는
> `rg_tool.py flash <앱>`은 부트로더를 절대 건드리지 않는다.

확인 절차(이대로 하면 오프셋 의심이 남지 않는다):

1. `build-img`로 통째로 말아 `esptool.py write_flash 0x0`으로 한 번에 굽는다.
2. 기기에서 테이블을 되읽어 대조한다 — `read_flash 0x8000 0x1000` 후 `gen_esp32part.py`로 풀어
   빌드된 `partitions.bin`과 diff. 일치하면 오프셋은 논쟁거리가 아니다.
3. 런처를 거쳐 앱을 띄우고 `MSPI DQS: tuning success` 두 줄과 `esp_psram: Speed: 80MHz`를 본다.

2026-08-02 확인: 파티션 테이블 일치, retro-core가 런처를 거쳐 정상 부팅, NES 55~59fps에 BUSY
11~19%. gwenesis도 같은 튜닝 포인트(`phase id 0`, `delayline id 15`)를 고른다.

#### MSPI 튜닝 진단을 보는 법

이게 반나절을 먹은 진짜 이유다. **PSRAM 타이밍 튜닝은 부트로더가 아니라 앱에서, `esp_psram_init()`
안에서, 앱의 첫 로그 한 줄보다 먼저 돈다.** 그리고 그 안의 진단은 전부 `ESP_EARLY_LOGD`/`LOGV`라
이 프로젝트 로그 레벨에서 컴파일 아웃된다 — **침묵하던 구간이 정확히 답이 있는 구간이었다.**

앱 전체 로그 레벨을 올리면 바이너리가 파티션을 넘긴다. 파일 하나만 올리면 된다:

```cmake
# <앱>/CMakeLists.txt, project() 다음
set_source_files_properties(
    "$ENV{IDF_PATH}/components/esp_hw_support/mspi_timing_tuning/mspi_timing_tuning.c"
    TARGET_DIRECTORY __idf_esp_hw_support
    PROPERTIES COMPILE_DEFINITIONS "LOG_LOCAL_LEVEL=ESP_LOG_VERBOSE")
```

#### 아직 못 하는 것

- **200MHz.** `CONFIG_IDF_EXPERIMENTAL_FEATURES`가 필요하고, 켜면 MSPI 타이밍 튜닝에서 부트루프.
- **플래시 120MHz.** 이 보드의 GD25Q256이 `High performance mode of this flash model hasn't
  been supported`라고 답한다. 80MHz QIO로 돈다(부트로더 로그로 확인).

#### 코어별 fps — 2026-08-02 확정 (0.2-1로 롬 지정 부팅)

⚠️ **아래는 PSRAM 20MHz 시절 값이다.** A15에서 80MHz가 되는 것으로 정정됐으므로 다시 재야 한다.
80MHz에서 다시 잰 것은 NES 55~59fps @ BUSY 11~19%(20MHz에서는 55~58 @ 11~14%)와 메가드라이브
55~62fps 뿐이다. PPA 블릿, 스크래치 프레임버퍼 있음 = **전치까지 도는 진짜 디스플레이 경로**.

| 코어 | 앱 | fps | BUSY |
|---|---|---|---|
| 게임앤워치 | retro-core | **115~120** | 22~24% |
| PC엔진 | retro-core | 58~60 | 40~50% |
| 아타리 2600 | stella-go | 57~58 | 47~49% |
| NES | retro-core | 55~58 | 11~14% |
| **메가드라이브** | gwenesis | **55~62** | 71~77% |
| PICO-8 | fake08 | 54~57 | 99% |
| 슈퍼비전 | supervision-go | 40~45 | 34~35% |
| 네오지오 포켓 컬러 | ngpocket-go | 33 | 96% |
| SNES | retro-core | 26~27 | 97% |
| GBA (인터프리터) | gbsp | 13~16 | 100% |

**A9의 수치와 직접 비교하지 말 것.** 그때는 프레임버퍼가 NULL이라 전치가 안 돌았다(A13).

남은 것은 GBA(Phase 2 다이나렉이 답), SNES, NGPC 셋이다. **SNES와 NGPC는 큐 병리가 아니라 진짜
계산량이다** — SNES는 65816+SPC700에 모드7/HDMA까지 있고, snes9x의 오디오 태스크는 아래 메가드라이브
문제와 달리 **프레임당 한 번** 배치로 넘긴다(확인함).

#### 메가드라이브 3~4 → 55~62fps — 원인은 두 번째 코어였다

`core1_task_sound` 하나였다. gwenesis가 오디오 캐치업을 **스캔라인마다** 코어1로 넘기고 있었고,
그게 기본값이었다.

| | fps | BUSY |
|---|---|---|
| `core1_task_sound = true` (옛 기본값) | **5~7** | 100% |
| `core1_task_sound = false` | **56~59** | 72~74% |

**깊이 1 큐는 소비자가 따라오는 동안만 fire-and-forget이다.** 못 따라오는 순간
`xQueueGenericSend`가 보내는 쪽을 진짜로 블록시키고(`vTaskPlaceOnEventList`, 바쁜 대기가 아니다),
줄마다 코어 간 인터럽트가 **두 번** 오간다 — 코어1을 깨우고, 슬롯이 비면 코어0을 다시 깨운다.
넘기는 일은 샘플 3개어치 곱셈 몇 번인데 프레임당 262번이면 그 왕복값을 낼 수 없다.
**두 번째 코어가 병렬화를 멈추고 랑데부 상대가 된다.**

**실기에서 죽인 가설들 — 전부 그럴듯했고 전부 틀렸다:**

| 가설 | 결과 |
|---|---|
| PSRAM 클럭 | 20/80 동일 |
| L2 캐시 128→256KB | 무변화 |
| 플래시가 DIO | 아님. 부트로더가 `SPI Mode: QIO / 80MHz` |
| IRAM 배치 (28KB 고정) | 3→4fps. 진짜지만 미미 |
| `rg_display`와 우선순위 6 충돌 | 디스플레이를 5로 낮춰도 5→9fps. **틱 경합 이론은 틀렸다** |

마지막 것은 코드 사실은 맞았다(둘 다 우선순위 6, 코어1). 실험만 살아남지 못했다.

#### 가는 길에 나온 진짜 버그 셋

전부 **첫 번째를 쫓느라 넣은 와일드 PC 진단 한 줄**이 잡았다.

1. **68000 명령어 페치에 24비트 마스크도 카트리지 크기 바운드도 없었다.** `m68k.h`의
   `m68k_read_immediate_16`이 비트 23만 보고 `ROM_DATA[A]`로 갔다. 게스트가 잘못 점프하면
   롬 포인터 밖으로 그대로 걸어나간다. **이게 크래시의 원인이었고, 그래서 코어1 경로를
   측정할 수조차 없었다.**
2. **카드의 `md.bin`은 512바이트 카피어 헤더가 붙은 파일이었다**(262,656 = 262,144 + 512).
   그대로 로드해서 리셋 벡터부터 512바이트 밀린 채 **쓰레기를 충실히 에뮬레이트**하고 있었다.
   이제 헤더를 판별해 벗기고, 그래도 카트리지처럼 안 보이면 `rg_system_rom_load_failed()`로
   거부한다. **크래시도 거부도 아닌 "그냥 도는" 게 제일 나쁘다.**
3. **오디오 버퍼가 NTSC 크기에 여유 0.** PAL은 매 프레임 346바이트를, 하필 **다음 프레임 길이를
   결정하는 인덱스 위로** 넘어쓴다.

#### 스피커를 달고 나서 — 소리가 밀리고 끊기던 이유 넷 (2026-08-02)

스피커를 개발보드에 직접 물리고 나서야 들을 수 있었다. 네 겹이었다.

| | 증상 | 원인 |
|---|---|---|
| 1 | 15초마다 크게 끊김 | `AutoSaveSecs=15`가 카드에 남아 있었다(A2 검증 잔여물, 기본값은 꺼짐). `rg_system_tick`이 **프레임 루프 안에서** SD에 세이브스테이트를 쓴다 — 직후 29fps로 떨어진다 |
| 2 | 상시 5% 부족 | I2S 스테이징 버퍼가 180프레임인데 메가드라이브는 프레임당 444를 낸다. **한 번에 못 보내고 세 번에 나눠 보냈고**, 그 호출은 DMA 링에 자리 날 때까지 진짜로 블록한다. 프레임당 대기 세 번 = 약 0.9ms |
| 3 | 계속 드리프트 | **코덱이 26,633Hz를 거부하고 있었다** — `E ES8311: Unable to configure sample rate 26633Hz with 6818048Hz MCLK`. I2S는 그 속도로 밀어넣고 코덱은 다른 속도로 재생. 표준 레이트 32000으로 리샘플해서 해결 |
| 4 | 소리가 "요상함" | **PSG(SN76489)가 아예 안 났다.** 제출 코드가 FM 버퍼만 읽고 있었고(`TODO: Mix in gwenesis_sn76489_buffer`가 그대로), 게다가 `sn_enable` 기본값이 **0**이었다. 둘 다 고쳤다 |

가는 길에 오디오 경로에서 나온 것 둘:

- **모노 버퍼를 스테레오 프레임으로 재해석하고 있었다.** `rg_audio_submit`의 count는 샘플이 아니라
  L/R 쌍이라, 인접한 모노 샘플 두 개가 한 프레임의 좌/우가 됐다. 필터 없는 2:1이라 13kHz 위가
  접히고, 좌우가 한 샘플(18.8µs) 어긋난다.
- 출력 버퍼가 528으로 남아 있었다 — 2:1 시절 크기다. 새 비율(1.6646:1)에서는 **매 NTSC 프레임의
  1.1%, PAL은 17%를 조용히 버린다.**

> **카드를 못 빼는 기기에서 롬을 바꾸는 법:** 정상 덤프를 앱 바이너리에 실어 보내고
> 부팅 때 `/sd`에 떨어뜨린다(0.3). 파티션을 넘기면 `build-img`로 통째로 다시 말아야 한다 —
> 파티션 크기는 거기서 실제 바이너리 크기로 계산된다. **남의 롬은 커밋하지 말 것.**

---

## B. 점퍼선만 있으면 — 선 2~4개

| # | 항목 | 절차 | 기대 |
|---|---|---|---|
| B1 | 입력 맵 | GPIO 20~31을 하나씩 GND에 | 각 키가 눌린 로그. 순서가 어긋나면 `config.h:25` |
| B2 | 리커버리 코드 | 28+29+30+31 잡고 전원 인가, 0.5초 유지 | `Button ... being held down...` ×5 후 리커버리. `RG_RECOVERY_HELD`(`rg_system.c:496`)가 **전부** 눌림을 요구하므로 하나라도 빠지면 안 들어간다 |
| B3 | MENU / OPTION | 28+29 / 28+30 | 게임 중 메뉴 진입 (화면은 C절) |
| B4 | 부트루프 rescue | 15초 안에 죽는 코드로 3회 부팅 | `Boot attempt 3 without a successful boot in between` → rescue. 성공 부팅 시 `Boot declared successful, clearing rescue counter`. RTC 메모리라 **전원을 끊으면 카운터가 날아가는 것이 정상** |

---

## C. 패널이 와야 되는 것

FLASHING.md "Known unknowns"의 순서가 그대로 우선순위다.

| # | 항목 | 실패 시 볼 곳 |
|---|---|---|
| C1 | 패널이 켜지는가 | ST7701 init 시퀀스가 D310N9362V0용이 아니라 드라이버 기본 480x800용이다. `targets/oc-gba/config.h`의 증상별 레지스터 메모 |
| C2 | 리본이 없을 때 안 멈추는가 | 리본을 빼고 부팅 → `Panel did not answer in N ms -- is the DSI ribbon connected?` 후 나머지가 계속 떠야 한다 (`st7701.h:380`). **이건 패널 없이 지금 확인 가능** |
| C3 | 상하 반전 | `lcd_set_rotation`(`st7701.h:183`)은 현재 no-op이고 회전은 `lcd_send_buffer`의 픽셀 루프가 한다. 뒤집혔으면 거기 |
| C4 | R/B 스왑 | 프레임버퍼 바이트 순서, 같은 파일 |
| C5 | 필러박스 기하 | 240x160 → 720x480이 800x480 안에 정수배로 앉는지. 로그의 `... => 720x480 ... left:40 top:0` |
| C6 | 스플래시 이미지 | `/boot/logo.png` 중앙 배치, 확대는 하지 않음 |
| C7 | rescue·크래시 다이얼로그 가독성 | 한글 폰트가 실제 패널에서 읽히는지. 글리프 없는 문자는 **아무것도 안 그려져서** 조용히 실패한다 |
| C8 | 블릿 비용 | A6 수치와의 차이가 곧 Phase 1이 걷어낼 몫 |

---

## D. 캐리어 보드가 와야 되는 것

| # | 항목 | 비고 |
|---|---|---|
| D1 | `pinmap_from_kicad.py --check` | **보드 리비전마다 먼저.** 배선 확인 전에 다른 걸 의심하면 시간을 버린다 |
| D2 | TCA9554 0x20 / 0x21 | 확장칩 경로. 개발보드에서 NACK만 확인된 상태 |
| D3 | 패들 2개 | 이미 한 번 다른 확장칩에 붙어 있던 것을 `--check`가 잡았다 |
| D4 | PCF8591 0x48 / 74LVC1G08 게이팅 DCK | 스펙 markdown이 아니라 `.kicad_pcb`가 기준 |
| D5 | IP5306 배터리 | 게이지 표시. 소프트 파워는 보류(핀 5 미연결) |
| D6 | 스위치 전원 차단 시 재개 | A2/A3를 실제 슬라이드 스위치로 재현 |

---

## 실기 검증 대상이 아닌 것 (오해 정리)

- **`components/odroid-compat/`** — 어느 앱의 `COMPONENTS` 목록에도 없어서 **빌드조차 되지 않고**,
  `odroid_system.h`를 include하는 코드도 자기 자신뿐이다. 실기 항목이 아니라 "빌드 편입 + 소비자
  확보"가 선행 과제다.
- **GameSwitcher** — 미착수. 부품(`bookmarks.c`의 recents, 슬롯별 스크린샷,
  `rg_system_switch_app`의 slot 인자)은 있으니 조립이지만, 코드가 없으므로 검증할 것도 없다.
