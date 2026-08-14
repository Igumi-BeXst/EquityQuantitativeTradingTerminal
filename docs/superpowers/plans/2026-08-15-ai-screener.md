# AI 閫夎偂宸ヤ綔娴?Implementation Plan锛圓I 閲忓寲宸ヤ綔娴?路 绗?3 杞級

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 閫夎偂闈㈡澘澧炲姞銆孉I 鍥犲瓙銆嶉厤缃紙褰㈡€?鎯呯华鍕鹃€?+ 鏉冮噸锛夆啋 澶嶇敤绗?1 杞?`composeSignal` 鍒嗛」閫昏緫瀵规睜鍐呰偂绁ㄧ畻 AI 缁煎悎鍒嗭紙0~100锛夆啋 缁撴灉琛ㄦ柊澧炪€孉I 鍒嗐€嶅垪骞舵寜 AI 鍒嗘帓搴忥紱鎯呯华鏁版嵁闄愰噺鎷夊彇锛堟睜鍓?30 鍙紝鍏朵綑闄嶇骇缂哄け锛夈€?
**Architecture:** 寮曟搸 `intelligence/screener/ai_screener.{h,cpp}`锛堢函 C++17锛屽鐢?`signal::composeSignal`鈥斺€旇璁℃枃妗ｆ槑纭€岀 3 杞鐢ㄥ叾鍒嗛」閫昏緫銆嶏級锛沀I 鍦?IO 闃舵鎷夋儏缁紙EastMoneyNewsProvider 闄愰噺 30 鍙級鈫?Worker 璺?`runAiScreener` 鈫?缁撴灉琛ㄦ寜 AI 鍒嗘帓搴忋€?
**Tech Stack:** C++17, Qt 6.11 (Widgets), 鏃犳柊渚濊禆銆?
璁捐鏂囨。锛歔2026-08-13-ai-quant-workflow-design.md](../specs/2026-08-13-ai-quant-workflow-design.md)

## Global Constraints

- 鍒嗗眰锛歚ai_screener` 鏀?`intelligence/screener/`锛堜緷璧?pattern/sentiment/signal/foundation锛屽叏閮ㄥ悜涓嬪悎娉曪級锛涙祴璇曟斁 `tests/test_intelligence/test_ai_screener.cpp`
- 缂栬瘧闆惰鍛婏細`cmake --build --preset with-qt` 闆堕敊璇浂璀﹀憡
- 鍥炲綊锛歝test 鐜版湁 **420 tests** 鍏ㄧ豢锛涙柊澧?8 渚?鈫?棰勮 **428**
- 蹇€?TDD锛歚ctest --preset default -R AiScreenerTest --output-on-failure`
- 瀹夊叏寮傛妯″紡锛欼O/Worker lambda 鎸夊€兼崟鑾?+ QPointer 瀹堝崼锛涙儏缁?fetch 涓茶浜?IO 闃舵锛岄檺閲?`min(30, 姹犲ぇ灏?` 鍙紙璁捐鏂囨。銆岄檺鑲＄エ鏁?+ 闄嶇骇銆嶏級
- **鍙栬垗锛堢浉瀵硅璁℃枃妗ｏ級**锛歷1 涓嶆妸鐜版湁 11 鍥犲瓙铻嶅悎杩?AI 鍒嗭紙StockScreener 娴佺▼淇濇寔鐙珛锛屾€诲垎鍒椾繚鐣欙級锛汚I 鍒?= 褰㈡€?鎯呯华+鎶€鏈紙composeSignal 鍙ｅ緞锛屼笌绗?1 杞竴鑷达級锛涖€屽彲鎸?AI 鍒嗘帓搴忋€? 缁撴灉琛ㄦ寜 compositeScore 闄嶅簭杈撳嚭

---

### Task 1: Engine 鈥?ai_screener 绾嚱鏁?+ 鍗曟祴

