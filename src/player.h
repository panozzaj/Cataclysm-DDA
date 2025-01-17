#ifndef PLAYER_H
#define PLAYER_H

#include "character.h"
#include "craft_command.h"
#include "item.h"
#include "player_activity.h"
#include "weighted_list.h"
#include "game_constants.h"

#include <unordered_set>
#include <bitset>
#include <memory>
#include <array>

static const std::string DEFAULT_HOTKEYS("1234567890abcdefghijklmnopqrstuvwxyz");

enum action_id : int;

class monster;
class game;
struct trap;
class mission;
class profession;
nc_color encumb_color(int level);
enum morale_type : int;
enum game_message_type : int;
class ma_technique;
class martialart;
struct recipe;
struct item_comp;
struct tool_comp;
class vehicle;
class vitamin;
using vitamin_id = string_id<vitamin>;
class start_location;
using start_location_id = string_id<start_location>;
struct w_point;
struct points_left;

// This tries to represent both rating and
// player's decision to respect said rating
enum edible_rating {
    // Edible or we pretend it is
    EDIBLE,
    // Not food at all
    INEDIBLE,
    // Not food because mutated mouth/system
    INEDIBLE_MUTATION,
    // You can eat it, but it will hurt morale
    ALLERGY,
    // Smaller allergy penalty
    ALLERGY_WEAK,
    // Cannibalism (unless psycho/cannibal)
    CANNIBALISM,
    // Rotten or not rotten enough (for saprophages)
    ROTTEN,
    // We can eat this, but we'll overeat
    TOO_FULL,
    // Some weird stuff that requires a tool we don't have
    NO_TOOL
};

struct special_attack {
    std::string text;
    int bash;
    int cut;
    int stab;

    special_attack()
    {
        bash = 0;
        cut = 0;
        stab = 0;
    };
};

class player_morale;
class player_morale_ptr : public std::unique_ptr<player_morale> {
    public:
        player_morale_ptr() = default;
        player_morale_ptr( const player_morale_ptr &rhs );
        player_morale_ptr( player_morale_ptr &&rhs );
        player_morale_ptr &operator = ( const player_morale_ptr &rhs );
        player_morale_ptr &operator = ( player_morale_ptr &&rhs );

        ~player_morale_ptr();
};

// The minimum level recoil will reach without aiming.
// Sets the floor for accuracy of a "snap" or "hip" shot.
#define MIN_RECOIL 150

//Don't forget to add new memorial counters
//to the save and load functions in savegame_json.cpp
struct stats : public JsonSerializer, public JsonDeserializer {
    int squares_walked;
    int damage_taken;
    int damage_healed;
    int headshots;

    void reset()
    {
        squares_walked = 0;
        damage_taken = 0;
        damage_healed = 0;
        headshots = 0;
    }

    stats()
    {
        reset();
    }

    using JsonSerializer::serialize;
    void serialize(JsonOut &json) const override
    {
        json.start_object();
        json.member("squares_walked", squares_walked);
        json.member("damage_taken", damage_taken);
        json.member("damage_healed", damage_healed);
        json.member("headshots", headshots);
        json.end_object();
    }
    using JsonDeserializer::deserialize;
    void deserialize(JsonIn &jsin) override
    {
        JsonObject jo = jsin.get_object();
        jo.read("squares_walked", squares_walked);
        jo.read("damage_taken", damage_taken);
        jo.read("damage_healed", damage_healed);
        jo.read("headshots", headshots);
    }
};

class player : public Character, public JsonSerializer, public JsonDeserializer
{
    public:
        player();
        player(const player &) = default;
        player(player &&) = default;
        virtual ~player() override;
        player &operator=(const player &) = default;
        player &operator=(player &&) = default;

        // newcharacter.cpp
        bool create(character_type type, std::string tempname = "");
        void randomize( bool random_scenario, points_left &points );
        bool load_template( const std::string &template_name );
        /** Calls Character::normalize()
         *  normalizes HP and bodytemperature
         */

        void normalize() override;

        /** Returns either "you" or the player's name */
        std::string disp_name(bool possessive = false) const override;
        /** Returns the name of the player's outer layer, e.g. "armor plates" */
        std::string skin_name() const override;

        virtual bool is_player() const override {
            return true;
        }

        /** Handles human-specific effect application effects before calling Creature::add_eff_effects(). */
        virtual void add_eff_effects(effect e, bool reduced) override;
        /** Processes human-specific effects effects before calling Creature::process_effects(). */
        void process_effects() override;
        /** Handles the still hard-coded effects. */
        void hardcoded_effects(effect &it);
        /** Returns the modifier value used for vomiting effects. */
        double vomit_mod();

        virtual bool is_npc() const override {
            return false;    // Overloaded for NPCs in npc.h
        }
        /** Returns what color the player should be drawn as */
        virtual nc_color basic_symbol_color() const override;

        /** Deserializes string data when loading files */
        virtual void load_info(std::string data);
        /** Outputs a serialized json string for saving */
        virtual std::string save_info() const;

        int print_info(WINDOW *w, int vStart, int vLines, int column) const override;

        // populate variables, inventory items, and misc from json object
        using JsonDeserializer::deserialize;
        virtual void deserialize(JsonIn &jsin) override;

        using JsonSerializer::serialize;
        // by default save all contained info
        virtual void serialize(JsonOut &jsout) const override;

        /** Prints out the player's memorial file */
        void memorial( std::ofstream &memorial_file, std::string epitaph );
        /** Handles and displays detailed character info for the '@' screen */
        void disp_info();
        /** Provides the window and detailed morale data */
        void disp_morale();
        /** Print the player's stamina bar. **/
        void print_stamina_bar( WINDOW *w ) const;
        /** Generates the sidebar and it's data in-game */
        void disp_status(WINDOW *w, WINDOW *w2);

        /** Resets stats, and applies effects in an idempotent manner */
        void reset_stats() override;
        /** Resets movement points and applies other non-idempotent changes */
        void process_turn() override;
        /** Drop items randomly if insufficient inventory space except during pending activity */
        virtual void drop_inventory_overflow() override;
        /** Calculates the various speed bonuses we will get from mutations, etc. */
        void recalc_speed_bonus();
        /** Called after every action, invalidates player caches */
        void action_taken();
        /** Ticks down morale counters and removes them */
        void update_morale();
        /** Ensures persistent morale effects are up-to-date */
        void apply_persistent_morale();
        /** Uses calc_focus_equilibrium to update the player's current focus */
        void update_mental_focus();
        /** Uses morale and other factors to return the player's focus gain rate */
        int calc_focus_equilibrium() const;
        /** Maintains body temperature */
        void update_bodytemp();
        /** Define color for displaying the body temperature */
        nc_color bodytemp_color(int bp) const;
        /** Returns the player's modified base movement cost */
        int  run_cost(int base_cost, bool diag = false) const;
        /** Returns the player's speed for swimming across water tiles */
        int  swim_speed() const;
        /** Maintains body wetness and handles the rate at which the player dries */
        void update_body_wetness( const w_point &weather );
        /** Updates all "biology" by one turn. Should be called once every turn. */
        void update_body();
        /** Updates all "biology" as if time between `from` and `to` passed. */
        void update_body( int from, int to );
        /** Increases hunger, thirst, fatigue and stimms wearing off. `rate_multiplier` is for retroactive updates. */
        void update_needs( int rate_multiplier );
        /**
          * Handles passive regeneration of pain and maybe hp, except sleep regeneration.
          * Updates health and checks for sickness.
          */
        void regen( int rate_multiplier );
        /** Regenerates stamina */
        void update_stamina( int turns );
        /** Kills the player if too hungry, stimmed up etc., forces tired player to sleep and prints warnings. */
        void check_needs_extremes();
        /** Handles hp regen during sleep. */
        void sleep_hp_regen( int rate_multiplier );

