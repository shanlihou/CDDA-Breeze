#include "math_exp.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "bodypart.h"
#include "calendar.h"
#include "character.h"
#include "condition.h"
#include "debug.h"
#include "dialogue.h"
#include "dialogue_helpers.h"
#include "global_vars.h"
#include "json.h"
#include "json_error.h"
#include "json_loader.h"
#include "output.h"
#include "rng.h"
#include "string_input_popup.h"
#include "talker.h"
#include "translations.h"
#include "type_id.h"

// ============================================================================
// 取值器：把 0.G 的「值」抽象成可读可写的 get/set 一对
// ============================================================================

namespace
{

struct value_access {
    std::function<double( const dialogue & )> get;
    std::function<void( const dialogue &, double )> set;
};

using value_access_ptr = std::shared_ptr<value_access>;

std::string json_escape( const std::string &s )
{
    std::string out;
    out.reserve( s.size() + 4 );
    for( const char c : s ) {
        if( c == '"' || c == '\\' ) {
            out.push_back( '\\' );
        }
        out.push_back( c );
    }
    return out;
}

/// 把整数存成不带小数点的字符串，便于人工看存档
std::string num_to_string( const double v )
{
    if( std::abs( v - std::round( v ) ) < 1e-9 ) {
        return std::to_string( static_cast<long long>( std::llround( v ) ) );
    }
    return std::to_string( v );
}

double string_to_num( const std::string &s )
{
    if( s.empty() ) {
        return 0.0;
    }
    try {
        return std::stod( s );
    } catch( ... ) {
        return 0.0;
    }
}

std::string trim_copy( const std::string &s )
{
    size_t b = 0;
    size_t e = s.size();
    while( b < e && std::isspace( static_cast<unsigned char>( s[b] ) ) ) {
        ++b;
    }
    while( e > b && std::isspace( static_cast<unsigned char>( s[e - 1] ) ) ) {
        --e;
    }
    return s.substr( b, e - b );
}

// ---------------------------------------------------------------------------
// 借助 0.G 已有的 u_val 取值系统（70 个键，可读可写）实现绝大多数内建函数
// ---------------------------------------------------------------------------

/// 用一段 JSON 文本构造取值器。
/// 读、写分别编译：0.G 里有些键只能读不能写（例如 effect_intensity），
/// 读那部分照样要能用，所以不能因为写侧失败就整体放弃。
value_access_ptr make_access_from_json( const std::string &json_body )
{
    value_access_ptr acc = std::make_shared<value_access>();
    bool any = false;
    try {
        JsonValue jv = json_loader::from_string( json_body );
        JsonObject jo = jv.get_object();
        std::function<int( const dialogue & )> g =
            conditional_t<dialogue>::get_get_int( jo );
        acc->get = [g]( const dialogue & d ) {
            return static_cast<double>( g( d ) );
        };
        any = true;
    } catch( const JsonError & ) {
        // 该键 0.G 读不了，保持 get 为空
    }
    try {
        JsonValue jv = json_loader::from_string( json_body );
        JsonObject jo = jv.get_object();
        std::function<void( const dialogue &, int )> s =
            get_set_int<dialogue>( jo, std::nullopt, std::nullopt, false );
        acc->set = [s]( const dialogue & d, const double v ) {
            s( d, static_cast<int>( v ) );
        };
        any = true;
    } catch( const JsonError & ) {
        // 该键 0.G 写不了，赋值时会报「左侧不可赋值」
    }
    return any ? acc : nullptr;
}

/// u_hp('ALL') / n_hp('torso')：0.G 没有对应的 u_val 键，单独实现
value_access_ptr make_hp_access( const std::string &key, const bool is_npc )
{
    const bool all = key.empty() || key == "ALL" || key == "all";
    std::optional<bodypart_id> bp;
    if( !all ) {
        bp = bodypart_id( key );
    }
    value_access_ptr acc = std::make_shared<value_access>();
    acc->get = [is_npc, bp]( const dialogue & d ) -> double {
        if( bp.has_value() ) {
            return d.actor( is_npc )->get_cur_hp( *bp );
        }
        const Character *ch = d.actor( is_npc )->get_character();
        return ch ? ch->get_hp() : 0;
    };
    acc->set = [is_npc, bp]( const dialogue & d, const double v ) {
        const int target = static_cast<int>( v );
        if( bp.has_value() ) {
            d.actor( is_npc )->set_part_hp_cur( *bp, target );
            return;
        }
        Character *ch = d.actor( is_npc )->get_character();
        if( !ch ) {
            return;
        }
        const int before = ch->get_hp();
        if( target > before ) {
            ch->healall( target - before );
        } else if( target < before ) {
            const std::vector<bodypart_id> parts = ch->get_all_body_parts();
            if( parts.empty() ) {
                return;
            }
            const int each = ( target - before ) / static_cast<int>( parts.size() );
            for( const bodypart_id &p : parts ) {
                ch->mod_part_hp_cur( p, each );
            }
        }
    };
    return acc;
}

/// u_coverage('torso')：护甲覆盖率
value_access_ptr make_coverage_access( const std::string &key, const bool is_npc )
{
    const bodypart_id bp( key );
    value_access_ptr acc = std::make_shared<value_access>();
    acc->get = [is_npc, bp]( const dialogue & d ) -> double {
        const talker *actor = d.actor( is_npc );
        if( const Character *ch = actor->get_character() ) {
            return ch->worn.get_coverage( bp );
        }
        // 0.H 兼容层：u_run_inv_eocs 会把「物品」当作 talker，此时取物品自身的覆盖率
        if( const item_location *loc = actor->get_item() ) {
            if( *loc ) {
                return loc->get_item()->get_coverage( bp );
            }
        }
        return 0;
    };
    return acc;
}

/// 需要按「取值名」预先编译好 get/set 的函数
value_access_ptr resolve_value_access( const std::string &fname, const std::string &key )
{
    const bool is_npc = fname.rfind( "n_", 0 ) == 0 || fname.rfind( "npc_", 0 ) == 0;
    const std::string scope = is_npc ? "npc_val" : "u_val";

    if( fname == "u_val" || fname == "n_val" || fname == "npc_val" ) {
        return make_access_from_json( "{\"" + scope + "\":\"" + json_escape( key ) + "\"}" );
    }
    if( fname == "u_vitamin" || fname == "n_vitamin" || fname == "npc_vitamin" ) {
        return make_access_from_json( "{\"" + scope + "\":\"vitamin\",\"name\":\"" +
                                      json_escape( key ) + "\"}" );
    }
    if( fname == "u_skill" || fname == "n_skill" || fname == "npc_skill" ) {
        return make_access_from_json( "{\"" + scope + "\":\"skill_level\",\"skill\":\"" +
                                      json_escape( key ) + "\"}" );
    }
    if( fname == "u_effect_intensity" || fname == "n_effect_intensity" ) {
        return make_access_from_json( "{\"" + scope + "\":\"effect_intensity\",\"effect\":\"" +
                                      json_escape( key ) + "\"}" );
    }
    if( fname == "u_hp" || fname == "n_hp" || fname == "npc_hp" ) {
        return make_hp_access( key, is_npc );
    }
    if( fname == "u_coverage" || fname == "n_coverage" || fname == "npc_coverage" ) {
        return make_coverage_access( key, is_npc );
    }
    return nullptr;
}

/// 首参是字符串字面量、且是「取值函数」的名字
bool is_value_function( const std::string &fname )
{
    static const std::vector<std::string> names = {
        "u_val", "n_val", "npc_val",
        "u_vitamin", "n_vitamin", "npc_vitamin",
        "u_skill", "n_skill", "npc_skill",
        "u_effect_intensity", "n_effect_intensity",
        "u_hp", "n_hp", "npc_hp",
        "u_coverage", "n_coverage", "npc_coverage"
    };
    return std::find( names.begin(), names.end(), fname ) != names.end();
}

// ============================================================================
// 词法分析
// ============================================================================

struct token {
    enum class type { num, str, ident, op, end } t = type::end;
    double num = 0.0;
    std::string s;
};

std::vector<token> tokenize( const std::string &src )
{
    std::vector<token> out;
    const size_t n = src.size();
    size_t i = 0;
    auto is_digit = []( const char c ) {
        return std::isdigit( static_cast<unsigned char>( c ) ) != 0;
    };
    while( i < n ) {
        const char c = src[i];
        if( std::isspace( static_cast<unsigned char>( c ) ) ) {
            ++i;
            continue;
        }
        // 数字（含小数与科学计数）
        if( is_digit( c ) || ( c == '.' && i + 1 < n && is_digit( src[i + 1] ) ) ) {
            size_t j = i;
            bool dot = false;
            bool exp = false;
            while( j < n ) {
                const char d = src[j];
                if( is_digit( d ) ) {
                    ++j;
                    continue;
                }
                if( d == '.' && !dot && !exp ) {
                    dot = true;
                    ++j;
                    continue;
                }
                if( ( d == 'e' || d == 'E' ) && !exp && j + 1 < n &&
                    ( is_digit( src[j + 1] ) || ( ( src[j + 1] == '+' || src[j + 1] == '-' ) &&
                                                  j + 2 < n && is_digit( src[j + 2] ) ) ) ) {
                    exp = true;
                    j += is_digit( src[j + 1] ) ? 1 : 2;
                    continue;
                }
                break;
            }
            token tk;
            tk.t = token::type::num;
            try {
                tk.num = std::stod( src.substr( i, j - i ) );
            } catch( ... ) {
                tk.num = 0.0;
            }
            out.push_back( tk );
            i = j;
            continue;
        }
        // 单引号字符串
        if( c == '\'' ) {
            size_t j = i + 1;
            std::string lit;
            while( j < n && src[j] != '\'' ) {
                if( src[j] == '\\' && j + 1 < n ) {
                    lit.push_back( src[j + 1] );
                    j += 2;
                    continue;
                }
                lit.push_back( src[j] );
                ++j;
            }
            token tk;
            tk.t = token::type::str;
            tk.s = lit;
            out.push_back( tk );
            i = j < n ? j + 1 : n;
            continue;
        }
        // 标识符
        if( std::isalpha( static_cast<unsigned char>( c ) ) || c == '_' ) {
            size_t j = i;
            while( j < n && ( std::isalnum( static_cast<unsigned char>( src[j] ) ) ||
                              src[j] == '_' ) ) {
                ++j;
            }
            token tk;
            tk.t = token::type::ident;
            tk.s = src.substr( i, j - i );
            out.push_back( tk );
            i = j;
            continue;
        }
        // 运算符：长匹配优先
        static const std::vector<std::string> ops = {
            "<=", ">=", "==", "!=", "&&", "||", "++", "--", "+=", "-=", "*=", "/=", "%=",
            "+", "-", "*", "/", "%", "(", ")", "?", ":", ",", "<", ">", "!", "="
        };
        bool matched = false;
        for( const std::string &op : ops ) {
            if( src.compare( i, op.size(), op ) == 0 ) {
                token tk;
                tk.t = token::type::op;
                tk.s = op;
                out.push_back( tk );
                i += op.size();
                matched = true;
                break;
            }
        }
        if( !matched ) {
            ++i; // 无法识别的字符直接跳过
        }
    }
    out.push_back( token() );
    return out;
}

} // namespace

