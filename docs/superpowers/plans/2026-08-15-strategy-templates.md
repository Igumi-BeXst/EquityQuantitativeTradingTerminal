# 绛栫暐妯℃澘澧炲己 + 鍚戝 Implementation Plan锛圓I 閲忓寲宸ヤ綔娴?路 绗?4 杞級

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 绛栫暐妯℃澘搴撴墿鍏?4 涓柊绛栫暐锛堝姩閲?鍧囧€煎洖褰?鏀剁洏绐佺牬/RSI锛? StrategyPanel 妯℃澘鎸夌被鍒垎缁?+ 鍙傛暟璇存槑锛坱ooltip 鍚戝锛夈€?
**Architecture:** 寮曟搸 `engine/strategy/templates/` 鏂板绛栫暐绫伙紙绾?C++锛屽彲鍗曟祴锛? `GridSearchOptimizer::makeStrategy` 娉ㄥ唽鍙傛暟鏄犲皠锛沀I `StrategyPanel` Spec 鍔?category/p1Desc/p2Desc锛屽垪琛ㄥ垎缁勬樉绀恒€乻pinbox tooltip 璇存槑銆乤pply 鍙傛暟鏄犲皠鎵╁睍銆?
**Tech Stack:** C++17, Qt 6.11 (Widgets), 鏃犳柊渚濊禆銆?
璁捐鏂囨。锛歔2026-08-13-ai-quant-workflow-design.md](../specs/2026-08-13-ai-quant-workflow-design.md)

## Global Constraints

- 鍒嗗眰锛氱瓥鐣ュ湪 `engine/strategy/templates/`锛堜緷璧?foundation 鎸囨爣搴擄級锛涙祴璇?`tests/test_engine/test_strategy_templates.cpp`
- 缂栬瘧闆惰鍛婏紱鍥炲綊 440 tests 鍏ㄧ豢锛涙柊澧?~14 渚?鈫?棰勮 **454**
- 蹇€?TDD锛歚ctest --preset default -R StrategyTemplateTest --output-on-failure`
- 姣忎釜绛栫暐鏆撮湶 **2 涓富瑕?int 鍙傛暟**锛堝榻愮幇鏈夊悜瀵?GridSearch 2 鍙傛暟妗嗘灦锛涘叾浣欏弬鏁板浐瀹氶粯璁わ級
- 绛栫暐鍖哄垎搴︼細Breakout = **鏀剁洏浠?*绐佺牬锛圱urtle 鐢ㄧ洏涓珮浣庝环锛夛紱Momentum = N 鏃ユ敹鐩婄巼闃堝€?+ 鍧囩嚎绂诲満锛汳eanReversion = 瓒呰穼涔板洖鍧囩嚎鍗栵紱Rsi = 瓒呭崠涔拌秴涔板崠
- 鍏变韩杈呭姪锛歚engine/strategy/templates/strategy_helpers.h`锛堝尶鍚嶅懡鍚嶇┖闂?`smaAt`/`hasPosition`锛屾柊绛栫暐鍏辩敤锛涗笉鍔?IStrategy/Turtle锛?- UI锛歷1 涓嶅仛 Advisor 寤鸿鍊硷紙闈欐€佸悜瀵兼棤鍥炴祴涓婁笅鏂囷級锛涖€屽簲鐢ㄥ洖娴嬨€嶆部鐢?applyStrategy 淇″彿

---

### Task 1: 寮曟搸 鈥?4 涓柊绛栫暐 + 娉ㄥ唽 + 鍗曟祴

