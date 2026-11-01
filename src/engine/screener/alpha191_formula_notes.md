# Alpha191 公式表整理记录（alpha191_formulas.inc）

本文件记录 `src/engine/screener/alpha191_formulas.inc` 的整理口径：数据来源、规范化规则、
逐条笔误修正、存疑清单与自检结果。**本次只写这两个文件，未改动其它源码/CMake。**

## 1. 来源与产物

| 项 | 内容 |
| --- | --- |
| 主来源 | BigQuant 知识库《Alpha191因子构建公式》 <https://bigquant.com/wiki/doc/Pyf0TYya6H>（2026-11-01 用 `web_fetch` 抓取，页面 `<pre><code>` 块含 Alpha1–Alpha191 全文） |
| 交叉核对 | 国泰君安研报《基于短周期价量特征的多因子选股体系——数量化专题之九十三》PDF（镜像：<https://guorn.com/static/upload/file/3/134065454575605.pdf>）。解压 PDF content stream、按表格结构元素 `/ActualText` 逐条抽取原文，与 BigQuant 文本按「去除全部空白后相等」比对 |
| 核对结论 | 可抽取的 186 条与 BigQuant 文本完全一致；其余 12 条差异全部是 PDF 提取噪声（行尾 `;`、字体字形把 `?`/`（`/en dash 映射成控制字符、下一页行首串入），**未发现 BigQuant 转录性错误**。研报本身自带的笔误（见 §3）在 BigQuant 页面原样保留，故需修正的是「研报原文笔误」而非「转录错误」 |
| 产物 | `src/engine/screener/alpha191_formulas.inc`：191 条，`id = name = alpha001..alpha191`，第三字段为 `R"(...)"` 原始字符串公式 |
| 说明 | 研报 PDF 仅用于核对，其提取脚本与中间文件都在仓库外临时目录，不进入仓库 |

## 2. 规范化规则（不改变语义）

1. **全角/中文标点 → ASCII**：`（）`→`()`、`，`→`,`、`：`→`:`、`；`→`;`、`– —`→`-`、
   `＊`→`*`、`／`→`/`、`＋`→`+`、`＝`→`=`、`＞ ＜`→`> <`、`＾`→`^`、`！`→`!`、`％`→`%`、`、`→`,`。
2. **空白**：多条空格合并为 1 个；`(` 后、`)`/`,` 前不留空格；统一为「二元运算符前后单空格、
   逗号后单空格、`?` `:` 两侧单空格」的可读形式。空白重排前后逐条重新分词，token 序列必须完全一致
   （191/191 通过），因此空白规范化不引入任何语义变化。
3. **研报排版产物**（散见于 alpha011/alpha060 等）：`)./(`、`).*(` 这类把除号/乘号写成 `.` 的
   写法统一为 `/`、`*`（即 `./`→`/`、`.*`→`*`）。
4. **保留** `?:`、`&&`、`||`、`&`、`^`、`<`、`>`、`<=`、`>=`、`==`、`!=` 等运算符原样。
5. **算子/字段名沿用研报附录拼写**（未擅自改名）：`REGRESI`、`COVIANCE`、`BANCHMARKINDEXCLOSE`、
   `BANCHMARKINDEXOPEN`、`SELF`、`DTM`、`DBM`、`TR`、`HD`、`LD`、`VOLUME`、`AMOUNT`、`MKT/SMB/HML`。
   其中研报附录明确给出：`COVIANCE` 即协方差、`REGRESI` 即多因子回归残差、`SELF` 为 t-1 日自身因子值
   （仅 alpha143 使用）。下游表达式引擎需按这些名字实现/起别名，不应视为拼写错误。

## 3. 逐条修正记录

> 说明：alpha011/alpha060 的 `./`、`.*` 与 alpha188 的 en dash、alpha030 的全角逗号由「通用规范化
> 规则」统一处理（脚本中不计入逐条 FIXES）；其余 15 处为逐条定点替换（脚本 FIXES 共 15 条）。
> 下表共 18 条记录，其中 17 条是有改动的公式，最后一条 alpha146 为「核对后确认不需改」的说明。

