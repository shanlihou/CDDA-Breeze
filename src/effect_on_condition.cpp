#include "effect_on_condition.h"

#include <map>
#include <sstream>

#include "avatar.h"
#include "cata_utility.h"
#include "character.h"
#include "condition.h"
#include "event_bus.h"
#include "event_subscriber.h"
#include "game.h"
#include "generic_factory.h"
#include "json.h"
#include "scenario.h"
#include "talker.h"
#include "type_id.h"

namespace io
{
    // *INDENT-OFF*
    template<>
    std::string enum_to_string<eoc_type>( eoc_type data )
    {
        switch ( data ) {
        case eoc_type::ACTIVATION: return "ACTIVATION";
        case eoc_type::RECURRING: return "RECURRING";
        case eoc_type::SCENARIO_SPECIFIC: return "SCENARIO_SPECIFIC";
        case eoc_type::AVATAR_DEATH: return "AVATAR_DEATH";
        case eoc_type::NPC_DEATH: return "NPC_DEATH";
        case eoc_type::OM_MOVE: return "OM_MOVE";
        case eoc_type::PREVENT_DEATH: return "PREVENT_DEATH";
        case eoc_type::FALL_DAMAGE: return "FALL_DAMAGE";
        case eoc_type::EVENT: return "EVENT";
        case eoc_type::NUM_EOC_TYPES: break;
        }
        cata_fatal( "Invalid eoc_type" );
    }    
    // *INDENT-ON*
} // namespace io

namespace
{
generic_factory<effect_on_condition>
effect_on_condition_factory( "effect_on_condition" );

// 0.H 兼容层：EVENT 类型 EOC —— event_type -> 监听它的 EOC 列表
std::map<event_type, std::vector<effect_on_condition_id>> event_eocs;

bool eoc_subscriber_subscribed = false;

class eoc_event_subscriber : public event_subscriber
{
    public:
        void notify( const cata::event &e ) override {
            const auto it = event_eocs.find( e.type() );
            if( it == event_eocs.end() || it->second.empty() ) {
                return;
            }
            // 事件关联的角色（game_* 之类的事件没有，退回玩家）
            Character *ch = nullptr;
            const cata_variant cv = e.get_variant_or_void( "character" );
            if( cv.type() != cata_variant_type::void_ ) {
                const character_id cid = cv.get<character_id>();
                if( cid == get_avatar().getID() ) {
                    ch = &get_avatar();
                } else if( npc *who = g->find_npc( cid ) ) {
                    ch = who;
                }
            }
            if( ch == nullptr ) {
                ch = &get_avatar();
            }
            dialogue d( get_talker_for( static_cast<Creature *>( ch ) ), nullptr );
            // 事件数据作为上下文变量暴露（如 character_gains_effect 的 effect）
            for( const std::pair<const std::string, cata_variant> &p : e.data() ) {
                std::string val;
                // 布尔值序列化成 "1"/"0"：math 引擎用 std::stod 读取上下文变量，
                // 若写成 "true"/"false" 会解析失败退化成 0，导致永假。
                if( p.second.type() == cata_variant_type::bool_ ) {
                    val = p.second.get<bool>() ? "1" : "0";
                } else if( p.second.type() == cata_variant_type::int_ ) {
                    val = std::to_string( p.second.get<int>() );
                } else {
                    std::ostringstream ss;
                    JsonOut jsout( ss );
                    p.second.serialize( jsout );
                    val = ss.str();
                    if( val.size() >= 2 && val.front() == '"' && val.back() == '"' ) {
                        val = val.substr( 1, val.size() - 2 );
                    }
                }
                d.context_vars[p.first] = val;
            }
            for( const effect_on_condition_id &eoc : it->second ) {
                eoc->activate( d );
            }
        }
};

eoc_event_subscriber eoc_subscriber_instance;
} // namespace

template<>
const effect_on_condition &effect_on_condition_id::obj() const
{
    return effect_on_condition_factory.obj( *this );
}

/** @relates string_id */
template<>
bool string_id<effect_on_condition>::is_valid() const
{
    return effect_on_condition_factory.is_valid( *this );
}

void effect_on_conditions::check_consistency()
{
}