        /** Returns if the player has hibernation mutation and is asleep and well fed */
        bool is_hibernating() const;

        /** Returns true if the player has a conflicting trait to the entered trait
         *  Uses has_opposite_trait(), has_lower_trait(), and has_higher_trait() to determine conflicts.
         */
        bool has_conflicting_trait(const std::string &flag) const;
        /** Returns true if the player has a trait which cancels the entered trait */
        bool has_opposite_trait(const std::string &flag) const;
        /** Returns true if the player has a trait which upgrades into the entered trait */
        bool has_lower_trait(const std::string &flag) const;
        /** Returns true if the player has a trait which is an upgrade of the entered trait */
        bool has_higher_trait(const std::string &flag) const;
        /** Returns true if the player has crossed a mutation threshold
         *  Player can only cross one mutation threshold.
         */
        bool crossed_threshold() const;
        /** Returns true if the entered trait may be purified away
         *  Defaults to true
         */
        bool purifiable(const std::string &flag) const;
        /** Modifies mutation_category_level[] based on the entered trait */
        void set_cat_level_rec(const std::string &sMut);
        /** Recalculates mutation_category_level[] values for the player */
        void set_highest_cat_level();
        /** Returns the highest mutation category */
        std::string get_highest_category() const;
        /** Returns a dream's description selected randomly from the player's highest mutation category */
        std::string get_category_dream(const std::string &cat, int strength) const;

        /** Returns true if the player is in a climate controlled area or armor */
        bool in_climate_control();

        /** Returns true if the player is wearing an active optical cloak */
        bool has_active_optcloak() const;
        /** Adds a bionic to my_bionics[] */
        void add_bionic(std::string const &b);
        /** Removes a bionic from my_bionics[] */
        void remove_bionic(std::string const &b);
        /** Used by the player to perform surgery to remove bionics and possibly retrieve parts */
        bool uninstall_bionic(std::string const &b_id, int skill_level = -1);
        /** Adds the entered amount to the player's bionic power_level */
        void charge_power(int amount);
        /** Generates and handles the UI for player interaction with installed bionics */
        void power_bionics();
        void power_mutations();
        /** Handles bionic activation effects of the entered bionic, returns if anything activated */
        bool activate_bionic(int b, bool eff_only = false);
        /** Handles bionic deactivation effects of the entered bionic, returns if anything deactivated */
        bool deactivate_bionic(int b, bool eff_only = false);
        /** Handles bionic effects over time of the entered bionic */
        void process_bionic(int b);
        /** Randomly removes a bionic from my_bionics[] */
        bool remove_random_bionic();
        /** Returns the size of my_bionics[] */
        int num_bionics() const;
        /** Returns amount of Storage CBMs in the corpse **/
        std::pair<int, int> amount_of_storage_bionics() const;
        /** Returns the bionic at a given index in my_bionics[] */
        bionic &bionic_at_index(int i);
        /** Returns the bionic with the given invlet, or NULL if no bionic has that invlet */
        bionic *bionic_by_invlet( long ch );
        /** Returns player lumination based on the brightest active item they are carrying */
        float active_light() const;

        /** Returns true if the player doesn't have the mutation or a conflicting one and it complies with the force typing */
        bool mutation_ok( const std::string &mutation, bool force_good, bool force_bad ) const;
        /** Picks a random valid mutation and gives it to the player, possibly removing/changing others along the way */
        void mutate();
        /** Picks a random valid mutation in a category and mutate_towards() it */
        void mutate_category( const std::string &mut_cat );
        /** Mutates toward the entered mutation, upgrading or removing conflicts if necessary */
        bool mutate_towards( const std::string &mut );
        /** Removes a mutation, downgrading to the previous level if possible */
        void remove_mutation( const std::string &mut );
        /** Returns true if the player has the entered mutation child flag */
        bool has_child_flag( const std::string &mut ) const;
        /** Removes the mutation's child flag from the player's list */
        void remove_child_flag( const std::string &mut );

        const tripoint &pos() const override;
        /** Returns the player's sight range */
        int sight_range( int light_level ) const override;
        /** Returns the player maximum vision range factoring in mutations, diseases, and other effects */
        int  unimpaired_range() const;
        /** Returns true if overmap tile is within player line-of-sight */
        bool overmap_los( const tripoint &omt, int sight_points );
        /** Returns the distance the player can see on the overmap */
        int  overmap_sight_range(int light_level) const;
        /** Returns the distance the player can see through walls */
        int  clairvoyance() const;
        /** Returns true if the player has some form of impaired sight */
        bool sight_impaired() const;
        /** Returns true if the player has two functioning arms */
        bool has_two_arms() const;
        /** Returns true if the player is wielding something, including bionic weapons */
        bool is_armed() const;
        /** Calculates melee weapon wear-and-tear through use, returns true if item is destroyed. */
        bool handle_melee_wear( float wear_multiplier = 1.0f );
        bool handle_melee_wear( item &shield, float wear_multiplier = 1.0f );
        /** True if unarmed or wielding a weapon with the UNARMED_WEAPON flag */
        bool unarmed_attack() const;
        /** Called when a player triggers a trap, returns true if they don't set it off */
        bool avoid_trap( const tripoint &pos, const trap &tr ) const override;
        /** Picks a random body part, adjusting for mutations, broken body parts etc. */
        body_part get_random_body_part( bool main ) const override;

        /** Returns true if the player or their vehicle has an alarm clock */
        bool has_alarm_clock() const;
        /** Returns true if the player or their vehicle has a watch */
        bool has_watch() const;

        using Creature::sees;
        // see Creature::sees
        bool sees( const tripoint &c, bool is_player = false ) const override;
        // see Creature::sees
        bool sees( const Creature &critter ) const override;
        /**
         * Get all hostile creatures currently visible to this player.
         */
         std::vector<Creature*> get_hostile_creatures() const;

        /**
         * Returns all creatures that this player can see and that are in the given
         * range. This player object itself is never included.
         * The player character (g->u) is checked and might be included (if applicable).
         * @param range The maximal distance (@ref rl_dist), creatures at this distance or less
         * are included.
         */
        std::vector<Creature*> get_visible_creatures( int range ) const;
        /**
         * As above, but includes all creatures the player can detect well enough to target
         * with ranged weapons, e.g. with infared vision.
         */
        std::vector<Creature*> get_targetable_creatures( int range ) const;
        /**
         * Check whether the this player can see the other creature with infrared. This implies
         * this player can see infrared and the target is visible with infrared (is warm).
         * And of course a line of sight exists.
         */
        bool sees_with_infrared( const Creature &critter ) const;

        Attitude attitude_to( const Creature &other ) const override;

        void pause(); // '.' command; pauses & reduces recoil
        void toggle_move_mode(); // Toggles to the next move mode.
        void shout( std::string text = "" );

