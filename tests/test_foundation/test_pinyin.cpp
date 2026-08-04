#include "foundation/utils/pinyin.h"
#include <gtest/gtest.h>
#include <string>

namespace st::utils {

TEST(PinyinTest, InitialsCommon) {
    EXPECT_EQ(pinyinInitials("贵州茅台"), "gzmt");
    EXPECT_EQ(pinyinInitials("中国平安"), "zgpa");
    EXPECT_EQ(pinyinInitials("宁德时代"), "ndsd");
    EXPECT_EQ(pinyinInitials("工商银行"), "gsyh");  // 行→háng 词覆盖
    EXPECT_EQ(pinyinInitials("招商银行"), "zsyh");
    EXPECT_EQ(pinyinInitials("华夏银行"), "hxyh");
}

TEST(PinyinTest, PolyphoneWordOverrides) {
    EXPECT_EQ(pinyinInitials("重庆啤酒"), "cqpj");  // 重→chóng
    EXPECT_EQ(pinyinInitials("西藏矿业"), "xzky");  // 藏→zàng
    EXPECT_EQ(pinyinInitials("厦门钨业"), "xmwy");  // 厦→xià
    EXPECT_EQ(pinyinFull("重庆啤酒"), "chongqingpijiu");
    EXPECT_EQ(pinyinFull("西藏矿业"), "xizangkuangye");
}

TEST(PinyinTest, InitialsWithAscii) {
    EXPECT_EQ(pinyinInitials("ST安泰"), "stat");  // ASCII 保留（小写）+ 汉字首字母
}

TEST(PinyinTest, InitialsNonHanIgnored) {
    EXPECT_EQ(pinyinInitials(""), "");
    EXPECT_EQ(pinyinInitials("123"), "");
    EXPECT_EQ(pinyinInitials("A股"), "ag");
}

TEST(PinyinTest, FullCommon) {
    EXPECT_EQ(pinyinFull("贵州茅台"), "guizhoumaotai");  // 贵=guì
    EXPECT_EQ(pinyinFull("中国平安"), "zhongguopingan");
    EXPECT_EQ(pinyinFull("万科"), "wanke");
}

TEST(PinyinTest, FullUmlautAsV) {
    // ü → v（输入法约定），如 绿城/女
    EXPECT_EQ(pinyinFull("绿城"), "lvcheng");
    EXPECT_EQ(pinyinInitials("绿城"), "lc");
}

TEST(PinyinTest, IndexLikeStockName) {
    // 上证指数 / 深证成指
    EXPECT_EQ(pinyinInitials("上证指数"), "szzs");
    EXPECT_EQ(pinyinInitials("深证成指"), "szcz");
    EXPECT_EQ(pinyinFull("上证指数"), "shangzhengzhishu");
}

}  // namespace st::utils