void effect_on_condition::load( const JsonObject &jo, const std::string & )
{
    mandatory( jo, was_loaded, "id", id );
    optional( jo, was_loaded, "eoc_type", type, eoc_type::NUM_EOC_TYPES );
    if( jo.has_member( "recurrence" ) ) {
        if( type != eoc_type::NUM_EOC_TYPES && type != eoc_type::RECURRING ) {
            jo.throw_error( "A recurring effect_on_condition must be of type RECURRING." );
        }
        type = eoc_type::RECURRING;
        recurrence = get_duration_or_var<dialogue>( jo, "recurrence", false );
    }
    if( type == eoc_type::NUM_EOC_TYPES ) {
        type = eoc_type::ACTIVATION;
    }

    if( jo.has_member( "deactivate_condition" ) ) {
        read_condition<dialogue>( jo, "deactivate_condition", deactivate_condition, false );
        has_deactivate_condition = true;
    }
    if( jo.has_member( "condition" ) ) {
        read_condition<dialogue>( jo, "condition", condition, false );
        has_condition = true;
    }
    true_effect.load_effect( jo, "effect" );

    if( jo.has_member( "false_effect" ) ) {
        false_effect.load_effect( jo, "false_effect" );
        has_false_effect = true;
    }

    optional( jo, was_loaded, "run_for_npcs", run_for_npcs, false );
    optional( jo, was_loaded, "global", global, false );
    if( !global && run_for_npcs ) {
        jo.throw_error( "run_for_npcs should only be true for global effect_on_conditions." );
    }
    // 0.H 兼容层：EVENT 类型 EOC 监听的事件
    if( jo.has_member( "required_event" ) ) {
        const std::string ev_name = jo.get_string( "required_event" );
        const std::optional<event_type> ev = io::string_to_enum_optional<event_type>( ev_name );
        if( !ev.has_value() ) {
            jo.throw_error_at( "required_event", "invalid event type \"" + ev_name + "\"" );
        }
        required_event = ev.value();
        if( type != eoc_type::EVENT ) {
            jo.throw_error( "\"required_event\" requires \"eoc_type\" of \"EVENT\"" );
        }
    } else if( type == eoc_type::EVENT ) {
        jo.throw_error( "An event EOC requires a \"required_event\"" );
    }
}

effect_on_condition_id effect_on_conditions::load_inline_eoc( const JsonValue &jv,
        const std::string &src )
{
    if( jv.test_string() ) {
        return effect_on_condition_id( jv.get_string() );
    } else if( jv.test_object() ) {
        effect_on_condition inline_eoc;
        inline_eoc.load( jv.get_object(), src );
        mod_tracker::assign_src( inline_eoc, src );
        effect_on_condition_factory.insert( inline_eoc );
        return inline_eoc.id;
    } else {
        jv.throw_error( "effect_on_condition needs to be either a string or an effect_on_condition object." );
    }
}

static time_duration next_recurrence( const effect_on_condition_id &eoc, dialogue &d )
{
    return eoc->recurrence.evaluate( d );
}

void effect_on_conditions::load_new_character( Character &you )
{
    bool is_avatar = you.is_avatar();
    for( const effect_on_condition_id &eoc_id : get_scenario()->eoc() ) {
        effect_on_condition eoc = eoc_id.obj();
        if( eoc.type == eoc_type::SCENARIO_SPECIFIC && ( is_avatar || eoc.run_for_npcs ) ) {
            queued_eoc new_eoc = queued_eoc{ eoc.id, calendar::turn_zero };
            you.queued_effect_on_conditions.push( new_eoc );
        }
    }
    for( const effect_on_condition &eoc : effect_on_conditions::get_all() ) {
        if( eoc.type == eoc_type::RECURRING && ( ( is_avatar && eoc.global ) || !eoc.global ) ) {
            dialogue d( get_talker_for( you ), nullptr );
            queued_eoc new_eoc = queued_eoc{ eoc.id, calendar::turn + next_recurrence( eoc.id, d ) };
            if( eoc.global ) {
                g->queued_global_effect_on_conditions.push( new_eoc );
            } else {
                you.queued_effect_on_conditions.push( new_eoc );
            }
        }
    }

    effect_on_conditions::process_effect_on_conditions( you );
}

