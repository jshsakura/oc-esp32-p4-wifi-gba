# elfspike — 코어를 런타임에 로드하는 설계의 타당성 검증

`docs/APP_PARTITION_CEILING.md`의 "코어를 SD에서 런타임 로딩하는 길" 절과 같이 볼 것.

## 결론부터

**성능 질문은 끝났다.** 앱 전체를 PSRAM에서 실행해도 SNES 프레임 시간이 안 변한다
(32,929 us/frame 대 플래시 XIP 33,043). 이 설계를 막는 것은 성능이 아니다.

**아직 안 되는 것은 `elf_loader`가 모듈을 실행시키는 부분이다.** 재배치까지는 간다.

## 파일

- `bench.c` — 에뮬레이터 인터프리터를 흉내낸 벤치마크. 호스트에 정적 링크하는 쪽과
  모듈로 빌드하는 쪽이 **같은 소스**를 쓴다. 체크섬을 비교하므로 다른 일을 하고 있으면
  바로 드러난다.
- `elfspike.c` — 호스트 쪽. 앱의 `main/`에 복사해서 쓴다. A/B/A로 잰다.
- `bench.so` — 손으로 컴파일한 모듈:

      riscv32-esp-elf-gcc -march=rv32imafc_zicsr_zifencei -mabi=ilp32f \
        -fPIC -shared -nostdlib -O2 -o bench.so bench.c

- `loader-patches/` — `espressif__elf_loader` 1.3.2에 넣었던 수정본 전체 파일.
  `managed_components/`는 재해결하면 지워지므로 여기 보존한다. `LOCAL FIX`로 검색할 것.

## 붙이는 법

앱(`sm-go`로 했다)에:

    main/idf_component.yml:  espressif/elf_loader: "*"
    main/CMakeLists.txt:     COMPONENT_EMBED_FILES "bench.so"
                             REQUIRES에 "espressif__elf_loader"
                             -DRG_ELF_SPIKE=1
    main/main.c:             rg_system_init() 뒤에 elfspike_run()

sdkconfig 오버레이:

    CONFIG_ELF_LOADER_LOAD_PSRAM=n
    CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT=y
    CONFIG_ESP_SYSTEM_PMP_IDRAM_SPLIT=n

**SD가 아니라 임베드로 넣는 이유**: 이 보드는 카드가 케이스 안에 있다. 로더가 타는 경로는
`esp_elf_relocate(&elf, buf)`로 동일하다.

## 결과 — 런타임 로딩이 끝까지 동작한다 (2026-08-04)

**모듈이 로드되고, 실행되고, 정적 링크판과 같은 답을 낸다.** 체크섬 `0x31eebd2f`가 양쪽
동일하다 — 같은 일을 하고 있다는 증거다.

    ELFSPIKE: module is 1364 bytes
    ELFSPIKE: relocate took 1618 us
    ELFSPIKE: static/host  147417 us  (126408, 168426)
    ELFSPIKE: loaded/PSRAM 175633 us  (176092, 175174)

### 결정적이었던 것: 모듈을 손으로 빌드하면 안 된다

`-fPIC -shared`로 직접 컴파일한 모듈은 **진입점이 `0x0`** 이었다. 컴포넌트의
`project_elf()` 매크로가 만든 것은 `0x340`이다 — `-e app_main`, `-Dmain=app_main`이
그 차이다. 로더는 멀쩡했고, 우리가 준 파일이 틀렸던 것이다. 재배치 시간도 9,900us에서
**1,600us로** 떨어졌다.

`tools/elfspike/module/`이 그 매크로로 빌드하는 프로젝트다. `project_so()`가 아니라
`project_elf()`를 써야 한다 — `project_so()`는 일반 앱 링크를 남겨서 `app_main` 미정의로
실패한다. 그리고 `include(elf_loader)`는 반드시 `project()` **뒤에** 와야 한다.

### 반환값은 argv로 받는다

`esp_elf_request()`는 진입 함수의 반환값을 **버린다** (`elf->entry(argc, argv)` 호출 후
무조건 0을 반환). 체크섬은 `argv[0]`이 가리키는 곳에 모듈이 써넣는다.

### 호스트 함수 호출까지 된다 — 이게 진짜 관문이었다

모듈이 자기 코드만 도는 건 로더가 재배치하고 CPU가 실행한다는 증명일 뿐이다. **진짜
코어는 거의 전부가 프레임워크로 되돌아오는 호출**이다 — `rg_display_submit`,
`rg_audio_submit`, `rg_alloc`. 그게 로드 시점에 돌아가는 펌웨어의 심볼로 풀려야 한다.

**된다.** 모듈에 호스트만 정의하는 함수를 하나 두고(`elfspike_host_mix`), 호스트가
`esp_elf_register_symbol()`로 `{이름, 포인터}` 한 줄짜리 테이블을 등록했다. 모듈은
`R_RISCV_JUMP_SLOT` 미해결 심볼(Sym.Value 0)로 나가고, 로더가 그걸 채운다.

