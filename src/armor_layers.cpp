#include "game.h"
#include "player.h"
#include "catacharset.h"
#include "input.h"
#include "output.h"
#include "item.h"
#include "translations.h"
#include "npc.h"

#include <vector>
#include <string>

namespace
{
std::string clothing_layer( item const &worn_item );
std::vector<std::string> clothing_properties( item const &worn_item, int width );
std::vector<std::string> clothing_flags_description( item const &worn_item );

void draw_mid_pane( WINDOW *w_sort_middle, item const &worn_item )
{
    int middle_w = getmaxx( w_sort_middle );
    size_t i = fold_and_print( w_sort_middle, 0, 1, middle_w - 1, c_white,
                               worn_item.type_name( 1 ) ) - 1;
    std::vector<std::string> props = clothing_properties( worn_item, middle_w - 3 );
    for( auto &iter : props ) {
        // [headers] are green, info is gray
        nc_color color = ( iter[0] == '[' ? c_green : c_ltgray );
        mvwprintz( w_sort_middle, ++i, 2, color, iter.c_str() );
    }

    i += 2;
    i += fold_and_print( w_sort_middle, i, 0, middle_w, c_ltblue,
                         clothing_layer( worn_item ) );

    std::vector<std::string> desc = clothing_flags_description( worn_item );
    if( !desc.empty() ) {
        for( size_t j = 0; j < desc.size(); ++j ) {
            i += -1 + fold_and_print( w_sort_middle, i + j, 0, middle_w, c_ltblue, desc[j] );
        }
    }
}

std::string clothing_layer( item const &worn_item )
{
    std::string layer;

    if( worn_item.has_flag( "SKINTIGHT" ) ) {
        layer = _( "This is worn next to the skin." );
    } else if( worn_item.has_flag( "WAIST" ) ) {
        layer = _( "This is worn on or around your waist." );
    } else if( worn_item.has_flag( "OUTER" ) ) {
        layer = _( "This is worn over your other clothes." );
    } else if( worn_item.has_flag( "BELTED" ) ) {
        layer = _( "This is strapped onto you." );
    }

    return layer;
}

std::vector<std::string> clothing_properties( item const &worn_item, int const width )
{
    std::vector<std::string> props;
    props.reserve( 9 );

    const std::string space = "  ";
    props.push_back( string_format( "[%s]", _( "Properties" ) ) );
    props.push_back( name_and_value( space + _( "Coverage:" ),
                                     string_format( "%3d", worn_item.get_coverage() ), width ) );
    props.push_back( name_and_value( space + _( "Encumbrance:" ),
                                     string_format( "%3d", worn_item.get_encumber() ), width ) );
    props.push_back( name_and_value( space + _( "Warmth:" ),
                                     string_format( "%3d", worn_item.get_warmth() ), width ) );
    props.push_back( name_and_value( space + _( "Storage:" ),
                                     string_format( "%3d", worn_item.get_storage() ), width ) );
    props.push_back( string_format( "[%s]", _( "Protection" ) ) );
    props.push_back( name_and_value( space + _( "Bash:" ),
                                     string_format( "%3d", int( worn_item.bash_resist() ) ), width ) );
    props.push_back( name_and_value( space + _( "Cut:" ),
                                     string_format( "%3d", int( worn_item.cut_resist() ) ), width ) );
    props.push_back( name_and_value( space + _( "Environmental:" ),
                                     string_format( "%3d", int( worn_item.get_env_resist() ) ), width ) );

    return props;
}

std::vector<std::string> clothing_flags_description( item const &worn_item )
{
    std::vector<std::string> description_stack;

    if( worn_item.has_flag( "FIT" ) ) {
        description_stack.push_back( _( "It fits you well." ) );
    } else if( worn_item.has_flag( "VARSIZE" ) ) {
        description_stack.push_back( _( "It could be refitted." ) );
    }

    if( worn_item.has_flag( "HOOD" ) ) {
        description_stack.push_back( _( "It has a hood." ) );
    }
    if( worn_item.has_flag( "POCKETS" ) ) {
        description_stack.push_back( _( "It has pockets." ) );
    }
    if( worn_item.has_flag( "WATERPROOF" ) ) {
        description_stack.push_back( _( "It is waterproof." ) );
    }
    if( worn_item.has_flag( "WATER_FRIENDLY" ) ) {
        description_stack.push_back( _( "It is water friendly." ) );
    }
    if( worn_item.has_flag( "FANCY" ) ) {
        description_stack.push_back( _( "It looks fancy." ) );
    }
    if( worn_item.has_flag( "SUPER_FANCY" ) ) {
        description_stack.push_back( _( "It looks really fancy." ) );
    }
    if( worn_item.has_flag( "FLOTATION" ) ) {
        description_stack.push_back( _( "You will not drown today." ) );
    }
    if( worn_item.has_flag( "OVERSIZE" ) ) {
        description_stack.push_back( _( "It is very bulky." ) );
    }
    if( worn_item.has_flag( "SWIM_GOGGLES" ) ) {
        description_stack.push_back( _( "It helps you to see clearly underwater." ) );
    }

    return description_stack;
}

} //namespace

