# Forcetris — 인수인계

이 문서 하나로 이 저장소에서 작업을 시작할 수 있게 쓴 것. 읽는 순서는
**이 문서 → `DESIGN.md` §10(불변 제약) → `cpp/README.md`(엔진·GUI 상세)**.

---

## 1. 이게 뭔가

캐주얼 우선 로그라이트 테트리스, **"The Forge Map"**. GPL-3.

엔진이 **둘**이고 둘은 항상 같은 게임이어야 한다.

| | 무엇 | 역할 |
|---|---|---|
| `engine/` | Python + pygame | **레퍼런스 구현.** 채점 기준 |
| `cpp/` | C++17 + SDL2 + Dear ImGui | 실제로 출시하는 것 (Windows exe / Android APK) |

두 엔진은 `equivalence`·`trace` 테스트로 **프레임 단위 대조**된다. 심(sim)
쪽 규칙을 한쪽에서만 고치면 빌드가 깨진다 — 그게 설계 의도다.

- 컨셉: 조각 하나하나가 대장간의 불에 들어간다. **Flow** 게이지가 차면
  **Overdrive**. 맵을 오르며 **Temper 카드**(40장, 7가문)로 빌드를 짠다.
- 모드: 캠페인(The Forge Map, 3챕터 + Endless Climb), Quick Play 6종.

---

## 2. 환경과 명령

```bash
cd /home/user/Forcetris          # 저장소 루트. 모든 명령은 여기 기준
```

### 빌드

```bash
# 리눅스(개발·테스트용)
cd cpp/build && cmake --build . -j4

# Windows 크로스 빌드
cd cpp/build-win && cmake --build . -j4

# Android APK
cd cpp/android && \
  ANDROID_SDK=/root/android/rootfs/opt/android-sdk-linux \
  ANDROID_NDK=/root/android/rootfs/opt/android-sdk-linux/ndk/28.0.13004108 \
  SDL2_SRC=/root/android/SDL2-2.30.11 \
  R8_JAR=/root/android/r8.jar ./build.sh
```

### 테스트 — **전체 매트릭스가 통과해야 커밋**

```bash
cd cpp/build && ctest -j4 --output-on-failure     # 30개, 약 150초
```

```bash
cd /home/user/Forcetris
for t in tools/test_*.py; do
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy python3 "$t" | tail -1
done                                               # 12개 스위트
```

ctest 30개: `equivalence trace replay_cross hiscore_cross cascade_check
rating_check cheese_check versus_check bot_check opponent_check clear_check
sealed_check cold_check chaos_check input_check ward_check skill_check
fuse_check career_check munch_check profile_check temper_check
campaign_check audio_check feel_check gui_smoke gui_smoke_versus
gui_smoke_run gui_smoke_endless gui_smoke_stops`

가장 느린 셋: `gui_smoke_stops`(~150s), `equivalence`(~70s), `trace`(~20s).

### 헤드리스로 게임 돌리기 / 스크린샷

전부 `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy` 필요.

| 환경변수 | 뜻 |
|---|---|
| `FORCETRIS_SMOKE=<frames>` | 봇이 키를 난타하며 N프레임 돌린다 |
| `FORCETRIS_SHOT=<파일.bmp>` | 마지막 프레임을 찍는다 |
| `FORCETRIS_SHOTS=<디렉터리>` | **화면마다 한 장** (16화면 투어) |
| `FORCETRIS_SMOKE_MODE=0..5` | 모드 강제 (3=치즈, 5=듀얼) |
| `FORCETRIS_SMOKE_RUN=1` | 캠페인 맵 런 전체를 자동 주행 |
| `FORCETRIS_SMOKE_ENDLESS=1` | 같은 걸 Endless Climb로 |
| `FORCETRIS_SMOKE_STOPS=1` | 대장간/이벤트 방을 강제로 밟는다 |
| `FORCETRIS_MOBILE=1080x2280` | 폰 세로 레이아웃 |
| `FORCETRIS_CAMPAIGN=<경로>` | campaign.dat 리다이렉트 |

투어 예시 (**시각 변경은 반드시 이걸로 확인**):

```bash
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  FORCETRIS_SMOKE=3400 FORCETRIS_SHOTS=/tmp/tour cpp/build/forcetris
python3 -c "
from PIL import Image; import glob
for f in glob.glob('/tmp/tour/*.bmp'): Image.open(f).save(f[:-4]+'.png')"
```

> 투어는 전환 커튼(`kCurtain`)이 끝난 뒤 셔터를 누른다. 예전엔 4프레임에
> 찍어서 **16장 전부에 전환 와이프가 찍혀** 있었고, 그걸 렌더 버그로
> 오진할 뻔했다. 지금은 고쳐져 있으니 화면 한복판의 밝은 가로선이 보이면
> 그건 진짜 버그다.

---

## 3. 절대 어기면 안 되는 것 (요약 — 전문은 `DESIGN.md` §10)

