#include "player.h"
#include "action.h"
#include "game.h"
#include "map.h"
#include "debug.h"
#include "rng.h"
#include "input.h"
#include "item.h"
#include "bionics.h"
#include "bodypart.h"
#include "messages.h"
#include "overmapbuffer.h"
#include "sounds.h"
#include "translations.h"
#include "monster.h"
#include "overmap.h"
#include "itype.h"
#include "vehicle.h"
#include "field.h"
#include "weather_gen.h"
#include "weather.h"
#include "cata_utility.h"
#include "output.h"

#include <algorithm> //std::min
#include <sstream>

const skill_id skilll_electronics( "electronics" );
const skill_id skilll_firstaid( "firstaid" );
const skill_id skilll_mechanics( "mechanics" );

const efftype_id effect_adrenaline( "adrenaline" );
const efftype_id effect_adrenaline_mycus( "adrenaline_mycus" );
const efftype_id effect_bleed( "bleed" );
const efftype_id effect_bloodworms( "bloodworms" );
const efftype_id effect_brainworms( "brainworms" );
const efftype_id effect_cig( "cig" );
const efftype_id effect_datura( "datura" );
const efftype_id effect_dermatik( "dermatik" );
const efftype_id effect_drunk( "drunk" );
const efftype_id effect_fungus( "fungus" );
const efftype_id effect_hallu( "hallu" );
const efftype_id effect_high( "high" );
const efftype_id effect_iodine( "iodine" );
const efftype_id effect_meth( "meth" );
const efftype_id effect_paincysts( "paincysts" );
const efftype_id effect_pblue( "pblue" );
const efftype_id effect_pkill1( "pkill1" );
const efftype_id effect_pkill2( "pkill2" );
const efftype_id effect_pkill3( "pkill3" );
const efftype_id effect_pkill_l( "pkill_l" );
const efftype_id effect_poison( "poison" );
const efftype_id effect_stung( "stung" );
const efftype_id effect_tapeworm( "tapeworm" );
const efftype_id effect_teleglow( "teleglow" );
const efftype_id effect_tetanus( "tetanus" );
const efftype_id effect_took_flumed( "took_flumed" );
const efftype_id effect_took_prozac( "took_prozac" );
const efftype_id effect_took_xanax( "took_xanax" );
const efftype_id effect_visuals( "visuals" );
const efftype_id effect_weed_high( "weed_high" );

namespace
{
std::map<std::string, bionic_data> bionics;
std::vector<std::string> faulty_bionics;
} //namespace

bool is_valid_bionic( std::string const &id )
{
    return !!bionics.count( id );
}

bionic_data const &bionic_info( std::string const &id )
{
    auto const it = bionics.find( id );
    if( it != bionics.end() ) {
        return it->second;
    }

    debugmsg("bad bionic id");

    static bionic_data const null_value {
        "bad bionic", false, false, 0, 0, 0, 0, 0, "bad_bionic", false, {{bp_torso, 0}}
    };
    return null_value;
}

void bionics_install_failure( player *u, int difficulty, int success );

bionic_data::bionic_data(std::string nname, bool ps, bool tog, int pac, int pad, int pot,
                         int ct, int cap, std::string desc, bool fault, std::map<body_part, size_t> bps
) : name(std::move(nname)), description(std::move(desc)), power_activate(pac),
    power_deactivate(pad), power_over_time(pot), charge_time(ct), capacity(cap), faulty(fault),
    power_source( ps ), activated( tog || pac || ct ), toggled( tog ),
    occupied_bodyparts( std::move( bps ) )
{
}

void force_comedown( effect &eff )
{
    if( eff.is_null() || eff.get_effect_type() == nullptr || eff.get_duration() <= 1 ) {
        return;
    }

    eff.set_duration( std::min( eff.get_duration(), eff.get_int_dur_factor() ) );
}

