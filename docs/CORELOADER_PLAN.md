# 코어 런타임 로딩 — 제품 구성 계획

증명은 끝났다. `tools/elfspike/README.md`가 그 기록이다:

    static gnuboy (retro-core)   58.1 fps  BUSY 17.4%  3,002 us/frame
    /sd 파일에서 런타임 로드      58.1 fps  BUSY 17.1%  2,940 us/frame

이 문서는 그걸 **스파이크에서 제품 구성으로** 옮기는 계획이다.

## 지금 무엇이 스파이크인가

| 지금 | 되어야 할 것 |
|---|---|
| `sm-go`(SNES 앱) 안에 `RG_ELF_SPIKE`로 얹혀 있다 | 독립 앱 `coreloader` |
| `prboom-go` 파티션에 구워 돌린다 | 자기 파티션 |
| gnuboy 하나만 | 코어 여러 개, 런처가 고른다 |
| 심볼 테이블을 손으로 18줄 적었다 | `tool/symbols.py`로 생성 |
| 모듈을 임베드해서 첫 부팅에 카드로 떨어뜨린다 | 이미지 빌드가 카드에 넣는다 |
| `elf_loader` 패치가 `managed_components/`에 손으로 복사된다 | 빌드가 자동 적용 |

## 목표 구조

    coreloader/                     <- 앱 하나. 코어를 안 가진다.
      main/coreloader.c             <- 심볼 테이블 등록 → 로드 → 코어 API로 구동
    cores/
      gnuboy/                       <- 코어별 모듈 프로젝트 (project_elf)
      nofrendo/
      smsplus/
    /sd/retro-go/cores/*.elf        <- 배포 산출물

런처는 기종 탭을 `coreloader`로 보내고, `app->configNs`로 어느 `.elf`를 열지 고른다 —
`retro-core`가 스무 기종을 가르는 것과 같은 방식이고, 다른 점은 **안 쓰는 코어가
바이너리에 없다는 것**이다.

## 왜 이게 이 저장소의 모든 천장을 없애는가

- OTA 슬롯 16개 → 무관. 코어가 파티션이 아니다.
- 16MB 부팅 한계 → 무관. 코어가 부팅 이미지가 아니다.
- 스무 코어 정적 상주로 내부 RAM 고갈 → 무관. 도는 코어만 메모리에 있다.
- 앱마다 ~950KB 프레임워크 사본 → 무관. 호스트 하나뿐이다.

## 나눠 맡을 것

### 레인 A — 코어를 더 모듈로 (기계적, 병렬 가능)

`tools/elfspike/gnuboy-module/`이 검증된 조리법이다. 같은 방식으로:

- `nofrendo` (NES)
- `smsplus` (마스터 시스템/게임기어/콜레코/SG-1000)
- `pce-go` (PC Engine)

각각에 대해 **함수 테이블 헤더 + 진입점 + 미해결 심볼 목록**을 낸다. 심볼 목록이
호스트 테이블의 입력이다.

### 레인 B — 파티션 이상 규명 (출하 전 필수, 블로커) — 해결됨

호스트가 `0x3c0000`(prboom-go)에서는 부팅하고 `0xef0000`(sm-go)에서는
`invalid segment length 0xffffffff`로 거부된다. 이미지는 양쪽 다 esptool 기준 유효하고
기기에서 읽어와도 해시가 맞고 크기도 들어간다. **XIP 실패 때와 같은 서명이다.**

**원인 = 16MB 경계 침범. XIP 실패와 같은 근인이다.** 호스트 이미지(1,161,744바이트)를
`0xef0000`에 구우면 끝이 `0x100BA10`이 되어 16MB(`0x1000000`)를 47,632바이트 초과한다.
구 bootloader는 32비트 캐시가 없어 16MB 위를 못 읽고, 16MB 위에 걸친 segment 6의 헤더를
읽다 erased flash(`0xFF`)를 가져와 `length=0xffffffff`가 된다. `0x3c0000`에서는 이미지
전체가 16MB 아래(끝 `0x4DBA10`)라 정상. 실측으로 확인했다 — 같은 바이너리, 같은 bootloader로
`0x3c0000`→부팅 / `0xef0000`→`invalid segment length 0xffffffff` 후 caprice32-go로 폴백.

**파티션 크기가 거짓말한다:** sm-go 슬롯은 1280K(1,310,720)지만 16MB 아래 쓸 수 있는 건
`16MB − 0xef0000 = 1,114,112바이트(1088K)`뿐이다. 호스트(1,161,744)가 이를 47KB 초과한다.

**coreloader 파티션 선택 규칙:** `offset + image_size ≤ 0x1000000`을 만족하는 슬롯에 둘 것.
prboom-go(`0x3c0000`)는 OK, sm-go(`0xef0000`)는 NG. coreloader는 특별하지 않다 — 모든 앱과
같은 16MB 제약이며, 천장을 실험적 32비트 캐시로 올리는 건 retro-core가 죽어서 불가하다
(별건 별건으로 마감). 그러므로 coreloader 자리도 16MB 아래로 잡는 수밖에 없다.

### 레인 C — 심볼 테이블 자동 생성

지금 `gbhost.c`가 18줄을 손으로 들고 있다. 코어가 늘면 유지가 안 된다.
`elf_loader`의 `tool/symbols.py`가 ELF에서 뽑아준다 — 빌드에 엮어서
`coreloader`가 자기 심볼 테이블을 생성하게 한다.

**주의**: 코어의 import는 호스트와의 계약이다. 자동 생성하되 **생성된 목록을
빌드 산출물로 남겨서** 무엇이 계약에 들어왔는지 사람이 볼 수 있어야 한다.

## 반드시 지킬 것

- **루프를 맞추기 전에 성능을 재지 마라.** 첫 측정이 50fps/BUSY100%로 나와 17% 손해처럼
  보였는데, 전부 호스트 루프 탓이었다(프레임스킵·`rg_display_sync()` 없음). 비교 대상의
  루프와 똑같이 맞춘 뒤에 재라.
- **모듈을 손으로 컴파일하지 마라.** `-fPIC -shared`는 진입점 `0x0`을 만든다.
  `project_elf()`를 써라. `include(elf_loader)`는 `project()` **뒤에**.
- **좁은 시험 통과 = 끝이 아니다.** 오늘 16MB 건이 좁게는 되고 넓게는 기기를 죽였다.
  전 기종 스윕까지 통과해야 한다.