| 编号 | 原文问题 | 采用版本（关键差异） | 是否存疑 |
| --- | --- | --- | --- |
| alpha011 | 用 `)./(`、`).*(` 表示除/乘（研报排版产物） | `SUM(((CLOSE - LOW) - (HIGH - CLOSE)) / (HIGH - LOW) * VOLUME, 6)` | 否 |
| alpha022 | `SMEAN(...)`；研报附录算子表无 `SMEAN` | `SMEAN` → `SMA` | 否 |
| alpha023 | 第一组三元缺冒号：`... ? STD(CLOSE:20), 0)` | `... ? STD(CLOSE, 20) : 0)`（与同式后两组写法一致；原文 PDF 亦如此） | 否 |
| alpha028 | 第二组 KDJ 分母 `MAX(HIGH,9)-TSMAX(LOW,9)` | `TSMAX(HIGH,9)-TSMIN(LOW,9)`（标准 KDJ 口径，与同式第一组一致） | 否 |
| alpha030 | `HML，60` 全角逗号；`DELAY(CLOSE)` 缺窗口参数 | `HML, 60`；`DELAY(CLOSE, 1)` | 否（缺窗口按 1 日补全） |
| alpha052 | 第二项 `... - L), 26)`，`L` 未定义 | `... - LOW), 26)` | 否 |
| alpha060 | 同 alpha011（窗口 20） | `SUM(((CLOSE - LOW) - (HIGH - CLOSE)) / (HIGH - LOW) * VOLUME, 20)` | 否 |
| alpha078 | `MA((HIGH+LOW+CLOSE)/3,12)`；附录算子表只有 `MEAN`，同式其余位置也用 `MEAN` | `MA` → `MEAN` | 低 |
| alpha111 | `VOL`；附录字段名为 `VOLUME` | `VOL` → `VOLUME`（2 处） | 低 |
| alpha116 | `REGBETA(CLOSE,SEQUENCE,20)`，`SEQUENCE` 缺参数 | `REGBETA(CLOSE, SEQUENCE(20), 20)`（对照 alpha021/alpha147 同类写法） | 低 |
| alpha131 | `DELAT(VWAP, 1)` | `DELTA(VWAP, 1)` | 否 |
| alpha159 | `HGIH`（3 处） | `HIGH` | 否 |
| alpha166 | 见 §4（缺乘号 + SUM 内参数错位，原文数学上不成立） | 按样本偏度 G1 还原（见 §4） | **是** |
| alpha180 | 三元真值分支括号错位：`? (` 打开的括号未在 `:` 前闭合，行尾却多一个 `)`（总配平但结构非法；研报 PDF 原文亦如此） | `? ((-1 * TSRANK(ABS(DELTA(CLOSE, 7)), 60)) * SIGN(DELTA(CLOSE, 7))) : (-1 * VOLUME)`，把 `)` 移到 `:` 前 | 低（由真实解析器验证发现） |
| alpha182 | 逻辑或写作 `OR`：`...)OR(...` | `||`（两侧 `&` 保留） | 否 |
| alpha188 | `HIGH-LOW–SMA(...)` 中 `–` 为 en dash（非减号字符） | `-` | 否 |
| alpha190 | 第一处 `SUMIF` 平方底数括号错位（`...-1-(B)^(1/20)-1`），`DELAY(CLOSE)` 缺窗口（6 处） | 与同式后一处 `SUMIF` 对齐为 `...-1-((B)^(1/20)-1)`；`DELAY(CLOSE, 1)` | 低 |
| alpha146（未改） | BigQuant 文本疑排版坏 | 研报 PDF `ActualText` 核对：括号配平、语义完整（仅行尾多一个 `;`），**保持原文**，去空白后与 PDF 完全一致 | 否 |

## 4. alpha166 存疑说明（唯一「存疑=是」）

**原文**（BigQuant 与研报 PDF 完全一致，典型排版坏例）：

```
-20*（20-1 ）^1.5*SUM(CLOSE/DELAY(CLOSE,1)-1-MEAN(CLOSE/DELAY(CLOSE,1)-1,20),20)/((20-1)*(20-2)(SUM((CLOSE/DELAY(CLOSE,1),20)^2,20))^1.5)
```

**问题**：
1. `(20-2)(SUM(...` 缺乘号（靠隐式乘法的写法在表达式里非法）；
2. `SUM((CLOSE/DELAY(CLOSE,1),20)^2,20)` 中 `(X, 20)` 不是合法表达式（逗号既不在函数调用层也不在 `SUM` 参数层）；
3. 即使补成合法写法（把 `(X,20)` 视作 `MEAN(X,20)`，DolphinDB 的实现口径），分子
   `SUM(收益 - 均值, 20)` 恒≈0，因子无意义 —— 原文必有掉字，不能只补乘号了事。

