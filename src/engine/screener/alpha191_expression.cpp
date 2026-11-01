#include "engine/screener/alpha191_expression.h"

#include "engine/screener/factor_math.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace st::factors::alpha191 {

namespace {

using math::Series;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// ============================ 值类型 ============================

struct Value {
    enum class Kind { Scalar, Series, Matrix };
    Kind kind = Kind::Scalar;
    size_t rows = 0;
    size_t cols = 1;
    double scalar = 0.0;
    Series data;

    static Value scalarValue(double v) {
        Value x;
        x.kind = Kind::Scalar;
        x.scalar = v;
        return x;
    }
    static Value seriesValue(size_t rows) {
        Value x;
        x.kind = Kind::Series;
        x.rows = rows;
        x.cols = 1;
        x.data.assign(rows, kNaN);
        return x;
    }
    static Value matrixValue(size_t rows, size_t cols) {
        Value x;
        x.kind = Kind::Matrix;
        x.rows = rows;
        x.cols = cols;
        x.data.assign(rows * cols, kNaN);
        return x;
    }
    [[nodiscard]] bool isScalar() const { return kind == Kind::Scalar; }
    [[nodiscard]] bool isSeries() const { return kind == Kind::Series; }
    [[nodiscard]] size_t columnCount() const { return kind == Kind::Matrix ? cols : 1; }
    [[nodiscard]] size_t cellCount() const { return kind == Kind::Scalar ? 1 : data.size(); }
};

// 值运算函数前置声明（EvalCtx 的派生变量会用到）
Value subValues(const Value& a, const Value& b);
Value absValue(const Value& v);
Value maxValues(const Value& a, const Value& b);
Value delayValue(const Value& v, int n);
Value compareValues(const Value& a, const Value& b, const std::string& op);
Value selectValues(const Value& cond, const Value& a, const Value& b);

// ============================ AST ============================

struct Node;
using NodePtr = std::unique_ptr<Node>;

struct Node {
    enum class Type { Number, Variable, Sequence, Unary, Binary, Ternary, Call };
    Type type = Type::Number;
    double number = 0.0;
    int sequenceLen = -1;  // SEQUENCE(n) 的 n；裸 SEQUENCE 为 -1
    std::string name;      // 变量名 / 函数名 / 运算符
    std::vector<NodePtr> args;
};

// ============================ 词法 ============================

enum class TokenKind { End, Number, Ident, Op, LParen, RParen, Comma, Question, Colon };

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    double number = 0.0;
};

class Lexer {
public:
    explicit Lexer(const std::string& src) : src_(src) {}

    std::vector<Token> tokenize() {
        std::vector<Token> out;
        size_t i = 0;
        while (i < src_.size()) {
            char c = src_[i];
            if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
            if (std::isdigit(static_cast<unsigned char>(c)) ||
                (c == '.' && i + 1 < src_.size() &&
                 std::isdigit(static_cast<unsigned char>(src_[i + 1])))) {
                size_t start = i;
                bool dot = false;
                while (i < src_.size()) {
                    char d = src_[i];
                    if (std::isdigit(static_cast<unsigned char>(d))) { ++i; continue; }
                    if (d == '.' && !dot) { dot = true; ++i; continue; }
                    break;
                }
                Token t;
                t.kind = TokenKind::Number;
                t.text = src_.substr(start, i - start);
                t.number = std::stod(t.text);
                out.push_back(std::move(t));
                continue;
            }
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                size_t start = i;
                while (i < src_.size() &&
                       (std::isalnum(static_cast<unsigned char>(src_[i])) || src_[i] == '_')) {
                    ++i;
                }
                std::string ident = src_.substr(start, i - start);
                for (char& ch : ident) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                if (ident == "OR") {
                    out.push_back(OpToken("||"));
                } else if (ident == "AND") {
                    out.push_back(OpToken("&&"));
                } else {
                    Token t;
                    t.kind = TokenKind::Ident;
                    t.text = std::move(ident);
                    out.push_back(std::move(t));
                }
                continue;
            }
            switch (c) {
                case '(': { Token t; t.kind = TokenKind::LParen; out.push_back(t); ++i; continue; }
                case ')': { Token t; t.kind = TokenKind::RParen; out.push_back(t); ++i; continue; }
                case ',': { Token t; t.kind = TokenKind::Comma; out.push_back(t); ++i; continue; }
                case '?': { Token t; t.kind = TokenKind::Question; out.push_back(t); ++i; continue; }
                case ':': { Token t; t.kind = TokenKind::Colon; out.push_back(t); ++i; continue; }
                case '.':  // 宽松处理：单独的点（. / 或 .* 的残留）跳过
                    if (i + 1 < src_.size() && (src_[i + 1] == '/' || src_[i + 1] == '*')) {
                        ++i;
                        continue;
                    }
                    throw std::runtime_error("非法字符 '.'");
                case '+': case '-': case '*': case '/': case '^':
                    out.push_back(OpToken(std::string(1, c)));
                    ++i;
                    continue;
                case '<': case '>': case '=': case '!': {
                    if (i + 1 < src_.size() && src_[i + 1] == '=') {
                        out.push_back(OpToken(std::string(1, c) + "="));
                        i += 2;
                    } else if (c == '=') {
                        // GTJA 公式中单个 '=' 表示相等比较
                        out.push_back(OpToken("=="));
                        ++i;
                    } else {
                        out.push_back(OpToken(std::string(1, c)));
                        ++i;
                    }
                    continue;
                }
                case '&': {
                    if (i + 1 < src_.size() && src_[i + 1] == '&') { out.push_back(OpToken("&&")); i += 2; }
                    else { out.push_back(OpToken("&&")); ++i; }
                    continue;
                }
                case '|': {
                    if (i + 1 < src_.size() && src_[i + 1] == '|') { out.push_back(OpToken("||")); i += 2; }
                    else { out.push_back(OpToken("||")); ++i; }
                    continue;
                }
                default:
                    throw std::runtime_error(std::string("非法字符 '") + c + "'");
            }
        }
        Token end;
        end.kind = TokenKind::End;
        out.push_back(end);
        return out;
    }