static void process_new_eocs( std::priority_queue<queued_eoc, std::vector<queued_eoc>, eoc_compare>
                              &eoc_queue, std::vector<effect_on_condition_id> &eoc_vector,
                              std::map<effect_on_condition_id, bool> &new_eocs )
{
    std::priority_queue<queued_eoc, std::vector<queued_eoc>, eoc_compare>
    temp_queued_eocs;
    while( !eoc_queue.empty() ) {
        if( eoc_queue.top().eoc.is_valid() ) {
            temp_queued_eocs.push( eoc_queue.top() );
        }
        new_eocs[eoc_queue.top().eoc] = false;
        eoc_queue.pop();
    }
    eoc_queue = temp_queued_eocs;
    for( auto eoc = eoc_vector.begin();
         eoc != eoc_vector.end(); ) {
        if( !eoc->is_valid() ) {
            eoc = eoc_vector.erase( eoc );
        } else {
            new_eocs[*eoc] = false;
            eoc++;
        }
    }
}

void effect_on_conditions::load_existing_character( Character &you )
{
    bool is_avatar = you.is_avatar();
    std::map<effect_on_condition_id, bool> new_eocs;
    for( const effect_on_condition &eoc : effect_on_conditions::get_all() ) {
        if( eoc.type == eoc_type::RECURRING && ( is_avatar || !eoc.global ) ) {
            new_eocs[eoc.id] = true;
        }
    }
    process_new_eocs( you.queued_effect_on_conditions, you.inactive_effect_on_condition_vector,
                      new_eocs );
    if( is_avatar ) {
        process_new_eocs( g->queued_global_effect_on_conditions,
                          g->inactive_global_effect_on_condition_vector, new_eocs );
    }

    for( const std::pair<const effect_on_condition_id, bool> &eoc_pair : new_eocs ) {
        if( eoc_pair.second ) {
            dialogue d( get_talker_for( you ), nullptr );
            queue_effect_on_condition( next_recurrence( eoc_pair.first, d ),
                                       eoc_pair.first, you );
        }
    }
}

void effect_on_conditions::queue_effect_on_condition( time_duration duration,
        effect_on_condition_id eoc, Character &you )
{
    queued_eoc new_eoc = queued_eoc{ eoc, calendar::turn + duration };
    if( eoc->global ) {
        g->queued_global_effect_on_conditions.push( new_eoc );
    } else if( eoc->type == eoc_type::ACTIVATION || eoc->type == eoc_type::RECURRING ) {
        you.queued_effect_on_conditions.push( new_eoc );
    } else {
        debugmsg( "Invalid effect_on_condition and/or target.  EOC: %s", eoc->id.c_str() );
    }
}

static void process_eocs( std::priority_queue<queued_eoc, std::vector<queued_eoc>, eoc_compare>
                          &eoc_queue, std::vector<effect_on_condition_id> &eoc_vector, dialogue &d )
{
    std::vector<queued_eoc> eocs_to_queue;
    while( !eoc_queue.empty() &&
           eoc_queue.top().time <= calendar::turn ) {
        queued_eoc top = eoc_queue.top();
        bool activated = top.eoc->activate( d );
        if( top.eoc->type == eoc_type::RECURRING ) {
            if( activated ) { // It worked so add it back
                queued_eoc new_eoc = queued_eoc{ top.eoc, calendar::turn + next_recurrence( top.eoc, d ) };
                eocs_to_queue.push_back( new_eoc );
            } else {
                if( !top.eoc->check_deactivate( d ) ) { // It failed but shouldn't be deactivated so add it back
                    queued_eoc new_eoc = queued_eoc{ top.eoc, calendar::turn + next_recurrence( top.eoc, d ) };
                    eocs_to_queue.push_back( new_eoc );
                } else { // It failed and should be deactivated for now
                    eoc_vector.push_back( top.eoc );
                }
            }
        }
        eoc_queue.pop();
    }
    for( const queued_eoc &q_eoc : eocs_to_queue ) {
        eoc_queue.push( q_eoc );
    }
}

void effect_on_conditions::process_effect_on_conditions( Character &you )
{
    dialogue d( get_talker_for( you ), nullptr );
    process_eocs( you.queued_effect_on_conditions, you.inactive_effect_on_condition_vector, d );
    //only handle global eocs on the avatars turn
    if( you.is_avatar() ) {
        process_eocs( g->queued_global_effect_on_conditions, g->inactive_global_effect_on_condition_vector,
                      d );
    }
}

