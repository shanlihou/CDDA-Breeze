#pragma once
#ifndef CATA_SRC_CONDITION_H
#define CATA_SRC_CONDITION_H

#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <unordered_set>

#include "dialogue.h"
#include "dialogue_helpers.h"
#include "mission.h"

class JsonObject;
namespace dialogue_data
{
// when updating this, please also update `dynamic_line_string_keys` in
// `lang/extract_json_string.py` so the lines are properly extracted for translation
const std::unordered_set<std::string> simple_string_conds = { {
        "u_male", "u_female", "npc_male", "npc_female",
        "has_no_assigned_mission", "has_assigned_mission", "has_many_assigned_missions",
        "has_no_available_mission", "has_available_mission", "has_many_available_missions",
        "mission_complete", "mission_incomplete", "mission_has_generic_rewards",
        "npc_available", "npc_following", "npc_friend", "npc_hostile",
        "npc_train_skills", "npc_train_styles", "npc_train_spells",
        "at_safe_space", "is_day", "npc_has_activity", "is_outside", "u_is_outside", "npc_is_outside", "u_has_camp",
        "u_can_stow_weapon", "npc_can_stow_weapon", "u_can_drop_weapon", "npc_can_drop_weapon", "u_has_weapon", "npc_has_weapon",
        "u_driving", "npc_driving",
        "has_pickup_list", "is_by_radio", "has_reason"
    }
};
const std::unordered_set<std::string> complex_conds = { {
        "u_has_any_trait", "npc_has_any_trait", "u_has_trait", "npc_has_trait",
        "u_has_flag", "npc_has_flag", "npc_has_class", "u_has_mission", "u_monsters_in_direction", "u_safe_mode_trigger",
        "u_has_strength", "npc_has_strength", "u_has_dexterity", "npc_has_dexterity",
        "u_has_intelligence", "npc_has_intelligence", "u_has_perception", "npc_has_perception",
        "u_is_wearing", "npc_is_wearing", "u_has_item", "npc_has_item", "u_has_move_mode", "npc_has_move_mode",
        "u_has_items", "npc_has_items", "u_has_item_category", "npc_has_item_category",
        "u_has_bionics", "npc_has_bionics", "u_has_effect", "npc_has_effect", "u_need", "npc_need",
        // 0.H 兼容层新增的条件键（必须登记在此白名单里，否则会被判为 unrecognized condition）
        "u_has_any_effect", "npc_has_any_effect", "u_has_species", "npc_has_species",
        "expects_vars", "get_condition",
        "u_at_om_location", "u_near_om_location", "npc_at_om_location", "npc_near_om_location",
        "npc_role_nearby", "npc_allies", "npc_allies_global", "npc_service",
        "u_has_cash", "u_are_owed", "u_query", "npc_query", "u_has_item_with_flag", "npc_has_item_with_flag",
        "npc_aim_rule", "npc_engagement_rule", "npc_rule", "npc_override", "npc_personality",
        "npc_movement_policy", "u_has_hp", "npc_has_hp",
        "u_has_part_temp", "npc_has_part_temp", "npc_cbm_reserve_rule", "npc_cbm_recharge_rule", "u_has_faction_trust",
        "days_since_cataclysm", "is_season", "mission_goal", "u_has_var", "npc_has_var",
        // 0.H 兼容层：{"is_mid_autumn_holiday": true/false} —— 按现实日期判定中秋
        "is_mid_autumn_holiday",
        "u_has_skill", "npc_has_skill", "u_know_recipe", "u_compare_var", "npc_compare_var",
        "u_compare_time_since_var", "npc_compare_time_since_var", "is_weather", "mod_is_loaded", "one_in_chance", "x_in_y_chance",
        "is_temperature", "is_windpower", "is_humidity", "is_pressure", "u_is_height", "npc_is_height",
        "u_has_worn_with_flag", "npc_has_worn_with_flag", "u_has_wielded_with_flag", "npc_has_wielded_with_flag",
        "u_has_pain", "npc_has_pain", "u_has_power", "npc_has_power", "u_has_focus", "npc_has_focus", "u_has_morale",
        "npc_has_morale", "u_is_on_terrain", "npc_is_on_terrain", "u_is_in_field", "npc_is_in_field", "compare_int",
        "compare_string", "roll_contested" ,"u_joined_the_faction" ,"u_get_food_from_the_faction", "u_reported_work",
        // 0.H 兼容层新增的简单字符串条件
        "u_is_avatar", "npc_is_avatar", "u_is_character", "npc_is_character",
        "u_is_npc", "npc_is_npc", "player_see_u", "player_see_npc",
        // 0.H 兼容层的 math 表达式条件 / 动态行条件
        "math",
        // 0.H 兼容层：以下键除了要在条件构造函数里实现分支，还必须登记进这张白名单。
        // 因为构造函数结尾有一个 `if( !found_sub_member ) throw "unrecognized condition"`
        // 的二次校验，只靠分支命中是不会通过的。
        "u_has_any_effect", "npc_has_any_effect",
        "u_has_species", "npc_has_species",
        "expects_vars", "get_condition"
    }
};
} // namespace dialogue_data