**判定依据**：该因子的官方长名是 **"20-Day Skewness of Price Returns"**（来源：Cross-Market Alpha
论文 Online Appendix A.4 的 Alpha191 全表，<https://arxiv.org/html/2601.06499>；BigQuant 页未给长名）。
把原文系数/分母结构与「样本偏度 G1」对照，n=20 时
`G1 = n·(n-1)^1.5·Σ(x-x̄)^3 / ((n-1)(n-2)·(Σ(x-x̄)^2)^1.5)`
的系数 `-20·(20-1)^1.5`、分母 `(20-1)(20-2)·(Σdev²)^1.5` 与原文**完全一致**，因此原文应是
「分子 Σdev³、分母 Σdev²」，排版时丢了 `^3` 和内层 dev 表达式。

**采用版本**：

```
-20 * (20 - 1) ^ 1.5 * SUM((CLOSE / DELAY(CLOSE, 1) - 1 - MEAN(CLOSE / DELAY(CLOSE, 1) - 1, 20)) ^ 3, 20) / ((20 - 1) * (20 - 2) * (SUM((CLOSE / DELAY(CLOSE, 1) - 1 - MEAN(CLOSE / DELAY(CLOSE, 1) - 1, 20)) ^ 2, 20)) ^ 1.5)
```

**备选**（更贴近原文、可解析，但因子近似恒 0，不推荐）：把「缺乘号 + `(X,20)` 视作 `MEAN(X,20)`」
最小修正为
`.../((20 - 1) * (20 - 2) * (SUM(MEAN(CLOSE / DELAY(CLOSE, 1), 20) ^ 2, 20)) ^ 1.5)`
（DolphinDB `gtja191Alpha::gtjaAlpha166` 即此口径）。若下游要求「严格按原文可解析化」，可切换到此式。

**结论**：存疑 = 是（唯一一条）。建议以后用「20 日收益率偏度」的独立实现与引擎结果对照校验。

## 5. 原样保留、但下游需要注意（未修改）

- **alpha143**：使用 `SELF`（研报附录：SELF = t-1 日的 Alpha143 自身值），**不是纯表达式**，
  表达式引擎/因子计算需要跨日状态或递归支持。
- **alpha165 / alpha183**：`MAX(SUMAC(...)) - MIN(SUMAC(...)) / STD(...)`。按常规优先级除法只作用于
  MIN 项，而研报意图疑似 `(MAX - MIN) / STD`。为忠实原文未加括号，下游实现时建议按 `(MAX-MIN)/STD`
  理解并二次确认。
- **alpha030**：`REGRESI(x, MKT, SMB, HML, 60)` 需要 Fama-French 三因子序列输入，非 OHLCV 可直接算。
- **alpha075 / 149 / 181 / 182**：引用 `BANCHMARKINDEXCLOSE/BANCHMARKINDEXOPEN`（研报原文拼写，
  非 BENCHMARK），需要基准指数行情。
- **alpha021 / 116 / 147**：`SEQUENCE(n)` 按研报附录为「生成 1..n 序列」，`REGBETA(A, B, n)` 为
  A 对 B 的 n 日滚动回归 beta。
- **alpha070 / 095 / 132 / 144**：使用 `AMOUNT`（成交额）；alpha011/060/111 使用 `VOLUME`。
- **alpha010**：`(CLOSE)^2`、**alpha013**：`(...)^0.5`、**alpha017/056/108/121/127/166/171/181**：
  均使用 `^` 幂运算（不是 C++ 的按位异或），引擎需把 `^` 实现为幂。
- **alpha154**：`((VWAP - MIN(VWAP, 16)) < CORR(VWAP, MEAN(VOLUME, 180), 18))` 直接输出布尔
  （0/1）因子；alpha098/101/123/148/180 等虽含比较运算，但经三元/乘 -1 转成数值因子，按研报原式保留。

## 6. 自检结果

脚本在仓库外临时目录运行（生成脚本 `build_inc.py`，未提交），对最终产物重新解析校验，结果：

```
Alpha191 formulas self-check (generated)
======================================================================
entries               : 191  (expected 191) -> OK
id continuity 1..191  : OK
paren balance         : all OK (191/191)
non-ascii chars       : none
double-quote inside   : none
double spaces         : none
token-preserving respace: OK (191/191, token stream identical before/after)
final formula length  : min=14 max=757 total=21290 chars
```

另有消费者视角复检（用解析 `.inc` 的正则 `\{"(alpha\d{3})", "(alpha\d{3})", R"\((.*?)\)"\},` 重新读取）：