// ============================================================================
// 语法树
// ============================================================================

struct math_exp::ast {
    enum class kind {
        num, str, ident, unary, binary, ternary, call, assign, incdec
    };
    kind k = kind::num;
    double num = 0.0;
    std::string text;
    std::vector<std::shared_ptr<ast>> kids;
    /// 仅 call：预先编译好的 0.G 取值器
    std::shared_ptr<value_access> access;
};

namespace
{

using ast_ptr = std::shared_ptr<math_exp::ast>;
using c_ast_ptr = std::shared_ptr<const math_exp::ast>;

ast_ptr make_node( const math_exp::ast::kind k )
{
    ast_ptr n = std::make_shared<math_exp::ast>();
    n->k = k;
    return n;
}

class parser
{
    public:
        parser( const std::string &src, const std::vector<token> &toks )
            : src_( src ), toks_( toks ) {}

        ast_ptr run() {
            ast_ptr n = parse_assign();
            if( peek().t != token::type::end ) {
                fail( "表达式尾部有多余内容" );
                return nullptr;
            }
            return n;
        }

    private:
        const std::string &src_;
        const std::vector<token> &toks_;
        size_t pos_ = 0;
        bool failed_ = false;

        const token &peek( const size_t ahead = 0 ) const {
            const size_t idx = pos_ + ahead;
            return idx < toks_.size() ? toks_[idx] : toks_.back();
        }
        bool is_op( const std::string &s, const size_t ahead = 0 ) const {
            return peek( ahead ).t == token::type::op && peek( ahead ).s == s;
        }
        bool is_ident( const size_t ahead = 0 ) const {
            return peek( ahead ).t == token::type::ident;
        }
        bool accept( const std::string &s ) {
            if( is_op( s ) ) {
                ++pos_;
                return true;
            }
            return false;
        }
        void fail( const std::string &what ) {
            if( !failed_ ) {
                failed_ = true;
                debugmsg( "math 表达式解析失败(%s): %s", what, src_ );
            }
        }