private:
    static Token OpToken(std::string text) {
        Token t;
        t.kind = TokenKind::Op;
        t.text = std::move(text);
        return t;
    }
    std::string src_;
};

// ============================ 语法分析 ============================

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    NodePtr parse() {
        auto root = parseTernary();
        if (peek().kind != TokenKind::End) throw std::runtime_error("公式末尾存在多余内容");
        return root;
    }

private:
    const Token& peek() const { return tokens_[pos_]; }
    const Token& advance() { return tokens_[pos_++]; }
    bool matchOp(const char* op) {
        if (peek().kind == TokenKind::Op && peek().text == op) { ++pos_; return true; }
        return false;
    }
    void expect(TokenKind kind, const char* what) {
        if (peek().kind != kind) throw std::runtime_error(std::string("缺少 ") + what);
        ++pos_;
    }

    NodePtr makeNumber(double v) {
        auto n = std::make_unique<Node>();
        n->type = Node::Type::Number;
        n->number = v;
        return n;
    }

    NodePtr parseTernary() {
        auto cond = parseOr();
        if (peek().kind == TokenKind::Question) {
            ++pos_;
            auto a = parseTernary();
            expect(TokenKind::Colon, "':'");
            auto b = parseTernary();
            auto n = std::make_unique<Node>();
            n->type = Node::Type::Ternary;
            n->args.push_back(std::move(cond));
            n->args.push_back(std::move(a));
            n->args.push_back(std::move(b));
            return n;
        }
        return cond;
    }

    NodePtr makeBinary(std::string op, NodePtr a, NodePtr b) {
        auto n = std::make_unique<Node>();
        n->type = Node::Type::Binary;
        n->name = std::move(op);
        n->args.push_back(std::move(a));
        n->args.push_back(std::move(b));
        return n;
    }

    NodePtr parseOr() {
        auto left = parseAnd();
        while (matchOp("||")) left = makeBinary("||", std::move(left), parseAnd());
        return left;
    }
    NodePtr parseAnd() {
        auto left = parseEquality();
        while (matchOp("&&")) left = makeBinary("&&", std::move(left), parseEquality());
        return left;
    }
    NodePtr parseEquality() {
        auto left = parseRelational();
        while (peek().kind == TokenKind::Op &&
               (peek().text == "==" || peek().text == "!=")) {
            std::string op = advance().text;
            left = makeBinary(op, std::move(left), parseRelational());
        }
        return left;
    }
    NodePtr parseRelational() {
        auto left = parseAdditive();
        while (peek().kind == TokenKind::Op &&
               (peek().text == "<" || peek().text == ">" ||
                peek().text == "<=" || peek().text == ">=")) {
            std::string op = advance().text;
            left = makeBinary(op, std::move(left), parseAdditive());
        }
        return left;
    }
    NodePtr parseAdditive() {
        auto left = parseMultiplicative();
        while (peek().kind == TokenKind::Op && (peek().text == "+" || peek().text == "-")) {
            std::string op = advance().text;
            left = makeBinary(op, std::move(left), parseMultiplicative());
        }
        return left;
    }
    NodePtr parseMultiplicative() {
        auto left = parseUnary();
        while (peek().kind == TokenKind::Op && (peek().text == "*" || peek().text == "/")) {
            std::string op = advance().text;
            left = makeBinary(op, std::move(left), parseUnary());
        }
        return left;
    }
    NodePtr parseUnary() {
        if (peek().kind == TokenKind::Op && (peek().text == "-" || peek().text == "+")) {
            std::string op = advance().text;
            auto n = std::make_unique<Node>();
            n->type = Node::Type::Unary;
            n->name = op;
            n->args.push_back(parseUnary());
            return n;
        }
        return parsePower();
    }
    NodePtr parsePower() {
        auto base = parsePrimary();
        if (peek().kind == TokenKind::Op && peek().text == "^") {
            ++pos_;
            return makeBinary("^", std::move(base), parseUnary());
        }
        return base;
    }
    NodePtr parsePrimary() {
        if (peek().kind == TokenKind::Number) {
            double v = advance().number;
            return makeNumber(v);
        }
        if (peek().kind == TokenKind::LParen) {
            ++pos_;
            auto e = parseTernary();
            expect(TokenKind::RParen, "')'");
            return e;
        }
        if (peek().kind == TokenKind::Ident) {
            std::string name = advance().text;
            if (name == "SEQUENCE") {
                auto n = std::make_unique<Node>();
                n->type = Node::Type::Sequence;
                if (peek().kind == TokenKind::LParen) {
                    ++pos_;
                    if (peek().kind != TokenKind::Number) throw std::runtime_error("SEQUENCE 参数必须是常数");
                    n->sequenceLen = static_cast<int>(advance().number);
                    expect(TokenKind::RParen, "')'");
                }
                return n;
            }
            if (peek().kind == TokenKind::LParen) {
                ++pos_;
                auto n = std::make_unique<Node>();
                n->type = Node::Type::Call;
                n->name = name;
                if (peek().kind != TokenKind::RParen) {
                    n->args.push_back(parseTernary());
                    while (peek().kind == TokenKind::Comma) {
                        ++pos_;
                        n->args.push_back(parseTernary());
                    }
                }
                expect(TokenKind::RParen, "')'");
                return n;
            }
            auto n = std::make_unique<Node>();
            n->type = Node::Type::Variable;
            n->name = name;
            return n;
        }
        throw std::runtime_error("无法解析公式片段");
    }

    std::vector<Token> tokens_;
    size_t pos_ = 0;
};