- 解析出的条目数：**191 / 191**
- id 连续：`alpha001..alpha191` 恰好 191 个、无缺号无重复：**OK**
- 每条括号配平（栈深度不为负、末尾归零）：**191/191 OK**
- 每条公式纯 ASCII、无 `"`（保证 `R"(...)"` 不被提前截断）、无连续空格：**OK**
- `name` 与 `id` 逐条一致：**OK**
- 文件本身：194 行 = 3 行头注释 + 191 条；UTF-8（无 BOM）、LF 换行。

**额外：真实表达式引擎解析验证（C++ 消费者视角）**

把 `.inc` 按生产代码同款 `struct FormulaEntry { const char* id; const char* name; const char* formula; }`
数组包含，用 g++ 独立编译并链接仓库中真实的 `src/engine/screener/alpha191_expression.cpp`，逐条
构造 `st::factors::alpha191::Expression` 并检查 `valid()`：

```
rows=191 invalid=0
```

即 191/191 条公式均能被实际表达式引擎解析（对应 `test_panel_engine.cpp` 的
`Alpha191FormulaTableTest.AllFormulasParse`）。该 harness 在仓库外临时目录（`parse_check.cpp`），
不进入仓库、不改动任何工程文件。**正是这一步发现了 alpha180 的括号结构错误**（括号总数配平但三元
表达式结构非法，纯平衡检查无法发现），已按 §3 修复并复验通过。

自检脚本逻辑（可复现）：

1. 从 BigQuant 页面抓 `<pre><code>` 块，按 `^\s*Alpha(\d+)\s*[:：]\s*(.*)$` 解析 191 条原文；
2. 全角→ASCII、空白规范化、逐条笔误替换，再重新分词 + 重排空白，校验重排前后 token 序列相同；
3. 校验：条数 == 191、编号集合 == {1..191}、括号深度（`(` +1 / `)` -1，中途不得为负、末尾为 0）、
   非 ASCII 字符扫描、双引号扫描、连续空格扫描；
4. 以消费者视角的正则重新解析生成好的 `.inc`，重复 3 的全部校验，确保落盘内容与校验内存一致；
5. 用真实 C++ 表达式引擎（仓库内 `alpha191_expression.cpp`，仓库外 harness 编译）逐条解析 191 条，
   断言 `valid()` 全部为 true。

## 7. Lead 接入表达式引擎时的补充修正（2026-11-01）

以下 5 条公式在 `.tests/test_panel_engine.cpp::AllFormulasParse` 或求值阶段暴露出「原研报缺参数/括号错位，通用公式引擎无法直接求值」，按同式上下文补全（均已写入 `alpha191_formulas.inc`）：

| 编号 | 问题 | 采用版本 | 说明 |
| --- | --- | --- | --- |
| alpha010 | `RANK(MAX(...), 5)`：窗口 5 落在 RANK 参数位，RANK 变成 2 参数 | `RANK(MAX(..., 5))` | 按同式意图把窗口移入 MAX |
| alpha036 | `CORR(RANK(VOLUME), RANK(VWAP))` 缺窗口；`RANK(..., 2)` 多一个窗口 | `RANK(SUM(CORR(RANK(VOLUME), RANK(VWAP), 6), 2))` | CORR 窗口 6、SUM 窗口 2 |
| alpha054 | `STD(ABS(CLOSE - OPEN))` 缺窗口 | `STD(ABS(CLOSE - OPEN), 5)` | 参考同式/公开实现窗口 5 |
| alpha146 | 分母 `SMA(..., 60)` 缺第三个参数（SMA(X,N,M) 需 3 参） | `SMA(..., 60, 1)` | 按 GTJA SMA(X,N,M) 惯例补 M=1 |
| alpha181 | 分母 `SUM((BANCH...-MEAN(...))^3)` 缺窗口 | `SUM(..., 20)` | 与分子窗口一致 |

另：`REGBETA(MEAN(CLOSE,6), SEQUENCE(6))`（alpha021/alpha147）由引擎按「2 参数 REGBETA + SEQUENCE(n) → n 日回归斜率」支持，公式表未改。

**求值结果**：191/191 公式经 `Expression::valid()` 解析通过；在合成面板（6 只股票 × 320 日、含基准指数与 SMB/HML）上，除 alpha166（存疑还原版）等少数公式受数据条件限制外均能产出原始值。alpha143/149/157/165/183/127 由因子层自定义实现兜底（见 DEVLOG）。