        ast_ptr parse_assign() {
            ast_ptr lhs = parse_ternary();
            if( lhs == nullptr ) {
                return nullptr;
            }
            static const std::vector<std::string> assign_ops = {
                "=", "+=", "-=", "*=", "/=", "%="
            };
            if( peek().t == token::type::op &&
                std::find( assign_ops.begin(), assign_ops.end(), peek().s ) != assign_ops.end() ) {
                const std::string op = peek().s;
                ++pos_;
                ast_ptr rhs = parse_assign();
                if( rhs == nullptr ) {
                    return nullptr;
                }
                ast_ptr n = make_node( math_exp::ast::kind::assign );
                n->text = op;
                n->kids.push_back( lhs );
                n->kids.push_back( rhs );
                return n;
            }
            return lhs;
        }

        ast_ptr parse_ternary() {
            ast_ptr cond = parse_logic_or();
            if( cond == nullptr ) {
                return nullptr;
            }
            if( accept( "?" ) ) {
                ast_ptr yes = parse_assign();
                if( !accept( ":" ) ) {
                    fail( "三元表达式缺少 ':'" );
                    return nullptr;
                }
                ast_ptr no = parse_assign();
                if( yes == nullptr || no == nullptr ) {
                    return nullptr;
                }
                ast_ptr n = make_node( math_exp::ast::kind::ternary );
                n->kids = { cond, yes, no };
                return n;
            }
            return cond;
        }