// Why put this in a Big Switch?  Why not let bionics have pointers to
// functions, much like monsters and items?
//
// Well, because like diseases, which are also in a Big Switch, bionics don't
// share functions....
bool player::activate_bionic( int b, bool eff_only )
{
    bionic &bio = my_bionics[b];

    // Special compatibility code for people who updated saves with their claws out
    if( ( weapon.type->id == "bio_claws_weapon" && bio.id == "bio_claws_weapon" ) ||
        ( weapon.type->id == "bio_blade_weapon" && bio.id == "bio_blade_weapon" ) ) {
        return deactivate_bionic( b );
    }

    // eff_only means only do the effect without messing with stats or displaying messages
    if( !eff_only ) {
        if( bio.powered ) {
            // It's already on!
            return false;
        }
        if( power_level < bionics[bio.id].power_activate ) {
            add_msg( m_info, _( "You don't have the power to activate your %s." ),
                     bionics[bio.id].name.c_str() );
            return false;
        }

        //We can actually activate now, do activation-y things
        charge_power( -bionics[bio.id].power_activate );
        if( bionics[bio.id].toggled || bionics[bio.id].charge_time > 0 ) {
            bio.powered = true;
        }
        if( bionics[bio.id].charge_time > 0 ) {
            bio.charge = bionics[bio.id].charge_time;
        }
        add_msg( m_info, _( "You activate your %s." ), bionics[bio.id].name.c_str() );
    }

    std::vector<std::string> good;
    std::vector<std::string> bad;
    tripoint dirp = pos();
    int &dirx = dirp.x;
    int &diry = dirp.y;
    item tmp_item;
    w_point const weatherPoint = g->weather_gen->get_weather( global_square_location(), calendar::turn );

    // On activation effects go here
    if(bio.id == "bio_painkiller") {
        mod_pain( -2 );
        mod_painkiller( 6 );
        if ( get_painkiller() > get_pain() ) {
            set_painkiller( get_pain() );
        }
    } else if (bio.id == "bio_ears" && has_active_bionic("bio_earplugs")) {
        for (auto &i : my_bionics) {
            if (i.id == "bio_earplugs") {
                i.powered = false;
                add_msg(m_info, _("Your %s automatically turn off."), bionics[i.id].name.c_str());
            }
        }
    } else if (bio.id == "bio_earplugs" && has_active_bionic("bio_ears")) {
        for (auto &i : my_bionics) {
            if (i.id == "bio_ears") {
                i.powered = false;
                add_msg(m_info, _("Your %s automatically turns off."), bionics[i.id].name.c_str());
            }
        }
    } else if (bio.id == "bio_tools") {
        invalidate_crafting_inventory();
    } else if (bio.id == "bio_cqb") {
        if (!pick_style()) {
            bio.powered = false;
            add_msg(m_info, _("You change your mind and turn it off."));
            return false;
        }
    } else if (bio.id == "bio_nanobots") {
        remove_effect( effect_bleed );
        healall(4);
    } else if (bio.id == "bio_resonator") {
        //~Sound of a bionic sonic-resonator shaking the area
        sounds::sound( pos(), 30, _("VRRRRMP!"));
        for (int i = posx() - 1; i <= posx() + 1; i++) {
            for (int j = posy() - 1; j <= posy() + 1; j++) {
                tripoint bashpoint( i, j, posz() );
                g->m.bash( bashpoint, 110 );
                g->m.bash( bashpoint, 110 ); // Multibash effect, so that doors &c will fall
                g->m.bash( bashpoint, 110 );
            }
        }
    } else if (bio.id == "bio_time_freeze") {
        moves += power_level;
        power_level = 0;
        add_msg(m_good, _("Your speed suddenly increases!"));
        if (one_in(3)) {
            add_msg(m_bad, _("Your muscles tear with the strain."));
            apply_damage( nullptr, bp_arm_l, rng( 5, 10 ) );
            apply_damage( nullptr, bp_arm_r, rng( 5, 10 ) );
            apply_damage( nullptr, bp_leg_l, rng( 7, 12 ) );
            apply_damage( nullptr, bp_leg_r, rng( 7, 12 ) );
            apply_damage( nullptr, bp_torso, rng( 5, 15 ) );
        }
        if (one_in(5)) {
            add_effect( effect_teleglow, rng( 50, 400 ) );
        }
    } else if (bio.id == "bio_teleport") {
        g->teleport();
        add_effect( effect_teleglow, 300 );
        // TODO: More stuff here (and bio_blood_filter)
    } else if(bio.id == "bio_blood_anal") {
        WINDOW *w = newwin(20, 40, 3 + ((TERMY > 25) ? (TERMY - 25) / 2 : 0),
                10 + ((TERMX > 80) ? (TERMX - 80) / 2 : 0));
        draw_border(w);
        if (has_effect( effect_fungus )) {
            bad.push_back(_("Fungal Parasite"));
        }
        if (has_effect( effect_dermatik )) {
            bad.push_back(_("Insect Parasite"));
        }
        if (has_effect( effect_stung )) {
            bad.push_back(_("Stung"));
        }
        if (has_effect( effect_poison )) {
            bad.push_back(_("Poison"));
        }
        if (radiation > 0) {
            bad.push_back(_("Irradiated"));
        }
        if (has_effect( effect_pkill1 )) {
            good.push_back(_("Minor Painkiller"));
        }
        if (has_effect( effect_pkill2 )) {
            good.push_back(_("Moderate Painkiller"));
        }
        if (has_effect( effect_pkill3 )) {
            good.push_back(_("Heavy Painkiller"));
        }
        if (has_effect( effect_pkill_l )) {
            good.push_back(_("Slow-Release Painkiller"));
        }
        if (has_effect( effect_drunk )) {
            good.push_back(_("Alcohol"));
        }
        if (has_effect( effect_cig )) {
            good.push_back(_("Nicotine"));
        }
        if (has_effect( effect_meth )) {
            good.push_back(_("Methamphetamines"));
        }
        if (has_effect( effect_high )) {
            good.push_back(_("Intoxicant: Other"));
        }
        if (has_effect( effect_weed_high )) {
            good.push_back(_("THC Intoxication"));
        }
        if (has_effect( effect_hallu ) || has_effect( effect_visuals )) {
            bad.push_back(_("Hallucinations"));
        }
        if (has_effect( effect_pblue )) {
            good.push_back(_("Prussian Blue"));
        }
        if (has_effect( effect_iodine )) {
            good.push_back(_("Potassium Iodide"));
        }
        if (has_effect( effect_datura )) {
            good.push_back(_("Anticholinergic Tropane Alkaloids"));
        }
        if (has_effect( effect_took_xanax )) {
            good.push_back(_("Xanax"));
        }
        if (has_effect( effect_took_prozac )) {
            good.push_back(_("Prozac"));
        }
        if (has_effect( effect_took_flumed )) {
            good.push_back(_("Antihistamines"));
        }
        if (has_effect( effect_adrenaline )) {
            good.push_back(_("Adrenaline Spike"));
        }
        if (has_effect( effect_adrenaline_mycus )) {
            good.push_back(_("Mycal Spike"));
        }
        if (has_effect( effect_tapeworm )) {  // This little guy is immune to the blood filter though, as he lives in your bowels.
            good.push_back(_("Intestinal Parasite"));
        }
        if (has_effect( effect_bloodworms )) {
            good.push_back(_("Hemolytic Parasites"));
        }
        if (has_effect( effect_brainworms )) {  // These little guys are immune to the blood filter too, as they live in your brain.
            good.push_back(_("Intracranial Parasite"));
        }
        if (has_effect( effect_paincysts )) {  // These little guys are immune to the blood filter too, as they live in your muscles.
            good.push_back(_("Intramuscular Parasites"));
        }
        if (has_effect( effect_tetanus )) {  // Tetanus infection.
            good.push_back(_("Clostridium Tetani Infection"));
        }
        if (good.empty() && bad.empty()) {
            mvwprintz(w, 1, 1, c_white, _("No effects."));
        } else {
            for (unsigned line = 1; line < 39 && line <= good.size() + bad.size(); line++) {
                if (line <= bad.size()) {
                    mvwprintz(w, line, 1, c_red, "%s", bad[line - 1].c_str());
                } else {
                    mvwprintz(w, line, 1, c_green, "%s", good[line - 1 - bad.size()].c_str());
                }
            }
        }
        wrefresh(w);
        refresh();
        getch();
        delwin(w);
    } else if(bio.id == "bio_blood_filter") {
        remove_effect( effect_fungus );
        remove_effect( effect_dermatik );
        remove_effect( effect_bloodworms );
        remove_effect( effect_tetanus );
        remove_effect( effect_poison );
        remove_effect( effect_stung );
        remove_effect( effect_pkill1 );
        remove_effect( effect_pkill2 );
        remove_effect( effect_pkill3 );
        remove_effect( effect_pkill_l );
        remove_effect( effect_drunk );
        remove_effect( effect_cig );
        remove_effect( effect_high );
        remove_effect( effect_hallu );
        remove_effect( effect_visuals );
        remove_effect( effect_pblue );
        remove_effect( effect_iodine );
        remove_effect( effect_datura );
        remove_effect( effect_took_xanax );
        remove_effect( effect_took_prozac );
        remove_effect( effect_took_flumed );
        // Purging the substance won't remove the fatigue it caused
        force_comedown( get_effect( effect_adrenaline ) );
        force_comedown( get_effect( effect_meth ) );
        set_painkiller( 0 );
        stim = 0;
    } else if(bio.id == "bio_evap") {
        item water = item("water_clean", 0);
        int humidity = weatherPoint.humidity;
        int water_charges = (humidity * 3.0) / 100.0 + 0.5;
        // At 50% relative humidity or more, the player will draw 2 units of water
        // At 16% relative humidity or less, the player will draw 0 units of water
        water.charges = water_charges;
        if (water_charges == 0) {
            add_msg_if_player(m_bad, _("There was not enough moisture in the air from which to draw water!"));
        } else if (g->handle_liquid(water, true, false)) {
            moves -= 100;
        } else {
            water.charges -= drink_from_hands( water );
            if( water.charges == water_charges ) {
                charge_power(bionics["bio_evap"].power_activate);
            }
        }
    } else if(bio.id == "bio_lighter") {
        g->refresh_all();
        if(!choose_adjacent(_("Start a fire where?"), dirp) ||
           (!g->m.add_field(dirp, fd_fire, 1, 0))) {
            add_msg_if_player(m_info, _("You can't light a fire there."));
            charge_power(bionics["bio_lighter"].power_activate);
        }
    } else if(bio.id == "bio_leukocyte") {
        set_healthy(std::min(100, get_healthy() + 2));
        mod_healthy_mod(20, 100);
    } else if(bio.id == "bio_geiger") {
        add_msg(m_info, _("Your radiation level: %d"), radiation);
    } else if(bio.id == "bio_radscrubber") {
        if (radiation > 4) {
            radiation -= 5;
        } else {
            radiation = 0;
        }
    } else if(bio.id == "bio_adrenaline") {
        if( has_effect( effect_adrenaline ) ) {
            // Safety
            add_msg_if_player( m_bad, _( "The bionic refuses to activate!" ) );
            charge_power( bionics[bio.id].power_activate );
        } else {
            add_effect( effect_adrenaline, 200 );
        }
    } else if(bio.id == "bio_blaster") {
        tmp_item = weapon;
        weapon = item("bio_blaster_gun", 0);
        g->refresh_all();
        g->plfire(false);
        if(weapon.charges == 1) { // not fired
            charge_power(bionics[bio.id].power_activate);
        }
        weapon = tmp_item;
    } else if (bio.id == "bio_laser") {
        tmp_item = weapon;
        weapon = item("bio_laser_gun", 0);
        g->refresh_all();
        g->plfire(false);
        if(weapon.charges == 1) { // not fired
            charge_power(bionics[bio.id].power_activate);
        }
        weapon = tmp_item;
    } else if(bio.id == "bio_chain_lightning") {
        tmp_item = weapon;
        weapon = item("bio_lightning", 0);
        g->refresh_all();
        g->plfire(false);
        if(weapon.charges == 1) { // not fired
            charge_power(bionics[bio.id].power_activate);
        }
        weapon = tmp_item;
    } else if (bio.id == "bio_emp") {
        g->refresh_all();
        if(choose_adjacent(_("Create an EMP where?"), dirx, diry)) {
            g->emp_blast( tripoint( dirx, diry, posz() ) );
        } else {
            charge_power(bionics["bio_emp"].power_activate);
        }
    } else if (bio.id == "bio_hydraulics") {
        add_msg(m_good, _("Your muscles hiss as hydraulic strength fills them!"));
        //~ Sound of hissing hydraulic muscle! (not quite as loud as a car horn)
        sounds::sound( pos(), 19, _("HISISSS!"));
    } else if (bio.id == "bio_water_extractor") {
        bool extracted = false;
        for( auto it = g->m.i_at(pos()).begin();
             it != g->m.i_at(pos()).end(); ++it) {
            if( it->is_corpse() ) {
                const int avail = it->get_var( "remaining_water", it->volume() / 2 );
                if(avail > 0 && query_yn(_("Extract water from the %s"), it->tname().c_str())) {
                    item water = item("water_clean", 0);
                    water.charges = avail;
                    if (g->handle_liquid(water, true, false)) {
                        moves -= 100;
                    } else {
                        water.charges -= drink_from_hands( water );
                    }
                    if( water.charges != avail ) {
                        extracted = true;
                        it->set_var( "remaining_water", static_cast<int>( water.charges ) );
                    }
                    break;
                }
            }
        }
        if (!extracted) {
            charge_power(bionics["bio_water_extractor"].power_activate);
        }
    } else if(bio.id == "bio_magnet") {
        std::vector<tripoint> traj;
        for (int i = posx() - 10; i <= posx() + 10; i++) {
            for (int j = posy() - 10; j <= posy() + 10; j++) {
                if (g->m.i_at(i, j).size() > 0) {
                    traj = g->m.find_clear_path( {i, j, posz()}, pos() );
                }
                traj.insert(traj.begin(), {i, j, posz()});
                if( g->m.has_flag( "SEALED", i, j ) ) {
                    continue;
                }
                for (unsigned k = 0; k < g->m.i_at(i, j).size(); k++) {
                    tmp_item = g->m.i_at(i, j)[k];
                    static const std::vector<material_id> affected_materials =
                        { material_id( "iron" ), material_id( "steel" ) };
                    if( tmp_item.made_of_any( affected_materials ) &&
                        tmp_item.weight() < weight_capacity() ) {
                        g->m.i_rem(i, j, k);
                        std::vector<tripoint>::iterator it;
                        for (it = traj.begin(); it != traj.end(); ++it) {
                            int index = g->mon_at(*it);
                            if (index != -1) {
                                g->zombie(index).apply_damage( this, bp_torso, tmp_item.weight() / 225 );
                                g->zombie(index).check_dead_state();
                                g->m.add_item_or_charges(it->x, it->y, tmp_item);
                                break;
                            } else if (g->m.impassable(it->x, it->y)) {
                                if (it != traj.begin()) {
                                    g->m.bash( tripoint( it->x, it->y, posz() ), tmp_item.weight() / 225 );
                                    if (g->m.impassable(it->x, it->y)) {
                                        g->m.add_item_or_charges((it - 1)->x, (it - 1)->y, tmp_item);
                                        break;
                                    }
                                } else {
                                    g->m.bash( *it, tmp_item.weight() / 225 );
                                    if (g->m.impassable(it->x, it->y)) {
                                        break;
                                    }
                                }
                            }
                        }
                        if (it == traj.end()) {
                            g->m.add_item_or_charges(pos(), tmp_item);
                        }
                    }
                }
            }
        }
        moves -= 100;
    } else if(bio.id == "bio_lockpick") {
        tmp_item = item( "pseuso_bio_picklock", 0 );
        g->refresh_all();
        if( invoke_item( &tmp_item ) == 0 ) {
            if (tmp_item.charges > 0) {
                // restore the energy since CBM wasn't used
                charge_power(bionics[bio.id].power_activate);
            }
            return true;
        }
        if( tmp_item.damage > 0 ) {
            // TODO: damage the player / their bionics
        }
    } else if(bio.id == "bio_flashbang") {
        g->flashbang( pos(), true);
    } else if(bio.id == "bio_shockwave") {
        g->shockwave( pos(), 3, 4, 2, 8, true );
        add_msg_if_player(m_neutral, _("You unleash a powerful shockwave!"));
    } else if(bio.id == "bio_meteorologist") {
        // Calculate local wind power
        int vpart = -1;
        vehicle *veh = g->m.veh_at( pos(), vpart );
        int vehwindspeed = 0;
        if( veh != nullptr ) {
            vehwindspeed = abs(veh->velocity / 100); // vehicle velocity in mph
        }
        const oter_id &cur_om_ter = overmap_buffer.ter( global_omt_location() );
        std::string omtername = otermap[cur_om_ter].name;
        /* windpower defined in internal velocity units (=.01 mph) */
        double windpower = 100.0f * get_local_windpower( weatherPoint.windpower + vehwindspeed,
                                                         omtername, g->is_sheltered( g->u.pos() ) );
        add_msg_if_player( m_info, _( "Temperature: %s." ),
                           print_temperature( g->get_temperature() ).c_str() );
        add_msg_if_player( m_info, _( "Relative Humidity: %s." ),
                           print_humidity(
                               get_local_humidity( weatherPoint.humidity, g->weather,
                                                   g->is_sheltered( g->u.pos() ) ) ).c_str() );
        add_msg_if_player( m_info, _( "Pressure: %s."),
                           print_pressure( (int)weatherPoint.pressure ).c_str() );
        add_msg_if_player( m_info, _( "Wind Speed: %.1f %s." ),
                           convert_velocity( int( windpower ), VU_WIND ),
                           velocity_units( VU_WIND ) );
        add_msg_if_player( m_info, _( "Feels Like: %s." ),
                           print_temperature(
                               get_local_windchill( weatherPoint.temperature, weatherPoint.humidity,
                                                    windpower ) + g->get_temperature() ).c_str() );
    } else if(bio.id == "bio_claws") {
        if (weapon.has_flag ("NO_UNWIELD")) {
            add_msg(m_info, _("Deactivate your %s first!"),
                    weapon.tname().c_str());
            charge_power(bionics[bio.id].power_activate);
            bio.powered = false;
            return false;
        } else if(weapon.type->id != "null") {
            add_msg(m_warning, _("Your claws extend, forcing you to drop your %s."),
                    weapon.tname().c_str());
            g->m.add_item_or_charges(pos(), weapon);
            weapon = item("bio_claws_weapon", 0);
            weapon.invlet = '#';
        } else {
            add_msg(m_neutral, _("Your claws extend!"));
            weapon = item("bio_claws_weapon", 0);
            weapon.invlet = '#';
        }
    } else if(bio.id == "bio_blade") {
        if (weapon.has_flag ("NO_UNWIELD")) {
            add_msg(m_info, _("Deactivate your %s first!"),
                    weapon.tname().c_str());
            charge_power(bionics[bio.id].power_activate);
            bio.powered = false;
            return false;
        } else if(weapon.type->id != "null") {
            add_msg(m_warning, _("Your blade extends, forcing you to drop your %s."),
                    weapon.tname().c_str());
            g->m.add_item_or_charges(pos(), weapon);
            weapon = item("bio_blade_weapon", 0);
            weapon.invlet = '#';
        } else {
            add_msg(m_neutral, _("You extend your blade!"));
            weapon = item("bio_blade_weapon", 0);
            weapon.invlet = '#';
        }
    } else if( bio.id == "bio_remote" ) {
        int choice = menu( true, _("Perform which function:"), _("Nothing"),
                           _("Control vehicle"), _("RC radio"), NULL );
        if( choice >= 2 && choice <= 3 ) {
            item ctr;
            if( choice == 2 ) {
                ctr = item( "remotevehcontrol", 0 );
            } else {
                ctr = item( "radiocontrol", 0 );
            }
            ctr.charges = power_level;
            int power_use = invoke_item( &ctr );
            charge_power(-power_use);
            bio.powered = ctr.active;
        } else {
            bio.powered = g->remoteveh() != nullptr || get_value( "remote_controlling" ) != "";
        }
    } else if (bio.id == "bio_plutdump") {
        if (query_yn(_("WARNING: Purging all fuel is likely to result in radiation!  Purge anyway?"))) {
            slow_rad += (tank_plut + reactor_plut);
            tank_plut = 0;
            reactor_plut = 0;
            }
    }

    // Recalculate stats (strength, mods from pain etc.) that could have been affected
    reset();

    return true;
}