**Files:**
- Create: `src/engine/strategy/templates/strategy_helpers.h`
- Create: `src/engine/strategy/templates/momentum_strategy.{h,cpp}`锛圡omentum锛歭ookbackPeriod=20 鍔ㄩ噺鍥炵湅銆乪xitPeriod=10 鍧囩嚎绂诲満锛泃hresholdPct=50 鍥哄畾 5%锛?- Create: `src/engine/strategy/templates/breakout_strategy.{h,cpp}`锛圔reakout锛歟ntryPeriod=20銆乪xitPeriod=10锛涙敹鐩樹环绐佺牬 N 鏃ユ渶楂樻敹鐩樹拱 / 璺岀牬 M 鏃ユ渶浣庢敹鐩樺崠锛?- Create: `src/engine/strategy/templates/mean_reversion_strategy.{h,cpp}`锛圡eanReversion锛歮aPeriod=20銆乨eviationPct=30锛涗綆浜庡潎绾?3% 涔?/ 鍥炲埌鍧囩嚎涓婃柟鍗栵級
- Create: `src/engine/strategy/templates/rsi_strategy.{h,cpp}`锛圧si锛歜uyLevel=30銆乻ellLevel=70锛沺eriod=14 鍥哄畾锛宖oundation indicators::rsi锛?- Modify: `src/engine/optimizer/grid_search.cpp`锛坢akeStrategy 娉ㄥ唽 4 涓?id + 鍙傛暟鏄犲皠锛?- Modify: `src/CMakeLists.txt`锛坰t_engine +4 涓?.cpp锛?- Test: `tests/test_engine/test_strategy_templates.cpp`锛垀14 渚嬶細姣忕瓥鐣?3~4 渚嬧€斺€斾拱鍏ヨЕ鍙?鍗栧嚭瑙﹀彂/鏁版嵁涓嶈冻涓嶅姩/鍙傛暟杈圭晫锛?
**娴嬭瘯鏁版嵁妯″紡**锛氬悎鎴愬崌搴忔棩绾匡紙澶嶇敤 ai_screener 娴嬭瘯鐨?makeBars 鎬濊矾锛屾湰鍦板鍒讹級锛?- 姣忕瓥鐣ョ敤 BacktestEngine 璺戝悎鎴愭暟鎹獙璇佷氦鏄擄紵鎴栫洿鎺ユ瀯閫?StrategyContext 鎵嬪姩椹卞姩 onBar锛熸墜鍔ㄩ┍鍔ㄧ畝鍗曪細鏋勯€?BarSeries + Portfolio + ctx锛岃皟 onBar锛屾柇瑷€ portfolio 鍙樺寲銆侭acktestEngine 闆嗘垚鏇寸湡瀹炰絾澶嶆潅銆?*鐢ㄦ墜鍔?ctx 椹卞姩**锛圱urtle/MACross 鏃犲厛渚嬶紝浣嗙洿鎺ャ€佸彲鎺э級銆?- 鏂█鏂瑰紡锛氱瓥鐣ユ棤鍏紑鎸佷粨鏌ヨ鈥斺€旈€氳繃 portfolio() 淇濇姢鏂规硶锛無nBar 鍚庢鏌?ctx.portfolio->positions/available 鍙樺寲銆?
- [x] **Step 1: 鍐欏け璐ユ祴璇?*锛堟瘡绛栫暐鐢ㄤ緥锛氣憼瓒呰穼/绐佺牬瑙﹀彂涔板叆锛坅vailable 鍑忓皯銆乸ositions 闈炵┖锛夆憽绂诲満鏉′欢瑙﹀彂鍗栧嚭 鈶㈡暟鎹笉瓒筹紙history 鐭級涓嶅姩 鈶ｅ弬鏁拌竟鐣岋紙threshold=0 绔嬪嵆瑙﹀彂绛夛級锛?- [x] **Step 2: CMake + 澶辫触纭**
- [x] **Step 3: 瀹炵幇 4 绛栫暐 + helpers + makeStrategy 娉ㄥ唽**
- [x] **Step 4: 鏋勫缓 + 14 渚嬪叏 PASS**
- [x] **Step 5: Commit**

---

### Task 2: UI 鈥?StrategyPanel 鍒嗙粍 + 鍙傛暟璇存槑

**Files:**
- Modify: `src/ui/panels/strategy_panel.h`锛圫pec 鍔?category/p1Desc/p2Desc锛?- Modify: `src/ui/panels/strategy_panel.cpp`锛堝垪琛ㄩ」銆孾绫诲埆] 鍚嶇О銆嶅垎缁勬帓搴忋€乻pinbox tooltip銆乤pply 鍙傛暟鏄犲皠鎵╁睍 6 涓瓥鐣?id锛?- 鏃犳柊澧炴枃浠?
**Interfaces:**
- Consumes: 6 涓瓥鐣?id锛圡ACross/Turtle/Momentum/Breakout/MeanReversion/Rsi锛?- Produces: 鍒嗙粍妯℃澘鍒楄〃 + 鍙傛暟 tooltip 鍚戝锛沘pplyStrategy(id, params) 涓嶅彉

- [x] **Step 1: Spec 鎵╁睍 + builtinTemplates 6 鏉＄洰**锛堝惈鏂?4 绛栫暐涓枃鍚?鎻忚堪/鍙傛暟璇存槑/鑼冨洿榛樿鍊硷級
- [x] **Step 2: 鍒楄〃鍒嗙粍鏄剧ず**锛堛€孾瓒嬪娍] 鍙屽潎绾跨瓥鐣ャ€嶅墠缂€锛屾寜绫诲埆鎺掑簭锛氳秼鍔库啋鍔ㄩ噺鈫掔獊鐮粹啋鍧囧€煎洖褰掆啋鍙嶈浆锛?- [x] **Step 3: 鍙傛暟璇存槑 tooltip**锛坧1_/p2_ setToolTip(p1Desc/p2Desc) + 琛ㄥ崟琛?tooltip锛?- [x] **Step 4: onApplyClicked 鍙傛暟鏄犲皠鎵╁睍**锛坰witch 6 涓?id锛?- [x] **Step 5: 鏋勫缓闆惰鍛?+ ctest 454 鍏ㄧ豢 + GUI 鍐掔儫**

- [x] **Step 6: Commit**

---

### Task 3: 鏂囨。鏀跺熬

- [x] **Step 1: DEVLOG/changelog/CLAUDE.md**锛堢 4 杞潯鐩細Engine 174 鈫?188锛屾€昏 440 鈫?454锛汣LAUDE.md 褰撳墠闃舵琛岃拷鍔?+ 娴嬭瘯鏁版洿鏂帮級
- [x] **Step 2: 璁″垝鏂囨。鍕鹃€?+ Commit**