        // martialarts.cpp
        /** Fires all non-triggered martial arts events */
        void ma_static_effects();
        /** Fires all move-triggered martial arts events */
        void ma_onmove_effects();
        /** Fires all hit-triggered martial arts events */
        void ma_onhit_effects();
        /** Fires all attack-triggered martial arts events */
        void ma_onattack_effects();
        /** Fires all dodge-triggered martial arts events */
        void ma_ondodge_effects();
        /** Fires all block-triggered martial arts events */
        void ma_onblock_effects();
        /** Fires all get hit-triggered martial arts events */
        void ma_ongethit_effects();

        /** Returns true if the player has any martial arts buffs attached */
        bool has_mabuff(mabuff_id buff_id) const;
        /** Returns true if the player has access to the entered martial art */
        bool has_martialart(const matype_id &ma_id) const;
        /** Adds the entered martial art to the player's list */
        void add_martialart(const matype_id &ma_id);

        /** Returns the to hit bonus from martial arts buffs */
        int mabuff_tohit_bonus() const;
        /** Returns the dodge bonus from martial arts buffs */
        int mabuff_dodge_bonus() const;
        /** Returns the block bonus from martial arts buffs */
        int mabuff_block_bonus() const;
        /** Returns the speed bonus from martial arts buffs */
        int mabuff_speed_bonus() const;
        /** Returns the armor bonus against given type from martial arts buffs */
        int mabuff_armor_bonus( damage_type type ) const;
        /** Returns the damage multiplier to given type from martial arts buffs */
        float mabuff_damage_mult( damage_type type ) const;
        /** Returns the flat damage bonus to given type from martial arts buffs, applied after the multiplier */
        int mabuff_damage_bonus( damage_type type ) const;
        /** Returns true if the player is immune to throws */
        bool is_throw_immune() const;
        /** Returns value of player's stable footing */
        int stability_roll() const override;
        /** Returns true if the player has quiet melee attacks */
        bool is_quiet() const;
        /** Returns true if the current martial art works with the player's current weapon */
        bool can_melee() const;
        /** Returns true if the current martial art ignores the current weapon and uses unarmed attacks instead. */
        bool unarmed_override() const;
        /** Always returns false, since players can't dig currently */
        bool digging() const override;
        /** Returns true if the player is knocked over or has broken legs */
        bool is_on_ground() const override;
        /** Returns true if the player should be dead */
        bool is_dead_state() const override;
        /** Returns true is the player is protected from electric shocks */
        bool is_elec_immune() const override;
        /** Returns true if the player is immune to this kind of effect */
        bool is_immune_effect( const efftype_id& ) const override;
        /** Returns true if the player is immune to this kind of damage */
        bool is_immune_damage( const damage_type ) const override;

        /** Returns true if the player has technique-based miss recovery */
        bool has_miss_recovery_tec() const;
        /** Returns true if the player has a grab breaking technique available */
        bool has_grab_break_tec() const override;
        /** Returns true if the player has the leg block technique available */
        bool can_leg_block() const;
        /** Returns true if the player has the arm block technique available */
        bool can_arm_block() const;
        /** Returns true if either can_leg_block() or can_arm_block() returns true */
        bool can_limb_block() const;

        // melee.cpp
        /** Returns the best item for blocking with */
        item &best_shield();
        /** Returns true if the player has a weapon with a block technique */
        bool can_weapon_block() const;
        using Creature::melee_attack;
        /**
         * Sets up a melee attack and handles melee attack function calls
         * @param t
         * @param allow_special whether non-forced martial art technique or mutation attack should be
         *   possible with this attack.
         * @param force_technique special technique to use in attack.
         */
        void melee_attack(Creature &t, bool allow_special, const matec_id &force_technique) override;
        /** Returns a weapon's modified dispersion value */
        double get_weapon_dispersion( const item *weapon, bool random ) const;
        /** Returns true if a gun misfires, jams, or has other problems, else returns false */
        bool handle_gun_damage( const itype &firing, const std::set<std::string> &curammo_effects );

        /**
         *  Fires a gun or axuiliary gunmod (ignoring any current mode)
         *  @param target where the first shot is aimed at (may vary for later shots)
         *  @param shots maximum number of shots to fire (less may be fired in some circumstances)
         *  @param gun item to fire (which does not necessary have to be in the players possession)
         *  @return number of shots actually fired
         */
        int fire_gun( const tripoint &target, int shots = 1 );
        int fire_gun( const tripoint &target, int shots, item& gun );

        /** Handles reach melee attacks */
        void reach_attack( const tripoint &target );

        /** Checks for valid block abilities and reduces damage accordingly. Returns true if the player blocks */
        bool block_hit(Creature *source, body_part &bp_hit, damage_instance &dam) override;
        /**
         * Reduces and mutates du, prints messages about armor taking damage.
         * @return true if the armor was completely destroyed (and the item must be deleted).
         */
        bool armor_absorb(damage_unit &du, item &armor);
        /** Runs through all bionics and armor on a part and reduces damage through their armor_absorb */
        void absorb_hit(body_part bp, damage_instance &dam) override;
        /** Called after the player has successfully dodged an attack */
        void on_dodge( Creature *source, int difficulty ) override;
        /** Handles special defenses from an attack that hit us (source can be null) */
        void on_hit( Creature *source, body_part bp_hit = num_bp,
                     int difficulty = INT_MIN, dealt_projectile_attack const* const proj = nullptr ) override;
        /** Handles effects that happen when the player is damaged and aware of the fact. */
        void on_hurt( Creature *source, bool disturb = true );

        /** Returns the bonus bashing damage the player deals based on their stats */
        float bonus_damage( bool random ) const;
        /** Returns Creature::get_hit_base() modified by weapon skill */
        int get_hit_base() const override;
        /** Returns the player's basic hit roll that is compared to the target's dodge roll */
        int hit_roll() const override;
        /** Returns the chance to crit given a hit roll and target's dodge roll */
        double crit_chance( int hit_roll, int target_dodge, const item &weap ) const;
        /** Returns true if the player scores a critical hit */
        bool scored_crit(int target_dodge = 0) const;
        /** Returns cost (in moves) of attacking with given item (no modifiers, like stuck) */
        int attack_speed( const item &weap, bool average = false ) const;
        /** Gets melee accuracy component from weapon+skills */
        int get_hit_weapon( const item &weap ) const;
        /** NPC-related item rating functions */
        double weapon_value( const item &weap, long ammo = 10 ) const; // Evaluates item as a weapon
        double gun_value( const item &weap, long ammo = 10 ) const; // Evaluates item as a gun
        double melee_value( const item &weap ) const; // As above, but only as melee
        double unarmed_value() const; // Evaluate yourself!

        // If average == true, adds expected values of random rolls instead of rolling.
        /** Adds all 3 types of physical damage to instance */
        void roll_all_damage( bool crit, damage_instance &di ) const;
        void roll_all_damage( bool crit, damage_instance &di, bool average, const item &weap ) const;
        /** Adds player's total bash damage to the damage instance */
        void roll_bash_damage( bool crit, damage_instance &di, bool average, const item &weap ) const;
        /** Adds player's total cut damage to the damage instance */
        void roll_cut_damage( bool crit, damage_instance &di, bool average, const item &weap ) const;
        /** Adds player's total stab damage to the damage instance */
        void roll_stab_damage( bool crit, damage_instance &di, bool average, const item &weap ) const;
        /** Returns the number of moves unsticking a weapon will penalize for */
        int roll_stuck_penalty( bool stabbing, const ma_technique &tec ) const;
        std::vector<matec_id> get_all_techniques() const;