bool player::deactivate_bionic( int b, bool eff_only )
{
    bionic &bio = my_bionics[b];

    // Just do the effect, no stat changing or messages
    if( !eff_only ) {
        if( !bio.powered ) {
            // It's already off!
            return false;
        }
        if( !bionics[bio.id].toggled ) {
            // It's a fire-and-forget bionic, we can't turn it off but have to wait for it to run out of charge
            add_msg( m_info, _( "You can't deactivate your %s manually!" ), bionics[bio.id].name.c_str() );
            return false;
        }
        if( power_level < bionics[bio.id].power_deactivate ) {
            add_msg( m_info, _( "You don't have the power to deactivate your %s." ),
                     bionics[bio.id].name.c_str() );
            return false;
        }

        //We can actually deactivate now, do deactivation-y things
        charge_power( -bionics[bio.id].power_deactivate );
        bio.powered = false;
        add_msg( m_neutral, _( "You deactivate your %s." ), bionics[bio.id].name.c_str() );
    }

    // Deactivation effects go here
    if( bio.id == "bio_cqb" ) {
        // check if player knows current style naturally, otherwise drop them back to style_none
        if( style_selected != matype_id( "style_none" ) ) {
            bool has_style = false;
            for( auto &elem : ma_styles ) {
                if( elem == style_selected ) {
                    has_style = true;
                }
            }
            if( !has_style ) {
                style_selected = matype_id( "style_none" );
            }
        }
    } else if( bio.id == "bio_claws" ) {
        if( weapon.type->id == "bio_claws_weapon" ) {
            add_msg( m_neutral, _( "You withdraw your claws." ) );
            weapon = ret_null;
        }
    } else if( bio.id == "bio_blade" ) {
        if( weapon.type->id == "bio_blade_weapon" ) {
            add_msg( m_neutral, _( "You retract your blade." ) );
            weapon = ret_null;
        }
    } else if( bio.id == "bio_remote" ) {
        if( g->remoteveh() != nullptr && !has_active_item( "remotevehcontrol" ) ) {
            g->setremoteveh( nullptr );
        } else if( get_value( "remote_controlling" ) != "" && !has_active_item( "radiocontrol" ) ) {
            set_value( "remote_controlling", "" );
        }
    } else if( bio.id == "bio_tools" ) {
        invalidate_crafting_inventory();
    }

    // Recalculate stats (strength, mods from pain etc.) that could have been affected
    reset();

    return true;
}

