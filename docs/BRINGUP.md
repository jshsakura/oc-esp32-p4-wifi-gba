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
| GB/GBC/NES/SNES/SMS/GG/PCE/Lynx/G&W/ColecoVision | `gb` `gbc` `nes` `snes` `sms` `gg` `pce` `lnx` `gw` `col` | `retro-core` |
| 메가드라이브 | `md` | `gwenesis` |
| DOOM | `doom` | `prboom-go` |
| MSX | `msx` | `fmsx` |

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

### 0.4 로그 읽기

```sh
python3 rg_tool.py --target oc-gba-devkit --port /dev/ttyACM0 monitor
```

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

### A2. 오토세이브 무장

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

### A3. 재개 — 스위치를 꺼서 검증한다

A2가 무장 로그를 찍은 다음, **USB를 뽑아서** (정상 종료 경로를 타지 않게) 전원을 끊고 다시 연결.

- `boot.json`이 `{"BootSlot": 0, "BootFlags": 1}`로 바뀌어 있어야 한다 (SD를 PC에 꽂아 확인).
- 재부팅하면 같은 롬이 그 슬롯에서 뜨는지 로그로 확인.
- `rg_system_restart()`로 재부팅한 경우와 구분할 것: 정상 종료는 `shutdown_cleanup()`에서
  세이브 기회를 따로 갖는다.

### A4. 오버클럭 스윕

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

### A10. 크래시 복구

`RG_PANIC`을 일부러 한 번 일으켜서:

- `/sd/crash.log`가 생기는지
- 다음 부팅이 **크래시한 앱이 아니라 런처로** 돌아오는지
- 패닉 트레이스가 로그에 남는지 (`begin_panic_trace`, `rg_system.c:109`)

임시 코드가 필요하다. dev 빌드에서만.

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