        /** Returns true if the player has a weapon or martial arts skill available with the entered technique */
        bool has_technique( const matec_id & tec ) const;
        /** Returns a random valid technique */
        matec_id pick_technique(Creature &t,
                                bool crit, bool dodge_counter, bool block_counter);
        void perform_technique(const ma_technique &technique, Creature &t, damage_instance &di, int &move_cost);
        /** Performs special attacks and their effects (poisonous, stinger, etc.) */
        void perform_special_attacks(Creature &t);

        /** Returns a vector of valid mutation attacks */
        std::vector<special_attack> mutation_attacks(Creature &t) const;
        /** Handles combat effects, returns a string of any valid combat effect messages */
        std::string melee_special_effects(Creature &t, damage_instance &d, const ma_technique &tec);
        /** Returns Creature::get_dodge_base modified by the player's skill level */
        int get_dodge_base() const override;   // Returns the players's dodge, modded by clothing etc
        /** Returns Creature::get_dodge() modified by any player effects */
        int get_dodge() const override;
        /** Returns the player's dodge_roll to be compared against an agressor's hit_roll() */
        int dodge_roll() override;

        /** Returns melee skill level, to be used to throttle dodge practice. **/
        int get_melee() const override;
        /**
         * Adds a reason for why the player would miss a melee attack.
         *
         * To possibly be messaged to the player when he misses a melee attack.
         * @param reason A message for the player that gives a reason for him missing.
         * @param weight The weight used when choosing what reason to pick when the
         * player misses.
         */
        void add_miss_reason(const char *reason, unsigned int weight);
        /** Clears the list of reasons for why the player would miss a melee attack. */
        void clear_miss_reasons();
        /**
         * Returns an explanation for why the player would miss a melee attack.
         */
        const char *get_miss_reason();

        /** Handles the uncanny dodge bionic and effects, returns true if the player successfully dodges */
        bool uncanny_dodge() override;
        /** Returns an unoccupied, safe adjacent point. If none exists, returns player position. */
        tripoint adjacent_tile() const;

        /**
         * Checks both the neighborhoods of from and to for climbable surfaces,
         * returns move cost of climbing from `from` to `to`.
         * 0 means climbing is not possible.
         * Return value can depend on the orientation of the terrain.
         */
        int climbing_cost( const tripoint &from, const tripoint &to ) const;

        // ranged.cpp
        /** Execute a throw */
        dealt_projectile_attack throw_item( const tripoint &target, const item &thrown );
        /** Returns the throwing attack dexterity mod */
        int throw_dex_mod(bool return_stat_effect = true) const;

        // Mental skills and stats
        /** Returns the player's reading speed */
        int read_speed( bool real_life = true ) const;
        /** Returns the player's skill rust rate */
        int rust_rate( bool real_life = true ) const;
        /** Returns a value used when attempting to convince NPC's of something */
        int talk_skill() const;
        /** Returns a value used when attempting to intimidate NPC's */
        int intimidation() const;

        /** Calls Creature::deal_damage and handles damaged effects (waking up, etc.) */
        dealt_damage_instance deal_damage(Creature *source, body_part bp, const damage_instance &d) override;
        /** Actually hurt the player, hurts a body_part directly, no armor reduction */
        void apply_damage(Creature *source, body_part bp, int amount) override;
        /** Modifies a pain value by player traits before passing it to Creature::mod_pain() */
        void mod_pain(int npain) override;
        /** Sets new intensity of pain an reacts to it */
        void set_pain(int npain) override;
        /** Returns perceived pain (reduced with painkillers)*/
        int get_perceived_pain() const override;

        void cough(bool harmful = false, int volume = 4);

        void add_pain_msg(int val, body_part bp) const;

        /** Modifies intensity of painkillers  */
        void mod_painkiller(int npkill);
        /** Sets intensity of painkillers  */
        void set_painkiller(int npkill);
        /** Returns intensity of painkillers  */
        int get_painkiller() const;
        /** Heals a body_part for dam */
        void heal(body_part healed, int dam);
        /** Heals an hp_part for dam */
        void heal(hp_part healed, int dam);
        /** Heals all body parts for dam */
        void healall(int dam);
        /** Hurts all body parts for dam, no armor reduction */
        void hurtall(int dam, Creature *source, bool disturb = true);
        /** Harms all body parts for dam, with armor reduction. If vary > 0 damage to parts are random within vary % (1-100) */
        int hitall(int dam, int vary, Creature *source);
        /** Knocks the player back one square from a tile */
        void knock_back_from( const tripoint &p ) override;

        /** Returns multiplier on fall damage at low velocity (knockback/pit/1 z-level, not 5 z-levels) */
        float fall_damage_mod() const override;
        /** Deals falling/collision damage with terrain/creature at pos */
        int impact( int force, const tripoint &pos ) override;

        /** Converts a body_part to an hp_part */
        static hp_part bp_to_hp(body_part bp);
        /** Converts an hp_part to a body_part */
        static body_part hp_to_bp(hp_part hpart);

        /** Returns overall % of HP remaining */
        int hp_percentage() const override;

        /** Handles the chance to be infected by random diseases */
        void get_sick();
        /** Handles health fluctuations over time, redirects into Creature::update_health */
        void update_health(int external_modifiers = 0) override;
        /** Returns list of rc items in player inventory. **/
        std::list<item *> get_radio_items();

        /** Adds an addiction to the player */
        void add_addiction(add_type type, int strength);
        /** Removes an addition from the player */
        void rem_addiction(add_type type);
        /** Returns true if the player has an addiction of the specified type */
        bool has_addiction(add_type type) const;
        /** Returns the intensity of the specified addiction */
        int  addiction_level(add_type type) const;

        /** Siphons fuel from the specified vehicle into the player's inventory */
        bool siphon(vehicle *veh, const itype_id &desired_liquid);
        /** Handles a large number of timers decrementing and other randomized effects */
        void suffer();
        /** Handles the chance for broken limbs to spontaneously heal to 1 HP */
        void mend( int rate_multiplier );
        /** Handles player vomiting effects */
        void vomit();

        /** Drenches the player with water, saturation is the percent gotten wet */
        void drench( int saturation, int flags, bool ignore_waterproof );
        /** Recalculates mutation drench protection for all bodyparts (ignored/good/neutral stats) */
        void drench_mut_calc();
        /** Recalculates morale penalty/bonus from wetness based on mutations, equipment and temperature */
        void apply_wetness_morale( int temperature );

        /** used for drinking from hands, returns how many charges were consumed */
        int drink_from_hands(item &water);
        /** Used for eating object at pos, returns true if object is removed from inventory (last charge was consumed) */
        bool consume(int pos);
        /** Used for eating a particular item that doesn't need to be in inventory.
         *  Returns true if the item is to be removed (doesn't remove). */
        bool consume_item( item &eat );

        /** This block is to be moved to character.h */
        bool is_allergic( const item &food ) const;
        /** Returns allergy type or MORALE_NULL if not allergic for this player */
        morale_type allergy_type( const item &food ) const;
        /** Used for eating entered comestible, returns true if comestible is successfully eaten */
        bool eat( item &food, bool force = false );
        edible_rating can_eat( const item &food, bool interactive = false,
                               bool force = false ) const;