void player::process_bionic( int b )
{
    bionic &bio = my_bionics[b];
    if( !bio.powered ) {
        // Only powered bionics should be processed
        return;
    }

    if( bio.charge > 0 ) {
        // Units already with charge just lose charge
        bio.charge--;
    } else {
        if( bionics[bio.id].charge_time > 0 ) {
            // Try to recharge our bionic if it is made for it
            if( bionics[bio.id].power_over_time > 0 ) {
                if( power_level < bionics[bio.id].power_over_time ) {
                    // No power to recharge, so deactivate
                    bio.powered = false;
                    add_msg( m_neutral, _( "Your %s powers down." ), bionics[bio.id].name.c_str() );
                    // This purposely bypasses the deactivation cost
                    deactivate_bionic( b, true );
                    return;
                } else {
                    // Pay the recharging cost
                    charge_power( -bionics[bio.id].power_over_time );
                    // We just spent our first turn of charge, so -1 here
                    bio.charge = bionics[bio.id].charge_time - 1;
                }
            // Some bionics are a 1-shot activation so they just deactivate at 0 charge.
            } else {
                bio.powered = false;
                add_msg( m_neutral, _( "Your %s powers down." ), bionics[bio.id].name.c_str() );
                // This purposely bypasses the deactivation cost
                deactivate_bionic( b, true );
                return;
            }
        }
    }

    // Bionic effects on every turn they are active go here.
    if( bio.id == "bio_night" ) {
        if( calendar::once_every( 5 ) ) {
            add_msg( m_neutral, _( "Artificial night generator active!" ) );
        }
    } else if( bio.id == "bio_remote" ) {
        if( g->remoteveh() == nullptr && get_value( "remote_controlling" ) == "" ) {
            bio.powered = false;
            add_msg( m_warning, _("Your %s has lost connection and is turning off."),
                     bionics[bio.id].name.c_str() );
        }
    } else if (bio.id == "bio_hydraulics") {
        // Sound of hissing hydraulic muscle! (not quite as loud as a car horn)
        sounds::sound( pos(), 19, _("HISISSS!"));
    }
}