// ============================ 求值上下文 ============================

class EvalCtx {
public:
    explicit EvalCtx(const PanelData& panel) : panel_(panel) {}

    [[nodiscard]] const PanelData& panel() const { return panel_; }

    Value variable(const std::string& name) {
        auto it = cache_.find(name);
        if (it != cache_.end()) return it->second;
        Value v = computeVariable(name);
        cache_[name] = v;
        return v;
    }

private:
    Value fromMatrix(const std::vector<double>& m) const {
        Value v;
        v.kind = Value::Kind::Matrix;
        v.rows = panel_.rows();
        v.cols = panel_.cols();
        v.data.assign(m.begin(), m.end());
        return v;
    }
    Value fromSeries(const std::vector<double>& s) const {
        Value v;
        v.kind = Value::Kind::Series;
        v.rows = s.size();
        v.cols = 1;
        v.data.assign(s.begin(), s.end());
        return v;
    }

    Value computeVariable(const std::string& name) {
        if (name == "OPEN") return fromMatrix(panel_.open);
        if (name == "HIGH") return fromMatrix(panel_.high);
        if (name == "LOW") return fromMatrix(panel_.low);
        if (name == "CLOSE") return fromMatrix(panel_.close);
        if (name == "VOLUME" || name == "VOL") return fromMatrix(panel_.volume);
        if (name == "AMOUNT") return fromMatrix(panel_.amount);
        if (name == "VWAP") return fromMatrix(panel_.vwap);
        if (name == "RET") return fromMatrix(panel_.ret);
        if (name == "MARKETCAP") return fromMatrix(panel_.marketCap);
        if (name == "MKT") return fromSeries(panel_.mkt);
        if (name == "SMB") return fromSeries(panel_.smb);
        if (name == "HML") return fromSeries(panel_.hml);
        if (name == "BANCHMARKINDEXCLOSE" || name == "BENCHMARKINDEXCLOSE") {
            return fromSeries(panel_.benchClose);
        }
        if (name == "BANCHMARKINDEXOPEN" || name == "BENCHMARKINDEXOPEN") {
            return fromSeries(panel_.benchOpen);
        }
        // ---- 派生量（官方公式里的 DTM/DBM/LD/HD/TR）----
        if (name == "HD") {
            Value h = fromMatrix(panel_.high);
            return subValues(h, delayValue(h, 1));
        }
        if (name == "LD") {
            Value l = fromMatrix(panel_.low);
            return subValues(l, delayValue(l, 1));
        }
        if (name == "TR") {
            Value h = fromMatrix(panel_.high);
            Value l = fromMatrix(panel_.low);
            Value c = fromMatrix(panel_.close);
            Value prevC = delayValue(c, 1);
            Value a = subValues(h, l);
            Value b = absValue(subValues(h, prevC));
            Value d = absValue(subValues(l, prevC));
            return maxValues(maxValues(a, b), d);
        }
        if (name == "DTM" || name == "DBM") {
            Value o = fromMatrix(panel_.open);
            Value prevO = delayValue(o, 1);
            Value h = fromMatrix(panel_.high);
            Value l = fromMatrix(panel_.low);
            Value up = subValues(h, o);
            Value down = subValues(o, l);
            Value gapOpen = subValues(o, prevO);
            Value upSide = maxValues(up, gapOpen);
            Value downSide = maxValues(down, gapOpen);
            Value rise = compareValues(o, prevO, ">");
            Value fall = compareValues(o, prevO, "<");
            Value dtm = selectValues(rise, Value::scalarValue(0.0), upSide);
            if (name == "DTM") return dtm;
            return selectValues(fall, Value::scalarValue(0.0), downSide);
        }
        throw std::runtime_error("未知变量: " + name);
    }

    const PanelData& panel_;
    std::map<std::string, Value> cache_;
};

// ============================ 值运算 ============================

std::vector<double> extractColumn(const Value& v, size_t col, size_t rows) {
    std::vector<double> out(rows, kNaN);
    if (v.isScalar()) {
        std::fill(out.begin(), out.end(), v.scalar);
        return out;
    }
    if (v.isSeries()) {
        if (v.rows != rows) throw std::runtime_error("序列行数与面板不一致");
        return v.data;
    }
    if (v.rows != rows || col >= v.cols || v.data.size() < rows * v.cols) {
        throw std::runtime_error("矩阵列越界/尺寸不一致");
    }
    const size_t cols = v.cols;
    for (size_t t = 0; t < rows; ++t) out[t] = v.data[t * cols + col];
    return out;
}

void writeColumn(Value& out, size_t col, const std::vector<double>& data) {
    if (data.size() < out.rows) throw std::runtime_error("列数据长度不足");
    if (out.isSeries()) {
        out.data = data;
        return;
    }
    if (col >= out.cols || out.data.size() < out.rows * out.cols) {
        throw std::runtime_error("矩阵列越界/尺寸不一致");
    }
    const size_t cols = out.cols;
    for (size_t t = 0; t < out.rows; ++t) out.data[t * cols + col] = data[t];
}