struct layering_item_info {
    int damage;
    int encumber;
    std::string name;
    bool operator ==( const layering_item_info &o ) const {
        return this->damage == o.damage &&
               this->encumber == o.encumber &&
               this->name == o.name;
    }
};

std::vector<layering_item_info> items_cover_bp( const Character &c, int bp )
{
    std::vector<layering_item_info> s;
    for( auto &elem : c.worn ) {
        if( elem.covers( static_cast<body_part>( bp ) ) ) {
            layering_item_info t = {elem.damage, elem.get_encumber(), elem.type_name( 1 )};
            s.push_back( t );
        }
    }
    return s;
}

void draw_grid( WINDOW *w, int left_pane_w, int mid_pane_w )
{
    const int win_w = getmaxx( w );
    const int win_h = getmaxy( w );

    draw_border( w );
    mvwhline( w, 2, 1, 0, win_w - 2 );
    mvwvline( w, 3, left_pane_w + 1, 0, win_h - 4 );
    mvwvline( w, 3, left_pane_w + mid_pane_w + 2, 0, win_h - 4 );

    // intersections
    mvwputch( w, 2, 0, BORDER_COLOR, LINE_XXXO );
    mvwputch( w, 2, win_w - 1, BORDER_COLOR, LINE_XOXX );
    mvwputch( w, 2, left_pane_w + 1, BORDER_COLOR, LINE_OXXX );
    mvwputch( w, win_h - 1, left_pane_w + 1, BORDER_COLOR, LINE_XXOX );
    mvwputch( w, 2, left_pane_w + mid_pane_w + 2, BORDER_COLOR, LINE_OXXX );
    mvwputch( w, win_h - 1, left_pane_w + mid_pane_w + 2, BORDER_COLOR, LINE_XXOX );

    wrefresh( w );
}