void bionics_uninstall_failure( player *u )
{
    switch( rng( 1, 5 ) ) {
        case 1:
            add_msg( m_neutral, _( "You flub the removal." ) );
            break;
        case 2:
            add_msg( m_neutral, _( "You mess up the removal." ) );
            break;
        case 3:
            add_msg( m_neutral, _( "The removal fails." ) );
            break;
        case 4:
            add_msg( m_neutral, _( "The removal is a failure." ) );
            break;
        case 5:
            add_msg( m_neutral, _( "You screw up the removal." ) );
            break;
    }
    add_msg( m_bad, _( "Your body is severely damaged!" ) );
    u->hurtall( rng( 30, 80 ), u ); // stop hurting yourself!
}

// bionic manipulation chance of success
int bionic_manip_cos( int p_int, int s_electronics, int s_firstaid, int s_mechanics,
                      int bionic_difficulty )
{
    int pl_skill = p_int         * 4 +
                   s_electronics * 4 +
                   s_firstaid    * 3 +
                   s_mechanics   * 1;

    // Medical residents have some idea what they're doing
    if( g->u.has_trait( "PROF_MED" ) ) {
        pl_skill += 3;
        add_msg( m_neutral, _( "You prep yourself to begin surgery." ) );
    }

    // for chance_of_success calculation, shift skill down to a float between ~0.4 - 30
    float adjusted_skill = float ( pl_skill ) - std::min( float ( 40 ),
                           float ( pl_skill ) - float ( pl_skill ) / float ( 10.0 ) );

    // we will base chance_of_success on a ratio of skill and difficulty
    // when skill=difficulty, this gives us 1.  skill < difficulty gives a fraction.
    float skill_difficulty_parameter = float( adjusted_skill / ( 4.0 * bionic_difficulty ) );

    // when skill == difficulty, chance_of_success is 50%. Chance of success drops quickly below that
    // to reserve bionics for characters with the appropriate skill.  For more difficult bionics, the
    // curve flattens out just above 80%
    int chance_of_success = int( ( 100 * skill_difficulty_parameter ) /
                                 ( skill_difficulty_parameter + sqrt( 1 / skill_difficulty_parameter ) ) );

    return chance_of_success;
}