template <typename F>
Value binaryValues(const Value& a, const Value& b, F fn) {
    if (a.isScalar() && b.isScalar()) {
        return Value::scalarValue(fn(a.scalar, b.scalar));
    }
    if (a.isScalar()) {
        Value out = b;
        for (double& x : out.data) x = fn(a.scalar, x);
        return out;
    }
    if (b.isScalar()) {
        Value out = a;
        for (double& x : out.data) x = fn(x, b.scalar);
        return out;
    }
    if (a.rows != b.rows) throw std::runtime_error("矩阵行数不一致");
    const size_t aCols = a.columnCount();
    const size_t bCols = b.columnCount();
    const size_t cols = std::max(aCols, bCols);
    if (aCols != bCols && aCols != 1 && bCols != 1) {
        throw std::runtime_error("矩阵列数不一致");
    }
    Value out = (a.isSeries() && b.isSeries()) ? Value::seriesValue(a.rows)
                                               : Value::matrixValue(a.rows, cols);
    for (size_t c = 0; c < cols; ++c) {
        const size_t ca = aCols == 1 ? 0 : c;
        const size_t cb = bCols == 1 ? 0 : c;
        for (size_t t = 0; t < a.rows; ++t) {
            const double x = a.isSeries() ? a.data[t] : a.data[t * aCols + ca];
            const double y = b.isSeries() ? b.data[t] : b.data[t * bCols + cb];
            out.data[out.isSeries() ? t : t * cols + c] = fn(x, y);
        }
    }
    return out;
}

Value addValues(const Value& a, const Value& b) {
    return binaryValues(a, b, [](double x, double y) {
        return (std::isnan(x) || std::isnan(y)) ? kNaN : x + y;
    });
}
Value subValues(const Value& a, const Value& b) {
    return binaryValues(a, b, [](double x, double y) {
        return (std::isnan(x) || std::isnan(y)) ? kNaN : x - y;
    });
}
Value mulValues(const Value& a, const Value& b) {
    return binaryValues(a, b, [](double x, double y) {
        return (std::isnan(x) || std::isnan(y)) ? kNaN : x * y;
    });
}
Value divValues(const Value& a, const Value& b) {
    return binaryValues(a, b, [](double x, double y) {
        if (std::isnan(x) || std::isnan(y) || y == 0.0) return kNaN;
        return x / y;
    });
}
Value powValues(const Value& a, const Value& b) {
    return binaryValues(a, b, [](double x, double y) {
        if (std::isnan(x) || std::isnan(y)) return kNaN;
        return std::pow(x, y);
    });
}
Value compareValues(const Value& a, const Value& b, const std::string& op) {
    return binaryValues(a, b, [&op](double x, double y) {
        if (std::isnan(x) || std::isnan(y)) return 0.0;
        if (op == "<") return x < y ? 1.0 : 0.0;
        if (op == ">") return x > y ? 1.0 : 0.0;
        if (op == "<=") return x <= y ? 1.0 : 0.0;
        if (op == ">=") return x >= y ? 1.0 : 0.0;
        if (op == "==") return x == y ? 1.0 : 0.0;
        return x != y ? 1.0 : 0.0;
    });
}
Value logicalValues(const Value& a, const Value& b, bool isAnd) {
    return binaryValues(a, b, [isAnd](double x, double y) {
        const bool bx = !std::isnan(x) && x > 0.0;
        const bool by = !std::isnan(y) && y > 0.0;
        return (isAnd ? (bx && by) : (bx || by)) ? 1.0 : 0.0;
    });
}
Value negValue(const Value& v) {
    if (v.isScalar()) return Value::scalarValue(-v.scalar);
    Value out = v;
    for (double& x : out.data) {
        if (!std::isnan(x)) x = -x;
    }
    return out;
}
Value absValue(const Value& v) {
    if (v.isScalar()) return Value::scalarValue(std::abs(v.scalar));
    Value out = v;
    for (double& x : out.data) {
        if (!std::isnan(x)) x = std::abs(x);
    }
    return out;
}
Value signValue(const Value& v) {
    if (v.isScalar()) return Value::scalarValue(v.scalar > 0 ? 1.0 : (v.scalar < 0 ? -1.0 : 0.0));
    Value out = v;
    for (double& x : out.data) {
        if (!std::isnan(x)) x = x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0);
    }
    return out;
}
Value logValue(const Value& v) {
    if (v.isScalar()) return Value::scalarValue(v.scalar > 0 ? std::log(v.scalar) : kNaN);
    Value out = v;
    for (double& x : out.data) {
        if (std::isnan(x) || x <= 0.0) x = kNaN;
        else x = std::log(x);
    }
    return out;
}
Value sqrtValue(const Value& v) {
    if (v.isScalar()) return Value::scalarValue(v.scalar >= 0 ? std::sqrt(v.scalar) : kNaN);
    Value out = v;
    for (double& x : out.data) {
        if (std::isnan(x) || x < 0.0) x = kNaN;
        else x = std::sqrt(x);
    }
    return out;
}
Value expValue(const Value& v) {
    if (v.isScalar()) return Value::scalarValue(std::exp(v.scalar));
    Value out = v;
    for (double& x : out.data) {
        if (!std::isnan(x)) x = std::exp(x);
    }
    return out;
}
Value maxValues(const Value& a, const Value& b) {
    return binaryValues(a, b, [](double x, double y) {
        if (std::isnan(x)) return y;
        if (std::isnan(y)) return x;
        return std::max(x, y);
    });
}
Value minValues(const Value& a, const Value& b) {
    return binaryValues(a, b, [](double x, double y) {
        if (std::isnan(x)) return y;
        if (std::isnan(y)) return x;
        return std::min(x, y);
    });
}
Value selectValues(const Value& cond, const Value& a, const Value& b) {
    Value c = cond;
    // 统一到矩阵/序列三路广播
    const Value* vals[3] = {&c, &a, &b};
    size_t rows = 0, cols = 1;
    bool anyMatrix = false;
    for (const Value* v : vals) {
        if (v->isScalar()) continue;
        if (rows == 0) rows = v->rows;
        if (v->isSeries()) continue;
        anyMatrix = true;
        cols = std::max(cols, v->cols);
    }
    if (rows == 0) {
        const bool pickA = !std::isnan(c.scalar) && c.scalar > 0.0;
        return Value::scalarValue(pickA ? a.scalar : b.scalar);
    }
    Value out = anyMatrix ? Value::matrixValue(rows, cols) : Value::seriesValue(rows);
    const std::function<double(const Value&, size_t, size_t)> at =
        [](const Value& v, size_t t, size_t c) {
            if (v.isScalar()) return v.scalar;
            if (v.isSeries()) return v.data[t];
            const size_t cc = v.cols == 1 ? 0 : (c < v.cols ? c : 0);
            return v.data[t * v.cols + cc];
        };
    for (size_t cc = 0; cc < cols; ++cc) {
        for (size_t t = 0; t < rows; ++t) {
            const double cv = at(c, t, cc);
            const bool pickA = !std::isnan(cv) && cv > 0.0;
            out.data[out.isSeries() ? t : t * cols + cc] = pickA ? at(a, t, cc) : at(b, t, cc);
        }
    }
    return out;
}