template<class T>
static dialogue copy_dialogue( const T &d )
{
    Creature *creature_alpha = d.has_alpha ? d.actor( false )->get_creature() : nullptr;
    item_location *item_alpha = d.has_alpha ? d.actor( false )->get_item() : nullptr;
    Creature *creature_beta = d.has_beta ? d.actor( true )->get_creature() : nullptr;
    item_location *item_beta = d.has_beta ? d.actor( true )->get_item() : nullptr;
    return dialogue(
               creature_alpha ? get_talker_for( creature_alpha ) : item_alpha ? get_talker_for(
                   item_alpha ) : nullptr,
               creature_beta ? get_talker_for( creature_beta ) : item_beta ? get_talker_for(
                   item_beta ) : nullptr
           );
}

template<class T>
str_or_var<T> get_str_or_var( const JsonValue &jv, const std::string &member, bool required = true,
                              const std::string &default_val = "" );
template<class T>
int_or_var<T> get_int_or_var( const JsonObject &jo, std::string member, bool required = true,
                              int default_val = 0 );
template<class T>
int_or_var_part<T> get_int_or_var_part( const JsonValue &jv, const std::string &member,
                                        bool required = true,
                                        int default_val = 0 );
template<class T>
duration_or_var<T> get_duration_or_var( const JsonObject &jo, std::string member,
                                        bool required = true,
                                        time_duration default_val = 0_seconds );
template<class T>
duration_or_var_part<T> get_duration_or_var_part( const JsonValue &jv, const std::string &member,
        bool required = true,
        time_duration default_val = 0_seconds );
template<class T>
tripoint_abs_ms get_tripoint_from_var( std::optional<var_info> var, const T &d );
var_info read_var_info( const JsonObject &jo );
// d / indirect 为 0.H 兼容层新增：context_val 写入需要 dialogue，var_val 需要运行期解引用。
void write_var_value( var_type type, const std::string &name, talker *talk,
                      const std::string &value, const dialogue *d = nullptr, bool indirect = false );
// 0.H 兼容层：context_val 的读写。
// 定义放在 condition.cpp 是因为需要 dialogue 的完整定义（dialogue_helpers.h 只前置声明了它）。
std::string read_context_var( const dialogue &d, const std::string &name );
void write_context_var( const dialogue &d, const std::string &name, const std::string &value );
// 0.H 兼容层：set_condition / get_condition 共用的条件存储
void store_dialogue_condition( const std::string &name,
                               const std::function<bool( const dialogue & )> &cond );
bool run_stored_dialogue_condition( const std::string &name, const dialogue &d );
template<class T>
std::string get_talk_varname( const JsonObject &jo, const std::string &member,
                              bool check_value, int_or_var<T> &default_val );
std::string get_talk_var_basename( const JsonObject &jo, const std::string &member,
                                   bool check_value );
// the truly awful declaration for the conditional_t loading helper_function
template<class T>
void read_condition( const JsonObject &jo, const std::string &member_name,
                     std::function<bool( const T & )> &condition, bool default_val );