        ast_ptr parse_logic_or() {
            ast_ptr n = parse_logic_and();
            while( n != nullptr && is_op( "||" ) ) {
                ++pos_;
                ast_ptr rhs = parse_logic_and();
                if( rhs == nullptr ) {
                    return nullptr;
                }
                ast_ptr b = make_node( math_exp::ast::kind::binary );
                b->text = "||";
                b->kids = { n, rhs };
                n = b;
            }
            return n;
        }

        ast_ptr parse_logic_and() {
            ast_ptr n = parse_comparison();
            while( n != nullptr && is_op( "&&" ) ) {
                ++pos_;
                ast_ptr rhs = parse_comparison();
                if( rhs == nullptr ) {
                    return nullptr;
                }
                ast_ptr b = make_node( math_exp::ast::kind::binary );
                b->text = "&&";
                b->kids = { n, rhs };
                n = b;
            }
            return n;
        }

        ast_ptr parse_comparison() {
            ast_ptr n = parse_additive();
            static const std::vector<std::string> cmps = {
                "<=", ">=", "==", "!=", "<", ">"
            };
            while( n != nullptr && peek().t == token::type::op &&
                   std::find( cmps.begin(), cmps.end(), peek().s ) != cmps.end() ) {
                const std::string op = peek().s;
                ++pos_;
                ast_ptr rhs = parse_additive();
                if( rhs == nullptr ) {
                    return nullptr;
                }
                ast_ptr b = make_node( math_exp::ast::kind::binary );
                b->text = op;
                b->kids = { n, rhs };
                n = b;
            }
            return n;
        }

        ast_ptr parse_additive() {
            ast_ptr n = parse_multiplicative();
            while( n != nullptr && ( is_op( "+" ) || is_op( "-" ) ) ) {
                const std::string op = peek().s;
                ++pos_;
                ast_ptr rhs = parse_multiplicative();
                if( rhs == nullptr ) {
                    return nullptr;
                }
                ast_ptr b = make_node( math_exp::ast::kind::binary );
                b->text = op;
                b->kids = { n, rhs };
                n = b;
            }
            return n;
        }