static void process_reactivation( std::vector<effect_on_condition_id>
                                  &inactive_effect_on_condition_vector,
                                  std::priority_queue<queued_eoc, std::vector<queued_eoc>, eoc_compare>
                                  &queued_effect_on_conditions, dialogue &d )
{
    std::vector<effect_on_condition_id> ids_to_reactivate;
    for( const effect_on_condition_id &eoc : inactive_effect_on_condition_vector ) {
        if( !eoc->check_deactivate( d ) ) {
            ids_to_reactivate.push_back( eoc );
        }
    }
    for( const effect_on_condition_id &eoc : ids_to_reactivate ) {
        queued_effect_on_conditions.push( queued_eoc{ eoc, calendar::turn + next_recurrence( eoc, d ) } );
        inactive_effect_on_condition_vector.erase( std::remove(
                    inactive_effect_on_condition_vector.begin(), inactive_effect_on_condition_vector.end(),
                    eoc ), inactive_effect_on_condition_vector.end() );
    }
}

void effect_on_conditions::process_reactivate( Character &you )
{
    dialogue d( get_talker_for( you ), nullptr );
    process_reactivation( you.inactive_effect_on_condition_vector, you.queued_effect_on_conditions, d );
}

void effect_on_conditions::process_reactivate()
{
    dialogue d( get_talker_for( get_avatar() ), nullptr );
    process_reactivation( g->inactive_global_effect_on_condition_vector,
                          g->queued_global_effect_on_conditions, d );
}

bool effect_on_condition::activate( dialogue &d ) const
{
    bool retval = false;
    if( !has_condition || condition( d ) ) {
        true_effect.apply( d );
        retval = true;
    } else if( has_false_effect ) {
        false_effect.apply( d );
    }
    // This works because if global is true then this is recurring and thus should only ever be passed containing the player
    // Thus we just need to run the npcs.
    if( global && run_for_npcs ) {
        for( npc &guy : g->all_npcs() ) {
            dialogue d_npc( get_talker_for( guy ), nullptr );
            if( !has_condition || condition( d_npc ) ) {
                true_effect.apply( d_npc );
            } else if( has_false_effect ) {
                false_effect.apply( d_npc );
            }
        }
    }
    return retval;
}

bool effect_on_condition::check_deactivate( dialogue &d ) const
{
    if( !has_deactivate_condition || has_false_effect ) {
        return false;
    }
    return deactivate_condition( d );
}

bool effect_on_condition::test_condition( dialogue &d ) const
{
    return !has_condition || condition( d );
}

void effect_on_condition::apply_true_effects( dialogue &d )  const
{
    true_effect.apply( d );
}

void effect_on_conditions::clear( Character &you )
{
    while( !you.queued_effect_on_conditions.empty() ) {
        you.queued_effect_on_conditions.pop();
    }
    you.inactive_effect_on_condition_vector.clear();
    while( !g->queued_global_effect_on_conditions.empty() ) {
        g->queued_global_effect_on_conditions.pop();
    }
    g->inactive_global_effect_on_condition_vector.clear();
}

void effect_on_conditions::write_eocs_to_file( Character &you )
{
    write_to_file( "eocs.output", [&you]( std::ostream & testfile ) {
        testfile << "Character Name: " + you.get_name() << std::endl;
        testfile << "id;timepoint;recurring" << std::endl;

        testfile << "queued eocs:" << std::endl;
        std::vector<queued_eoc> temp_queue;
        while( !you.queued_effect_on_conditions.empty() ) {
            temp_queue.push_back( you.queued_effect_on_conditions.top() );
            you.queued_effect_on_conditions.pop();
        }

        for( const queued_eoc &queue_entry : temp_queue ) {
            time_duration temp = queue_entry.time - calendar::turn;
            testfile << queue_entry.eoc.c_str() << ";" << to_string( temp ) << std::endl;
        }

        for( const queued_eoc &queued : temp_queue ) {
            you.queued_effect_on_conditions.push( queued );
        }

        testfile << "inactive eocs:" << std::endl;
        for( const effect_on_condition_id &eoc : you.inactive_effect_on_condition_vector ) {
            testfile << eoc.c_str() << std::endl;
        }

    }, "eocs test file" );
}