Value delayValue(const Value& v, int n) {
    if (v.isScalar()) return v;
    const size_t rows = v.rows;
    const size_t cols = v.columnCount();
    Value out = v;
    out.data.assign(v.data.size(), kNaN);
    for (size_t c = 0; c < cols; ++c) {
        const auto col = extractColumn(v, c, rows);
        auto d = math::delay(col, n);
        writeColumn(out, c, d);
    }
    return out;
}

template <typename Fn>
Value rollingUnary(const Value& x, Fn fn) {
    if (x.isScalar()) return Value::scalarValue(kNaN);
    const size_t rows = x.rows;
    const size_t cols = x.columnCount();
    Value out = x;
    out.data.assign(x.data.size(), kNaN);
    for (size_t c = 0; c < cols; ++c) {
        const auto col = extractColumn(x, c, rows);
        writeColumn(out, c, fn(col));
    }
    return out;
}

template <typename Fn>
Value rollingPair(const Value& a, const Value& b, Fn fn) {
    if (a.isScalar() || b.isScalar()) {
        // 常数与序列的成对滚动没有意义 → 全 NaN
        return a.isScalar() ? (b.isScalar() ? Value::scalarValue(kNaN)
                                           : Value::seriesValue(b.rows))
                            : (a.rows > 0 ? Value::seriesValue(a.rows) : Value::scalarValue(kNaN));
    }
    if (a.rows != b.rows) throw std::runtime_error("滚动算子行数不一致");
    const size_t rows = a.rows;
    const size_t cols = std::max(a.columnCount(), b.columnCount());
    if (a.columnCount() != b.columnCount() && a.columnCount() != 1 && b.columnCount() != 1) {
        throw std::runtime_error("滚动算子列数不一致");
    }
    Value out = Value::matrixValue(rows, cols);
    if (a.isSeries() && b.isSeries()) out = Value::seriesValue(rows);
    for (size_t c = 0; c < cols; ++c) {
        const size_t ca = a.columnCount() == 1 ? 0 : c;
        const size_t cb = b.columnCount() == 1 ? 0 : c;
        const auto x = extractColumn(a, ca, rows);
        const auto y = extractColumn(b, cb, rows);
        writeColumn(out, c, fn(x, y));
    }
    return out;
}

Value evalNode(const Node& node, EvalCtx& ctx);

Value evalRank(const Value& x) {
    if (x.isScalar()) return Value::scalarValue(kNaN);
    if (x.isSeries()) return Value::seriesValue(x.rows);
    const size_t rows = x.rows;
    const size_t cols = x.cols;
    Value out = Value::matrixValue(rows, cols);
    std::vector<std::pair<double, size_t>> buffer;
    buffer.reserve(cols);
    for (size_t t = 0; t < rows; ++t) {
        buffer.clear();
        for (size_t i = 0; i < cols; ++i) {
            const double v = x.data[t * cols + i];
            if (std::isfinite(v)) buffer.emplace_back(v, i);
        }
        if (buffer.empty()) continue;
        std::sort(buffer.begin(), buffer.end(),
                  [](const auto& p, const auto& q) { return p.first < q.first; });
        const double count = static_cast<double>(buffer.size());
        size_t k = 0;
        while (k < buffer.size()) {
            size_t e = k;
            while (e + 1 < buffer.size() && buffer[e + 1].first == buffer[k].first) ++e;
            const double rank = (static_cast<double>(k) + static_cast<double>(e) + 2.0) / 2.0;
            for (size_t m = k; m <= e; ++m) {
                out.data[t * cols + buffer[m].second] = rank / count;
            }
            k = e + 1;
        }
    }
    return out;
}

int scalarWindowArg(const std::vector<Value>& args, size_t idx) {
    if (idx >= args.size() || !args[idx].isScalar()) throw std::runtime_error("滚动窗口必须是常数");
    return static_cast<int>(std::llround(args[idx].scalar));
}