**Files:**
- Create: `src/intelligence/screener/ai_screener.h`
- Create: `src/intelligence/screener/ai_screener.cpp`
- Test: `tests/test_intelligence/test_ai_screener.cpp`
- Modify: `src/CMakeLists.txt`锛坰t_intelligence 鍔?`intelligence/screener/ai_screener.cpp`锛?- Modify: `tests/CMakeLists.txt`锛坱est_intelligence 鍔?`test_intelligence/test_ai_screener.cpp`锛?
**Interfaces:**
- Produces:
  - `struct AiScreenerConfig { std::vector<double> weights = {0.4,0.3,0.3}; bool useSentiment = true; }`锛坵eights 瀵归綈 composeSignal锛氬舰鎬?鎯呯华/鎶€鏈級
  - `struct AiScore { StockCode code; double compositeScore = 0.0; std::optional<double> patternScore, sentimentScore, technicalScore; std::string summary; }`
  - `std::vector<AiScore> runAiScreener(const std::vector<StockCode>& pool, const std::vector<std::vector<Bar>>& barsByCode, const std::vector<std::optional<sentiment::SentimentScore>>& sentiments, const AiScreenerConfig& cfg)`
- Consumes: `signal::composeSignal`锛坕ntelligence/signal/composite_signal.h锛夈€乣pattern::PatternRecognizer`銆乣indicators::rsi/macd`銆乣sentiment::SentimentScore`銆?
**瑙勫垯锛?*
- pool/barsByCode/sentiments 涓夎€呯瓑闀垮榻愶紙闀垮害涓嶄竴鑷?鈫?鍙栨渶鐭墠缂€锛岄槻寰★級
- bars 涓虹┖ 鈫?璇ヨ偂璺宠繃锛堜笉杩涜緭鍑猴級
- sentiment 缂哄け锛坣ullopt 鎴?useSentiment=false锛夆啋 SentimentScore{} 浼犲叆 composeSignal锛坰ummary 绌?鈫?鍒嗛」缂哄け鎶樺噺锛?- compositeScore = (cs.score+1)/2脳100锛涘垎椤?score 鍚岀悊鏄犲皠锛岀己鍒嗛」 鈫?nullopt锛泂ummary = cs.summary
- 杈撳嚭鎸?compositeScore 闄嶅簭锛坰table_sort锛?
- [x] **Step 1: 鍐欏け璐ユ祴璇?`tests/test_intelligence/test_ai_screener.cpp`**

```cpp
#include "intelligence/screener/ai_screener.h"
#include "foundation/utils/datetime.h"
#include "intelligence/pattern/pattern_recognizer.h"
#include <gtest/gtest.h>

