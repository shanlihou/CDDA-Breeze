#pragma once
#ifndef CATA_SRC_MATH_EXP_H
#define CATA_SRC_MATH_EXP_H

#include <memory>
#include <string>

class JsonValue;
struct dialogue;

/**
 * 0.H 风格的 math 表达式引擎。
 *
 * 这是为「按 0.H 内核编写的数据 mod」提供的兼容层。0.G 只有 `arithmetic`：
 * 操作数是 JSON 对象、不支持任意嵌套表达式；0.H 的 `math` 则是真正的一门
 * 表达式语言。这里实现后者，让 0.H 系 mod（如 hentai-r18-h）无需改动即可加载。
 *
 * 支持的语法：
 *   - 字面量：整数、浮点、单引号字符串
 *   - 变量：裸标识符，读写 0.G 的全局对话变量（var_type::global）
 *   - 运算符：+ - * / %  ( )  < <= > >= == !=  ! && ||
 *   - 三元：cond ? a : b
 *   - 赋值：= += -= *= /= %= 以及 ++ / --
 *   - 内建函数：
 *       u_val / n_val / u_vitamin / n_vitamin / u_skill / n_skill
 *       u_effect_intensity / n_effect_intensity / u_hp / n_hp
 *       u_coverage / n_coverage / u_has_trait / n_has_trait
 *       rand / rng / max / min / floor / ceil / abs / round / sqrt / pow
 *       time / time_since / distance / num_input
 *
 * JSON 形态（数组各元素用空格拼接后按完整表达式解析）：
 *   "math": [ "expr" ]                     求值 / 真值判定
 *   "math": [ "lvalue", "op", "expr" ]      赋值或比较
 *   "math": [ "lvalue", "++" ]              自增
 */
class math_exp
{
    public:
        math_exp() = default;
        explicit math_exp( const std::string &expr );
        math_exp( const math_exp & ) = default;
        math_exp( math_exp && ) = default;
        math_exp &operator=( const math_exp & ) = default;
        math_exp &operator=( math_exp && ) = default;
        ~math_exp() = default;

        /// 从 JSON 成员构造；既接受字符串也接受数组
        static math_exp from_json( const JsonValue &jv );
        /// 把 JSON 成员（字符串 / 数组）拼成表达式文本
        static std::string to_expr_string( const JsonValue &jv );

        /// 表达式是否解析成功
        bool is_valid() const {
            return root_ != nullptr;
        }
        /// 是否含赋值运算（= += -= *= /= %= ++ --）
        bool has_assignment() const;
        /// 表达式原文，便于报错与缓存
        const std::string &source() const {
            return src_;
        }

        /// 求值。含赋值时顺带写入
        double evaluate( const dialogue &d ) const;
        /// 真值判定：非 0 为真
        bool evaluate_bool( const dialogue &d ) const;

        /// 内部 AST；定义在 .cpp，此处仅作不透明句柄
        struct ast;

    private:
        std::shared_ptr<const ast> root_;
        std::string src_;
};

#endif // CATA_SRC_MATH_EXP_H