Value evalRegBeta(const Node& node, EvalCtx& ctx) {
    if (node.args.size() == 2) {
        // REGBETA(x, SEQUENCE(n))：窗口由序列长度给出
        if (node.args[1]->type != Node::Type::Sequence ||
            node.args[1]->sequenceLen <= 1) {
            throw std::runtime_error("REGBETA 2 参数形式需要 SEQUENCE(n)");
        }
        const int n = node.args[1]->sequenceLen;
        Value y = evalNode(*node.args[0], ctx);
        return rollingUnary(y, [n](const Series& series) { return math::regBetaSeq(series, n); });
    }
    if (node.args.size() != 3) throw std::runtime_error("REGBETA 需要 2~3 个参数");
    Value y = evalNode(*node.args[0], ctx);
    const bool sequence = node.args[1]->type == Node::Type::Sequence;
    Value out;
    if (sequence) {
        int n = 0;
        if (node.args[1]->sequenceLen > 0) n = node.args[1]->sequenceLen;
        else n = scalarWindowArg({evalNode(*node.args[2], ctx)}, 0);
        if (n <= 1) throw std::runtime_error("REGBETA SEQUENCE 窗口非法");
        out = rollingUnary(y, [n](const Series& series) { return math::regBetaSeq(series, n); });
    } else {
        Value x = evalNode(*node.args[1], ctx);
        const int n = scalarWindowArg({evalNode(*node.args[2], ctx)}, 0);
        if (n <= 1) throw std::runtime_error("REGBETA 窗口非法");
        out = rollingPair(y, x, [n](const Series& a, const Series& b) {
            return math::regBeta(a, b, n);
        });
    }
    return out;
}

/// 滚动多元回归残差 REGRESI(y, x1[,x2[,x3]], n)
Value evalRegresi(const Node& node, EvalCtx& ctx) {
    if (node.args.size() < 3) throw std::runtime_error("REGRESI 参数不足");
    const size_t k = node.args.size() - 2;  // 回归元个数
    if (k > 3) throw std::runtime_error("REGRESI 只支持最多 3 个回归元");
    Value y = evalNode(*node.args[0], ctx);
    std::vector<Value> xs;
    for (size_t m = 0; m < k; ++m) xs.push_back(evalNode(*node.args[1 + m], ctx));
    Value w = evalNode(*node.args[node.args.size() - 1], ctx);
    if (!w.isScalar()) throw std::runtime_error("REGRESI 窗口必须是常数");
    const int n = static_cast<int>(std::llround(w.scalar));
    if (n <= 1) throw std::runtime_error("REGRESI 窗口非法");
    if (y.isScalar()) return Value::scalarValue(kNaN);

    const size_t rows = y.rows;
    const size_t cols = y.columnCount();
    Value out = y.isSeries() ? Value::seriesValue(rows) : Value::matrixValue(rows, cols);
    const size_t kk = k + 1;  // 含截距
    for (size_t c = 0; c < cols; ++c) {
        const Series yv = extractColumn(y, c, rows);
        std::vector<Series> xv;
        for (auto& x : xs) xv.push_back(extractColumn(x, x.cols == 1 ? 0 : c, rows));
        Series res(rows, kNaN);
        for (size_t t = static_cast<size_t>(n - 1); t < rows; ++t) {
            bool ok = true;
            std::vector<double> sums(kk * kk, 0.0), rhs(kk, 0.0);
            for (size_t m = 0; m < kk; ++m) {
                for (size_t q = 0; q < kk; ++q) {
                    double s = 0.0;
                    for (int d = 0; d < n; ++d) {
                        const size_t idx = t - static_cast<size_t>(n - 1 - d);
                        const double vm = (m == 0) ? 1.0 : xv[m - 1][idx];
                        const double vq = (q == 0) ? 1.0 : xv[q - 1][idx];
                        if ((m > 0 && std::isnan(xv[m - 1][idx])) ||
                            (q > 0 && std::isnan(xv[q - 1][idx]))) {
                            ok = false;
                        }
                        s += vm * vq;
                    }
                    sums[m * kk + q] = s;
                }
                double sy = 0.0;
                for (int d = 0; d < n; ++d) {
                    const size_t idx = t - static_cast<size_t>(n - 1 - d);
                    const double ym = yv[idx];
                    if (std::isnan(ym)) ok = false;
                    const double vm = (m == 0) ? 1.0 : xv[m - 1][idx];
                    sy += vm * ym;
                }
                rhs[m] = sy;
            }
            if (!ok) continue;
            // 高斯消元（含截距的最小二乘正规方程）
            std::vector<double> a = sums;
            std::vector<double> b = rhs;
            bool singular = false;
            for (size_t col = 0; col < kk; ++col) {
                size_t pivot = col;
                for (size_t r = col + 1; r < kk; ++r) {
                    if (std::abs(a[r * kk + col]) > std::abs(a[pivot * kk + col])) pivot = r;
                }
                if (std::abs(a[pivot * kk + col]) < 1e-15) { singular = true; break; }
                if (pivot != col) {
                    for (size_t q = col; q < kk; ++q) {
                        std::swap(a[pivot * kk + q], a[col * kk + q]);
                    }
                    std::swap(b[pivot], b[col]);
                }
                const double diag = a[col * kk + col];
                for (size_t q = col; q < kk; ++q) a[col * kk + q] /= diag;
                b[col] /= diag;
                for (size_t r = 0; r < kk; ++r) {
                    if (r == col) continue;
                    const double factor = a[r * kk + col];
                    if (factor == 0.0) continue;
                    for (size_t q = col; q < kk; ++q) {
                        a[r * kk + q] -= factor * a[col * kk + q];
                    }
                    b[r] -= factor * b[col];
                }
            }
            if (singular) continue;
            double fitted = b[0];
            for (size_t m = 0; m < k; ++m) fitted += b[m + 1] * xv[m][t];
            res[t] = yv[t] - fitted;
        }
        writeColumn(out, c, res);
    }
    return out;
}