검증은 산술로 했다. 모듈이 낸 값 `0x90de1cf5`가 `r ^ host_mix(r)`와 비트 단위로 같다:

    bench(seed)     = 0x31eebd2f
    host_mix(r)     = 0xa130a1da
    r ^ host_mix(r) = 0x90de1cf5   <- 모듈이 낸 값

우연히 맞을 수 없는 값이다. **로드된 코드가 호스트 펌웨어 안으로 실제로 점프했다.**

실제 코어를 올릴 때 필요한 건 같은 메커니즘에 항목이 많아지는 것뿐이다 — 프레임워크의
`rg_*` 표면을 테이블로 만들면 된다. 컴포넌트에 `tool/symbols.py`가 있어서 ELF에서
자동 생성할 수도 있다.

### 숫자는 아직 확정 아니다

로드된 쪽은 175.6k로 **매우 안정적**이다(0.5% 이내, 3회 반복 동일). 문제는 정적 쪽이
매번 126k / 168k로 갈린다는 것이다 — A/B/A에서 첫 정적 실행이 항상 빠르고 둘째가 항상
느리다. 재현은 되지만 원인이 아직 없다.

그래서 **"19% 느리다"는 두 다른 상태의 평균일 뿐이다.** 첫 샘플 기준이면 39%, 둘째
기준이면 4%다. **원인을 찾기 전에는 이 숫자를 인용하지 말 것.**

의심 가는 것: 모듈의 실행 가능 메모리 할당/해제가 힙을 흔들거나, 패치로 넣은 I-캐시
무효화가 호스트 코드에 영향을 주거나, PIC의 GOT 간접 접근 비용.

## 어디까지 갔나## 어디까지 갔나 (2026-08-04)

| 단계 | 상태 |
|---|---|
| 컴포넌트가 P4/RISC-V로 빌드 | ✅ |
| 모듈 재배치 | ✅ (2,612바이트에 ~10ms) |
| 실행 가능 메모리 할당 | ✅ (아래 패치 셋 필요) |
| 모듈 실행 | ❌ 진입점에서 죽는다 |

### 넣어야 했던 패치 셋 (전부 P4 한정 결함)

1. `esp_elf_malloc()`이 `MALLOC_CAP_EXEC`를 요구하지 않는다. P4는
   `ELF_LOADER_BUS_ADDRESS_MIRROR`가 `n`이라 그 분기를 탄다.
2. 같은 자리에서 `MALLOC_CAP_CACHE_ALIGNED`가 무조건 OR된다. P4 **내부 RAM은 그걸
   광고하지 않으므로**(`heap/port/esp32p4/memory_layout.c`) 조합이 만족 불가 →
   `-ENOMEM`. PSRAM 경로에만 붙어야 한다.
3. `CONFIG_ESP_SYSTEM_PMP_IDRAM_SPLIT=y`(기본값)면 내부 RAM에 EXEC 능력이 아예 없다.
   런타임에 코드를 올리는 기기는 이 보호를 포기해야 한다.
4. 캐시 유지보수(`esp_elf_arch_flush`)가 **함수도 호출부도 전부**
   `#ifdef CONFIG_ELF_LOADER_LOAD_PSRAM` 안에 있다. 내부 SRAM 경로엔 아예 없다.

### 지금 막힌 지점

패치를 다 넣어도 진입점에서 죽는다 — `MCAUSE 0x1f`. 이건 **가짜 원인 코드**다:
`panic_soc_check_pseudo_cause()`가 캐시 오류가 떠 있을 때 illegal-instruction /
instruction-access-fault / load-access-fault를 이걸로 덮어쓴다. 즉 근본 원인은 여전히
페치 실패다.

**다음에 볼 것 — 모듈이 잘못됐을 가능성이 크다.** 로더가 계속
`Too much padding before segment[2], padding: 4096`을 찍는다. 위 `bench.so`는 컴포넌트의
`project_elf()` / `project_so()` 매크로를 안 쓰고 손으로 컴파일한 것이다. 그 매크로는
`-e app_main`, `-Dmain=app_main`, `-nostartfiles`, `-fvisibility=hidden`, gc-sections,
strip을 쓴다(`elf_loader.cmake`). **별도 IDF 프로젝트를 만들어 그 매크로로 모듈을 빌드해
보는 것이 다음 한 걸음이다.** 로더가 깨진 게 아니라 우리가 준 파일이 로더가 기대하는
모양이 아닐 수 있다.

## PSRAM 경로는 왜 안 쓰나

`CONFIG_ELF_LOADER_LOAD_PSRAM=y`(기본값)로 하면 PSRAM 명령어 페치가 필요하고, 그건
`CONFIG_SPIRAM_XIP_FROM_PSRAM`으로만 켤 수 있다. 그런데 **로더 + XIP이면 이미지가 부팅
불가**가 된다 — `invalid segment length 0xffffffff`, 플래시는 바이트 단위로 검증되는데도.
컴포넌트의 `linker.lf`가 **모든** 아카이브의 `.got`/`.got.plt`를 `flash_rodata`로 보내는데
XIP이 옮기는 게 바로 그 섹션이라, 그 충돌로 보인다.