bool player::uninstall_bionic( std::string const &b_id, int skill_level )
{
    // malfunctioning bionics don't have associated items and get a difficulty of 12
    int difficulty = 12;
    if( item::type_is_defined( b_id ) ) {
        auto type = item::find_type( b_id );
        if( type->bionic ) {
            difficulty = type->bionic->difficulty;
        }
    }

    if( !has_bionic( b_id ) ) {
        popup( _( "You don't have this bionic installed." ) );
        return false;
    }
    //If you are paying the doctor to do it, shouldn't use your supplies
    if( !( has_quality( "CUT" ) && has_amount( "1st_aid", 1 ) ) &&
        skill_level == -1 ) {
        popup( _( "Removing bionics requires a cutting tool and a first aid kit." ) );
        return false;
    }

    if( b_id == "bio_blaster" ) {
        popup( _( "Removing your Fusion Blaster Arm would leave you with a useless stump." ) );
        return false;
    }

    if( ( b_id == "bio_reactor" ) || ( b_id == "bio_advreactor" ) ) {
        if( !query_yn(
                _( "WARNING: Removing a reactor may leave radioactive material! Remove anyway?" ) ) ) {
            return false;
        }
    } else if( b_id == "bio_plutdump" ) {
        popup( _( "You must remove your reactor to remove the Plutonium Purger." ) );
    }

    if( b_id == "bio_earplugs" ) {
        popup( _( "You must remove the Enhanced Hearing bionic to remove the Sound Dampeners." ) );
        return false;
    }

    if( b_id == "bio_eye_optic" ) {
        popup( _( "The Telescopic Lenses are part of your eyes now.  Removing them would leave you blind." ) );
        return false;
    }

    if( b_id == "bio_blindfold" ) {
        popup( _( "You must remove the Anti-glare Compensators bionic to remove the Optical Dampers." ) );
        return false;
    }

    // removal of bionics adds +2 difficulty over installation
    int chance_of_success;
    if( skill_level != -1 ) {
        chance_of_success = bionic_manip_cos( skill_level,
                                              skill_level,
                                              skill_level,
                                              skill_level,
                                              difficulty + 2 );
    } else {
        ///\EFFECT_INT increases chance of success removing bionics with unspecified skil level
        chance_of_success = bionic_manip_cos( int_cur,
                                              get_skill_level( skilll_electronics ),
                                              get_skill_level( skilll_firstaid ),
                                              get_skill_level( skilll_mechanics ),
                                              difficulty + 2 );
    }

    if( !query_yn(
            _( "WARNING: %i percent chance of failure and SEVERE bodily damage! Remove anyway?" ),
            100 - chance_of_success ) ) {
        return false;
    }

    // surgery is imminent, retract claws or blade if active
    if( has_bionic( "bio_claws" ) && skill_level == -1 ) {
        if( weapon.type->id == "bio_claws_weapon" ) {
            add_msg( m_neutral, _( "You withdraw your claws." ) );
            weapon = ret_null;
        }
    }

    if( has_bionic( "bio_blade" ) && skill_level == -1 ) {
        if( weapon.type->id == "bio_blade_weapon" ) {
            add_msg( m_neutral, _( "You retract your blade." ) );
            weapon = ret_null;
        }
    }

    //If you are paying the doctor to do it, shouldn't use your supplies
    if( skill_level == -1 ) {
        use_charges( "1st_aid", 1 );
    }

    practice( skilll_electronics, int( ( 100 - chance_of_success ) * 1.5 ) );
    practice( skilll_firstaid, int( ( 100 - chance_of_success ) * 1.0 ) );
    practice( skilll_mechanics, int( ( 100 - chance_of_success ) * 0.5 ) );

    int success = chance_of_success - rng( 1, 100 );

    if( success > 0 ) {
        add_memorial_log( pgettext( "memorial_male", "Removed bionic: %s." ),
                          pgettext( "memorial_female", "Removed bionic: %s." ),
                          bionics[b_id].name.c_str() );
        // until bionics can be flagged as non-removable
        add_msg( m_neutral, _( "You jiggle your parts back into their familiar places." ) );
        add_msg( m_good, _( "Successfully removed %s." ), bionics[b_id].name.c_str() );
        // remove power bank provided by bionic
        max_power_level -= bionics[b_id].capacity;
        remove_bionic( b_id );
        if( b_id == "bio_reactor" || b_id == "bio_advreactor" ) {
            remove_bionic( "bio_plutdump" );
        }
        g->m.spawn_item( pos(), "burnt_out_bionic", 1 );
    } else {
        add_memorial_log( pgettext( "memorial_male", "Removed bionic: %s." ),
                          pgettext( "memorial_female", "Removed bionic: %s." ),
                          bionics[b_id].name.c_str() );
        bionics_uninstall_failure( this );
    }
    g->refresh_all();
    return true;
}

bool player::install_bionics( const itype &type, int skill_level )
{
    if( type.bionic.get() == nullptr ) {
        debugmsg( "Tried to install NULL bionic" );
        return false;
    }
    const std::string &bioid = type.bionic->bionic_id;
    if( bionics.count( bioid ) == 0 ) {
        popup( "invalid / unknown bionic id %s", bioid.c_str() );
        return false;
    }
    if( bioid == "bio_reactor_upgrade" ) {
        if( !has_bionic( "bio_reactor" ) ) {
            popup( _( "There is nothing to upgrade!" ) );
            return false;
        }
    }
    if( has_bionic( bioid ) ) {
        if( !( bioid == "bio_power_storage" || bioid == "bio_power_storage_mkII" ) ) {
            popup( _( "You have already installed this bionic." ) );
            return false;
        }
    }
    const int difficult = type.bionic->difficulty;
    int chance_of_success;
    if( skill_level != -1 ) {
        chance_of_success = bionic_manip_cos( skill_level,
                                              skill_level,
                                              skill_level,
                                              skill_level,
                                              difficult );
    } else {
        ///\EFFECT_INT increases chance of success installing bionics with unspecified skill level
        chance_of_success = bionic_manip_cos( int_cur,
                                              get_skill_level( skilll_electronics ),
                                              get_skill_level( skilll_firstaid ),
                                              get_skill_level( skilll_mechanics ),
                                              difficult );
    }

    const std::map<body_part, int> &issues = bionic_installation_issues( bioid );
    // show all requirements which are not satisfied
    if( !issues.empty() ) {
        std::string detailed_info;
        for( auto& elem : issues ) {
            //~ <Body part name>: <number of slots> more slot(s) needed.
            detailed_info += string_format( _( "\n%s: %i more slot(s) needed." ),
                                            body_part_name_as_heading( elem.first, 1 ).c_str(),
                                            elem.second );
        }
        popup( _( "Not enough space for bionic installation!%s" ), detailed_info.c_str() );
        return false;
    }

    if( !query_yn( _( "WARNING: %i percent chance of genetic damage, blood loss, or damage to existing bionics! Continue anyway?" ),
                   ( 100 - int( chance_of_success ) ) ) ) {
        return false;
    }

    practice( skilll_electronics, int( ( 100 - chance_of_success ) * 1.5 ) );
    practice( skilll_firstaid, int( ( 100 - chance_of_success ) * 1.0 ) );
    practice( skilll_mechanics, int( ( 100 - chance_of_success ) * 0.5 ) );
    int success = chance_of_success - rng( 0, 99 );
    if( success > 0 ) {
        add_memorial_log( pgettext( "memorial_male", "Installed bionic: %s." ),
                          pgettext( "memorial_female", "Installed bionic: %s." ),
                          bionics[bioid].name.c_str() );

        add_msg( m_good, _( "Successfully installed %s." ), bionics[bioid].name.c_str() );
        add_bionic( bioid );

        if( bioid == "bio_eye_optic" && has_trait( "HYPEROPIC" ) ) {
            remove_mutation( "HYPEROPIC" );
        }
        if( bioid == "bio_eye_optic" && has_trait( "MYOPIC" ) ) {
            remove_mutation( "MYOPIC" );
        } else if( bioid == "bio_ears" ) {
            add_bionic( "bio_earplugs" ); // automatically add the earplugs, they're part of the same bionic
        } else if( bioid == "bio_sunglasses" ) {
            add_bionic( "bio_blindfold" ); // automatically add the Optical Dampers, they're part of the same bionic
        } else if( bioid == "bio_reactor_upgrade" ) {
            remove_bionic( "bio_reactor" );
            remove_bionic( "bio_reactor_upgrade" );
            add_bionic( "bio_advreactor" );
        } else if( bioid == "bio_reactor" || bioid == "bio_advreactor" ) {
            add_bionic( "bio_plutdump" );
        }
    } else {
        add_memorial_log( pgettext( "memorial_male", "Installed bionic: %s." ),
                          pgettext( "memorial_female", "Installed bionic: %s." ),
                          bionics[bioid].name.c_str() );
        bionics_install_failure( this, difficult, success );
    }
    g->refresh_all();
    return true;
}