namespace st {
namespace {
using st::screener::runAiScreener;
using st::screener::AiScreenerConfig;
using st::screener::AiScore;

// 鍚堟垚鍗囧簭鏃ョ嚎锛歯 鏍癸紝鍙寚瀹氭渶鍚庡嚑鏍圭殑褰㈡€侊紙鍚炴病/娴佹槦绛夌敱 PatternRecognizer 鍒ゅ畾锛?std::vector<Bar> makeBars(int n, double start, double step, int upTrend) {
    std::vector<Bar> bars;
    bars.reserve(n);
    for (int i = 0; i < n; ++i) {
        Bar b;
        b.code = StockCode(std::string_view("SH600000"));
        b.time = utils::parseDate("2026-01-01") + i * 86400;
        b.period = BarPeriod::Daily;
        const double c = start + step * i;
        b.open = c - step * 0.4;
        b.high = c + step * 0.6;
        b.low = c - step * 0.8;
        b.close = c;
        b.volume = 100000 + i * 100;
        bars.push_back(b);
    }
    return bars;
}
...
```

- [x] **Step 2: CMake + 澶辫触纭**锛堝悓鍓嶈疆妯″紡锛?- [x] **Step 3: 鍐欏疄鐜?*锛坄runAiScreener` 寰幆鍐咃細closes鈫抮si/macd 鈫?detectAt(3) 鈫?composeSignal 鈫?鏄犲皠鍒嗛」 鈫?鎺掑簭锛?- [x] **Step 4: 鏋勫缓 + 8 渚嬪叏 PASS**
- [x] **Step 5: Commit**

---

### Task 2: UI 鈥?ScreenerPanel AI 鍥犲瓙閰嶇疆 + 缁撴灉琛?AI 鍒嗗垪

**Files:**
- Modify: `src/ui/panels/screener_panel.h`锛圓I 鍕鹃€夋垚鍛?+ sentiments 浼犻€掞級
- Modify: `src/ui/panels/screener_panel.cpp`锛圓I 閰嶇疆鍖?+ IO 闃舵闄愰噺鎷夋儏缁?+ worker 璺?runAiScreener + onResult 鎸?AI 鍒嗘帓搴忥級
- Modify: `src/ui/models/screen_result_model.h/.cpp`锛堝彲閫?AI 鍒嗗垪锛歚aiScores_` 闈炵┖鏃跺姞鍒椼€孉I 鍒嗐€嶏級
- Modify: `src/CMakeLists.txt`锛堟棤鏂板鏂囦欢锛?
**Interfaces:**
- Consumes: Task 1 `runAiScreener`锛沗EastMoneyNewsProvider`锛堥潰鏉挎瀯閫犳敞鍏?shared_ptr锛屼豢 AiSignalPanel锛?- Produces: 缁撴灉琛?AI 鍒嗗垪锛汚I 鍕鹃€?鈫?缁撴灉鎸?AI 鍒嗛檷搴?
- [x] **Step 1: ScreenResultModel 鍔?AI 鍒嗗垪**锛坰etResults 杩藉姞 `const std::vector<double>& aiScores` 鍙傛暟锛岄粯璁ょ┖锛沰FixedColumns 4鈫?锛岀 4 鍒?header "AI 鍒?锛宎iScores_ 绌烘椂涓嶆樉绀鸿鍒椻€斺€斿嵆 columnCount = 4 + factorNames + (aiScores 绌?0:1)锛宒ata/header 鐩稿簲鍋忕Щ锛?- [x] **Step 2: ScreenerPanel 閰嶇疆鍖?*锛堝洜瀛愬尯涓嬪姞銆孉I 鍥犲瓙銆岹roupBox锛氬舰鎬?鎯呯华涓?QCheckBox 榛樿鍕鹃€?+ 鎻愮ず QLabel銆屾儏缁垎椤逛粎鎷夊彇姹犱腑鍓?30 鍙€嶏紱`aiEnabled()` = 涓ゅ嬀閫変换涓€锛?- [x] **Step 3: IO 闃舵鎷夋儏缁?*锛坥nRunClicked 鐨?IO lambda锛歜ars 鎷夊畬鍚庯紝鑻?AI 鎯呯华鍕鹃€?鈫?瀵?pool 鍓?`min(30,size)` 鍙?`newsProvider->fetchNews(code, 10)` 鈫?SentimentAnalyzer::averageScore锛泂entiments 涓?pool 瀵归綈锛屽叾浣?nullopt锛涘洖涓荤嚎绋嬩紶閫掔粰 onAllDataFetched锛?- [x] **Step 4: Worker 璺?runAiScreener**锛坥nAllDataFetched锛氱幇鏈?StockScreener 娴佺▼淇濈暀锛汚I 鍚敤鏃堕澶?`runAiScreener(pool, barsByCode, sentiments, cfg)`鈥斺€攂ars 浠?cache 鍙栧洖锛圖ataCache::getBars锛夆啋 aiScores 鏁扮粍锛堟寜 results 椤哄簭瀵归綈锛岀己鍒?0锛夆啋 onResult 閲屾寜 compositeScore 闄嶅簭閲嶆帓 results+aiScores锛?- [x] **Step 5: CMake 鏃犳柊澧?+ 鏋勫缓闆惰鍛?+ ctest 428 鍏ㄧ豢 + GUI 鍐掔儫**

- [x] **Step 6: Commit**

---

### Task 3: 鏂囨。鏀跺熬

- [x] **Step 1: DEVLOG/changelog/CLAUDE.md**锛堢 3 杞潯鐩細Intelligence 65 鈫?73锛屾€昏 420 鈫?428锛汣LAUDE.md 褰撳墠闃舵琛岃拷鍔?+ 娴嬭瘯鏁版洿鏂帮級
- [x] **Step 2: 璁″垝鏂囨。鍕鹃€?+ Commit**