        /** Gets player's minimum hunger and thirst */
        int stomach_capacity() const;

        /** Handles the nutrition value for a comestible **/
        int nutrition_for( const itype *comest ) const;

        /** Get vitamin contents for a comestible */
        std::map<vitamin_id, int> vitamins_from( const item& it ) const;
        std::map<vitamin_id, int> vitamins_from( const itype_id& id ) const;

        /** Get vitamin usage rate (minutes per unit) accounting for bionics, mutations and effects */
        int vitamin_rate( const vitamin_id& vit ) const;

        /**
         * Add or subtract vitamins from player storage pools
         * @param qty amount by which to adjust @ref vit (negative values are permitted)
         * @param capped if true prevent vitamins which can accumulate in excess from doing so
         * @return adjusted level for the vitamin or zero if @ref vit does not exist
         */
        int vitamin_mod( const vitamin_id& vit, int qty, bool capped = true );

        /** Returns current level for a vitamin (or zero if @ref vit) does not exist */
        int vitamin_get( const vitamin_id& vit ) const;

        /**
         * Sets level of a vitamin or returns false if @ref vit does not exist
         * @note status effects are still set for deficiency/excess
         */
        bool vitamin_set( const vitamin_id& vit, int qty );

        /** Stable base metabolic rate due to traits */
        float metabolic_rate_base() const;
        /** Current metabolic rate due to traits, hunger, speed, etc. */
        float metabolic_rate() const;
        /** Handles the effects of consuming an item */
        void consume_effects( item &eaten, bool rotten = false );
        /** Handles rooting effects */
        void rooted_message() const;
        void rooted();

        /** Check player strong enough to lift an object unaided by equipment (jacks, levers etc) */
        template <typename T>
        bool can_lift( const T& obj ) const {
            // avoid comparing by weight as different objects use differing scales (grams vs kilograms etc)
            int str = get_str();
            if( has_trait( "STRONGBACK" ) ) {
                str *= 1.35;
            } else if( has_trait( "BADBACK" ) ) {
                str /= 1.35;
            }
            return get_str() >= obj.lift_strength();
        }

        /** Check player capable of wearing an item.
          * @param alert display reason for any failure */
        bool can_wear( const item& it, bool alert = true ) const;

        /** Check player capable of wielding an item.
          * @param alert display reason for any failure */
        bool can_wield( const item& it, bool alert = true ) const;
        /** Check player capable of unwielding an item.
          * @param alert display reason for any failure */
        bool can_unwield( const item& it, bool alert = true ) const;
        /**
         * Removes currently wielded item (if any) and replaces it with the target item
         * @param target replacement item to wield or null item to remove existing weapon without replacing it
         * @return whether both removal and replacement were successful (they are performed atomically)
         */
        virtual bool wield( item& target );
        /** Creates the UI and handles player input for picking martial arts styles */
        bool pick_style();
        /**
         * Checks if player stats and skills meet minimum requirements for the item
         * @param interactive controls whether informative messages are printed if item requirements not met
         */
        bool can_use( const item& it, bool interactive = true ) const;

        /**
         * Whether a tool or gun is potentially reloadable (optionally considering a specific ammo)
         * @param ammo if set also check item currently compatible with this specific ammo or magazine
         * @note items currently loaded with a detachable magazine are considered reloadable
         * @note items with integral magazines are reloadable if free capacity permits (+/- ammo matches)
         */
        bool can_reload( const item& it, const itype_id& ammo = std::string() ) const;

        /**
         * Drop, wear, stash or otherwise try to dispose of an item consuming appropriate moves
         * @param obj item to dispose of which must in the players possession
         * @param prompt optional message to display in any menu
         * @return whether the item was successfully disposed of
         */
        virtual bool dispose_item( item& obj, const std::string& prompt = std::string() );

        /**
         * Attempt to mend an item (fix any current faults)
         * @param interactive if true prompts player when multiple faults, otherwise mends the first
         */
        void mend_item( item_location&& obj, bool interactive = true );

        /**
         * Calculate (but do not deduct) the number of moves required when handling (eg. storing, drawing etc.) an item
         * @param effects whether temporary player effects should be considered (eg. GRABBED, DOWNED)
         * @param factor base move cost per unit volume before considering any other modifiers
         * @return cost in moves ranging from 0 to MAX_HANDLING_COST
         */
        int item_handling_cost( const item& it, bool effects = true, int factor = VOLUME_MOVE_COST) const;

        /**
         * Calculate (but do not deduct) the number of moves required when storing an item in a container
         * @param effects whether temporary player effects should be considered (eg. GRABBED, DOWNED)
         * @param factor base move cost per unit volume before considering any other modifiers
         * @return cost in moves ranging from 0 to MAX_HANDLING_COST
         */
        int item_store_cost( const item& it, const item& container, bool effects = true,
                             int factor = VOLUME_MOVE_COST ) const;

        /**
         * Calculate (but do not deduct) the number of moves required to reload an item with specified quantity of ammo
         * @param ammo either ammo or magazine to use when reloading the item
         * @param qty maximum units of ammo to reload capped by remaining capacity. Defaults to remaining capacity
         * (or 1 if RELOAD_ONE). Ignored if reloading using a magazine.
         */
        int item_reload_cost( const item& it, const item& ammo, long qty = -1 ) const;

        /** Calculate (but do not deduct) the number of moves required to wear an item */
        int item_wear_cost( const item& to_wear ) const;

        /** Wear item; returns false on fail. If interactive is false, don't alert the player or drain moves on completion. */
        bool wear( int pos, bool interactive = true );
        /** Wear item; returns false on fail. If interactive is false, don't alert the player or drain moves on completion. */
        bool wear_item( const item &to_wear, bool interactive = true );
        /** Swap side on which item is worn; returns false on fail. If interactive is false, don't alert player or drain moves */
        bool change_side( item *target, bool interactive = true );
        bool change_side( int pos, bool interactive = true );
        /** Takes off an item, returning false on fail, if an item vector
         *  is given, stores the items in that vector and not in the inventory */
        bool takeoff( item *target, bool autodrop = false, std::vector<item> *items = nullptr );
        bool takeoff( int pos, bool autodrop = false, std::vector<item> *items = nullptr );
        /** Try to wield a contained item consuming moves proportional to weapon skill and volume.
         *  @param pos index of contained item to wield. Set to -1 to show menu if container has more than one item
         *  @param factor scales moves cost and can be set to zero if item should be wielded without any delay
         *  @param effects whether temporary player effects such (eg. GRABBED) are considered when consuming moves */
        bool wield_contents( item *container, int pos = 0, int factor = VOLUME_MOVE_COST,
                             bool effects = true );
        /** Stores an item inside another consuming moves proportional to weapon skill and volume
         *  @param factor scales moves cost and can be set to zero if item should be stored without any delay
         *  @param effects whether temporary player effects such (eg. GRABBED) are considered when consuming moves */
        void store( item *container, item *put, int factor = VOLUME_MOVE_COST, bool effects = true );
        /** Draws the UI and handles player input for the armor re-ordering window */
        void sort_armor();
        /** Uses a tool */
        void use(int pos);
        /** Uses the current wielded weapon */
        void use_wielded();
        /**
         * Asks how to use the item (if it has more than one use_method) and uses it.
         * Returns true if it destroys the item. Consumes charges from the item.
         * Multi-use items are ONLY supported when all use_methods are iuse_actor!
         */
        bool invoke_item( item*, const tripoint &pt );
        /** As above, but with a pre-selected method. Debugmsg if this item doesn't have this method. */
        bool invoke_item( item*, const std::string&, const tripoint &pt );
        /** As above two, but with position equal to current position */
        bool invoke_item( item* );
        bool invoke_item( item*, const std::string& );