void bionics_install_failure( player *u, int difficulty, int success )
{
    // "success" should be passed in as a negative integer representing how far off we
    // were for a successful install.  We use this to determine consequences for failing.
    success = abs( success );

    // it would be better for code reuse just to pass in skill as an argument from install_bionic
    // pl_skill should be calculated the same as in install_bionics
    ///\EFFECT_INT randomly decreases severity of bionics installation failure
    int pl_skill = u->int_cur * 4 +
                   u->get_skill_level( skilll_electronics ) * 4 +
                   u->get_skill_level( skilll_firstaid )    * 3 +
                   u->get_skill_level( skilll_mechanics )   * 1;
    // Medical residents get a substantial assist here
    if( u->has_trait( "PROF_MED" ) ) {
        pl_skill += 6;
    }

    // for failure_level calculation, shift skill down to a float between ~0.4 - 30
    float adjusted_skill = float ( pl_skill ) - std::min( float ( 40 ),
                           float ( pl_skill ) - float ( pl_skill ) / float ( 10.0 ) );

    // failure level is decided by how far off the character was from a successful install, and
    // this is scaled up or down by the ratio of difficulty/skill.  At high skill levels (or low
    // difficulties), only minor consequences occur.  At low skill levels, severe consequences
    // are more likely.
    int failure_level = int( sqrt( success * 4.0 * difficulty / float ( adjusted_skill ) ) );
    int fail_type = ( failure_level > 5 ? 5 : failure_level );

    if( fail_type <= 0 ) {
        add_msg( m_neutral, _( "The installation fails without incident." ) );
        return;
    }

    switch( rng( 1, 5 ) ) {
        case 1:
            add_msg( m_neutral, _( "You flub the installation." ) );
            break;
        case 2:
            add_msg( m_neutral, _( "You mess up the installation." ) );
            break;
        case 3:
            add_msg( m_neutral, _( "The installation fails." ) );
            break;
        case 4:
            add_msg( m_neutral, _( "The installation is a failure." ) );
            break;
        case 5:
            add_msg( m_neutral, _( "You screw up the installation." ) );
            break;
    }

    if( u->has_trait( "PROF_MED" ) ) {
        //~"Complications" is USian medical-speak for "unintended damage from a medical procedure".
        add_msg( m_neutral, _( "Your training helps you minimize the complications." ) );
        // In addition to the bonus, medical residents know enough OR protocol to avoid botching.
        // Take MD and be immune to faulty bionics.
        if( fail_type == 5 ) {
            fail_type = rng( 1, 3 );
        }
    }

    if( fail_type == 3 && u->num_bionics() == 0 ) {
        fail_type = 2;    // If we have no bionics, take damage instead of losing some
    }

    switch( fail_type ) {

        case 1:
            if( !( u->has_trait( "NOPAIN" ) ) ) {
                add_msg( m_bad, _( "It really hurts!" ) );
                u->mod_pain( rng( failure_level * 3, failure_level * 6 ) );
            }
            break;

        case 2:
            add_msg( m_bad, _( "Your body is damaged!" ) );
            u->hurtall( rng( failure_level, failure_level * 2 ), u ); // you hurt yourself
            break;

        case 3:
            if( u->num_bionics() <= failure_level && u->max_power_level == 0 ) {
                add_msg( m_bad, _( "All of your existing bionics are lost!" ) );
            } else {
                add_msg( m_bad, _( "Some of your existing bionics are lost!" ) );
            }
            for( int i = 0; i < failure_level && u->remove_random_bionic(); i++ ) {
                ;
            }
            break;

        case 4:
            add_msg( m_mixed, _( "You do damage to your genetics, causing mutation!" ) );
            while( failure_level > 0 ) {
                u->mutate();
                failure_level -= rng( 1, failure_level + 2 );
            }
            break;

        case 5: {
            add_msg( m_bad, _( "The installation is faulty!" ) );
            std::vector<std::string> valid;
            std::copy_if( begin( faulty_bionics ), end( faulty_bionics ), std::back_inserter( valid ),
            [&]( std::string const & id ) {
                return !u->has_bionic( id );
            } );

            if( valid.empty() ) { // We've got all the bad bionics!
                if( u->max_power_level > 0 ) {
                    int old_power = u->max_power_level;
                    add_msg( m_bad, _( "You lose power capacity!" ) );
                    u->max_power_level = rng( 0, u->max_power_level - 25 );
                    u->add_memorial_log( pgettext( "memorial_male", "Lost %d units of power capacity." ),
                                         pgettext( "memorial_female", "Lost %d units of power capacity." ),
                                         old_power - u->max_power_level );
                }
                // TODO: What if we can't lose power capacity?  No penalty?
            } else {
                const std::string &id = random_entry( valid );
                u->add_bionic( id );
                u->add_memorial_log( pgettext( "memorial_male", "Installed bad bionic: %s." ),
                                     pgettext( "memorial_female", "Installed bad bionic: %s." ),
                                     bionics[ id ].name.c_str() );
            }
        }
        break;
    }
}

std::string list_occupied_bps( const std::string &bio_id, const std::string &intro,
                               const bool each_bp_on_new_line )
{
    if( bionic_info( bio_id ).occupied_bodyparts.empty() ) {
    return "";
    }
    std::ostringstream desc;
    desc << intro;
    for( const auto &elem : bionic_info( bio_id ).occupied_bodyparts ) {
        desc << ( each_bp_on_new_line ? "\n" : " " );
        //~ <Bodypart name> (<number of occupied slots> slots);
        desc << string_format( _( "%s (%i slots);" ),
                           body_part_name_as_heading( elem.first, 1 ).c_str(),
                           elem.second );
    }
    return desc.str();
}

int player::get_used_bionics_slots( const body_part bp ) const
{
    int used_slots = 0;
    for( auto &bio : my_bionics ) {
        auto search = bionics[bio.id].occupied_bodyparts.find( bp );
        if( search != bionics[bio.id].occupied_bodyparts.end() ) {
            used_slots += search->second;
        }
    }

    return used_slots;
}

