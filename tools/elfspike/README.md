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

## 어디까지 갔나 (2026-08-04)

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