void effect_on_conditions::write_global_eocs_to_file( )
{
    write_to_file( "eocs.output", [&]( std::ostream & testfile ) {
        testfile << "global" << std::endl;
        testfile << "id;timepoint;recurring" << std::endl;

        testfile << "queued eocs:" << std::endl;
        std::vector<queued_eoc> temp_queue;
        while( !g->queued_global_effect_on_conditions.empty() ) {
            temp_queue.push_back( g->queued_global_effect_on_conditions.top() );
            g->queued_global_effect_on_conditions.pop();
        }

        for( const queued_eoc &queue_entry : temp_queue ) {
            time_duration temp = queue_entry.time - calendar::turn;
            testfile << queue_entry.eoc.c_str() << ";" << to_string( temp ) << std::endl;
        }

        for( const queued_eoc &queued : temp_queue ) {
            g->queued_global_effect_on_conditions.push( queued );
        }

        testfile << "inactive eocs:" << std::endl;
        for( const effect_on_condition_id &eoc : g->inactive_global_effect_on_condition_vector ) {
            testfile << eoc.c_str() << std::endl;
        }

    }, "eocs test file" );
}

void effect_on_conditions::prevent_death()
{
    avatar &player_character = get_avatar();
    dialogue d( get_talker_for( player_character ), nullptr );
    for( const effect_on_condition &eoc : effect_on_conditions::get_all() ) {
        if( eoc.type == eoc_type::PREVENT_DEATH ) {
            eoc.activate( d );
        }
        if( !player_character.is_dead_state() ) {
            break;
        }
    }
}

void effect_on_conditions::avatar_death()
{
    avatar &player_character = get_avatar();
    Creature *klr = player_character.get_killer();
    // Make sure the creature still exists in game
    klr = !klr ? klr : g->get_creature_if( [klr]( const Creature & c ) {
        return klr == &c;
    } );
    dialogue d( get_talker_for( get_avatar() ), klr == nullptr ? nullptr : get_talker_for( klr ) );
    for( const effect_on_condition &eoc : effect_on_conditions::get_all() ) {
        if( eoc.type == eoc_type::AVATAR_DEATH ) {
            eoc.activate( d );
        }
    }
}

void effect_on_conditions::om_move()
{
    avatar &player_character = get_avatar();
    dialogue d( get_talker_for( player_character ), nullptr );
    for( const effect_on_condition &eoc : effect_on_conditions::get_all() ) {
        if( eoc.type == eoc_type::OM_MOVE ) {
            eoc.activate( d );
        }
    }
}

void effect_on_conditions::process_fall_damage( Character &you )
{
    dialogue d( get_talker_for( you ), nullptr );
    for( const effect_on_condition &eoc : effect_on_conditions::get_all() ) {
        if( eoc.type == eoc_type::FALL_DAMAGE ) {
            eoc.activate( d );
        }
    }
}

void effect_on_condition::finalize()
{
}

void effect_on_conditions::finalize_all()
{
    effect_on_condition_factory.finalize();
    event_eocs.clear();
    for( const effect_on_condition &eoc : effect_on_condition_factory.get_all() ) {
        const_cast<effect_on_condition &>( eoc ).finalize();
        if( eoc.type == eoc_type::EVENT && eoc.required_event.has_value() ) {
            event_eocs[eoc.required_event.value()].push_back( eoc.id );
        }
    }
    // 0.H 兼容层：订阅事件总线（重复 finalize 时避免重复订阅）
    if( g != nullptr && !eoc_subscriber_subscribed ) {
        get_event_bus().subscribe( &eoc_subscriber_instance );
        eoc_subscriber_subscribed = true;
    }
}

void effect_on_condition::check() const
{
}

const std::vector<effect_on_condition> &effect_on_conditions::get_all()
{
    return effect_on_condition_factory.get_all();
}

void effect_on_conditions::reset()
{
    effect_on_condition_factory.reset();
}

void effect_on_conditions::load( const JsonObject &jo, const std::string &src )
{
    effect_on_condition_factory.load( jo, src );
}
