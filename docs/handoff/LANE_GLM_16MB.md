# GLM 레인 — 16MB 천장을 제대로 열어라 (2026-08-04)

이제 개발은 네가 한다. 나는 검증과 감독만 한다.

## 무엇이 걸려 있나

`fceumm-go`(NES)와 `tgbdual-go`(GB/GBC)는 파티션이 16MB 위(`0x1030000`, `0x1230000`)라
**이 보드에서 한 번도 부팅한 적이 없다.** 그래서 세 기종이 지금 더 나쁜 코어로 우회 중이다:

| 기종 | 지금 (우회) | 제대로 됐을 때 |
|---|---|---|
| NES | nofrendo 57fps | fceumm 40fps (정확도↑, 속도↓ — 선택의 문제) |
| 게임보이 | gnuboy 58fps | **tgbdual ~200fps** |
| GBC | gnuboy 55fps | **tgbdual** |

게임보이는 3.5배 차이다. 이게 걸린 판이다.

## 이미 알아낸 것 (여기서 출발해라, 다시 유도하지 마라)

**천장은 실리콘이 아니라 소프트웨어 게이트다.** 여는 옵션이 있다:

    CONFIG_IDF_EXPERIMENTAL_FEATURES=y
    CONFIG_BOOTLOADER_CACHE_32BIT_ADDR_QUAD_FLASH=y

그리고 **좁게 시험하면 동작한다.** 부트로더를 다시 굽고 두 파티션 다 부팅시켜 확인했다 —
NES 40fps, GB 200fps. 실측이다.

**그런데 전체 빌드에 적용하면 기기가 죽는다.** 공유 타깃 sdkconfig에 넣고 전 기종
스윕을 돌리면 **모든 앱이 부팅 실패**하고, 부트로더가 `CHIP_LP_WDT_RESET`으로 부트루프에
빠진다. 로그에 이게 새로 뜬다:

    W flash HPM: HPM mode is optional feature that depends on flash model
    W flash HPM: HPM mode with DC adjustment is disabled

`CONFIG_SPI_FLASH_HPM_DIS=y`로 명시적으로 꺼봤다. **소용없었다.** 같은 워치독 루프다.
그러니 HPM은 원인이 아니거나 원인의 일부일 뿐이다.

전말은 `docs/APP_PARTITION_CEILING.md` 마지막 절에 있다. **먼저 읽어라.**

## 풀어야 할 질문 — 이게 전부다

**좁은 시험은 되고 넓은 적용은 왜 안 되는가?**

두 경우 모두 부트로더는 **같은 옵션으로 빌드**했다. 유일한 차이는 **앱들이 같은 설정으로
다시 빌드됐다는 것**이다. 앱이 부트로더를 어떻게 죽이는지가 미지수다.

볼 만한 것들:

1. 앱 이미지 헤더의 플래시 모드/크기/주파수 바이트. `esptool image_info`로 실패하는
   앱과 성공하는 앱을 비교해라.
2. **스윕은 `write_flash`에 `--flash_mode`/`--flash_size`/`--flash_freq`를 안 준다.**
   내 수동 플래시는 줬다. 이게 헤더를 바꾸는지 확인해라. 이 차이가 좁은 시험과 넓은
   적용의 진짜 차이일 수 있다 — 가장 유력한 가설이다.
3. `IDF_EXPERIMENTAL_FEATURES`가 앱 쪽에서 또 무엇을 켜는지. `sdkconfig` diff를
   옵션 켜기 전/후로 떠서 **전부** 비교해라. HPM 말고 다른 게 있을 것이다.
4. 옵션을 부트로더에만 적용하고 앱에는 적용하지 않는 방법이 있는지.

## 지켜야 할 것

- **기기를 죽여도 복구된다.** 부트로더 백업이
  `/tmp/claude-1000/-home-jshsakura-app-oc-esp32-p4-wifi-gba/6f085ba5-085d-4c28-9aee-f929329934d3/scratchpad/bootloader_backup.bin`
  (md5 `233f8530058c88179efaf3227f66ea8b`)에 있다. `0x2000`에 굽고 앱을 원래 설정으로
  다시 구우면 돌아온다. **한 번 해봤고 두 번 다 복구됐다.** 겁내지 말고, 대신 매번
  복구 경로를 확인하고 진행해라.
- **시리얼 포트는 공유다.** 지금 내가 검증 스윕을 돌리고 있다. 쓰기 전에 물어라.
- `sm-go/`와 `tools/elfspike/`는 건드리지 마라 (내 작업 + 코덱스 레인).
- **막히면 스텁이나 우회 만들지 말고 멈추고 보고해라.** 이건 "동작하는 것처럼 보이게"
  만들면 안 되는 종류다 — 기기가 벽돌처럼 보이는 실패 모드다.
- **좁은 시험이 통과했다고 끝난 게 아니다.** 반드시 전 기종 스윕까지 통과해야 한다.
  내가 오늘 그걸로 틀렸다.

## 다 하면

`git commit`. 그리고 **완료 보고를 한 줄로 남겨라** — 내가 검증한다.