1. **심은 20ms/50Hz 그리드.** 표시는 보간해도 심은 안 건드린다.
2. **심 규칙을 고치면 C++와 파이썬 둘 다 고친다.** 한쪽만 고치면
   `equivalence`/`trace`가 떨어진다. (그게 안전망이다 — 우회하지 말 것)
3. **새 `SimConfig` 필드는 기본값이 무해**해야 한다. 그래야
   `equivalence`/`trace`가 한 칸도 안 움직인다.
4. **저장 id는 영원히 동결**: 스테이지·카드·모루·노드 id. 이름과 효과는
   바꿔도 id는 못 바꾼다.
5. **저장 형식은 관용 key=value** — 모르는 줄은 보존하고 되쓴다.
6. **새 심 기능 = 새 ctest.** 결정론적 검사 없이 기믹만 들어오지 않는다.
7. **새 화면은 폰 세로(1080x2280) 스크린샷 검증**을 거친다.
8. **색은 두 곳에만**: `cpp/gui/palette.hpp` + `tools/make_gfx.py` 머리
   상수. 그리기 코드에 색 리터럴을 새로 박지 않는다.
9. **GUI 전용 레버는 채점 엔진이 안 부른다** (`impose_gravity`,
   `drain_flow`, `stoke_flow`, `shed_garbage`, `shear_floor`).

---

## 4. 코드 지도

```
cpp/
  include/forcetris/*.hpp   공개 헤더
  src/
    sim.cpp      (1455줄)  심 본체 — 중력·락·클리어·공격·Flow
    board.cpp              보드, sealed 열, 캐스케이드
    attack.cpp    (51줄)   공격 표. engine/attack.py와 항상 같아야 한다
    spins.cpp     (59줄)   스핀 판정 (3코너 / immobile)
    temper.cpp   (601줄)   카드 40장 + 저주 6장 + apply()
    campaign.cpp (927줄)   맵/경제/모루/도구/엔드리스 다이얼
    stages.cpp   (574줄)   레시피 데이터(콘텐츠). 메커니즘과 분리됨
    bot.cpp      (979줄)   빔서치 봇
  gui/
    main.cpp   (10933줄)  ★ 화면 전부. 커서 대부분이 여기에 있다
    versus.cpp            듀얼/스킬 로스터
    session.cpp, audio.cpp, palette.hpp, theme.hpp
  tests/*.cpp             ctest 30개의 소스
engine/                   파이썬 레퍼런스 (game.py가 본체)
tools/                    test_*.py 12개 + make_gfx.py + make_sounds.py
DESIGN.md                 설계·로드맵·불변 제약 (한국어)
cpp/README.md  (1676줄)   엔진/GUI 상세 설명 (영어)
```

### `cpp/gui/main.cpp` 안에서 자주 찾는 것

| 심볼 | 하는 일 |
|---|---|
| `screen_head()` | 모든 전체화면 상단 밴드(이름 + 엠버 헤어라인 + Back) |
| `section()` / `field_slider()` / `open_field()` / `tab_strip()` / `big_stat()` | 화면 어휘 4종. **새 UI는 이걸로 짠다** |
| `forge_panel()` / `forge_plate()` | 판때기 크롬 |
| `card_button()` / `tile_button()` | 카드 / 타일 |
| `open_column()` / `close_column()` | 전체화면 안의 읽기 좋은 컬럼 |
| `draw_setup()` | 난이도·챕터 선택(Set Out) |
| `draw_career()` | 맵 |
| `draw_curtain()` | 화면 전환 페이드 |
| `kick()` / `hit_stop()` / `pop_number()` / `draw_chain_rim()` | 타격감 |
| `launch_streak()` / `blow_ink()` | 공격 일제사격 연출 |
| `draw_char_cell()` | 쓰레기 = 다 탄 석탄 렌더 |

---

## 5. 지금까지 (V4.0 기준, 커밋 `bf75026`)

최근 아크만. 전체는 `DESIGN.md` §9 로드맵 표.

- **V3.7 타격감** — 방향 있는 킥, 히트스톱, 우물 밖 숫자 팝업, 일제사격
  공격 연출, 기계적 SFX(`snap`/`servo`/`detent`), 콤보마다 반음 상승
- **V3.8 화면 구성** — ImGui 기본 타이틀바 8개 제거 → `screen_head` 밴드
  하나. 화면 어휘 4종 신설. Settings 전면 재구성
- **V3.8a 쓰레기 = 석탄** — 주조 블록 대신 덩어리. 옆면 좌석 없애서 한 줄이
  타일 열 개가 아니라 껍질 한 겹으로 도착
- **V3.9 타이틀 화면** — 330px 상자 → 좌: 마크 + 저장 상태 판 / 우: 두 관문
  + 다섯 타일. 폰 세로에선 세로 적층
- **V3.9a 전환** — 와이프(선만 보였음) → 따뜻한 어둠의 페이드 업
- **V3.9b T스핀 루프홀** — 낙하가 스핀 플래그를 안 지웠다. 허공에서 돌리고
  내리꽂으면 3코너 홈에서 풀 T스핀이 됐다. C++/파이썬 동시 수정