        /** Consume charges of a tool or comestible item, potentially destroying it in the process
         *  @qty number of charges to consume which must be non-zero
         *  @return true if item was destroyed */
        bool consume_charges( item& used, long qty );

        /** Removes gunmod after first unloading any contained ammo and returns true on success */
        bool gunmod_remove( item& gun, item& mod );

        /** Starts activity to install gunmod having warned user about any risk of failure or irremovable mods s*/
        void gunmod_add( item& gun, item& mod );

        /** Attempts to install bionics, returns false if the player cancels prior to installation */
        bool install_bionics(const itype &type, int skill_level = -1);
        /** Handles reading effects and returns true if activity started */
        bool read(int pos);
        /** Completes book reading action. **/
        void do_read( item *book );
        /** Note that we've read a book at least once. **/
        bool has_identified( std::string item_id ) const;
        /** Handles sleep attempts by the player, adds "lying_down" */
        void try_to_sleep();
        /** Rate point's ability to serve as a bed. Takes mutations, fatigue and stimms into account. */
        int sleep_spot( const tripoint &p ) const;
        /** Checked each turn during "lying_down", returns true if the player falls asleep */
        bool can_sleep();
        /** Adds "sleep" to the player */
        void fall_asleep(int duration);
        /** Removes "sleep" and "lying_down" from the player */
        void wake_up();
        /** Checks to see if the player is using floor items to keep warm, and return the name of one such item if so */
        std::string is_snuggling() const;
        /** Returns a value from 1.0 to 5.0 that acts as a multiplier
         * for the time taken to perform tasks that require detail vision,
         * above 4.0 means these activities cannot be performed. */
        float fine_detail_vision_mod() const;

        /** Used to determine player feedback on item use for the inventory code.
         *  rates usability lower for non-tools (books, etc.) */
        hint_rating rate_action_use( const item &it ) const;
        hint_rating rate_action_wear( const item &it ) const;
        hint_rating rate_action_change_side( const item &it ) const;
        hint_rating rate_action_eat( const item &it ) const;
        hint_rating rate_action_read( const item &it ) const;
        hint_rating rate_action_takeoff( const item &it ) const;
        hint_rating rate_action_reload( const item &it ) const;
        hint_rating rate_action_unload( const item &it ) const;
        hint_rating rate_action_mend( const item &it ) const;
        hint_rating rate_action_disassemble( const item &it );

        /** Returns warmth provided by armor, etc. */
        int warmth(body_part bp) const;
        /** Returns warmth provided by an armor's bonus, like hoods, pockets, etc. */
        int bonus_item_warmth(body_part bp) const;
        /** Returns overall bashing resistance for the body_part */
        int get_armor_bash(body_part bp) const override;
        /** Returns overall cutting resistance for the body_part */
        int get_armor_cut(body_part bp) const override;
        /** Returns bashing resistance from the creature and armor only */
        int get_armor_bash_base(body_part bp) const override;
        /** Returns cutting resistance from the creature and armor only */
        int get_armor_cut_base(body_part bp) const override;
        /** Returns overall env_resist on a body_part */
        int get_env_resist(body_part bp) const override;
        /** Returns overall acid resistance for the body part */
        int get_armor_acid(body_part bp) const;
        /** Returns overall fire resistance for the body part */
        int get_armor_fire(body_part bp) const;
        /** Returns overall resistance to given type on the bod part */
        int get_armor_type( damage_type dt, body_part bp ) const override;
        /** Returns true if the player is wearing something on the entered body_part */
        bool wearing_something_on(body_part bp) const;
        /** Returns true if the player is wearing something on the entered body_part, ignoring items with the ALLOWS_NATURAL_ATTACKS flag */
        bool natural_attack_restricted_on(body_part bp) const;
        /** Returns true if the player is wearing something on their feet that is not SKINTIGHT */
        bool is_wearing_shoes(std::string side = "both") const;
        /** Returns 1 if the player is wearing something on both feet, .5 if on one, and 0 if on neither */
        double footwear_factor() const;
        /** Returns 1 if the player is wearing an item of that count on one foot, 2 if on both, and zero if on neither */
        int shoe_type_count(const itype_id &it) const;
        /** Returns true if the player is wearing power armor */
        bool is_wearing_power_armor(bool *hasHelmet = NULL) const;
        /** Returns wind resistance provided by armor, etc **/
        int get_wind_resistance(body_part bp) const;

        int adjust_for_focus(int amount) const;
        void practice( const skill_id &s, int amount, int cap = 99 );

        void assign_activity(activity_type type, int moves, int index = -1, int pos = INT_MIN,
                             std::string name = "");
        bool has_activity(const activity_type type) const;
        void cancel_activity();

        int get_morale_level() const; // Modified by traits, &c
        void add_morale( morale_type type, int bonus, int max_bonus = 0, int duration = 60,
                        int decay_start = 30, bool capped = false, const itype *item_type = nullptr );
        int has_morale( morale_type type ) const;
        void rem_morale( morale_type type, const itype *item_type = nullptr );
        bool has_morale_to_read() const;

        /** Get the formatted name of the currently wielded item (if any) */
        std::string weapname() const;

        virtual float power_rating() const override;
        virtual float speed_rating() const override;

        /**
         * All items that have the given flag (@ref item::has_flag).
         */
        std::vector<const item *> all_items_with_flag( const std::string flag ) const;

        void process_active_items();
        /**
         * Remove charges from a specific item (given by its item position).
         * The item must exist and it must be counted by charges.
         * @param position Item position of the item.
         * @param quantity The number of charges to remove, must not be larger than
         * the current charges of the item.
         * @return An item that contains the removed charges, it's effectively a
         * copy of the item with the proper charges.
         */
        item reduce_charges( int position, long quantity );
        /**
         * Remove charges from a specific item (given by a pointer to it).
         * Otherwise identical to @ref reduce_charges(int,long)
         * @param it A pointer to the item, it *must* exist.
         */
        item reduce_charges( item *it, long quantity );
        /** Return the item position of the item with given invlet, return INT_MIN if
         * the player does not have such an item with that invlet. Don't use this on npcs.
         * Only use the invlet in the user interface, otherwise always use the item position. */
        int invlet_to_position( long invlet ) const;

        const martialart &get_combat_style() const; // Returns the combat style object
        std::vector<item *> inv_dump(); // Inventory + weapon + worn (for death, etc)
        void place_corpse(); // put corpse+inventory on map at the place where this is.
        item  *pick_usb(); // Pick a usb drive, interactively if it matters

        bool covered_with_flag( const std::string &flag, const std::bitset<num_bp> &parts ) const;
        bool is_waterproof( const std::bitset<num_bp> &parts ) const;

        // has_amount works ONLY for quantity.
        // has_charges works ONLY for charges.
        std::list<item> use_amount( itype_id it, int quantity );
        bool use_charges_if_avail( itype_id it, long quantity );// Uses up charges

        std::list<item> use_charges( const itype_id& what, long qty ); // Uses up charges