Value evalCall(const Node& node, EvalCtx& ctx) {
    const std::string& name = node.name;
    const auto evalArg = [&ctx, &node](size_t i) {
        if (i >= node.args.size()) throw std::runtime_error("函数参数不足: " + node.name);
        return evalNode(*node.args[i], ctx);
    };
    const auto argCount = [&node]() { return node.args.size(); };

    if (name == "RANK") {
        if (argCount() != 1) throw std::runtime_error("RANK 需要 1 个参数");
        return evalRank(evalArg(0));
    }
    if (name == "REGBETA") return evalRegBeta(node, ctx);
    if (name == "REGRESI") return evalRegresi(node, ctx);

    auto rolling1 = [&](std::function<Series(const Series&)> fn,
                        int /*window*/) -> Value {
        if (argCount() != 2) throw std::runtime_error(name + " 需要 2 个参数");
        Value x = evalArg(0);
        if (!x.isScalar()) {
            // 单参数滚动（带窗口）
            return rollingUnary(x, [fn](const Series& s) { return fn(s); });
        }
        return Value::scalarValue(kNaN);
    };

    if (name == "DELAY") {
        Value x = evalArg(0);
        const int n = scalarWindowArg({evalArg(1)}, 0);
        return delayValue(x, n);
    }
    if (name == "DELTA" || name == "DELAT") {
        Value x = evalArg(0);
        const int n = scalarWindowArg({evalArg(1)}, 0);
        return subValues(x, delayValue(x, n));
    }
    if (name == "SUM") {
        const int n = scalarWindowArg({evalArg(1)}, 0);
        return rolling1([n](const Series& s) { return math::tsSum(s, n); }, n);
    }
    if (name == "MEAN" || name == "MA") {
        const int n = scalarWindowArg({evalArg(1)}, 0);
        return rolling1([n](const Series& s) { return math::tsMean(s, n); }, n);
    }
    if (name == "STD") {
        const int n = scalarWindowArg({evalArg(1)}, 0);
        return rolling1([n](const Series& s) { return math::tsStd(s, n); }, n);
    }
    if (name == "TSMAX") {
        const int n = scalarWindowArg({evalArg(1)}, 0);
        return rolling1([n](const Series& s) { return math::tsMax(s, n); }, n);
    }
    if (name == "TSMIN") {
        const int n = scalarWindowArg({evalArg(1)}, 0);
        return rolling1([n](const Series& s) { return math::tsMin(s, n); }, n);
    }
    if (name == "TSRANK") {
        const int n = scalarWindowArg({evalArg(1)}, 0);
        return rolling1([n](const Series& s) { return math::tsRank(s, n); }, n);
    }
    if (name == "DECAYLINEAR") {
        const int n = scalarWindowArg({evalArg(1)}, 0);
        return rolling1([n](const Series& s) { return math::decayLinear(s, n); }, n);
    }
    if (name == "WMA") {
        const int n = scalarWindowArg({evalArg(1)}, 0);
        return rolling1([n](const Series& s) { return math::wma(s, n); }, n);
    }
    if (name == "SMA" || name == "SMEAN") {
        if (argCount() != 3) throw std::runtime_error(name + " 需要 3 个参数");
        const int n = scalarWindowArg({evalArg(1)}, 0);
        const int m = scalarWindowArg({evalArg(2)}, 0);
        Value x = evalArg(0);
        return rollingUnary(x, [n, m](const Series& s) { return math::smaCn(s, n, m); });
    }
    if (name == "HIGHDAY") {
        const int n = scalarWindowArg({evalArg(1)}, 0);
        return rolling1([n](const Series& s) { return math::highDay(s, n); }, n);
    }
    if (name == "LOWDAY") {
        const int n = scalarWindowArg({evalArg(1)}, 0);
        return rolling1([n](const Series& s) { return math::lowDay(s, n); }, n);
    }
    if (name == "CORR") {
        if (argCount() != 3) throw std::runtime_error("CORR 需要 3 个参数");
        Value x = evalArg(0);
        Value y = evalArg(1);
        const int n = scalarWindowArg({evalArg(2)}, 0);
        return rollingPair(x, y, [n](const Series& a, const Series& b) {
            return math::corr(a, b, n);
        });
    }
    if (name == "COVIANCE" || name == "COVAR") {
        if (argCount() != 3) throw std::runtime_error(name + " 需要 3 个参数");
        Value x = evalArg(0);
        Value y = evalArg(1);
        const int n = scalarWindowArg({evalArg(2)}, 0);
        return rollingPair(x, y, [n](const Series& a, const Series& b) {
            return math::covar(a, b, n);
        });
    }
    if (name == "COUNT") {
        if (argCount() != 2) throw std::runtime_error("COUNT 需要 2 个参数");
        Value cond = evalArg(0);
        const int n = scalarWindowArg({evalArg(1)}, 0);
        return rollingUnary(cond, [n](const Series& s) {
            Series mask(s.size(), 0.0);
            for (size_t i = 0; i < s.size(); ++i) {
                mask[i] = (!std::isnan(s[i]) && s[i] > 0.0) ? 1.0 : 0.0;
            }
            return math::tsSum(mask, n);
        });
    }
    if (name == "SUMIF") {
        if (argCount() != 3) throw std::runtime_error("SUMIF 需要 3 个参数");
        Value x = evalArg(0);
        const int n = scalarWindowArg({evalArg(1)}, 0);
        Value cond = evalArg(2);
        return rollingPair(x, cond, [n](const Series& a, const Series& b) {
            return math::sumIf(a, b, n);
        });
    }
    if (name == "FILTER") {
        if (argCount() != 2) throw std::runtime_error("FILTER 需要 2 个参数");
        Value x = evalArg(0);
        Value cond = evalArg(1);
        return binaryValues(x, cond, [](double v, double c) {
            return (!std::isnan(c) && c > 0.0) ? v : kNaN;
        });
    }
    if (name == "SUMAC") {
        if (argCount() != 1) throw std::runtime_error("SUMAC 需要 1 个参数");
        Value x = evalArg(0);
        return rollingUnary(x, [](const Series& s) {
            Series out(s.size(), kNaN);
            double acc = 0.0;
            bool broken = false;
            for (size_t i = 0; i < s.size(); ++i) {
                if (std::isnan(s[i])) { broken = true; continue; }
                if (!broken) { acc += s[i]; out[i] = acc; }
            }
            return out;
        });
    }
    if (name == "ABS") {
        if (argCount() != 1) throw std::runtime_error("ABS 需要 1 个参数");
        return absValue(evalArg(0));
    }
    if (name == "SIGN") {
        if (argCount() != 1) throw std::runtime_error("SIGN 需要 1 个参数");
        return signValue(evalArg(0));
    }
    if (name == "LOG") {
        if (argCount() != 1) throw std::runtime_error("LOG 需要 1 个参数");
        return logValue(evalArg(0));
    }
    if (name == "SQRT") {
        if (argCount() != 1) throw std::runtime_error("SQRT 需要 1 个参数");
        return sqrtValue(evalArg(0));
    }
    if (name == "EXP") {
        if (argCount() != 1) throw std::runtime_error("EXP 需要 1 个参数");
        return expValue(evalArg(0));
    }
    if (name == "MAX" || name == "MIN") {
        if (argCount() == 2) {
            // 第二参数若是整数常数且首参数非标量 → 滚动最大/最小；否则逐元素
            const Node& second = *node.args[1];
            Value a = evalArg(0);
            if (second.type == Node::Type::Number && second.number >= 2.0 &&
                std::abs(second.number - std::round(second.number)) < 1e-9 &&
                !a.isScalar()) {
                const int n = static_cast<int>(std::llround(second.number));
                if (name == "MAX") {
                    return rollingUnary(a, [n](const Series& s) { return math::tsMax(s, n); });
                }
                return rollingUnary(a, [n](const Series& s) { return math::tsMin(s, n); });
            }
            Value b = evalArg(1);
            return name == "MAX" ? maxValues(a, b) : minValues(a, b);
        }
        throw std::runtime_error(name + " 需要 2 个参数");
    }
    throw std::runtime_error("不支持的函数: " + name);
}