        ast_ptr parse_multiplicative() {
            ast_ptr n = parse_unary();
            while( n != nullptr && ( is_op( "*" ) || is_op( "/" ) || is_op( "%" ) ) ) {
                const std::string op = peek().s;
                ++pos_;
                ast_ptr rhs = parse_unary();
                if( rhs == nullptr ) {
                    return nullptr;
                }
                ast_ptr b = make_node( math_exp::ast::kind::binary );
                b->text = op;
                b->kids = { n, rhs };
                n = b;
            }
            return n;
        }

        ast_ptr parse_unary() {
            if( is_op( "-" ) || is_op( "+" ) || is_op( "!" ) ) {
                const std::string op = peek().s;
                ++pos_;
                ast_ptr operand = parse_unary();
                if( operand == nullptr ) {
                    return nullptr;
                }
                ast_ptr n = make_node( math_exp::ast::kind::unary );
                n->text = op;
                n->kids.push_back( operand );
                return n;
            }
            return parse_postfix();
        }

        ast_ptr parse_postfix() {
            ast_ptr n = parse_primary();
            while( n != nullptr && ( is_op( "++" ) || is_op( "--" ) ) ) {
                const std::string op = peek().s;
                ++pos_;
                ast_ptr i = make_node( math_exp::ast::kind::incdec );
                i->text = op;
                i->kids.push_back( n );
                n = i;
            }
            return n;
        }

        ast_ptr parse_primary() {
            const token &tk = peek();
            if( tk.t == token::type::num ) {
                ++pos_;
                ast_ptr n = make_node( math_exp::ast::kind::num );
                n->num = tk.num;
                return n;
            }
            if( tk.t == token::type::str ) {
                ++pos_;
                ast_ptr n = make_node( math_exp::ast::kind::str );
                n->text = tk.s;
                return n;
            }
            if( tk.t == token::type::ident ) {
                const std::string name = tk.s;
                ++pos_;
                if( is_op( "(" ) ) {
                    return parse_call( name );
                }
                ast_ptr n = make_node( math_exp::ast::kind::ident );
                n->text = name;
                return n;
            }
            if( is_op( "(" ) ) {
                ++pos_;
                ast_ptr inner = parse_assign();
                if( !accept( ")" ) ) {
                    fail( "括号未闭合" );
                    return nullptr;
                }
                return inner;
            }
            fail( "无法识别的记号" );
            return nullptr;
        }