// 把一个「取值名」对象（{"u_val": ...} / {"npc_val": ...} 等）编译成写入器。
// 原本是 condition.cpp 里的内部函数，0.H 兼容层的 math 赋值需要复用它，故对外公开。
template<class T>
std::function<void( const T &, int )> get_set_int( const JsonObject &jo,
        const std::optional<int_or_var_part<T>> &min, const std::optional<int_or_var_part<T>> &max,
        bool temp_var = false );

/**
 * A condition for a response spoken by the player.
 * This struct only adds the constructors which will load the data from json
 * into a lambda, stored in the std::function object.
 * Invoking the function operator with a dialog reference (so the function can access the NPC)
 * returns whether the response is allowed.
 */
template<class T>
struct conditional_t {
    private:
        std::function<bool( const T & )> condition;

    public:
        conditional_t() = default;
        explicit conditional_t( const std::string &type );
        explicit conditional_t( const JsonObject &jo );

        void set_has_any_trait( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_has_trait( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_has_flag( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_has_var( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_compare_var( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_compare_time_since_var( const JsonObject &jo, const std::string &member,
                                         bool is_npc = false );
        void set_has_activity( bool is_npc = false );
        void set_is_riding( bool is_npc = false );
        void set_npc_has_class( const JsonObject &jo, const std::string &member, bool is_npc );
        void set_u_has_mission( const JsonObject &jo, const std::string &member );
        void set_u_monsters_in_direction( const JsonObject &jo, const std::string &member );
        void set_u_safe_mode_trigger( const JsonObject &jo, const std::string &member );
        void set_has_strength( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_has_dexterity( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_has_intelligence( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_has_perception( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_has_hp( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_has_part_temp( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_is_deaf( bool is_npc = false );
        void set_is_on_terrain( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_is_in_field( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_one_in_chance( const JsonObject &jo, const std::string &member );
        void set_query( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_x_in_y_chance( const JsonObject &jo, const std::string &member );
        void set_has_worn_with_flag( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_has_wielded_with_flag( const JsonObject &jo, const std::string &member,
                                        bool is_npc = false );
        void set_is_wearing( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_has_item( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_has_items( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_has_item_with_flag( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_has_item_category( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_has_bionics( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_has_effect( const JsonObject &jo, const std::string &member, bool is_npc = false );
        // ---- 0.H 兼容层新增条件 ----
        // u_is_avatar / npc_is_avatar / u_is_character / npc_is_character / u_is_npc / npc_is_npc
        void set_is_avatar( bool is_npc = false );
        void set_is_character( bool is_npc = false );
        void set_is_npc_actor( bool is_npc = false );
        // player_see_u / player_see_npc：玩家视野内能否看到指定 talker
        void set_player_see( bool is_npc = false );
        // u_has_species / npc_has_species
        void set_has_species( const JsonObject &jo, const std::string &member, bool is_npc = false );
        // u_has_any_effect / npc_has_any_effect（数组形式，任一命中即真）
        void set_has_any_effect( const JsonObject &jo, const std::string &member, bool is_npc = false );
        // expects_vars：列出的 context_val 全部存在时为真
        void set_expects_vars( const JsonObject &jo, const std::string &member );
        // get_condition：运行由 set_condition 存下来的条件
        void set_get_condition( const JsonObject &jo, const std::string &member );
        void set_need( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_at_om_location( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_near_om_location( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_has_move_mode( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_npc_role_nearby( const JsonObject &jo, const std::string &member );
        void set_npc_allies( const JsonObject &jo, const std::string &member );
        void set_npc_allies_global( const JsonObject &jo, const std::string &member );
        void set_u_has_cash( const JsonObject &jo, const std::string &member );
        void set_u_are_owed( const JsonObject &jo, const std::string &member );
        void set_npc_aim_rule( const JsonObject &jo, const std::string &member, bool is_npc );
        void set_npc_engagement_rule( const JsonObject &jo, const std::string &member, bool is_npc );
        void set_npc_cbm_reserve_rule( const JsonObject &jo, const std::string &member, bool is_npc );
        void set_npc_cbm_recharge_rule( const JsonObject &jo, const std::string &member, bool is_npc );
        void set_npc_rule( const JsonObject &jo, const std::string &member, bool is_npc );
        void set_npc_override( const JsonObject &jo, const std::string &member, bool is_npc );
        void set_npc_personality( const JsonObject &jo, const std::string &member );
        void set_npc_movement_policy( const JsonObject &jo, const std::string &member );
        void set_days_since( const JsonObject &jo, const std::string &member );
        void set_is_season( const JsonObject &jo, const std::string &member );
        // 0.H 兼容层：{"is_mid_autumn_holiday": true/false} —— 按现实日期判断是否中秋
        void set_is_mid_autumn_holiday( const JsonObject &jo, const std::string &member );
        void set_is_weather( const JsonObject &jo, const std::string &member );
        void set_mod_is_loaded( const JsonObject &jo, const std::string &member );
        void set_mission_goal( const JsonObject &jo, const std::string &member, bool is_npc );
        void set_has_faction_trust( const JsonObject &jo, const std::string &member );
        void set_no_assigned_mission();
        void set_has_assigned_mission();
        void set_has_many_assigned_missions();
        void set_no_available_mission( bool is_npc );
        void set_has_available_mission( bool is_npc );
        void set_has_many_available_missions( bool is_npc );
        void set_mission_complete( bool is_npc );
        void set_mission_incomplete( bool is_npc );
        void set_mission_failed( bool is_npc );
        void set_npc_available( bool is_npc );
        void set_npc_following( bool is_npc );
        void set_npc_friend( bool is_npc );
        void set_npc_hostile( bool is_npc );
        void set_npc_train_skills( bool is_npc );
        void set_npc_train_styles( bool is_npc );
        void set_npc_train_spells( bool is_npc );
        void set_at_safe_space( bool is_npc );
        void set_can_stow_weapon( bool is_npc = false );
        void set_can_drop_weapon( bool is_npc = false );
        void set_has_weapon( bool is_npc = false );
        void set_is_driving( bool is_npc = false );
        void set_is_day();
        void set_has_stolen_item( bool is_npc = false );
        void set_is_outside( bool is_npc = false );
        void set_is_underwater( bool is_npc = false );
        void set_is_by_radio();
        void set_u_has_camp();
        void set_has_pickup_list( bool is_npc );
        void set_has_reason();
        void set_is_gender( bool is_male, bool is_npc = false );
        void set_has_skill( const JsonObject &jo, const std::string &member, bool is_npc = false );
        void set_roll_contested( const JsonObject &jo, const std::string &member );
        void set_u_know_recipe( const JsonObject &jo, const std::string &member );
        void set_mission_has_generic_rewards();
        void set_can_see( bool is_npc = false );
        void set_compare_string( const JsonObject &jo, const std::string &member );
        void set_compare_int( const JsonObject &jo, const std::string &member );
        void set_u_joined_the_faction(const JsonObject& jo, const std::string& member);
        void set_u_get_food_today_from_the_faction(const JsonObject& jo, const std::string& member);
        void set_u_reported_work(const JsonObject& jo, const std::string& member);
        static std::function<int( const T & )> get_get_int( const JsonObject &jo );
        static std::function<int( const T & )> get_get_int( const std::string &value,
                const JsonObject &jo );
        bool operator()( const T &d ) const {
            if( !condition ) {
                return false;
            }
            return condition( d );
        }
};

#if !defined(MACOSX)
extern template struct conditional_t<dialogue>;
extern template void read_condition<dialogue>( const JsonObject &jo, const std::string &member_name,
        std::function<bool( const dialogue & )> &condition, bool default_val );

extern template duration_or_var<dialogue> get_duration_or_var( const JsonObject &jo,
        std::string member,
        bool required, time_duration default_val );
extern template std::string get_talk_varname<dialogue>( const JsonObject &jo,
        const std::string &member,
        bool check_value, int_or_var<dialogue> &default_val );
extern template struct conditional_t<mission_goal_condition_context>;
extern template void read_condition<mission_goal_condition_context>( const JsonObject &jo,
        const std::string &member_name,
        std::function<bool( const mission_goal_condition_context & )> &condition, bool default_val );
extern template struct talk_effect_fun_t<dialogue>;
#endif

#endif // CATA_SRC_CONDITION_H