Value evalNode(const Node& node, EvalCtx& ctx) {
    switch (node.type) {
        case Node::Type::Number:
            return Value::scalarValue(node.number);
        case Node::Type::Variable:
            return ctx.variable(node.name);
        case Node::Type::Sequence:
            throw std::runtime_error("SEQUENCE 只能出现在 REGBETA 参数中");
        case Node::Type::Unary: {
            Value v = evalNode(*node.args[0], ctx);
            return node.name == "-" ? negValue(v) : v;
        }
        case Node::Type::Binary: {
            Value a = evalNode(*node.args[0], ctx);
            Value b = evalNode(*node.args[1], ctx);
            const std::string& op = node.name;
            if (op == "+") return addValues(a, b);
            if (op == "-") return subValues(a, b);
            if (op == "*") return mulValues(a, b);
            if (op == "/") return divValues(a, b);
            if (op == "^") return powValues(a, b);
            if (op == "<" || op == ">" || op == "<=" || op == ">=" ||
                op == "==" || op == "!=") {
                return compareValues(a, b, op);
            }
            if (op == "&&") return logicalValues(a, b, true);
            if (op == "||") return logicalValues(a, b, false);
            throw std::runtime_error("未知运算符: " + op);
        }
        case Node::Type::Ternary: {
            Value cond = evalNode(*node.args[0], ctx);
            Value a = evalNode(*node.args[1], ctx);
            Value b = evalNode(*node.args[2], ctx);
            return selectValues(cond, a, b);
        }
        case Node::Type::Call:
            return evalCall(node, ctx);
    }
    throw std::runtime_error("非法 AST 节点");
}

}  // namespace

// ============================ Expression ============================

struct Expression::Impl {
    NodePtr root;
    std::string error;
};

Expression::Expression(const std::string& formula) : impl_(std::make_unique<Impl>()) {
    try {
        Lexer lexer(formula);
        Parser parser(lexer.tokenize());
        impl_->root = parser.parse();
    } catch (const std::exception& e) {
        impl_->error = e.what();
        impl_->root.reset();
    }
}

Expression::~Expression() = default;
Expression::Expression(Expression&&) noexcept = default;
Expression& Expression::operator=(Expression&&) noexcept = default;

bool Expression::valid() const { return impl_ && impl_->root != nullptr; }

const std::string& Expression::error() const { return impl_->error; }

std::vector<double> Expression::evaluateLastRow(const PanelData& panel) const {
    if (!valid() || panel.empty()) return {};
    try {
        EvalCtx ctx(panel);
        Value v = evalNode(*impl_->root, ctx);
        const size_t n = panel.cols();
        std::vector<double> out(n, kNaN);
        if (v.isScalar()) {
            std::fill(out.begin(), out.end(), v.scalar);
        } else if (v.isSeries()) {
            const double last = v.data.empty() ? kNaN : v.data.back();
            std::fill(out.begin(), out.end(), last);
        } else if (v.rows > 0) {
            const size_t row = v.rows - 1;
            for (size_t i = 0; i < n && i < v.cols; ++i) {
                out[i] = v.data[row * v.cols + i];
            }
        }
        return out;
    } catch (const std::exception&) {
        return {};
    }
}

} // namespace st::factors::alpha191