        ast_ptr parse_call( const std::string &name ) {
            if( !accept( "(" ) ) {
                return nullptr;
            }
            ast_ptr n = make_node( math_exp::ast::kind::call );
            n->text = name;
            if( !is_op( ")" ) ) {
                while( true ) {
                    ast_ptr arg = parse_assign();
                    if( arg == nullptr ) {
                        return nullptr;
                    }
                    n->kids.push_back( arg );
                    if( accept( "," ) ) {
                        continue;
                    }
                    break;
                }
            }
            if( !accept( ")" ) ) {
                fail( "函数调用缺少 ')'" );
                return nullptr;
            }
            // 取值函数：首参是字符串字面量时，在加载期就把 0.G 的取值器编译好
            if( is_value_function( name ) && !n->kids.empty() &&
                n->kids[0]->k == math_exp::ast::kind::str ) {
                n->access = resolve_value_access( name, n->kids[0]->text );
                if( n->access == nullptr ) {
                    debugmsg( "math: %s('%s') 不是 0.G 能识别的取值名", name,
                              n->kids[0]->text );
                }
            }
            return n;
        }
};

// ============================================================================
// 求值
// ============================================================================

/// 0.H math 的标识符作用域规则：
///   裸名      -> 全局对话变量
///   u_xxx     -> alpha talker（玩家）上的变量
///   n_xxx     -> beta talker（NPC/怪物）上的变量
///   _xxx      -> context_val 上下文变量
///   v_xxx     -> var_val 间接引用
static var_info make_math_var_info( const std::string &token )
{
    if( token.size() > 1 && token[0] == '_' ) {
        return var_info( var_type::context, token.substr( 1 ) );
    }
    if( token.size() > 2 && token.compare( 0, 2, "u_" ) == 0 ) {
        return var_info( var_type::u, token.substr( 2 ) );
    }
    if( token.size() > 2 && token.compare( 0, 2, "n_" ) == 0 ) {
        return var_info( var_type::npc, token.substr( 2 ) );
    }
    if( token.size() > 2 && token.compare( 0, 2, "g_" ) == 0 ) {
        return var_info( var_type::global, token.substr( 2 ) );
    }
    if( token.size() > 2 && token.compare( 0, 2, "v_" ) == 0 ) {
        var_info info( var_type::global, token.substr( 2 ) );
        info.indirect = true;
        return info;
    }
    return var_info( var_type::global, token );
}

double read_variable( const std::string &name, const dialogue &d )
{
    return string_to_num( read_var_value( make_math_var_info( name ), d ) );
}

void write_variable( const std::string &name, const double v, const dialogue &d )
{
    const var_info info = make_math_var_info( name );
    talker *tk = nullptr;
    if( info.type == var_type::u || info.type == var_type::npc ) {
        tk = d.actor( info.type == var_type::npc );
    }
    write_var_value( info.type, info.name, tk, num_to_string( v ), &d, info.indirect );
}

/// 求字符串型实参（通常来自字符串字面量）
std::string arg_string( const c_ast_ptr &n, const dialogue &d );

double eval_node( const c_ast_ptr &n, const dialogue &d );

/// 读取赋值目标当前值
double read_target( const c_ast_ptr &n, const dialogue &d )
{
    if( n == nullptr ) {
        return 0.0;
    }
    if( n->k == math_exp::ast::kind::ident ) {
        return read_variable( n->text, d );
    }
    if( n->k == math_exp::ast::kind::call && n->access ) {
        return n->access->get( d );
    }
    return eval_node( n, d );
}

/// 写入赋值目标
void write_target( const ast_ptr &n, const double v, const dialogue &d )
{
    if( n == nullptr ) {
        return;
    }
    if( n->k == math_exp::ast::kind::ident ) {
        write_variable( n->text, v, d );
        return;
    }
    if( n->k == math_exp::ast::kind::call && n->access && n->access->set ) {
        n->access->set( d, v );
        return;
    }
    debugmsg( "math: 表达式左侧不可赋值" );
}

std::string arg_string( const c_ast_ptr &n, const dialogue &d )
{
    if( n == nullptr ) {
        return std::string();
    }
    if( n->k == math_exp::ast::kind::str ) {
        return n->text;
    }
    if( n->k == math_exp::ast::kind::ident ) {
        return n->text;
    }
    return num_to_string( eval_node( n, d ) );
}

/// time('1 d') / time('3 h') / time('now') -> 回合数
double eval_time_string( const std::string &raw )
{
    const std::string s = trim_copy( raw );
    if( s == "now" ) {
        return static_cast<double>( to_turn<int>( calendar::turn ) );
    }
    size_t i = 0;
    const size_t n = s.size();
    while( i < n && ( std::isdigit( static_cast<unsigned char>( s[i] ) ) || s[i] == '.' ||
                      s[i] == '-' || s[i] == '+' ) ) {
        ++i;
    }
    const double amount = string_to_num( s.substr( 0, i ) );
    const std::string unit = trim_copy( s.substr( i ) );
    if( unit.empty() ) {
        return amount; // 默认按回合
    }
    for( const auto &pair : time_duration::units ) {
        if( pair.first == unit ) {
            return amount * static_cast<double>( to_turns<int>( pair.second ) );
        }
    }
    return amount;
}

double eval_call( const c_ast_ptr &n, const dialogue &d )
{
    const std::string &f = n->text;
    const size_t argc = n->kids.size();
    if( n->access ) {
        return n->access->get( d );
    }

    auto arg = [&]( const size_t idx ) -> double {
        return idx < argc ? eval_node( n->kids[idx], d ) : 0.0;
    };

    if( f == "rand" ) {
        return static_cast<double>( rng( 0, std::max( 0, static_cast<int>( arg( 0 ) ) ) ) );
    }
    if( f == "rng" || f == "random" ) {
        return static_cast<double>( rng( static_cast<int>( arg( 0 ) ),
                                         static_cast<int>( arg( 1 ) ) ) );
    }
    if( f == "max" ) {
        double r = arg( 0 );
        for( size_t i = 1; i < argc; ++i ) {
            r = std::max( r, arg( i ) );
        }
        return r;
    }
    if( f == "min" ) {
        double r = arg( 0 );
        for( size_t i = 1; i < argc; ++i ) {
            r = std::min( r, arg( i ) );
        }
        return r;
    }
    if( f == "floor" ) {
        return std::floor( arg( 0 ) );
    }
    if( f == "ceil" || f == "ceiling" ) {
        return std::ceil( arg( 0 ) );
    }
    if( f == "round" ) {
        return std::round( arg( 0 ) );
    }
    if( f == "abs" ) {
        return std::fabs( arg( 0 ) );
    }
    if( f == "sqrt" ) {
        return std::sqrt( arg( 0 ) );
    }
    if( f == "pow" ) {
        return std::pow( arg( 0 ), arg( 1 ) );
    }
    if( f == "time" ) {
        return eval_time_string( arg_string( n->kids.empty() ? nullptr : n->kids[0], d ) );
    }
    if( f == "time_since" ) {
        const std::string name = arg_string( n->kids.empty() ? nullptr : n->kids[0], d );
        const double stamp = read_variable( name, d );
        return static_cast<double>( to_turn<int>( calendar::turn ) ) - stamp;
    }
    if( f == "distance" ) {
        const std::string a = n->kids.empty() ? "u" : arg_string( n->kids[0], d );
        const std::string b = argc > 1 ? arg_string( n->kids[1], d ) : "npc";
        talker *ta = d.actor( a != "u" && a != "alpha" );
        talker *tb = d.actor( b != "u" && b != "alpha" );
        if( ta == nullptr || tb == nullptr ) {
            return 0.0;
        }
        return std::max( std::abs( ta->posx() - tb->posx() ),
                         std::abs( ta->posy() - tb->posy() ) );
    }
    if( f == "u_has_trait" || f == "n_has_trait" || f == "npc_has_trait" ||
        f == "has_trait" ) {
        const bool is_npc = f.compare( 0, 2, "n_" ) == 0 || f.compare( 0, 4, "npc_" ) == 0;
        const std::string tid = arg_string( n->kids.empty() ? nullptr : n->kids[0], d );
        return d.actor( is_npc )->has_trait( trait_id( tid ) ) ? 1.0 : 0.0;
    }
    if( f == "num_input" ) {
        const std::string prompt = n->kids.empty() ? std::string() : arg_string( n->kids[0], d );
        const int def = argc > 1 ? static_cast<int>( arg( 1 ) ) : 0;
        string_input_popup pop;
        pop.title( _( "输入" ) ).description( prompt ).text( std::to_string( def ) ).width( 40 );
        return string_to_num( pop.query_string() );
    }

    debugmsg( "math: 未知函数 %s()", f );
    return 0.0;
}

double eval_node( const c_ast_ptr &n, const dialogue &d )
{
    if( n == nullptr ) {
        return 0.0;
    }
    switch( n->k ) {
        case math_exp::ast::kind::num:
            return n->num;
        case math_exp::ast::kind::str:
            return 0.0;
        case math_exp::ast::kind::ident:
            return read_variable( n->text, d );
        case math_exp::ast::kind::unary: {
            const double v = eval_node( n->kids[0], d );
            if( n->text == "-" ) {
                return -v;
            }
            if( n->text == "+" ) {
                return v;
            }
            if( n->text == "!" ) {
                return v == 0.0 ? 1.0 : 0.0;
            }
            return v;
        }
        case math_exp::ast::kind::binary: {
            // 逻辑运算短路
            if( n->text == "&&" ) {
                return eval_node( n->kids[0], d ) != 0.0 &&
                       eval_node( n->kids[1], d ) != 0.0 ? 1.0 : 0.0;
            }
            if( n->text == "||" ) {
                return eval_node( n->kids[0], d ) != 0.0 ||
                       eval_node( n->kids[1], d ) != 0.0 ? 1.0 : 0.0;
            }
            const double a = eval_node( n->kids[0], d );
            const double b = eval_node( n->kids[1], d );
            const std::string &op = n->text;
            if( op == "+" ) {
                return a + b;
            }
            if( op == "-" ) {
                return a - b;
            }
            if( op == "*" ) {
                return a * b;
            }
            if( op == "/" ) {
                return b == 0.0 ? 0.0 : a / b;
            }
            if( op == "%" ) {
                return b == 0.0 ? 0.0 :
                       static_cast<double>( static_cast<long long>( a ) %
                                            static_cast<long long>( b ) );
            }
            if( op == "<" ) {
                return a < b ? 1.0 : 0.0;
            }
            if( op == "<=" ) {
                return a <= b ? 1.0 : 0.0;
            }
            if( op == ">" ) {
                return a > b ? 1.0 : 0.0;
            }
            if( op == ">=" ) {
                return a >= b ? 1.0 : 0.0;
            }
            if( op == "==" ) {
                return std::abs( a - b ) < 1e-9 ? 1.0 : 0.0;
            }
            if( op == "!=" ) {
                return std::abs( a - b ) >= 1e-9 ? 1.0 : 0.0;
            }
            return 0.0;
        }
        case math_exp::ast::kind::ternary:
            return eval_node( n->kids[0], d ) != 0.0 ? eval_node( n->kids[1], d )
                   : eval_node( n->kids[2], d );
        case math_exp::ast::kind::call:
            return eval_call( n, d );
        case math_exp::ast::kind::assign: {
            const double rhs = eval_node( n->kids[1], d );
            double result = rhs;
            const std::string &op = n->text;
            if( op != "=" ) {
                const double cur = read_target( n->kids[0], d );
                if( op == "+=" ) {
                    result = cur + rhs;
                } else if( op == "-=" ) {
                    result = cur - rhs;
                } else if( op == "*=" ) {
                    result = cur * rhs;
                } else if( op == "/=" ) {
                    result = rhs == 0.0 ? 0.0 : cur / rhs;
                } else if( op == "%=" ) {
                    result = rhs == 0.0 ? 0.0 :
                             static_cast<double>( static_cast<long long>( cur ) %
                                                  static_cast<long long>( rhs ) );
                }
            }
            write_target( n->kids[0], result, d );
            return result;
        }
        case math_exp::ast::kind::incdec: {
            const double cur = read_target( n->kids[0], d );
            const double result = n->text == "++" ? cur + 1.0 : cur - 1.0;
            write_target( n->kids[0], result, d );
            return result;
        }
    }
    return 0.0;
}

bool node_has_assignment( const c_ast_ptr &n )
{
    if( n == nullptr ) {
        return false;
    }
    if( n->k == math_exp::ast::kind::assign || n->k == math_exp::ast::kind::incdec ) {
        return true;
    }
    for( const ast_ptr &kid : n->kids ) {
        if( node_has_assignment( kid ) ) {
            return true;
        }
    }
    return false;
}

} // namespace