        bool has_charges( const itype_id &it, long quantity ) const;
        /** Returns the amount of item `type' that is currently worn */
        int  amount_worn( const itype_id &id ) const;

        int  leak_level( std::string flag ) const; // carried items may leak radiation or chemicals

        // Check for free container space for the whole liquid item
        bool has_container_for( const item &liquid ) const;
        // Has a weapon, inventory item or worn item with flag
        bool has_item_with_flag( std::string flag ) const;

        bool has_mission_item( int mission_id ) const; // Has item with mission_id
        /**
         * Check whether the player has a gun that uses the given type of ammo.
         */
        bool has_gun_for_ammo( const ammotype &at ) const;

        bool has_weapon() const override;

        // Checks crafting inventory for books providing the requested recipe.
        // Returns -1 to indicate recipe not found, otherwise difficulty to learn.
        int has_recipe( const recipe *r, const inventory &crafting_inv ) const;
        bool knows_recipe( const recipe *rec ) const;
        void learn_recipe( const recipe *rec, bool force = false );
        int exceeds_recipe_requirements( const recipe &rec ) const;
        bool has_recipe_requirements( const recipe &rec ) const;
        bool has_recipe_autolearned( const recipe &rec ) const;
        bool can_decomp_learn( const recipe &rec ) const;

        bool studied_all_recipes( const itype &book ) const;

        // crafting.cpp
        float lighting_craft_speed_multiplier( const recipe & rec ) const;
        bool has_morale_to_craft() const;
        bool can_make( const recipe * r, int batch_size = 1 ); // have components?
        bool making_would_work( const std::string & id_to_make, int batch_size );
        void craft();
        void recraft();
        void long_craft();
        void make_craft( const std::string & id, int batch_size );
        void make_all_craft( const std::string & id, int batch_size );
        void complete_craft();

        // also crafting.cpp
        /**
         * Check if the player can disassemble the item dis_item with the recipe
         * cur_recipe and the inventory crafting_inv.
         * If no cur_recipe given, searches for the first relevant, reversible one
         * Checks for example tools (and charges), enough input charges
         * (if disassembled item is counted by charges).
         * If print_msg is true show a message about missing tools/charges.
         */
        bool can_disassemble( const item &dis_item, const inventory &crafting_inv,
                              bool print_msg ) const;
        bool can_disassemble( const item &dis_item, const recipe *cur_recipe,
                              const inventory &crafting_inv, bool print_msg ) const;
        bool disassemble(int pos = INT_MAX);
        bool disassemble( item &dis_item, int dis_pos, bool ground, bool msg_and_query = true );
        void disassemble_all( bool one_pass ); // Disassemble all items on the tile
        void complete_disassemble();
        void complete_disassemble( int item_pos, const tripoint &loc,
                                   bool from_ground, const recipe &dis );

        // yet more crafting.cpp
        const inventory &crafting_inventory(); // includes nearby items
        void invalidate_crafting_inventory();
        std::vector<item> get_eligible_containers_for_crafting();
        comp_selection<item_comp>
            select_item_component( const std::vector<item_comp> &components,
                                   int batch, inventory &map_inv, bool can_cancel = false );
        std::list<item> consume_items( const comp_selection<item_comp> &cs, int batch );
        std::list<item> consume_items( const std::vector<item_comp> &components, int batch = 1 );
        comp_selection<tool_comp>
            select_tool_component( const std::vector<tool_comp> &tools, int batch, inventory &map_inv,
                                   const std::string &hotkeys = DEFAULT_HOTKEYS,
                                   bool can_cancel = false );
        void consume_tools( const comp_selection<tool_comp> &tool, int batch );
        void consume_tools( const std::vector<tool_comp> &tools, int batch = 1,
                            const std::string &hotkeys = DEFAULT_HOTKEYS );

        // Auto move methods
        void set_destination( const std::vector<tripoint> &route );
        void clear_destination();
        bool has_destination() const;
        std::vector<tripoint> &get_auto_move_route();
        action_id get_next_auto_move_direction();
        void shift_destination(int shiftx, int shifty);

        /**
         * Global position, expressed in map square coordinate system
         * (the most detailed coordinate system), used by the @ref map.
         */
        virtual tripoint global_square_location() const;
        /**
        * Returns the location of the player in global submap coordinates.
        */
        tripoint global_sm_location() const;
        /**
        * Returns the location of the player in global overmap terrain coordinates.
        */
        tripoint global_omt_location() const;

        // ---------------VALUES-----------------
        inline int posx() const override
        {
            return position.x;
        }
        inline int posy() const override
        {
            return position.y;
        }
        inline int posz() const override
        {
            return position.z;
        }
        inline void setx( int x )
        {
            position.x = x;
        }
        inline void sety( int y )
        {
            position.y = y;
        }
        inline void setz( int z )
        {
            position.z = z;
        }
        inline void setpos( const tripoint &p ) override
        {
            position = p;
        }
        tripoint view_offset;
        bool in_vehicle;       // Means player sit inside vehicle on the tile he is now
        bool controlling_vehicle;  // Is currently in control of a vehicle
        // Relative direction of a grab, add to posx, posy to get the coordinates of the grabbed thing.
        tripoint grab_point;
        object_type grab_type;
        player_activity activity;
        std::list<player_activity> backlog;
        int volume;

        const profession *prof;

        start_location_id start_location;

        std::map<std::string, int> mutation_category_level;

        int next_climate_control_check;
        bool last_climate_control_ret;
        std::string move_mode;
        int power_level, max_power_level;
        int tank_plut, reactor_plut, slow_rad;
        int oxygen;
        int stamina;
        int recoil;
        int driving_recoil;
        int scent;
        int dodges_left, blocks_left;
        int stim, radiation;
        unsigned long cash;
        int movecounter;
        std::array<int, num_bp> temp_cur, frostbite_timer, temp_conv;
        void temp_equalizer(body_part bp1, body_part bp2); // Equalizes heat between body parts

        // Drench cache
        enum water_tolerance {
            WT_IGNORED = 0,
            WT_NEUTRAL,
            WT_GOOD,
            NUM_WATER_TOLERANCE
        };
        std::array<std::array<int, NUM_WATER_TOLERANCE>, num_bp> mut_drench;
        std::array<int, num_bp> drench_capacity;
        std::array<int, num_bp> body_wetness;

        int focus_pool;

        std::map<std::string, const recipe *> learned_recipes;

        std::vector<matype_id> ma_styles;
        matype_id style_selected;
        bool keep_hands_free;

        std::vector <addiction> addictions;

        void make_craft_with_command( const std::string &id_to_make, int batch_size, bool is_long = false );
        craft_command last_craft;

        std::string lastrecipe;
        int last_batch;
        itype_id lastconsumed;        //used in crafting.cpp and construction.cpp

        int get_used_bionics_slots( const body_part bp ) const;
        int get_total_bionics_slots( const body_part bp ) const;
        int get_free_bionics_slots( const body_part bp ) const;
        std::map<body_part, int> bionic_installation_issues( const std::string &bioid );

        //Dumps all memorial events into a single newline-delimited string
        std::string dump_memorial() const;
        //Log an event, to be later written to the memorial file
        void add_memorial_log(const char *male_msg, const char *female_msg, ...) override;
        //Loads the memorial log from a file
        void load_memorial_file(std::ifstream &fin);
        //Notable events, to be printed in memorial
        std::vector <std::string> memorial_log;

