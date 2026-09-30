#include <memory>
#include "character.h"
#include "effect.h"
#include "item.h"
#include "magic.h"
#include "monster.h"
#include "mtype.h"
#include "pimpl.h"
#include "point.h"
#include "talker_monster.h"
#include "vehicle.h"

class time_duration;

std::string talker_monster::disp_name() const
{
    return me_mon->disp_name();
}

int talker_monster::posx() const
{
    return me_mon->posx();
}

int talker_monster::posy() const
{
    return me_mon->posy();
}

int talker_monster::posz() const
{
    return me_mon->posz();
}

tripoint talker_monster::pos() const
{
    return me_mon->pos();
}

tripoint_abs_ms talker_monster::global_pos() const
{
    return me_mon->get_location();
}

tripoint_abs_omt talker_monster::global_omt_location() const
{
    return me_mon->global_omt_location();
}

int talker_monster::pain_cur() const
{
    return me_mon->get_pain();
}

bool talker_monster::has_effect( const efftype_id &effect_id, const bodypart_id &bp ) const
{
    return me_mon->has_effect( effect_id, bp );
}

bool talker_monster::has_flag( const json_character_flag &flag ) const
{
    // 0.H 兼容层：怪物 flag 在 0.H 里由 JSON 动态声明，0.G 的 m_flag 枚举装不下，
    // 加载时收集在 mtype::json_flags 中，这里据此回答 npc_has_flag / u_has_flag 查询。
    return me_mon != nullptr && me_mon->type != nullptr && me_mon->type->has_json_flag( flag.str() );
}

effect talker_monster::get_effect( const efftype_id &effect_id, const bodypart_id &bp ) const
{
    return me_mon->get_effect( effect_id, bp );
}

void talker_monster::add_effect( const efftype_id &new_effect, const time_duration &dur,
                                 const std::string &bp, bool permanent, bool force,
                                 int intensity )
{
    me_mon->add_effect( new_effect, dur, bodypart_str_id( bp ), permanent, intensity, force );
}

void talker_monster::remove_effect( const efftype_id &old_effect )
{
    me_mon->remove_effect( old_effect );
}

void talker_monster::mod_pain( int amount )
{
    me_mon->mod_pain( amount );
}

std::string talker_monster:: get_value( const std::string &var_name ) const
{
    return me_mon->get_value( var_name );
}

void talker_monster::set_value( const std::string &var_name, const std::string &value )
{
    me_mon->set_value( var_name, value );
}

void talker_monster::remove_value( const std::string &var_name )
{
    me_mon->remove_value( var_name );
}

std::string talker_monster::short_description() const
{
    return me_mon->type->get_description();
}

int talker_monster::get_anger() const
{
    return me_mon->anger;
}

void talker_monster::set_anger( int new_val )
{
    me_mon->anger = new_val;
}

int talker_monster::morale_cur() const
{
    return me_mon->morale;
}

void talker_monster::set_morale( int new_val )
{
    me_mon->morale = new_val;
}

int talker_monster::get_friendly() const
{
    return me_mon->friendly;
}

void talker_monster::set_friendly( int new_val )
{
    me_mon->friendly = new_val;
}

std::vector<std::string> talker_monster::get_topics( bool )
{
    return me_mon->type->chat_topics;
}

bool talker_monster::will_talk_to_u( const Character &you, bool )
{
    return !you.is_dead_state();
}