// ============================================================================
// math_exp 对外接口
// ============================================================================

std::string math_exp::to_expr_string( const JsonValue &jv )
{
    if( jv.test_string() ) {
        return jv.get_string();
    }
    if( jv.test_int() ) {
        return std::to_string( jv.get_int() );
    }
    if( jv.test_float() ) {
        return num_to_string( jv.get_float() );
    }
    if( jv.test_array() ) {
        std::string out;
        for( const JsonValue entry : jv.get_array() ) {
            const std::string part = to_expr_string( entry );
            if( part.empty() ) {
                continue;
            }
            if( !out.empty() ) {
                out += " ";
            }
            out += part;
        }
        return out;
    }
    return std::string();
}

math_exp::math_exp( const std::string &expr ) : src_( expr )
{
    const std::string text = trim_copy( expr );
    if( text.empty() ) {
        return;
    }
    const std::vector<token> toks = tokenize( text );
    parser p( src_, toks );
    root_ = p.run();
}

math_exp math_exp::from_json( const JsonValue &jv )
{
    return math_exp( to_expr_string( jv ) );
}

bool math_exp::has_assignment() const
{
    return node_has_assignment( root_ );
}

double math_exp::evaluate( const dialogue &d ) const
{
    if( root_ == nullptr ) {
        return 0.0;
    }
    return eval_node( root_, d );
}

bool math_exp::evaluate_bool( const dialogue &d ) const
{
    return evaluate( d ) != 0.0;
}