void player::sort_armor()
{
    /* Define required height of the right pane:
    * + 3 - horizontal lines;
    * + 1 - caption line;
    * + 2 - innermost/outermost string lines;
    * + 12 - sub-categories (torso, head, eyes, etc.);
    * + 1 - gap;
    * number of lines required for displaying all items is calculated dynamically,
    * because some items can have multiple entries (i.e. cover a few parts of body).
    */

    int req_right_h = 3 + 1 + 2 + 12 + 1;
    for( int cover = 0; cover < num_bp; cover++ ) {
        for( auto &elem : worn ) {
            if( elem.covers( static_cast<body_part>( cover ) ) ) {
                req_right_h++;
            }
        }
    }

    /* Define required height of the mid pane:
    * + 3 - horizontal lines;
    * + 1 - caption line;
    * + 8 - general properties
    * + 7 - ASSUMPTION: max possible number of flags @ item
    * + 13 - warmth & enc block
    */
    const int req_mid_h = 3 + 1 + 8 + 7 + 13;

    const int win_h = std::min( TERMY, std::max( FULL_SCREEN_HEIGHT,
                                std::max( req_right_h, req_mid_h ) ) );
    const int win_w = FULL_SCREEN_WIDTH + ( TERMX - FULL_SCREEN_WIDTH ) * 3 / 4;
    const int win_x = TERMX / 2 - win_w / 2;
    const int win_y = TERMY / 2 - win_h / 2;

    int cont_h   = win_h - 4;
    int left_w   = ( win_w - 4 ) / 3;
    int right_w  = left_w;
    int middle_w = ( win_w - 4 ) - left_w - right_w;

    int tabindex = num_bp;
    int tabcount = num_bp + 1;

    int leftListSize;
    int leftListIndex  = 0;
    int leftListOffset = 0;
    int selected       = -1;

    int rightListSize;
    int rightListOffset = 0;

    std::vector<item *> tmp_worn;
    std::string tmp_str;

    std::string  armor_cat[] = {_( "Torso" ), _( "Head" ), _( "Eyes" ), _( "Mouth" ), _( "L. Arm" ), _( "R. Arm" ),
                                _( "L. Hand" ), _( "R. Hand" ), _( "L. Leg" ), _( "R. Leg" ), _( "L. Foot" ),
                                _( "R. Foot" ), _( "All" )
                               };

    // Layout window
    WINDOW *w_sort_armor = newwin( win_h, win_w, win_y, win_x );
    draw_grid( w_sort_armor, left_w, middle_w );
    // Subwindows (between lines)
    WINDOW *w_sort_cat    = newwin( 1, win_w - 4, win_y + 1, win_x + 2 );
    WINDOW *w_sort_left   = newwin( cont_h, left_w,   win_y + 3, win_x + 1 );
    WINDOW *w_sort_middle = newwin( cont_h - num_bp - 1, middle_w, win_y + 3, win_x + left_w + 2 );
    WINDOW *w_sort_right  = newwin( cont_h, right_w,  win_y + 3, win_x + left_w + middle_w + 3 );
    WINDOW *w_encumb      = newwin( num_bp + 1, middle_w, win_y + 3 + cont_h - num_bp - 1,
                                    win_x + left_w + 2 );

    nc_color dam_color[] = {c_green, c_ltgreen, c_yellow, c_magenta, c_ltred, c_red};

    input_context ctxt( "SORT_ARMOR" );
    ctxt.register_cardinal();
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "MOVE_ARMOR" );
    ctxt.register_action( "CHANGE_SIDE" );
    ctxt.register_action( "ASSIGN_INVLETS" );
    ctxt.register_action( "EQUIP_ARMOR" );
    ctxt.register_action( "REMOVE_ARMOR" );
    ctxt.register_action( "USAGE_HELP" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    bool exit = false;
    while( !exit ) {
        if( is_player() ) {
            // Totally hoisted this from advanced_inv
            if( g->u.moves < 0 ) {
                g->u.assign_activity( ACT_ARMOR_LAYERS, 0 );
                g->u.activity.auto_resume = true;
                return;
            }
        } else {
            // Player is sorting NPC's armor here
            // TODO: Add all sorts of checks here, to prevent player from wasting NPC moves
            if( rl_dist( g->u.pos(), pos() ) > 1 ) {
                return;
            }
            if( attitude_to( g->u ) != Creature::A_FRIENDLY ) {
                return;
            }
            if( moves < -200 ) {
                return;
            }
        }
        werase( w_sort_cat );
        werase( w_sort_left );
        werase( w_sort_middle );
        werase( w_sort_right );
        werase( w_encumb );

        // top bar
        wprintz( w_sort_cat, c_white, _( "Sort Armor" ) );
        wprintz( w_sort_cat, c_yellow, "  << %s >>", armor_cat[tabindex].c_str() );
        tmp_str = string_format( _( "Press %s for help. Press %s to change keybindings." ),
                                 ctxt.get_desc( "USAGE_HELP" ).c_str(),
                                 ctxt.get_desc( "HELP_KEYBINDINGS" ).c_str() );
        mvwprintz( w_sort_cat, 0, win_w - utf8_width( tmp_str ) - 4,
                   c_white, tmp_str.c_str() );

        // Create ptr list of items to display
        tmp_worn.clear();
        if( tabindex == 12 ) { // All
            for( auto &elem : worn ) {
                tmp_worn.push_back( &elem );
            }
        } else { // bp_*
            for( auto &elem : worn ) {
                if( elem.covers( static_cast<body_part>( tabindex ) ) ) {
                    tmp_worn.push_back( &elem );
                }
            }
        }
        leftListSize = ( ( int )tmp_worn.size() < cont_h - 2 ) ? ( int )tmp_worn.size() : cont_h - 2;

        // Left header
        mvwprintz( w_sort_left, 0, 0, c_ltgray, _( "(Innermost)" ) );
        mvwprintz( w_sort_left, 0, left_w - utf8_width( _( "Storage" ) ), c_ltgray, _( "Storage" ) );

        // Left list
        for( int drawindex = 0; drawindex < leftListSize; drawindex++ ) {
            int itemindex = leftListOffset + drawindex;

            if( itemindex == leftListIndex ) {
                mvwprintz( w_sort_left, drawindex + 1, 0, c_yellow, ">>" );
            }

            const int offset_x = ( itemindex == selected ) ? 3 : 2;
            trim_and_print( w_sort_left, drawindex + 1, offset_x, left_w - offset_x - 3,
                            dam_color[int( tmp_worn[itemindex]->damage + 1 )],
                            tmp_worn[itemindex]->type_name( 1 ).c_str() );
            mvwprintz( w_sort_left, drawindex + 1, left_w - 3, c_ltgray, "%3d",
                       tmp_worn[itemindex]->get_storage() );
        }

        // Left footer
        mvwprintz( w_sort_left, cont_h - 1, 0, c_ltgray, _( "(Outermost)" ) );
        if( leftListSize > ( int )tmp_worn.size() ) {
            mvwprintz( w_sort_left, cont_h - 1, left_w - utf8_width( _( "<more>" ) ), c_ltblue, _( "<more>" ) );
        }
        if( leftListSize == 0 ) {
            mvwprintz( w_sort_left, cont_h - 1, left_w - utf8_width( _( "<empty>" ) ), c_ltblue,
                       _( "<empty>" ) );
        }

        // Items stats
        if( leftListSize > 0 ) {
            draw_mid_pane( w_sort_middle, *tmp_worn[leftListIndex] );
        } else {
            fold_and_print( w_sort_middle, 0, 1, middle_w - 1, c_white,
                            _( "Nothing to see here!" ) );
        }

        mvwprintz( w_encumb, 0, 1, c_white, _( "Encumbrance and Warmth" ) );
        print_encumbrance( w_encumb, -1, ( leftListSize > 0 ) ? tmp_worn[leftListIndex] : nullptr );

        // Right header
        mvwprintz( w_sort_right, 0, 0, c_ltgray, _( "(Innermost)" ) );
        mvwprintz( w_sort_right, 0, right_w - utf8_width( _( "Encumbrance" ) ), c_ltgray,
                   _( "Encumbrance" ) );

        // Right list
        rightListSize = 0;
        for( int cover = 0, pos = 1; cover < num_bp; cover++ ) {
            bool combined = false;
            if( cover > 3 && cover % 2 == 0 &&
                items_cover_bp( *this, cover ) == items_cover_bp( *this, cover + 1 ) ) {
                combined = true;
            }
            if( rightListSize >= rightListOffset && pos <= cont_h - 2 ) {
                mvwprintz( w_sort_right, pos, 1, ( cover == tabindex ? c_yellow : c_white ),
                           "%s:", body_part_name_as_heading( bp_aBodyPart[cover], combined ? 2 : 1 ).c_str() );
                pos++;
            }
            rightListSize++;
            for( auto &elem : items_cover_bp( *this, cover ) ) {
                if( rightListSize >= rightListOffset && pos <= cont_h - 2 ) {
                    trim_and_print( w_sort_right, pos, 2, right_w - 4, dam_color[elem.damage + 1],
                                    elem.name.c_str() );
                    mvwprintz( w_sort_right, pos, right_w - 2, c_ltgray, "%d",
                               elem.encumber );
                    pos++;
                }
                rightListSize++;
            }
            if( combined ) {
                cover++;
            }
        }

        // Right footer
        mvwprintz( w_sort_right, cont_h - 1, 0, c_ltgray, _( "(Outermost)" ) );
        if( rightListSize > cont_h - 2 ) {
            mvwprintz( w_sort_right, cont_h - 1, right_w - utf8_width( _( "<more>" ) ), c_ltblue,
                       _( "<more>" ) );
        }
        // F5
        wrefresh( w_sort_cat );
        wrefresh( w_sort_left );
        wrefresh( w_sort_middle );
        wrefresh( w_sort_right );
        wrefresh( w_encumb );

        const std::string action = ctxt.handle_input();
        if( is_npc() && action == "ASSIGN_INVLETS" ) {
            // It doesn't make sense to assign invlets to NPC items
            continue;
        }

        if( is_npc() && ( action == "EQUIP_ARMOR" || action == "REMOVE_ARMOR" ) ) {
            const npc &np = dynamic_cast<const npc &>( *this );
            if( !np.is_minion() && !g->u.has_trait( "DEBUG_MIND_CONTROL" ) ) {
                popup( _( "%s says: I don't trust you enough to let you do that!" ), disp_name().c_str() );
                continue;
            }
        }

        if( action == "UP" && leftListSize > 0 ) {
            leftListIndex--;
            if( leftListIndex < 0 ) {
                leftListIndex = tmp_worn.size() - 1;
            }

            // Scrolling logic
            leftListOffset = ( leftListIndex < leftListOffset ) ? leftListIndex : leftListOffset;
            if( !( ( leftListIndex >= leftListOffset ) &&
                   ( leftListIndex < leftListOffset + leftListSize ) ) ) {
                leftListOffset = leftListIndex - leftListSize + 1;
                leftListOffset = ( leftListOffset > 0 ) ? leftListOffset : 0;
            }

            // move selected item
            if( selected >= 0 ) {
                if( leftListIndex < selected ) {
                    std::swap( *tmp_worn[leftListIndex], *tmp_worn[selected] );
                } else {
                    const item tmp_item = *tmp_worn[selected];
                    i_rem( tmp_worn[selected] );
                    worn.insert( worn.end(), tmp_item );
                }

                selected = leftListIndex;
            }
        } else if( action == "DOWN" && leftListSize > 0 ) {
            leftListIndex = ( leftListIndex + 1 ) % tmp_worn.size();

            // Scrolling logic
            if( !( ( leftListIndex >= leftListOffset ) &&
                   ( leftListIndex < leftListOffset + leftListSize ) ) ) {
                leftListOffset = leftListIndex - leftListSize + 1;
                leftListOffset = ( leftListOffset > 0 ) ? leftListOffset : 0;
            }

            // move selected item
            if( selected >= 0 ) {
                if( leftListIndex > selected ) {
                    std::swap( *tmp_worn[leftListIndex], *tmp_worn[selected] );
                } else {
                    const item tmp_item = *tmp_worn[selected];
                    i_rem( tmp_worn[selected] );
                    worn.insert( worn.begin(), tmp_item );
                }

                selected = leftListIndex;
            }
        } else if( action == "LEFT" ) {
            tabindex--;
            if( tabindex < 0 ) {
                tabindex = tabcount - 1;
            }
            leftListIndex = leftListOffset = 0;
            selected = -1;
        } else if( action == "RIGHT" ) {
            tabindex = ( tabindex + 1 ) % tabcount;
            leftListIndex = leftListOffset = 0;
            selected = -1;
        } else if( action == "NEXT_TAB" ) {
            rightListOffset++;
            if( rightListOffset + cont_h - 2 > rightListSize ) {
                rightListOffset = rightListSize - cont_h + 2;
            }
        } else if( action == "PREV_TAB" ) {
            rightListOffset--;
            if( rightListOffset < 0 ) {
                rightListOffset = 0;
            }
        } else if( action == "MOVE_ARMOR" ) {
            if( selected >= 0 ) {
                selected = -1;
            } else {
                selected = leftListIndex;
            }
        } else if( action == "CHANGE_SIDE" ) {
            if( leftListIndex < ( int ) tmp_worn.size() && tmp_worn[leftListIndex]->is_sided() ) {
                if( g->u.query_yn( _( "Swap side for %s?" ), tmp_worn[leftListIndex]->tname().c_str() ) ) {
                    change_side( tmp_worn[leftListIndex] );
                    wrefresh( w_sort_armor );
                }
            }
        } else if( action == "EQUIP_ARMOR" ) {
            // filter inventory for all items that are armor/clothing
            // NOTE: This is from player's inventory, even for NPCs!
            // @todo Allow making NPCs equip their own stuff
            int pos = g->inv_for_unequipped( _( "Put on:" ), []( const item & it ) {
                return it.is_armor();
            } );
            // only equip if something valid selected!
            if( pos != INT_MIN ) {
                // wear the item
                if( wear( pos ) ) {
                    // reorder `worn` vector to place new item at cursor
                    auto iter = worn.end();
                    item new_equip  = *( --iter );
                    // remove the item
                    worn.erase( iter );
                    iter = worn.begin();
                    // advance the iterator to cursor's position
                    std::advance( iter, leftListIndex );
                    // inserts at position before iter (no b0f, phew)
                    worn.insert( iter, new_equip );
                } else if( is_npc() ) {
                    // @todo Pass the reason here
                    popup( _( "Can't put this on" ) );
                }
            }
            draw_grid( w_sort_armor, left_w, middle_w );
        } else if( action == "REMOVE_ARMOR" ) {
            // query (for now)
            if( leftListIndex < ( int ) tmp_worn.size() ) {
                if( g->u.query_yn( _( "Remove selected armor?" ) ) ) {
                    // remove the item, asking to drop it if necessary
                    takeoff( tmp_worn[leftListIndex], is_npc() );
                    wrefresh( w_sort_armor );
                }
            }
        } else if( action == "ASSIGN_INVLETS" ) {
            // prompt first before doing this (yes yes, more popups...)
            if( query_yn( _( "Reassign invlets for armor?" ) ) ) {
                // Start with last armor (the most unimportant one?)
                auto iiter = inv_chars.rbegin();
                auto witer = worn.rbegin();
                while( witer != worn.rend() && iiter != inv_chars.rend() ) {
                    const char invlet = *iiter;
                    item &w = *witer;
                    if( invlet == w.invlet ) {
                        ++witer;
                    } else if( invlet_to_position( invlet ) != INT_MIN ) {
                        ++iiter;
                    } else {
                        w.invlet = invlet;
                        ++witer;
                        ++iiter;
                    }
                }
            }
        } else if( action == "USAGE_HELP" ) {
            popup_getkey( _( "\
Use the arrow- or keypad keys to navigate the left list.\n\
Press [%s] to select highlighted armor for reordering.\n\
Use   [%s] / [%s] to scroll the right list.\n\
Press [%s] to assign special inventory letters to clothing.\n\
Press [%s] to change the side on which item is worn.\n\
Use   [%s] to equip an armor item from the inventory.\n\
Press [%s] to remove selected armor from oneself.\n\
 \n\
[Encumbrance and Warmth] explanation:\n\
The first number is the summed encumbrance from all clothing on that bodypart.\n\
The second number is an additional encumbrance penalty caused by wearing multiple items on one of the bodypart's four layers.\n\
The sum of these values is the effective encumbrance value your character has for that bodypart." ),
                          ctxt.get_desc( "MOVE_ARMOR" ).c_str(),
                          ctxt.get_desc( "PREV_TAB" ).c_str(),
                          ctxt.get_desc( "NEXT_TAB" ).c_str(),
                          ctxt.get_desc( "ASSIGN_INVLETS" ).c_str(),
                          ctxt.get_desc( "CHANGE_SIDE" ).c_str(),
                          ctxt.get_desc( "EQUIP_ARMOR" ).c_str(),
                          ctxt.get_desc( "REMOVE_ARMOR" ).c_str()
                        );
            draw_grid( w_sort_armor, left_w, middle_w );
        } else if( action == "HELP_KEYBINDINGS" ) {
            draw_grid( w_sort_armor, left_w, middle_w );
        } else if( action == "QUIT" ) {
            exit = true;
        }
    }

    delwin( w_sort_cat );
    delwin( w_sort_left );
    delwin( w_sort_middle );
    delwin( w_sort_right );
    delwin( w_sort_armor );
    delwin( w_encumb );
}