std::map<body_part, int> player::bionic_installation_issues( const std::string &bioid )
{
    std::map<body_part, int> issues;
    if( !has_trait( "DEBUG_CBM_SLOTS" ) ) {
        return issues;
    }
    for( auto &elem : bionics[ bioid ].occupied_bodyparts ) {
        const int lacked_slots = elem.second - get_free_bionics_slots( elem.first );
        if( lacked_slots > 0 ) {
            issues.emplace( elem.first, lacked_slots );
        }
    }
    return issues;
}

int player::get_total_bionics_slots( const body_part bp ) const
{
    switch( bp ) {
    case bp_torso:
        return 80;

    case bp_head:
        return 18;

    case bp_eyes:
        return 4;

    case bp_mouth:
        return 4;

    case bp_arm_l:
    case bp_arm_r:
        return 20;

    case bp_hand_l:
    case bp_hand_r:
        return 5;

    case bp_leg_l:
    case bp_leg_r:
        return 30;

    case bp_foot_l:
    case bp_foot_r:
        return 7;

    case num_bp:
        debugmsg( "number of slots for incorrect bodypart is requested!" );
        return 0;
    }
    return 0;
}

int player::get_free_bionics_slots( const body_part bp ) const
{
    return get_total_bionics_slots( bp ) - get_used_bionics_slots( bp );
}

void player::add_bionic( std::string const &b )
{
    if( has_bionic( b ) ) {
        debugmsg( "Tried to install bionic %s that is already installed!", b.c_str() );
        return;
    }

    int pow_up = bionics[b].capacity;
    max_power_level += pow_up;
    if( b == "bio_power_storage" || b == "bio_power_storage_mkII" ) {
        add_msg_if_player( m_good, _( "Increased storage capacity by %i." ), pow_up );
        // Power Storage CBMs are not real bionic units, so return without adding it to my_bionics
        return;
    }

    my_bionics.push_back( bionic( b, get_free_invlet( *this ) ) );
    if( b == "bio_tools" || b == "bio_ears" ) {
        activate_bionic( my_bionics.size() - 1 );
    }
    recalc_sight_limits();
}

void player::remove_bionic( std::string const &b )
{
    std::vector<bionic> new_my_bionics;
    for( auto &i : my_bionics ) {
        if( b == i.id ) {
            continue;
        }

        // Ears and earplugs and sunglasses and blindfold go together like peanut butter and jelly.
        // Therefore, removing one, should remove the other.
        if( ( b == "bio_ears" && i.id == "bio_earplugs" ) ||
            ( b == "bio_earplugs" && i.id == "bio_ears" ) ) {
            continue;
        } else if( ( b == "bio_sunglasses" && i.id == "bio_blindfold" ) ||
                   ( b == "bio_blindfold" && i.id == "bio_sunglasses" ) ) {
            continue;
        }

        new_my_bionics.push_back( bionic( i.id, i.invlet ) );
    }
    my_bionics = new_my_bionics;
    recalc_sight_limits();
}

int player::num_bionics() const
{
    return my_bionics.size();
}

std::pair<int, int> player::amount_of_storage_bionics() const
{
    int lvl = max_power_level;

    // exclude amount of power capacity obtained via non-power-storage CBMs
    for( auto it : my_bionics ) {
        lvl -= bionics[it.id].capacity;
    }

    std::pair<int, int> results( 0, 0 );
    if( lvl <= 0 ) {
        return results;
    }

    int pow_mkI = bionics["bio_power_storage"].capacity;
    int pow_mkII = bionics["bio_power_storage_mkII"].capacity;

    while( lvl >= std::min( pow_mkI, pow_mkII ) ) {
        if( one_in( 2 ) ) {
            if( lvl >= pow_mkI ) {
                results.first++;
                lvl -= pow_mkI;
            }
        } else {
            if( lvl >= pow_mkII ) {
                results.second++;
                lvl -= pow_mkII;
            }
        }
    }
    return results;
}

bionic &player::bionic_at_index( int i )
{
    return my_bionics[i];
}



// Returns true if a bionic was removed.
bool player::remove_random_bionic()
{
    const int numb = num_bionics();
    if( numb ) {
        int rem = rng( 0, num_bionics() - 1 );
        const auto bionic = my_bionics[rem];
        remove_bionic( bionic.id );
        recalc_sight_limits();
    }
    return numb;
}

void reset_bionics()
{
    bionics.clear();
    faulty_bionics.clear();
}

void load_bionic( JsonObject &jsobj )
{
    std::string id = jsobj.get_string( "id" );
    std::string name = _( jsobj.get_string( "name" ).c_str() );
    std::string description = _( jsobj.get_string( "description" ).c_str() );
    int on_cost = jsobj.get_int( "act_cost", 0 );

    bool toggled = jsobj.get_bool( "toggled", false );
    // Requires ability to toggle
    int off_cost = jsobj.get_int( "deact_cost", 0 );

    int time = jsobj.get_int( "time", 0 );
    // Requires a non-zero time
    int react_cost = jsobj.get_int( "react_cost", 0 );

    int capacity = jsobj.get_int( "capacity", 0 );

    bool faulty = jsobj.get_bool( "faulty", false );
    bool power_source = jsobj.get_bool( "power_source", false );

    std::map<body_part, size_t> occupied_bodyparts;
    JsonArray jsarr = jsobj.get_array( "occupied_bodyparts" );
    if( !jsarr.empty() ) {
        while( jsarr.has_more() ) {
            JsonArray ja = jsarr.next_array();
            occupied_bodyparts.emplace( get_body_part_token( ja.get_string( 0 ) ),
                                        ja.get_int( 1 ) );
        }
    }

    if( faulty ) {
        faulty_bionics.push_back( id );
    }

    auto const result = bionics.insert( std::make_pair( std::move( id ),
                                        bionic_data( std::move( name ), power_source, toggled,
                                                on_cost, off_cost, react_cost, time, capacity,
                                                std::move( description ), faulty,
                                                std::move( occupied_bodyparts ) ) ) );

    if( !result.second ) {
        debugmsg( "duplicate bionic id" );
    }
}

void bionic::serialize( JsonOut &json ) const
{
    json.start_object();
    json.member( "id", id );
    json.member( "invlet", ( int )invlet );
    json.member( "powered", powered );
    json.member( "charge", charge );
    json.end_object();
}

void bionic::deserialize( JsonIn &jsin )
{
    JsonObject jo = jsin.get_object();
    id = jo.get_string( "id" );
    invlet = jo.get_int( "invlet" );
    powered = jo.get_bool( "powered" );
    charge = jo.get_int( "charge" );
}