        //Record of player stats, for posterity only
        stats *lifetime_stats();
        stats get_stats() const; // for serialization
        void mod_stat( const std::string &stat, int modifier ) override;

        int getID () const;
        // sets the ID, will *only* succeed when the current id is 0 (=not initialized)
        void setID (int i);

        bool is_underwater() const override;
        void set_underwater(bool);
        bool is_hallucination() const override;
        void environmental_revert_effect();

        bool is_invisible() const;
        bool is_deaf() const;
        // Checks whether a player can hear a sound at a given volume and location.
        bool can_hear( const tripoint &source, const int volume ) const;
        // Returns a multiplier indicating the keeness of a player's hearing.
        float hearing_ability() const;
        int visibility( bool check_color = false,
                        int stillness = 0 ) const; // just checks is_invisible for the moment

        m_size get_size() const override;
        int get_hp( hp_part bp ) const override;
        int get_hp_max( hp_part bp ) const override;
        int get_stamina_max() const;
        void burn_move_stamina( int moves );

        field_id playerBloodType() const;

        //message related stuff
        virtual void add_msg_if_player(const char *msg, ...) const override;
        virtual void add_msg_if_player(game_message_type type, const char *msg, ...) const override;
        virtual void add_msg_player_or_npc(const char *player_str, const char *npc_str, ...) const override;
        virtual void add_msg_player_or_npc(game_message_type type, const char *player_str,
                                           const char *npc_str, ...) const override;
        virtual void add_msg_player_or_say( const char *, const char *, ... ) const override;
        virtual void add_msg_player_or_say( game_message_type, const char *, const char *, ... ) const override;

        typedef std::map<tripoint, std::string> trap_map;
        bool knows_trap( const tripoint &pos ) const;
        void add_known_trap( const tripoint &pos, const trap &t );
        /** Search surrounding squares for traps (and maybe other things in the future). */
        void search_surroundings();

        // drawing related stuff
        /**
         * Returns a list of the IDs of overlays on this character,
         * sorted from "lowest" to "highest".
         *
         * Only required for rendering.
         */
        std::vector<std::string> get_overlay_ids() const;

        void spores();
        void blossoms();

        int add_ammo_to_worn_quiver(item &ammo);

        std::vector<mission*> get_active_missions() const;
        std::vector<mission*> get_completed_missions() const;
        std::vector<mission*> get_failed_missions() const;
        /**
         * Returns the mission that is currently active. Returns null if mission is active.
         */
        mission *get_active_mission() const;
        /**
         * Returns the target of the active mission or @ref overmap::invalid_point if there is
         * no active mission.
         */
        tripoint get_active_mission_target() const;
        /**
         * Set which mission is active. The mission must be listed in @ref active_missions.
         */
        void set_active_mission( mission &mission );
        /**
         * Called when a mission has been assigned to the player.
         */
        void on_mission_assignment( mission &new_mission );
        /**
         * Called when a mission has been completed or failed. Either way it's finished.
         * Check @ref mission::failed to see which case it is.
         */
        void on_mission_finished( mission &mission );
        /**
         * Called when a mutation is gained
         */
        virtual void on_mutation_gain( const std::string &mid ) override;
        /**
         * Called when a mutation is lost
         */
        virtual void on_mutation_loss( const std::string &mid ) override;
        /**
         * Called when a stat is changed
         */
        virtual void on_stat_change( const std::string &stat, int value ) override;
        /**
         * Called when an item is worn
         */
        virtual void on_item_wear( const item &it ) override;
        /**
         * Called when an item is taken off
         */
        virtual void on_item_takeoff( const item &it ) override;
        /**
         * Called when effect intensity has been changed
         */
        virtual void on_effect_int_change( const efftype_id &eid, int intensity, body_part bp = num_bp ) override;

        // formats and prints encumbrance info to specified window
        void print_encumbrance( WINDOW * win, int line = -1, item *selected_limb = nullptr ) const;

        // Prints message(s) about current health
        void print_health() const;

        bool query_yn( const char *mes, ... ) const override;

        /**
         * Has the item enough charges to invoke its use function?
         * Also checks if UPS from this player is used instead of item charges.
         */
        bool has_enough_charges(const item &it, bool show_msg) const;

    protected:
        // The player's position on the local map.
        tripoint position;

        trap_map known_traps;

        void store(JsonOut &jsout) const;
        void load(JsonObject &jsin);

    private:
        // Items the player has identified.
        std::unordered_set<std::string> items_identified;
        /** Check if an area-of-effect technique has valid targets */
        bool valid_aoe_technique( Creature &t, const ma_technique &technique );
        bool valid_aoe_technique( Creature &t, const ma_technique &technique,
                                  std::vector<int> &mon_targets, std::vector<int> &npc_targets );
        /**
         * Check whether the other creature is in range and can be seen by this creature.
         * @param range The maximal distance (@ref rl_dist), creatures at this distance or less
         * are included.
         */
        bool is_visible_in_range( const Creature &critter, int range ) const;

        /** Can the player lie down and cover self with blankets etc. **/
        bool can_use_floor_warmth() const;
        /**
         * Warmth from terrain, furniture, vehicle furniture and traps.
         * Can be negative.
         **/
        static int floor_bedding_warmth( const tripoint &pos );
        /** Warmth from clothing on the floor **/
        static int floor_item_warmth( const tripoint &pos );
        /** Final warmth from the floor **/
        int floor_warmth( const tripoint &pos ) const;
        /** Correction factor of the body temperature due to fire **/
        int bodytemp_modifier_fire() const;
        /** Correction factor of the body temperature due to traits and mutations **/
        int bodytemp_modifier_traits( bool overheated ) const;
        /** Correction factor of the body temperature due to traits and mutations for player lying on the floor **/
        int bodytemp_modifier_traits_floor() const;
        /** Value of the body temperature corrected by climate control **/
        int temp_corrected_by_climate_control( int temperature ) const;
        /** Define blood loss (in percents) */
        int blood_loss( body_part bp ) const;

        // Trigger and disable mutations that can be so toggled.
        void activate_mutation( const std::string &mutation );
        void deactivate_mutation( const std::string &mut );
        bool has_fire(const int quantity) const;
        void use_fire(const int quantity);

        void react_to_felt_pain( int intensity );

        bool can_study_recipe(const itype &book) const;
        bool try_study_recipe(const itype &book);

        int pkill;

        std::vector<tripoint> auto_move_route;
        // Used to make sure auto move is canceled if we stumble off course
        tripoint next_expected_position;

        inventory cached_crafting_inventory;
        int cached_moves;
        int cached_turn;
        tripoint cached_position;

        struct weighted_int_list<const char*> melee_miss_reasons;

        player_morale_ptr morale;

        int id; // A unique ID number, assigned by the game class private so it cannot be overwritten and cause save game corruptions.
        //NPCs also use this ID value. Values should never be reused.
        /**
         * Missions that the player has accepted and that are not finished (one
         * way or the other).
         */
        std::vector<mission*> active_missions;
        /**
         * Missions that the player has successfully completed.
         */
        std::vector<mission*> completed_missions;
        /**
         * Missions that have failed while being assigned to the player.
         */
        std::vector<mission*> failed_missions;
        /**
         * The currently active mission, or null if no mission is currently in progress.
         */
        mission *active_mission;

        /** Current deficiency/excess quantity for each vitamin */
        std::map<vitamin_id, int> vitamin_levels;
};

#endif