- **V4.0 글 감축** — 문(Set Out)을 별도 화면으로. 맵 헤더 5줄→2줄. 스킬
  경고 30개 문구 → id에서 뽑는 짧은 라벨 10종
- **V4.0a 다운스택 딜** — 정액 보너스가 표값 0인 싱글에 +4를 얹어 콤보5
  싱글이 10딜. **클리어 자체의 값 + 1**로 상한
- **V4.0b 인디케이터** — 콤보/B2B 0.9→1.9초, 스핀 배너 1.4→2.5초

---

## 6. 남은 일

### A. 확실히 남은 것

1. **Profile 화면 구성** — 아직 손 안 댐. 드롭다운 + ImGui 탭 + 같은 크기
   텍스트 나열. **`big_stat()`이 만들어져 있는데 아직 아무도 안 쓴다.**
   "Estimated TR 1717 (C-)"가 그 화면에서 제일 중요한 수치인데 나머지와
   같은 크기다. → `tab_strip` + `big_stat` + `section`으로 재구성.
   (`draw_profile()`)
2. **Analysis 화면** — 같은 이유로 아직 ImGui 탭 + 2열 표.
3. **How to Play** — 문단이 길다. V4.0에서 다른 곳은 줄였는데 여긴 안 줄임.
4. **V3.1 출시 마감** (`DESIGN.md` 로드맵 마지막 줄): 첫 실행 온보딩,
   저장 안정성 감사, (선택) 한국어 로컬라이즈, itch/APK 패키징.

### B. 유저가 말했지만 아직 안 끝난 것

- **"포장이 별로"** — 화면 구성은 V3.8~V4.0에서 크게 잡았고, 유저가 직접
  꼽은 남은 항목은 **아트 퀄리티**와 **텍스트/톤**.
- **글 줄이기**는 계속 진행형이다. 새 UI를 넣을 때 문장 대신 숫자·칩·라벨을
  쓴다. (V4.0의 스킬 라벨이 본보기: 문장 → 두 단어)

### C. 유저가 답 안 한 튜닝 질문 (물어보기 전엔 건드리지 말 것)

- 히트스톱 프레임 수 (현재 쿼드 4, 캐스케이드 5, 점화 6, 퍼펙트 9)
- 일제사격 슬러그의 굵기·속도
- 연쇄 림(chain rim) 밝기
- 콤보 고리당 반음이 맞는 폭인지
- move/rotate/lock의 기계식 클릭이 과한지

### D. 리스크 / 알려진 지저분한 곳

- **`cpp/gui/main.cpp`가 10,933줄.** 분할하고 싶어질 텐데, 하려면 별도
  아크로 하고 그 커밋에서 다른 걸 같이 하지 말 것.
- **파이썬 미러를 잊기 쉽다.** 심 동작을 바꿀 때마다 `engine/game.py`를
  같이 봐야 한다. 잊으면 `equivalence`가 잡아주지만, 잡힌 뒤에 고치는 게
  훨씬 비싸다.
- **`equivalence`/`trace`를 "고치려고" 골든을 다시 굽지 말 것.** 그 둘이
  움직였다는 건 두 엔진이 갈라졌다는 뜻이다.

---

## 7. 작업 절차 (이 저장소의 관례)

1. 브랜치: `claude/tetrio-forced-harddrop-timer-9v9js7`
   (`git push -u origin <branch>`)
2. 시각 변경이면 **투어 스크린샷을 찍어서 눈으로 본다.** 코드만 보고
   "됐겠지"로 넘어가지 않는다.
3. 심/밸런스 변경이면 **재현 프로브를 먼저 짜서 숫자를 잰다.** V4.0a의
   다운스택 수정이 그 예 — 고치기 전에 "콤보5 싱글 = 6, 오버드라이브
   1.6배 = 10"을 먼저 측정했다.
4. 새 심 동작이면 ctest에 핀을 박는다. **판정이 아니라 원인을 핀한다** —
   V3.9b는 스핀 판정이 아니라 `rotated` 플래그 자체를 검사한다.
5. 전체 매트릭스(ctest 30 + 파이썬 12) 통과 확인.
6. `DESIGN.md` 로드맵 표에 한 줄, `cpp/README.md`에 문단 하나.
7. 커밋 → push → main ff-merge → exe/APK 재빌드 후 전달.

### 커밋 규칙

- 커밋 메시지 끝에 항상:
  ```
  Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
  Claude-Session: <세션 URL>
  ```
- **저장소에 들어가는 어떤 산출물에도 모델 식별자를 쓰지 않는다**
  (커밋 메시지, PR 본문, 코드 주석 전부).
- **명시 요청 없이는 PR을 만들지 않는다.**

### 코드 스타일

- 탭 인덴트, 80칸 근처에서 줄바꿈.
- 주석은 **무엇을 하는지가 아니라 왜 이렇게 됐는지**를 쓴다. 이 저장소의
  주석은 대부분 "예전엔 이랬고, 그게 이래서 틀렸고, 그래서 지금 이렇다"
  형식이다. 새 코드도 그 톤을 맞춘다.
