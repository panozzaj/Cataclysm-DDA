#include "game.h"
#include "player.h"
#include "output.h"
#include "map.h"
#include "debug.h"
#include "catacharset.h"
#include "translations.h"
#include "uistate.h"
#include "auto_pickup.h"
#include "messages.h"
#include "player_activity.h"
#include "advanced_inv.h"
#include "compatibility.h"
#include "enums.h"
#include "input.h"
#include "options.h"
#include "ui.h"
#include "trap.h"
#include "itype.h"
#include "vehicle.h"
#include "mapdata.h"
#include "field.h"
#include "cata_utility.h"

#include <map>
#include <set>
#include <algorithm>
#include <string>
#include <sstream>
#include <cmath>
#include <vector>
#include <cassert>
#include <cstdlib>
#include <cstring>


enum aim_exit {
    exit_none = 0,
    exit_okay,
    exit_re_entry
};

advanced_inventory::advanced_inventory()
    : head_height( 5 )
    , min_w_height( 10 )
    , min_w_width( FULL_SCREEN_WIDTH )
    , max_w_width( 120 )
    , inCategoryMode( false )
    , recalc( true )
    , redraw( true )
    , src( left )
    , dest( right )
    , filter_edit( false )
      // panes don't need initialization, they are recalculated immediately
    , squares(
{ {
        //               hx  hy  x    y   z
        { AIM_INVENTORY, 25, 2, {0,   0,  0}, _( "Inventory" ),          _( "IN" ) },
        { AIM_SOUTHWEST, 30, 3, { -1,  1,  0}, _( "South West" ),         _( "SW" ) },
        { AIM_SOUTH,     33, 3, {0,   1,  0}, _( "South" ),              _( "S" )  },
        { AIM_SOUTHEAST, 36, 3, {1,   1,  0}, _( "South East" ),         _( "SE" ) },
        { AIM_WEST,      30, 2, { -1,  0,  0}, _( "West" ),               _( "W" )  },
        { AIM_CENTER,    33, 2, {0,   0,  0}, _( "Directly below you" ), _( "DN" ) },
        { AIM_EAST,      36, 2, {1,   0,  0}, _( "East" ),               _( "E" )  },
        { AIM_NORTHWEST, 30, 1, { -1, -1,  0}, _( "North West" ),         _( "NW" ) },
        { AIM_NORTH,     33, 1, {0,  -1,  0}, _( "North" ),              _( "N" )  },
        { AIM_NORTHEAST, 36, 1, {1,  -1,  0}, _( "North East" ),         _( "NE" ) },
        { AIM_DRAGGED,   25, 1, {0,   0,  0}, _( "Grabbed Vehicle" ),    _( "GR" ) },
        { AIM_ALL,       22, 3, {0,   0,  0}, _( "Surrounding area" ),   _( "AL" ) },
        { AIM_CONTAINER, 22, 1, {0,   0,  0}, _( "Container" ),          _( "CN" ) },
        { AIM_WORN,      25, 3, {0,   0,  0}, _( "Worn Items" ),         _( "WR" ) }
    }
} )
, head( nullptr )
, left_window( nullptr )
, right_window( nullptr )
{
    // initialise screen coordinates for small overview 3x3 grid, depending on control scheme
    if( tile_iso && use_tiles ) {
        // Rotate the coordinates.
        squares[1].hscreenx = 33;
        squares[2].hscreenx = 36;
        squares[3].hscreeny = 2;
        squares[4].hscreeny = 3;
        squares[6].hscreeny = 1;
        squares[7].hscreeny = 2;
        squares[8].hscreenx = 30;
        squares[9].hscreenx = 33;
    }
}

advanced_inventory::~advanced_inventory()
{
    save_settings( false );
    auto &aim_code = uistate.adv_inv_exit_code;
    if( aim_code != exit_re_entry ) {
        aim_code = exit_okay;
    }
    // Only refresh if we exited manually, otherwise we're going to be right back
    if( exit ) {
        werase( head );
        werase( minimap );
        werase( mm_border );
        werase( left_window );
        werase( right_window );
    }
    delwin( head );
    delwin( minimap );
    delwin( mm_border );
    delwin( left_window );
    delwin( right_window );
    if( exit ) {
        g->refresh_all();
    }
}

void advanced_inventory::save_settings( bool only_panes )
{
    if( only_panes == false ) {
        uistate.adv_inv_last_coords = g->u.pos();
        uistate.adv_inv_src = src;
        uistate.adv_inv_dest = dest;
    }
    for( int i = 0; i < NUM_PANES; ++i ) {
        uistate.adv_inv_in_vehicle[i] = panes[i].in_vehicle();
        uistate.adv_inv_area[i] = panes[i].get_area();
        uistate.adv_inv_index[i] = panes[i].index;
        uistate.adv_inv_filter[i] = panes[i].filter;
    }
}

void advanced_inventory::load_settings()
{
    aim_exit aim_code = static_cast<aim_exit>(uistate.adv_inv_exit_code);
    for(int i = 0; i < NUM_PANES; ++i) {
        auto location = static_cast<aim_location>(uistate.adv_inv_area[i]);
        auto square = squares[location];
        // determine the square's veh/map item presence
        bool has_veh_items = (square.can_store_in_vehicle()) ?
            !square.veh->get_items(square.vstor).empty() : false;
        bool has_map_items = !g->m.i_at(square.pos).empty();
        // determine based on map items and settings to show cargo
        bool show_vehicle = (aim_code == exit_re_entry) ?
            uistate.adv_inv_in_vehicle[i] : (has_veh_items) ?
            true : (has_map_items) ? false : square.can_store_in_vehicle();
        panes[i].set_area(square, show_vehicle);
        panes[i].sortby = static_cast<advanced_inv_sortby>(uistate.adv_inv_sort[i]);
        panes[i].index = uistate.adv_inv_index[i];
        panes[i].filter = uistate.adv_inv_filter[i];
    }
    uistate.adv_inv_exit_code = exit_none;
}

std::string advanced_inventory::get_sortname( advanced_inv_sortby sortby )
{
    switch( sortby ) {
        case SORTBY_NONE:
            return _( "none" );
        case SORTBY_NAME:
            return _( "name" );
        case SORTBY_WEIGHT:
            return _( "weight" );
        case SORTBY_VOLUME:
            return _( "volume" );
        case SORTBY_CHARGES:
            return _( "charges" );
        case SORTBY_CATEGORY:
            return _( "category" );
        case SORTBY_DAMAGE:
            return _( "damage" );
    }
    return "!BUG!";
}

bool advanced_inventory::get_square( const std::string action, aim_location &ret )
{
    if( action == "ITEMS_INVENTORY" ) {
        ret = AIM_INVENTORY;
    } else if( action == "ITEMS_WORN" ) {
        ret = AIM_WORN;
    } else if( action == "ITEMS_NW" ) {
        ret = screen_relative_location( AIM_NORTHWEST );
    } else if( action == "ITEMS_N" ) {
        ret = screen_relative_location( AIM_NORTH );
    } else if( action == "ITEMS_NE" ) {
        ret = screen_relative_location( AIM_NORTHEAST );
    } else if( action == "ITEMS_W" ) {
        ret = screen_relative_location( AIM_WEST );
    } else if( action == "ITEMS_CE" ) {
        ret = AIM_CENTER;
    } else if( action == "ITEMS_E" ) {
        ret = screen_relative_location( AIM_EAST );
    } else if( action == "ITEMS_SW" ) {
        ret = screen_relative_location( AIM_SOUTHWEST );
    } else if( action == "ITEMS_S" ) {
        ret = screen_relative_location( AIM_SOUTH );
    } else if( action == "ITEMS_SE" ) {
        ret = screen_relative_location( AIM_SOUTHEAST );
    } else if( action == "ITEMS_AROUND" ) {
        ret = AIM_ALL;
    } else if( action == "ITEMS_DRAGGED_CONTAINER" ) {
        ret = AIM_DRAGGED;
    } else if( action == "ITEMS_CONTAINER" ) {
        ret = AIM_CONTAINER;
    } else {
        return false;
    }
    return true;
}


void advanced_inventory::print_items( advanced_inventory_pane &pane, bool active )
{
    const auto &items = pane.items;
    WINDOW *window = pane.window;
    const auto index = pane.index;
    const int page = index / itemsPerPage;
    bool compact = ( TERMX <= 100 );

    int columns = getmaxx( window );
    std::string spaces( columns - 4, ' ' );

    nc_color norm = active ? c_white : c_dkgray;

    //print inventory's current and total weight + volume
    if( pane.get_area() == AIM_INVENTORY || pane.get_area() == AIM_WORN ) {
        //right align
        int hrightcol = columns -
                        to_string( convert_weight( g->u.weight_carried() ) ).length() - 3 - //"xxx.y/"
                        to_string( convert_weight( g->u.weight_capacity() ) ).length() - 3 - //"xxx.y_"
                        to_string( g->u.volume_carried() ).length() - 1 - //"xxx/"
                        to_string( g->u.volume_capacity() ).length() - 1; //"xxx|"
        nc_color color = c_ltgreen;//red color if overload
        if( g->u.weight_carried() > g->u.weight_capacity() ) {
            color = c_red;
        }
        mvwprintz( window, 4, hrightcol, color, "%.1f", convert_weight( g->u.weight_carried() ) );
        wprintz( window, c_ltgray, "/%.1f ", convert_weight( g->u.weight_capacity() ) );
        if( g->u.volume_carried() > g->u.volume_capacity() ) {
            color = c_red;
        } else {
            color = c_ltgreen;
        }
        wprintz( window, color, "%d", g->u.volume_carried() );
        wprintz( window, c_ltgray, "/%d ", g->u.volume_capacity() );
    } else { //print square's current and total weight + volume
        std::string head;
        if( pane.get_area() == AIM_ALL ) {
            head = string_format( "%3.1f %3d",
                                  convert_weight( squares[pane.get_area()].weight ),
                                  squares[pane.get_area()].volume );
        } else {
            int maxvolume = 0;
            auto &s = squares[pane.get_area()];
            if( pane.get_area() == AIM_CONTAINER && s.get_container(pane.in_vehicle()) != nullptr ) {
                maxvolume = s.get_container(pane.in_vehicle())->type->container->contains;
            } else if( pane.in_vehicle() ) {
                maxvolume = s.veh->max_volume( s.vstor );
            } else {
                maxvolume = g->m.max_volume( s.pos );
            }
            head = string_format( "%3.1f %3d/%3d", convert_weight( s.weight ), s.volume, maxvolume );
        }
        mvwprintz( window, 4, columns - 1 - head.length(), norm, "%s", head.c_str() );
    }

    //print header row and determine max item name length
    const int lastcol = columns - 2; // Last printable column
    const size_t name_startpos = ( compact ? 1 : 4 );
    const size_t src_startpos = lastcol - 17;
    const size_t amt_startpos = lastcol - 14;
    const size_t weight_startpos = lastcol - 9;
    const size_t vol_startpos = lastcol - 3;
    int max_name_length = amt_startpos - name_startpos - 1; // Default name length

    //~ Items list header. Table fields length without spaces: amt - 4, weight - 5, vol - 4.
    const int table_hdr_len1 = utf8_width( _( "amt weight vol" ) ); // Header length type 1
    //~ Items list header. Table fields length without spaces: src - 2, amt - 4, weight - 5, vol - 4.
    const int table_hdr_len2 = utf8_width( _( "src amt weight vol" ) ); // Header length type 2

    mvwprintz( window, 5, ( compact ? 1 : 4 ), c_ltgray, _( "Name (charges)" ) );
    if( pane.get_area() == AIM_ALL && !compact ) {
        mvwprintz( window, 5, lastcol - table_hdr_len2 + 1, c_ltgray, _( "src amt weight vol" ) );
        max_name_length = src_startpos - name_startpos - 1; // 1 for space
    } else {
        mvwprintz( window, 5, lastcol - table_hdr_len1 + 1, c_ltgray, _( "amt weight vol" ) );
    }

    for( int i = page * itemsPerPage , x = 0 ; i < ( int )items.size() &&
         x < itemsPerPage ; i++ , x++ ) {
        const auto &sitem = items[i];
        if( sitem.is_category_header() ) {
            mvwprintz( window, 6 + x, ( columns - utf8_width(sitem.name) - 6 ) / 2, c_cyan, "[%s]",
                       sitem.name.c_str() );
            continue;
        }
        if( !sitem.is_item_entry() ) {
            // Empty entry at the bottom of a page.
            continue;
        }
        const auto &it = *sitem.items.front();
        const bool selected = active && index == i;

        nc_color thiscolor = active ? it.color_in_inventory() : norm;
        nc_color thiscolordark = c_dkgray;
        nc_color print_color;

        if( selected ) {
            thiscolor = ( inCategoryMode &&
                          pane.sortby == SORTBY_CATEGORY ) ? c_white_red : hilite( thiscolor );
            thiscolordark = hilite( thiscolordark );
            if( compact ) {
                mvwprintz( window, 6 + x, 1, thiscolor, "  %s", spaces.c_str() );
            } else {
                mvwprintz( window, 6 + x, 1, thiscolor, ">>%s", spaces.c_str() );
            }
        }

        std::string item_name = it.display_name();
        if( OPTIONS["ITEM_SYMBOLS"] ) {
            item_name = string_format( "%c %s", it.symbol(), item_name.c_str() );
        }

        //print item name
        trim_and_print( window, 6 + x, compact ? 1 : 4, max_name_length, thiscolor, "%s",
                        item_name.c_str() );

        //print src column
        // TODO: specify this is coming from a vehicle!
        if( pane.get_area() == AIM_ALL && !compact ) {
            mvwprintz( window, 6 + x, src_startpos, thiscolor, "%s", squares[sitem.area].shortname.c_str() );
        }

        //print "amount" column
        int it_amt = sitem.stacks;
        if( it_amt > 1 ) {
            print_color = thiscolor;
            if( it_amt > 9999 ) {
                it_amt = 9999;
                print_color = selected ? hilite( c_red ) : c_red;
            }
            mvwprintz( window, 6 + x, amt_startpos, print_color, "%4d", it_amt );
        }

        //print weight column
        double it_weight = convert_weight( sitem.weight );
        size_t w_precision;
        print_color = ( it_weight > 0 ) ? thiscolor : thiscolordark;

        if( it_weight >= 1000.0 ) {
            if( it_weight >= 10000.0 ) {
                print_color = selected ? hilite( c_red ) : c_red;
                it_weight = 9999.0;
            }
            w_precision = 0;
        } else if( it_weight >= 100.0 ) {
            w_precision = 1;
        } else {
            w_precision = 2;
        }
        mvwprintz( window, 6 + x, weight_startpos, print_color, "%5.*f", w_precision, it_weight );

        //print volume column
        int it_vol = sitem.volume;
        print_color = ( it_vol > 0 ) ? thiscolor : thiscolordark;
        if( it_vol > 9999 ) {
            it_vol = 9999;
            print_color = selected ? hilite( c_red ) : c_red;
        }
        mvwprintz( window, 6 + x, vol_startpos, print_color, "%4d", it_vol );

        if( active && sitem.autopickup ) {
            mvwprintz( window, 6 + x, 1, magenta_background( it.color_in_inventory() ), "%s",
                       ( compact ? it.tname().substr( 0, 1 ) : ">" ).c_str() );
        }
    }
}

struct advanced_inv_sorter {
    advanced_inv_sortby sortby;
    advanced_inv_sorter( advanced_inv_sortby sort )
    {
        sortby = sort;
    };
    bool operator()( const advanced_inv_listitem &d1, const advanced_inv_listitem &d2 )
    {
        // Note: the item pointer can only be null on sort by category, otherwise it is always valid.
        switch( sortby ) {
            case SORTBY_NONE:
                if( d1.idx != d2.idx ) {
                    return d1.idx < d2.idx;
                }
                break;
            case SORTBY_NAME:
                // Fall through to code below the switch
                break;
            case SORTBY_WEIGHT:
                if( d1.weight != d2.weight ) {
                    return d1.weight > d2.weight;
                }
                break;
            case SORTBY_VOLUME:
                if( d1.volume != d2.volume ) {
                    return d1.volume > d2.volume;
                }
                break;
            case SORTBY_CHARGES:
                if( d1.items.front()->charges != d2.items.front()->charges ) {
                    return d1.items.front()->charges > d2.items.front()->charges;
                }
                break;
            case SORTBY_CATEGORY:
                assert( d1.cat != nullptr );
                assert( d2.cat != nullptr );
                if( d1.cat != d2.cat ) {
                    return *d1.cat < *d2.cat;
                } else if( d1.is_category_header() ) {
                    return true;
                } else if( d2.is_category_header() ) {
                    return false;
                }
                break;
            case SORTBY_DAMAGE:
                if( d1.items.front()->damage != d2.items.front()->damage ) {
                    return d1.items.front()->damage < d2.items.front()->damage;
                }
                break;
        }
        // secondary sort by name
        const std::string *n1;
        const std::string *n2;
        if( d1.name_without_prefix == d2.name_without_prefix ) {
            //if names without prefix equal, compare full name
            n1 = &d1.name;
            n2 = &d2.name;
        } else {
            //else compare name without prefix
            n1 = &d1.name_without_prefix;
            n2 = &d2.name_without_prefix;
        }
        return std::lexicographical_compare( n1->begin(), n1->end(),
                                             n2->begin(), n2->end(), sort_case_insensitive_less() );
    }
};

void advanced_inventory::menu_square( uimenu *menu )
{
    assert( menu != nullptr );
    assert( menu->entries.size() >= 9 );
    int ofs = -25 - 4;
    int sel = screen_relative_location( static_cast <aim_location>( menu->selected + 1 ) );
    for( int i = 1; i < 10; i++ ) {
        aim_location loc = screen_relative_location( static_cast <aim_location>( i ) );
        char key = get_location_key( loc );
        bool in_vehicle = squares[loc].can_store_in_vehicle();
        const char *bracket = ( in_vehicle ) ? "<>" : "[]";
        // always show storage option for vehicle storage, if applicable
        bool canputitems = ( menu->entries[i - 1].enabled && squares[loc].canputitems() );
        nc_color bcolor = ( canputitems ? ( sel == loc ? h_white : c_ltgray ) : c_dkgray );
        nc_color kcolor = ( canputitems ? ( sel == loc ? h_white : c_ltgray ) : c_dkgray );
        const int x = squares[loc].hscreenx + ofs;
        const int y = squares[loc].hscreeny + 5;
        mvwprintz( menu->window, y, x, bcolor, "%c", bracket[0] );
        wprintz( menu->window, kcolor, "%c", key );
        wprintz( menu->window, bcolor, "%c", bracket[1] );
    }
}

inline char advanced_inventory::get_location_key( aim_location area )
{
    switch( area ) {
        case AIM_INVENTORY:
            return 'I';
        case AIM_WORN:
            return 'W';
        case AIM_CENTER:
            return '5';
        case AIM_ALL:
            return 'A';
        case AIM_DRAGGED:
            return 'D';
        case AIM_CONTAINER:
            return 'C';
        case AIM_NORTH:
        case AIM_SOUTH:
        case AIM_EAST:
        case AIM_WEST:
        case AIM_NORTHEAST:
        case AIM_NORTHWEST:
        case AIM_SOUTHEAST:
        case AIM_SOUTHWEST:
            return  get_direction_key( area );
        default:
            debugmsg( "invalid [aim_location] in get_location_key()!" );
            return ' ';
    }
}

char advanced_inventory::get_direction_key( aim_location area )
{

    if( area == screen_relative_location( AIM_SOUTHWEST ) ) {
        return '1';
    }
    if( area == screen_relative_location( AIM_SOUTH ) ) {
        return '2';
    }
    if( area == screen_relative_location( AIM_SOUTHEAST ) ) {
        return '3';
    }
    if( area == screen_relative_location( AIM_WEST ) ) {
        return '4';
    }
    if( area == screen_relative_location( AIM_EAST ) ) {
        return '6';
    }
    if( area == screen_relative_location( AIM_NORTHWEST ) ) {
        return '7';
    }
    if( area == screen_relative_location( AIM_NORTH ) ) {
        return '8';
    }
    if( area == screen_relative_location( AIM_NORTHEAST ) ) {
        return '9';
    }
    debugmsg( "invalid [aim_location] in get_direction_key()!" );
    return '0';
}

int advanced_inventory::print_header( advanced_inventory_pane &pane, aim_location sel )
{
    WINDOW *window = pane.window;
    int area = pane.get_area();
    int wwidth = getmaxx( window );
    int ofs = wwidth - 25 - 2 - 14;
    for( int i = 0; i < NUM_AIM_LOCATIONS; ++i ) {
        const char key = get_location_key( static_cast<aim_location>( i ) );
        const char *bracket = ( squares[i].can_store_in_vehicle() ) ? "<>" : "[]";
        bool in_vehicle = ( pane.in_vehicle() && squares[i].id == area && sel == area && area != AIM_ALL );
        bool all_brackets = ( area == AIM_ALL && ( i >= AIM_SOUTHWEST && i <= AIM_NORTHEAST ) );
        nc_color bcolor = c_red, kcolor = c_red;
        if( squares[i].canputitems( pane.get_cur_item_ptr() ) ) {
            bcolor = ( in_vehicle ) ? c_ltblue :
                     ( area == i || all_brackets ) ? c_ltgray : c_dkgray;
            kcolor = ( area == i ) ? c_white : ( sel == i ) ? c_ltgray : c_dkgray;
        }
        const int x = squares[i].hscreenx + ofs;
        const int y = squares[i].hscreeny;
        mvwprintz( window, y, x, bcolor, "%c", bracket[0] );
        wprintz( window, kcolor, "%c", ( in_vehicle && sel != AIM_DRAGGED ) ? 'V' : key );
        wprintz( window, bcolor, "%c", bracket[1] );
    }
    return squares[AIM_INVENTORY].hscreeny + ofs;
}

int advanced_inv_area::get_item_count() const
{
    if( id == AIM_INVENTORY ) {
        return g->u.inv.size();
    } else if( id == AIM_WORN ) {
        return g->u.worn.size();
    } else if( id == AIM_ALL ) {
        return 0;
    } else if( id == AIM_DRAGGED ) {
        return ( can_store_in_vehicle() == true ) ? veh->get_items( vstor ).size() : 0;
    } else {
        return g->m.i_at( pos ).size();
    }
}

void advanced_inv_area::init()
{
    pos = g->u.pos() + off;
    veh = nullptr;
    vstor = -1;
    volume = 0; // must update in main function
    weight = 0; // must update in main function
    switch( id ) {
        case AIM_INVENTORY:
        case AIM_WORN:
            canputitemsloc = true;
            break;
        case AIM_DRAGGED:
            if( g->u.grab_type != OBJECT_VEHICLE ) {
                canputitemsloc = false;
                desc[0] = _( "Not dragging any vehicle" );
                break;
            }
            // offset for dragged vehicles is not statically initialized, so get it
            off = g->u.grab_point;
            // Reset position because offset changed
            pos = g->u.pos() + off;
            veh = g->m.veh_at( pos, vstor );
            if( veh != nullptr ) {
                vstor = veh->part_with_feature( vstor, "CARGO", false );
            }
            if( vstor >= 0 ) {
                desc[0] = veh->name;
                canputitemsloc = true;
                max_size = MAX_ITEM_IN_VEHICLE_STORAGE;
                max_volume = veh->max_volume( vstor );
            } else {
                veh = nullptr;
                canputitemsloc = false;
                desc[0] = _( "No dragged vehicle" );
            }
            break;
        case AIM_CONTAINER:
            // set container position based on location
            set_container_position();
            // location always valid, actual check is done in canputitems()
            // and depends on selected item in pane (if it is valid container)
            canputitemsloc = true;
            if( get_container() == nullptr ) {
                desc[0] = _( "Invalid container" );
            }
            break;
        case AIM_ALL:
            desc[0] = _( "All 9 squares" );
            canputitemsloc = true;
            break;
        case AIM_SOUTHWEST:
        case AIM_SOUTH:
        case AIM_SOUTHEAST:
        case AIM_WEST:
        case AIM_CENTER:
        case AIM_EAST:
        case AIM_NORTHWEST:
        case AIM_NORTH:
        case AIM_NORTHEAST:
            veh = g->m.veh_at( pos, vstor );
            if( veh != nullptr ) {
                vstor = veh->part_with_feature( vstor, "CARGO", false );
            }
            canputitemsloc = can_store_in_vehicle() || g->m.can_put_items_ter_furn( pos );
            max_size = MAX_ITEM_IN_SQUARE;
            max_volume = g->m.max_volume( pos );
            if( can_store_in_vehicle() ) {
                // get storage label
                const auto part = veh->parts[veh->global_part_at(pos.x, pos.y)];
                desc[1] = veh->get_label(part.mount.x, part.mount.y);
            }
            // get graffiti or terrain name
            desc[0] = ( g->m.has_graffiti_at( pos ) == true ) ?
                g->m.graffiti_at( pos ) : g->m.ter_at( pos ).name;
        default:
            break;
    }

    /* assemble a list of interesting traits of the target square */
    // fields? with a special case for fire
    bool danger_field = false;
    const field &tmpfld = g->m.field_at( pos );
    for( auto &fld : tmpfld ) {
        const field_entry &cur = fld.second;
        field_id curType = cur.getFieldType();
        switch( curType ) {
            case fd_fire:
                flags.append( _( " <color_white_red>FIRE</color>" ) );
                break;
            default:
                if( cur.is_dangerous() ) {
                    danger_field = true;
                }
                break;
        }
    }
    if( danger_field ) {
        flags.append( _( " DANGER" ) );
    }

    // trap?
    const trap &tr = g->m.tr_at( pos );
    if( tr.can_see( pos, g->u ) && !tr.is_benign() ) {
        flags.append( _( " TRAP" ) );
    }

    // water?
    static const std::array<ter_id, 6> ter_water = {
        {t_water_dp, t_water_pool, t_swater_dp, t_water_sh, t_swater_sh, t_sewage}
    };
    auto ter_check = [this]
        (const ter_id id) {
            return g->m.ter(this->pos) == id;
        };
    if(std::any_of(ter_water.begin(), ter_water.end(), ter_check)) {
        flags.append( _( " WATER" ) );
    }

    // remove leading space
    if( flags.length() && flags[0] == ' ' ) {
        flags.erase( 0, 1 );
    }
}

std::string center_text( const char *str, int width )
{
    return std::string( ( ( width - strlen( str ) ) / 2 ), ' ' ) + str;
}

void advanced_inventory::init()
{
    for( auto &square : squares ) {
        square.init();
    }

    load_settings();

    src  = static_cast<side>( uistate.adv_inv_src );
    dest = static_cast<side>( uistate.adv_inv_dest );

    w_height = ( TERMY < min_w_height + head_height ) ? min_w_height : TERMY - head_height;
    w_width = ( TERMX < min_w_width ) ? min_w_width : ( TERMX > max_w_width ) ? max_w_width :
              ( int )TERMX;

    headstart = 0; //(TERMY>w_height)?(TERMY-w_height)/2:0;
    colstart = ( TERMX > w_width ) ? ( TERMX - w_width ) / 2 : 0;

    head = newwin( head_height, w_width - minimap_width, headstart, colstart );
    mm_border = newwin( minimap_height + 2, minimap_width + 2, headstart,
                        colstart + ( w_width - ( minimap_width + 2 ) ) );
    minimap = newwin( minimap_height, minimap_width, headstart + 1,
                      colstart + ( w_width - ( minimap_width + 1 ) ) );
    left_window = newwin( w_height, w_width / 2, headstart + head_height, colstart );
    right_window = newwin( w_height, w_width / 2, headstart + head_height,
                           colstart + w_width / 2 );

    itemsPerPage = w_height - 2 - 5; // 2 for the borders, 5 for the header stuff

    panes[left].window = left_window;
    panes[right].window = right_window;
}

advanced_inv_listitem::advanced_inv_listitem( item *an_item, int index, int count,
        aim_location _area, bool from_veh )
    : idx( index )
    , area( _area )
    , id(an_item->type->id)
    , name( an_item->tname( count ) )
    , name_without_prefix( an_item->tname( 1, false ) )
    , autopickup( get_auto_pickup().has_rule( an_item->tname( 1, false ) ) )
    , stacks( count )
    , volume( an_item->volume() * stacks )
    , weight( an_item->weight() * stacks )
    , cat( &an_item->get_category() )
    , from_vehicle( from_veh )
{
    items.push_back(an_item);
    assert( stacks >= 1 );
}

advanced_inv_listitem::advanced_inv_listitem(const std::list<item*> &list, int index,
            aim_location loc, bool veh) :
    idx(index),
    area(loc),
    id(list.front()->type->id),
    items(list),
    name(list.front()->tname(list.size())),
    name_without_prefix(list.front()->tname(1, false)),
    autopickup(get_auto_pickup().has_rule(list.front()->tname(1, false))),
    stacks(list.size()),
    volume(list.front()->volume() * stacks),
    weight(list.front()->weight() * stacks),
    cat(&list.front()->get_category()),
    from_vehicle(veh)
{
    assert(stacks >= 1);
}

advanced_inv_listitem::advanced_inv_listitem()
    : idx()
    , area()
    , id("null")
    , name()
    , name_without_prefix()
    , autopickup()
    , stacks()
    , volume()
    , weight()
    , cat( nullptr )
{
}

advanced_inv_listitem::advanced_inv_listitem( const item_category *category )
    : idx()
    , area()
    , id("null")
    , name( category->name )
    , name_without_prefix()
    , autopickup()
    , stacks()
    , volume()
    , weight()
    , cat( category )
{
}

bool advanced_inv_listitem::is_category_header() const
{
    return items.empty() && cat != nullptr;
}

bool advanced_inv_listitem::is_item_entry() const
{
    return !items.empty();
}

bool advanced_inventory_pane::is_filtered( const advanced_inv_listitem &it ) const
{
    return is_filtered( it.items.front() );
}

bool advanced_inventory_pane::is_filtered( const item *it ) const
{
    if( filter.empty() ) {
        return false;
    }

    std::string str = it->tname();
    if( filtercache.find( str ) == filtercache.end() ) {
        bool match = !list_items_match( it, filter );
        filtercache[ str ] = match;

        return match;
    }

    return filtercache[ str ];
}

// roll our own, to handle moving stacks better
typedef std::vector<std::list<item*>> itemstack;

template <typename T>
static itemstack i_stacked(T items)
{
    //create a new container for our stacked items
    itemstack stacks;
//    // make a list of the items first, so we can add non stacked items back on
//    std::list<item> items(things.begin(), things.end());
    // used to recall indices we stored `itype_id' item at in itemstack
    std::unordered_map<itype_id, std::set<int>> cache;
    // iterate through and create stacks
    for(auto &elem : items) {
        const auto &id = elem.type->id;
        auto iter = cache.find(id);
        bool got_stacked = false;
        // cache entry exists
        if(iter != cache.end()) {
            // check to see if it stacks with each item in a stack, not just front()
            for(auto &idx : iter->second) {
                for(auto &it : stacks[idx]) {
                    if((got_stacked = it->stacks_with(elem))) {
                        stacks[idx].push_back(&elem);
                        break;
                    }
                }
                if(got_stacked) {
                    break;
                }
            }
        }
        if(!got_stacked) {
            cache[id].insert(stacks.size());
            stacks.push_back({&elem});
        }
    }
    return stacks;
}

void advanced_inventory_pane::add_items_from_area( advanced_inv_area &square, bool vehicle_override )
{
    assert( square.id != AIM_ALL );
    square.volume = 0;
    square.weight = 0;
    if( !square.canputitems() ) {
        return;
    }
    map &m = g->m;
    player &u = g->u;
    // Existing items are *not* cleared on purpose, this might be called
    // several times in case all surrounding squares are to be shown.
    if( square.id == AIM_INVENTORY ) {
        const invslice &stacks = u.inv.slice();
        for( size_t x = 0; x < stacks.size(); ++x ) {
            auto &an_item = stacks[x]->front();
            advanced_inv_listitem it( &an_item, x, stacks[x]->size(), square.id, false );
            if( is_filtered( it.items.front()) ) {
                continue;
            }
            square.volume += it.volume;
            square.weight += it.weight;
            items.push_back( it );
        }
    } else if( square.id == AIM_WORN ) {
        auto iter = u.worn.begin();
        for( size_t i = 0; i < u.worn.size(); ++i, ++iter ) {
            advanced_inv_listitem it( &*iter, i, 1, square.id, false );
            if( is_filtered( it.items.front() ) ) {
                continue;
            }
            square.volume += it.volume;
            square.weight += it.weight;
            items.push_back( it );
        }
    } else if( square.id == AIM_CONTAINER ) {
        item *cont = square.get_container( in_vehicle() );
        if( cont != nullptr ) {
            if( !cont->is_container_empty() ) {
                // filtering does not make sense for liquid in container
                item *it = &( square.get_container( in_vehicle() )->contents[0] );
                advanced_inv_listitem ait( it, 0, 1, square.id, in_vehicle() );
                square.volume += ait.volume;
                square.weight += ait.weight;
                items.push_back( ait );
            }
            square.desc[0] = cont->tname( 1, false );
        }
    } else {
        bool is_in_vehicle = square.can_store_in_vehicle() && (in_vehicle() || vehicle_override);
        const itemstack &stacks = (is_in_vehicle) ?
                                  i_stacked( square.veh->get_items( square.vstor ) ) :
                                  i_stacked( m.i_at( square.pos ) );

        for( size_t x = 0; x < stacks.size(); ++x ) {
            advanced_inv_listitem it(stacks[x], x, square.id, is_in_vehicle);
            if( is_filtered( it.items.front() ) ) {
                continue;
            }
            square.volume += it.volume;
            square.weight += it.weight;
            items.push_back( it );
        }
    }
}

void advanced_inventory_pane::paginate( size_t itemsPerPage )
{
    if( sortby != SORTBY_CATEGORY ) {
        return; // not needed as there are no category entries here.
    }
    // first, we insert all the items, then we sort the result
    for( size_t i = 0; i < items.size(); ++i ) {
        if( i % itemsPerPage == 0 ) {
            // first entry on the page, should be a category header
            if( items[i].is_item_entry() ) {
                items.insert( items.begin() + i, advanced_inv_listitem( items[i].cat ) );
            }
        }
        if( ( i + 1 ) % itemsPerPage == 0 && i + 1 < items.size() ) {
            // last entry of the page, but not the last entry at all!
            // Must *not* be a category header!
            if( items[i].is_category_header() ) {
                items.insert( items.begin() + i, advanced_inv_listitem() );
            }
        }
    }
}

void advanced_inventory::recalc_pane( side p )
{
    auto &pane = panes[p];
    pane.recalc = false;
    pane.items.clear();
    // Add items from the source location or in case of all 9 surrounding squares,
    // add items from several locations.
    if( pane.get_area() == AIM_ALL ) {
        auto &alls = squares[AIM_ALL];
        auto &there = panes[-p + 1];
        auto &other = squares[there.get_area()];
        alls.volume = 0;
        alls.weight = 0;
        for( auto &s : squares ) {
            // All the surrounding squares, nothing else
            if(s.id < AIM_SOUTHWEST || s.id > AIM_NORTHEAST) {
                continue;
            }

            // To allow the user to transfer all items from all surrounding squares to
            // a specific square, filter out items that are already on that square.
            // e.g. left pane AIM_ALL, right pane AIM_NORTH. The user holds the
            // enter key down in the left square and moves all items to the other side.
            const bool same = other.is_same( s );

            // Deal with squares with ground + vehicle storage
            // Also handle the case when the other tile covers vehicle
            // or the ground below the vehicle.
            if( s.can_store_in_vehicle() && !(same && there.in_vehicle()) ) {
                bool do_vehicle = ( there.get_area() == s.id ) ? !there.in_vehicle() : true;
                pane.add_items_from_area( s, do_vehicle );
                alls.volume += s.volume;
                alls.weight += s.weight;
            }

            // Add map items
            if( !same || there.in_vehicle() ) {
                pane.add_items_from_area( s );
                alls.volume += s.volume;
                alls.weight += s.weight;
            }
        }
    } else {
        pane.add_items_from_area( squares[pane.get_area()] );
    }
    // Insert category headers (only expected when sorting by category)
    if( pane.sortby == SORTBY_CATEGORY ) {
        std::set<const item_category *> categories;
        for( auto &it : pane.items ) {
            categories.insert( it.cat );
        }
        for( auto &cat : categories ) {
            pane.items.push_back( advanced_inv_listitem( cat ) );
        }
    }
    // Finally sort all items (category headers will now be moved to their proper position)
    std::stable_sort( pane.items.begin(), pane.items.end(), advanced_inv_sorter( pane.sortby ) );
    pane.paginate( itemsPerPage );
}

void advanced_inventory_pane::fix_index()
{
    if( items.empty() ) {
        index = 0;
        return;
    }
    if( index < 0 ) {
        index = 0;
    } else if( static_cast<size_t>( index ) >= items.size() ) {
        index = static_cast<int>( items.size() ) - 1;
    }
    skip_category_headers( +1 );
}

void advanced_inventory::redraw_pane( side p )
{
    // don't update ui if processing demands
    if(is_processing()) {
        return;
    }
    auto &pane = panes[p];
    if( recalc || pane.recalc ) {
        recalc_pane( p );
    } else if( !( redraw || pane.redraw ) ) {
        return;
    }
    pane.redraw = false;
    pane.fix_index();

    const bool active = p == src;
    const advanced_inv_area &square = squares[pane.get_area()];
    auto w = pane.window;

    werase( w );
    print_items( pane, active );

    auto itm = pane.get_cur_item_ptr();
    int width = print_header(pane, (itm != nullptr) ? itm->area : pane.get_area());
    bool same_as_dragged = (square.id >= AIM_SOUTHWEST && square.id <= AIM_NORTHEAST) && // only cardinals
            square.id != AIM_CENTER && panes[p].in_vehicle() && // not where you stand, and pane is in vehicle
            square.off == squares[AIM_DRAGGED].off; // make sure the offsets are the same as the grab point
    auto sq = (same_as_dragged) ? squares[AIM_DRAGGED] : square;
    bool car = square.can_store_in_vehicle() && panes[p].in_vehicle() && sq.id != AIM_DRAGGED;
    auto name = utf8_truncate((car == true) ? sq.veh->name : sq.name, width);
    auto desc = utf8_truncate(sq.desc[car], width);
    width -= 2 + 1; // starts at offset 2, plus space between the header and the text
    mvwprintz( w, 1, 2, active ? c_green  : c_ltgray, "%s", name.c_str() );
    mvwprintz( w, 2, 2, active ? c_ltblue : c_dkgray, "%s", desc.c_str() );
    trim_and_print( w, 3, 2, width, active ? c_cyan : c_dkgray, square.flags.c_str() );

    const int max_page = ( pane.items.size() + itemsPerPage - 1 ) / itemsPerPage;
    if( active && max_page > 1 ) {
        const int page = pane.index / itemsPerPage;
        mvwprintz( w, 4, 2, c_ltblue, _( "[<] page %d of %d [>]" ), page + 1, max_page );
    }

    if( active ) {
        wattron( w, c_cyan );
    }
    // draw a darker border around the inactive pane
    draw_border( w, ( active == true ) ? BORDER_COLOR : c_dkgray );
    mvwprintw( w, 0, 3, _( "< [s]ort: %s >" ), get_sortname( pane.sortby ).c_str() );
    int max = square.max_size;
    if( max > 0 ) {
        int itemcount = square.get_item_count();
        int fmtw = 7 + ( itemcount > 99 ? 3 : itemcount > 9 ? 2 : 1 ) +
            ( max > 99 ? 3 : max > 9 ? 2 : 1 );
        mvwprintw( w, 0 , ( w_width / 2 ) - fmtw, "< %d/%d >", itemcount, max );
    }

    const char *fprefix = _( "[F]ilter" );
    const char *fsuffix = _( "[R]eset" );
    if( ! filter_edit ) {
        if( !pane.filter.empty() ) {
            mvwprintw( w, getmaxy( w ) - 1, 2, "< %s: %s >", fprefix,
                       pane.filter.c_str() );
        } else {
            mvwprintw( w, getmaxy( w ) - 1, 2, "< %s >", fprefix );
        }
    }
    if( active ) {
        wattroff( w, c_white );
    }
    if( ! filter_edit && !pane.filter.empty() ) {
        mvwprintz( w, getmaxy( w ) - 1, 6 + std::strlen( fprefix ), c_white, "%s",
                   pane.filter.c_str() );
        mvwprintz( w, getmaxy( w ) - 1,
                   getmaxx( w ) - std::strlen( fsuffix ) - 2, c_white, "%s", fsuffix );
    }
    wrefresh( w );
}

// be explicit with the values
enum aim_entry {
    ENTRY_START     = 0,
    ENTRY_VEHICLE   = 1,
    ENTRY_MAP       = 2,
    ENTRY_RESET     = 3
};

bool advanced_inventory::move_all_items(bool nested_call)
{
    auto &spane = panes[src];
    auto &dpane = panes[dest];

    // AIM_ALL source area routine
    if(spane.get_area() == AIM_ALL) {
        // move all to `AIM_WORN' doesn't make sense (see `MAX_WORN_PER_TYPE')
        if(dpane.get_area() == AIM_WORN) {
            popup(_("You look at the items, then your clothes, and scratch your head..."));
            return false;
        }
        // if the source pane (AIM_ALL) is empty, then show a message and leave
        if(!is_processing() && spane.items.empty()) {
            popup(_("There are no items to be moved!"));
            return false;
        }
        // make sure that there are items to be moved
        bool done = false;
        // copy the current pane, to be restored after the move is queued
        auto shadow = panes[src];
        // here we recursively call this function with each area in order to
        // put all items in the proper destination area, with minimal fuss
        auto &loc = uistate.adv_inv_aim_all_location;
        // re-entry nonsense
        auto &entry = uistate.adv_inv_re_enter_move_all;
        // if we are just starting out, set entry to initial value
        switch(static_cast<aim_entry>(entry++)) {
            case ENTRY_START:
                ++entry;
            case ENTRY_VEHICLE:
                if(squares[loc].can_store_in_vehicle()) {
                    // either do the inverse of the pane (if it is the one we are transferring to),
                    // or just transfer the contents (if it is not the one we are transferring to)
                    spane.set_area(squares[loc], (dpane.get_area() == loc) ? !dpane.in_vehicle() : true);
                    // add items, calculate weights and volumes... the fun stuff
                    recalc_pane(src);
                    // then move the items to the destination area
                    move_all_items(true);
                }
                break;
            case ENTRY_MAP:
                spane.set_area(squares[loc++], false);
                recalc_pane(src);
                move_all_items(true);
                break;
            case ENTRY_RESET:
                if(loc > AIM_AROUND_END) {
                    loc = AIM_AROUND_BEGIN;
                    entry = ENTRY_START;
                    done = true;
                } else {
                    entry = ENTRY_VEHICLE;
                }
                break;
            default:
                debugmsg("Invalid `aim_entry' [%d] reached!", entry - 1);
                entry = ENTRY_START;
                loc = AIM_AROUND_BEGIN;
                return false;
        }
        // restore the pane to its former glory
        panes[src] = shadow;
        // make it auto loop back, if not already doing so
        if(!done && g->u.has_activity(ACT_NULL)) {
            do_return_entry();
        }
        return true;
    }

    // Check some preconditions to quickly leave the function.
    if( spane.items.empty() ) {
        return false;
    }
    bool restore_area = false;
    if( dpane.get_area() == AIM_ALL ) {
        auto loc = dpane.get_area();
        // ask where we want to store the item via the menu
        if(!query_destination(loc)) {
            return false;
        }
        restore_area = true;
    }
    if( spane.get_area() == AIM_INVENTORY &&
        !query_yn( _( "Really move everything from your inventory?" ) ) ) {
        return false;
    }
    if( spane.get_area() == AIM_WORN &&
        !query_yn( _( "Really remove all your clothes? (woo woo)" ) ) ) {
        return false;
    }
    auto &sarea = squares[spane.get_area()];
    auto &darea = squares[dpane.get_area()];

    // Make sure source and destination are different, otherwise items will disappear
    // Need to check actual position to account for dragged vehicles
    if( dpane.get_area() == AIM_DRAGGED && sarea.pos == darea.pos && spane.in_vehicle() == dpane.in_vehicle() ) {
        return false;
    } else if ( spane.get_area() == dpane.get_area() && spane.in_vehicle() == dpane.in_vehicle() ) {
        return false;
    }

    if( nested_call || !OPTIONS["CLOSE_ADV_INV"] ) {
        // Why is this here? It's because the activity backlog can act
        // like a stack instead of a single deferred activity in order to
        // accomplish some UI shenanigans. The inventory menu activity is
        // added, then an activity to drop is pushed on the stack, then
        // the drop activity is repeatedly popped and pushed on the stack
        // until all its items are processed. When the drop activity runs out,
        // the inventory menu activity is there waiting and seamlessly returns
        // the player to the menu. If the activity is interrupted instead of
        // completing, both activities are cancelled.
        // Thanks to kevingranade for the explanation.
        do_return_entry();
    }

    if( spane.get_area() == AIM_INVENTORY || spane.get_area() == AIM_WORN ) {
        g->u.assign_activity( ACT_DROP, 0 );
        g->u.activity.placement = darea.off;
        g->u.activity.values.push_back(dpane.in_vehicle());
    }
    if( spane.get_area() == AIM_INVENTORY ) {
        for( size_t index = 0; index < g->u.inv.size(); ++index ) {
            const auto &stack = g->u.inv.const_stack( index );
            if( spane.is_filtered( &( stack.front() ) ) ) {
                continue;
            }
            g->u.activity.values.push_back( index );
            if( stack.front().count_by_charges() ) {
                g->u.activity.values.push_back( stack.front().charges );
            } else {
                g->u.activity.values.push_back( stack.size() );
            }
        }
    } else if( spane.get_area() == AIM_WORN ) {
        // do this in reverse, to account for vector item removal messing with future indices
        auto iter = g->u.worn.rbegin();
        for( size_t idx = 0; idx < g->u.worn.size(); ++idx, ++iter ) {
            size_t index = ( g->u.worn.size() - idx - 1 );
            auto &elem = *iter;
            if( spane.is_filtered( &elem ) ) {
                continue;
            }
            g->u.activity.values.push_back( player::worn_position_to_index( index ) );
            int amount = (elem.count_by_charges() == true) ? elem.charges : 1;
            g->u.activity.values.push_back(amount);
        }
    } else {
        if( dpane.get_area() == AIM_INVENTORY || dpane.get_area() == AIM_WORN ) {
            g->u.assign_activity( ACT_PICKUP, 0 );
            g->u.activity.values.push_back( spane.in_vehicle() );
            if( dpane.get_area() == AIM_WORN ) {
                g->u.activity.str_values.push_back( "equip" );
            }
        } else { // Vehicle and map destinations are handled the same.
            g->u.assign_activity( ACT_MOVE_ITEMS, 0 );
            // store whether the source is from a vehicle (first entry)
            g->u.activity.values.push_back(spane.in_vehicle());
            // store whether the destination is a vehicle
            g->u.activity.values.push_back(dpane.in_vehicle());
            // Stash the destination
            g->u.activity.coords.push_back(darea.off);
        }
        g->u.activity.placement = sarea.off;

        std::list<item>::iterator begin, end;
        if( panes[src].in_vehicle() ) {
            begin = sarea.veh->get_items( sarea.vstor ).begin();
            end = sarea.veh->get_items( sarea.vstor ).end();
        } else {
            begin = g->m.i_at( sarea.pos ).begin();
            end = g->m.i_at( sarea.pos ).end();
        }
        // push back indices and item count[s] for [begin => end)
        int index = 0;
        for(auto item_it = begin; item_it != end; ++item_it, ++index) {
            if(spane.is_filtered(&(*item_it))) {
                continue;
            }
            int amount = (item_it->count_by_charges() == true) ? item_it->charges : 1;
            g->u.activity.values.push_back(index);
            g->u.activity.values.push_back(amount);
        }
    }
    // if dest was AIM_ALL then we used query_destination and should undo that
    if (restore_area) {
        dpane.restore_area();
    }
    return true;
}

bool advanced_inventory::show_sort_menu( advanced_inventory_pane &pane )
{
    uimenu sm;
    sm.return_invalid = true;
    sm.text = _( "Sort by... " );
    sm.addentry( SORTBY_NONE,     true, 'u', _( "Unsorted (recently added first)" ) );
    sm.addentry( SORTBY_NAME,     true, 'n', get_sortname( SORTBY_NAME ) );
    sm.addentry( SORTBY_WEIGHT,   true, 'w', get_sortname( SORTBY_WEIGHT ) );
    sm.addentry( SORTBY_VOLUME,   true, 'v', get_sortname( SORTBY_VOLUME ) );
    sm.addentry( SORTBY_CHARGES,  true, 'x', get_sortname( SORTBY_CHARGES ) );
    sm.addentry( SORTBY_CATEGORY, true, 'c', get_sortname( SORTBY_CATEGORY ) );
    sm.addentry( SORTBY_DAMAGE,   true, 'd', get_sortname( SORTBY_DAMAGE ) );
    // Pre-select current sort.
    sm.selected = pane.sortby - SORTBY_NONE;
    // Calculate key and window variables, generate window,
    // and loop until we get a valid answer.
    sm.query();
    if( sm.ret < SORTBY_NONE ) {
        return false;
    }
    pane.sortby = static_cast<advanced_inv_sortby>( sm.ret );
    return true;
}

void advanced_inventory::display()
{
    init();

    g->u.inv.sort();
    g->u.inv.restack( ( &g->u ) );

    input_context ctxt( "ADVANCED_INVENTORY" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "UP" );
    ctxt.register_action( "DOWN" );
    ctxt.register_action( "LEFT" );
    ctxt.register_action( "RIGHT" );
    ctxt.register_action( "PAGE_DOWN" );
    ctxt.register_action( "PAGE_UP" );
    ctxt.register_action( "TOGGLE_TAB" );
    ctxt.register_action( "TOGGLE_VEH" );
    ctxt.register_action( "FILTER" );
    ctxt.register_action( "RESET_FILTER" );
    ctxt.register_action( "EXAMINE" );
    ctxt.register_action( "SORT" );
    ctxt.register_action( "TOGGLE_AUTO_PICKUP" );
    ctxt.register_action( "MOVE_SINGLE_ITEM" );
    ctxt.register_action( "MOVE_VARIABLE_ITEM" );
    ctxt.register_action( "MOVE_ITEM_STACK" );
    ctxt.register_action( "MOVE_ALL_ITEMS" );
    ctxt.register_action( "CATEGORY_SELECTION" );
    ctxt.register_action( "ITEMS_NW" );
    ctxt.register_action( "ITEMS_N" );
    ctxt.register_action( "ITEMS_NE" );
    ctxt.register_action( "ITEMS_W" );
    ctxt.register_action( "ITEMS_CE" );
    ctxt.register_action( "ITEMS_E" );
    ctxt.register_action( "ITEMS_SW" );
    ctxt.register_action( "ITEMS_S" );
    ctxt.register_action( "ITEMS_SE" );
    ctxt.register_action( "ITEMS_INVENTORY" );
    ctxt.register_action( "ITEMS_WORN" );
    ctxt.register_action( "ITEMS_AROUND" );
    ctxt.register_action( "ITEMS_DRAGGED_CONTAINER" );
    ctxt.register_action( "ITEMS_CONTAINER" );

    exit = false;
    recalc = true;
    redraw = true;

    while( !exit ) {
        if( g->u.moves < 0 ) {
            do_return_entry();
            return;
        }
        dest = ( src == left ? right : left );

        redraw_pane( left );
        redraw_pane( right );

        if( redraw && !is_processing()) {
            werase( head );
            werase( minimap );
            werase( mm_border );
            draw_border( head );
            Messages::display_messages( head, 2, 1, w_width - 1, 4 );
            draw_minimap();
            const std::string msg = _( "< [?] show help >" );
            mvwprintz( head, 0,
                       w_width - ( minimap_width + 2 ) - utf8_width(msg) - 1,
                       c_white, msg.c_str() );
            if( g->u.has_watch() ) {
                const std::string time = calendar::turn.print_time();
                mvwprintz( head, 0, 2, c_white, time.c_str() );
            }
            wrefresh( head );
            refresh_minimap();
        }
        redraw = false;
        recalc = false;
        // source and destination pane
        advanced_inventory_pane &spane = panes[src];
        advanced_inventory_pane &dpane = panes[dest];
        // current item in source pane, might be null
        advanced_inv_listitem *sitem = spane.get_cur_item_ptr();
        aim_location changeSquare;

        const std::string action = (is_processing()) ? "MOVE_ALL_ITEMS" : ctxt.handle_input();
        if( action == "CATEGORY_SELECTION" ) {
            inCategoryMode = !inCategoryMode;
            spane.redraw = true; // We redraw to force the color change of the highlighted line and header text.
        } else if( action == "HELP_KEYBINDINGS" ) {
            redraw = true;
        } else if( get_square( action, changeSquare ) ) {
            if( panes[left].get_area() == changeSquare || panes[right].get_area() == changeSquare ) {
                if(squares[changeSquare].can_store_in_vehicle() && changeSquare != AIM_DRAGGED) {
                    // only deal with spane, as you can't _directly_ change dpane
                    if(squares[changeSquare].can_store_in_vehicle() && dpane.get_area() == changeSquare) {
                        spane.set_area(squares[changeSquare], !dpane.in_vehicle());
                        spane.recalc = true;
                    } else if(spane.get_area() == dpane.get_area()) {
                        // swap the `in_vehicle` element of each pane if "one in, one out"
                        spane.set_area(squares[spane.get_area()], !spane.in_vehicle());
                        dpane.set_area(squares[dpane.get_area()], !dpane.in_vehicle());
                        recalc = true;
                    }
                } else {
                    swap_panes();
                }
                redraw = true;
            // we need to check the original area if we can place items in vehicle storage
            } else if( squares[changeSquare].canputitems( spane.get_cur_item_ptr() ) ) {
                bool in_vehicle_cargo = false;
                if( changeSquare == AIM_CONTAINER ) {
                    squares[changeSquare].set_container( spane.get_cur_item_ptr() );
                } else if( spane.get_area() == AIM_CONTAINER ) {
                    squares[changeSquare].set_container( nullptr );
                    // auto select vehicle if items exist at said square, or both are empty
                } else if( squares[changeSquare].can_store_in_vehicle() && spane.get_area() != changeSquare ) {
                    if(changeSquare == AIM_DRAGGED) {
                        in_vehicle_cargo = true;
                    } else {
                        // check item stacks in vehicle and map at said square
                        auto sq = squares[changeSquare];
                        auto map_stack = g->m.i_at( sq.pos );
                        auto veh_stack = sq.veh->get_items( sq.vstor );
                        // auto switch to vehicle storage if vehicle items are there, or neither are there
                        if( !veh_stack.empty() || ( map_stack.empty() && veh_stack.empty() ) ) {
                            in_vehicle_cargo = true;
                        }
                    }
                }
                spane.set_area(squares[changeSquare], in_vehicle_cargo);
                spane.index = 0;
                spane.recalc = true;
                if( dpane.get_area() == AIM_ALL ) {
                    dpane.recalc = true;
                }
                redraw = true;
            } else {
                popup( _( "You can't put items there" ) );
                redraw = true; // to clear the popup
            }
        } else if( action == "MOVE_SINGLE_ITEM" ||
                   action == "MOVE_VARIABLE_ITEM" ||
                   action == "MOVE_ITEM_STACK" ) {
            if( sitem == nullptr || !sitem->is_item_entry() ) {
                continue;
            }
            aim_location destarea = dpane.get_area();
            aim_location srcarea = sitem->area;
            bool restore_area = (destarea == AIM_ALL);
            if( !query_destination( destarea ) ) {
                continue;
            }
            // AIM_ALL should disable same area check and handle it with proper filtering instead.
            // This is a workaround around the lack of vehicle location info in
            // either aim_location or advanced_inv_listitem.
            if( squares[srcarea].is_same( squares[destarea] ) &&
                    spane.get_area() != AIM_ALL &&
                    spane.in_vehicle() == dpane.in_vehicle() ) {
                popup( _( "Source area is the same as destination (%s)." ), squares[destarea].name.c_str() );
                redraw = true; // popup has messed up the screen
                continue;
            }
            assert( !sitem->items.empty() );
            const bool by_charges = sitem->items.front()->count_by_charges();
            long amount_to_move = 0;
            if( !query_charges( destarea, *sitem, action, amount_to_move ) ) {
                continue;
            }
            // This makes sure that all item references in the advanced_inventory_pane::items vector
            // are recalculated, even when they might not have changed, but they could (e.g. items
            // taken from inventory, but unable to put into the cargo trunk go back into the inventory,
            // but are potentially at a different place).
            recalc = true;
            assert( amount_to_move > 0 );
            if( destarea == AIM_CONTAINER ) {
                if( !move_content( *sitem->items.front(), *squares[destarea].get_container( dpane.in_vehicle() ) ) ) {
                    redraw = true;
                    continue;
                }
            } else if(srcarea == AIM_INVENTORY || srcarea == AIM_WORN) {
                // from inventory: remove all items first, then try to put them
                // onto the map/vehicle, if it fails, put them back into the inventory.
                // If no item has actually been moved, continue.

                // if worn, we need to fix with the worn index number (starts at -2, as -1 is weapon)
                int idx = (srcarea == AIM_INVENTORY) ? sitem->idx : player::worn_position_to_index(sitem->idx);
                if(by_charges) {
                    item moving_item = g->u.reduce_charges(idx, amount_to_move);
                    assert(!moving_item.is_null());
                    int items_left = add_item(destarea, moving_item);
                    // take care of charging back any items as well
                    if(items_left > 0) {
                        add_item(srcarea, moving_item, items_left);
                        continue;
                    }
                } else {
                    std::list<item> moving_items;
                    if(srcarea == AIM_INVENTORY) {
                        moving_items = g->u.inv.reduce_stack(idx, amount_to_move);
                    } else if(srcarea == AIM_WORN) {
                        std::vector<item> mv;
                        g->u.takeoff(idx, false, &mv);
                        std::copy(mv.begin(), mv.end(), std::back_inserter(moving_items));
                    }
                    int items_left = 0, moved = 0;
                    for(auto &elem : moving_items) {
                        assert(!elem.is_null());
                        items_left = add_item(destarea, elem);
                        if(items_left > 0) {
                            // chargeback the items if adding them failed
                            add_item(srcarea, elem, items_left);
                        } else {
                            ++moved;
                        }
                    }
                    if(moved == 0) {
                        continue;
                    }
                }
            // from map/vehicle: add the item to the destination.
            // if that worked, remove it from the source, else continue.
            } else {
                // create a new copy of the old item being manipulated
                item new_item(*sitem->items.front());
                if(by_charges) {
                    // set the new item's charge amount
                    new_item.charges = amount_to_move;
                }
                // add the item, and note any items that might be leftover
                int items_left = add_item(destarea, new_item, (by_charges) ? 1 : amount_to_move);
                // only remove item or charges if the add succeeded
                if(items_left == 0) {
                    if(by_charges) {
                        // `amount_to_move' will be `true' if the item needs to be removed
                        amount_to_move = sitem->items.front()->reduce_charges(amount_to_move);
                    }
                    remove_item(*sitem, amount_to_move);
                // note to the player (and possibly debug) that the item transfer failed somehow
                } else {
                    const char *msg = nullptr;
                    int items_unmoved = amount_to_move - items_left;
                    if(by_charges) {
                        msg = (items_unmoved > 0) ?
                            _("Only moved %d of %d charges.") :
                            _("Moved no charges.");
                    } else {
                        msg = (items_unmoved > 0) ?
                            _("Only moved %d of %d items.") :
                            _("Moved no items.");
                    }
                    assert(msg != nullptr);
                    g->u.add_msg_if_player(msg, amount_to_move - items_left, amount_to_move);
                    // redraw the screen if moving to AIM_WORN, so we can see that it didn't work
                    redraw = (destarea == AIM_WORN);
                }
            }
            // This is only reached when at least one item has been moved.
            g->u.moves -= 100; // In pickup/move functions this depends on item stats
            // Just in case the items have moved from/to the inventory
            g->u.inv.sort();
            g->u.inv.restack( &g->u );
            // if dest was AIM_ALL then we used query_destination and should undo that
            if (restore_area) {
                dpane.restore_area();
            }
        } else if( action == "MOVE_ALL_ITEMS" ) {
            exit = move_all_items();
            recalc = true;
        } else if( action == "SORT" ) {
            if( show_sort_menu( spane ) ) {
                recalc = true;
                uistate.adv_inv_sort[src] = spane.sortby;
            }
            redraw = true;
        } else if( action == "FILTER" ) {
            long key = 0;
            int spos = -1;
            std::string filter = spane.filter;
            filter_edit = true;

            g->draw_item_filter_rules( dpane.window, 12 );

            do {
                mvwprintz( spane.window, getmaxy( spane.window ) - 1, 2, c_cyan, "< " );
                mvwprintz( spane.window, getmaxy( spane.window ) - 1, ( w_width / 2 ) - 3, c_cyan, " >" );
                filter = string_input_win( spane.window, spane.filter, 256, 4,
                                           w_height - 1, ( w_width / 2 ) - 4, false, key, spos, "",
                                           4, getmaxy( spane.window ) - 1 );
                spane.set_filter( filter );
                redraw_pane( src );
            } while( key != '\n' && key != KEY_ESCAPE );
            filter_edit = false;
            spane.redraw = true;
            dpane.redraw = true;
        } else if( action == "RESET_FILTER" ) {
            spane.set_filter( "" );
        } else if( action == "TOGGLE_AUTO_PICKUP" ) {
            if( sitem == nullptr || !sitem->is_item_entry() ) {
                continue;
            }
            if( sitem->autopickup == true ) {
                get_auto_pickup().remove_rule( sitem->items.front()->tname() );
                sitem->autopickup = false;
            } else {
                get_auto_pickup().add_rule( sitem->items.front()->tname() );
                sitem->autopickup = true;
            }
            recalc = true;
        } else if( action == "EXAMINE" ) {
            if( sitem == nullptr || !sitem->is_item_entry() ) {
                continue;
            }
            int ret = 0;
            const int info_width = w_width / 2;
            const int info_startx = colstart + ( src == left ? info_width : 0 );
            if( spane.get_area() == AIM_INVENTORY || spane.get_area() == AIM_WORN ) {
                int idx = ( spane.get_area() == AIM_INVENTORY ) ?
                          sitem->idx : player::worn_position_to_index( sitem->idx );
                // Setup a "return to AIM" activity. If examining the item creates a new activity
                // (e.g. reading, reloading, activating), the new activity will be put on top of
                // "return to AIM". Once the new activity is finished, "return to AIM" comes back
                // (automatically, see player activity handling) and it re-opens the AIM.
                // If examining the item did not create a new activity, we have to remove
                // "return to AIM".
                do_return_entry();
                assert( g->u.has_activity( ACT_ADV_INVENTORY ) );
                ret = g->inventory_item_menu( idx, info_startx, info_width,
                                              src == left ? game::LEFT_OF_INFO : game::RIGHT_OF_INFO );
                if( !g->u.has_activity( ACT_ADV_INVENTORY ) ) {
                    exit = true;
                } else {
                    g->u.cancel_activity();
                }
                // Might have changed a stack (activated an item, repaired an item, etc.)
                if( spane.get_area() == AIM_INVENTORY ) {
                    g->u.inv.restack( &g->u );
                }
                recalc = true;
            } else {
                item &it = *sitem->items.front();
                std::vector<iteminfo> vThisItem, vDummy;
                it.info( true, vThisItem );
                int iDummySelect = 0;
                ret = draw_item_info( info_startx,
                                      info_width, 0, 0, it.tname(), it.type_name(), vThisItem, vDummy, iDummySelect,
                                      false, false, true );
            }
            if( ret == KEY_NPAGE || ret == KEY_DOWN ) {
                spane.scroll_by( +1 );
            } else if( ret == KEY_PPAGE || ret == KEY_UP ) {
                spane.scroll_by( -1 );
            }
            redraw = true; // item info window overwrote the other pane and the header
        } else if( action == "QUIT" ) {
            exit = true;
        } else if( action == "PAGE_DOWN" ) {
            spane.scroll_by( +itemsPerPage );
        } else if( action == "PAGE_UP" ) {
            spane.scroll_by( -itemsPerPage );
        } else if( action == "DOWN" ) {
            if( inCategoryMode ) {
                spane.scroll_category( +1 );
            } else {
                spane.scroll_by( +1 );
            }
        } else if( action == "UP" ) {
            if( inCategoryMode ) {
                spane.scroll_category( -1 );
            } else {
                spane.scroll_by( -1 );
            }
        } else if( action == "LEFT" ) {
            src = left;
            redraw = true;
        } else if( action == "RIGHT" ) {
            src = right;
            redraw = true;
        } else if( action == "TOGGLE_TAB" ) {
            src = dest;
            redraw = true;
        } else if( action == "TOGGLE_VEH" ){
            if( squares[spane.get_area()].can_store_in_vehicle() ) {
                // swap the panes if going vehicle will show the same tile
                if(spane.get_area() == dpane.get_area() && spane.in_vehicle() != dpane.in_vehicle()) {
                    swap_panes();
                // disallow for dragged vehicles
                } else if(spane.get_area() != AIM_DRAGGED) {
                    // Toggle between vehicle and ground
                    spane.set_area(squares[spane.get_area()], !spane.in_vehicle());
                    spane.index = 0;
                    spane.recalc = true;
                    if( dpane.get_area() == AIM_ALL ) {
                        dpane.recalc = true;
                    }
                    // make sure to update the minimap as well!
                    redraw = true;
                }
            } else {
                popup( _("No vehicle there!") );
            }
        }
    }
}

void advanced_inventory_pane::skip_category_headers( int offset )
{
    assert( offset != 0 ); // 0 would make no sense
    assert( static_cast<size_t>( index ) < items.size() ); // valid index is required
    assert( offset == -1 || offset == +1 ); // only those two offsets are allowed
    assert( !items.empty() ); // index would not be valid, and this would be an endless loop
    while( !items[index].is_item_entry() ) {
        mod_index( offset );
    }
}

void advanced_inventory_pane::mod_index( int offset )
{
    assert( offset != 0 ); // 0 would make no sense
    assert( !items.empty() );
    index += offset;
    if( index < 0 ) {
        index = static_cast<int>( items.size() ) - 1;
    } else if( static_cast<size_t>( index ) >= items.size() ) {
        index = 0;
    }
}

void advanced_inventory_pane::scroll_by( int offset )
{
    assert( offset != 0 ); // 0 would make no sense
    if( items.empty() ) {
        return;
    }
    mod_index( offset );
    skip_category_headers( offset > 0 ? +1 : -1 );
    redraw = true;
}

void advanced_inventory_pane::scroll_category( int offset )
{
    assert( offset != 0 ); // 0 would make no sense
    assert( offset == -1 || offset == +1 ); // only those two offsets are allowed
    if( items.empty() ) {
        return;
    }
    assert( get_cur_item_ptr() != nullptr ); // index must already be valid!
    auto cur_cat = items[index].cat;
    if( offset > 0 ) {
        while( items[index].cat == cur_cat ) {
            index++;
            if( static_cast<size_t>( index ) >= items.size() ) {
                index = 0; // wrap to begin, stop there.
                break;
            }
        }
    } else {
        while( items[index].cat == cur_cat ) {
            index--;
            if( index < 0 ) {
                index = static_cast<int>( items.size() ) - 1; // wrap to end, stop there.
                break;
            }
        }
    }
    // Make sure we land on an item entry.
    skip_category_headers( offset > 0 ? +1 : -1 );
    redraw = true;
}

advanced_inv_listitem *advanced_inventory_pane::get_cur_item_ptr()
{
    if( static_cast<size_t>( index ) >= items.size() ) {
        return nullptr;
    }
    return &items[index];
}

void advanced_inventory_pane::set_filter( const std::string &new_filter )
{
    if( filter == new_filter ) {
        return;
    }
    filter = new_filter;
    filtercache.clear();
    recalc = true;
}

bool advanced_inventory::query_destination( aim_location &def )
{
    if( def != AIM_ALL ) {
        if( squares[def].canputitems() ) {
            return true;
        }
        popup( _( "You can't put items there" ) );
        redraw = true; // the popup has messed the screen up.
        return false;
    }

    uimenu menu;
    menu.text = _( "Select destination" );
    menu.pad_left = 9; /* free space for advanced_inventory::menu_square */

    {
        // the direction locations should be contiguous in the enum
        std::vector <aim_location> ordered_locs;
        assert( AIM_NORTHEAST - AIM_SOUTHWEST == 8 );
        for( int i = AIM_SOUTHWEST; i <= AIM_NORTHEAST; i++ ) {
            ordered_locs.push_back( screen_relative_location( static_cast <aim_location>( i ) ) );
        }
        for( std::vector <aim_location>::iterator iter = ordered_locs.begin(); iter != ordered_locs.end();
             ++iter ) {
            auto &s = squares[*iter];
            const int size = s.get_item_count();
            std::string prefix = string_format( "%2d/%d", size, MAX_ITEM_IN_SQUARE );
            if( size >= MAX_ITEM_IN_SQUARE ) {
                prefix += _( " (FULL)" );
            }
            menu.addentry( *iter ,
                           ( s.canputitems() && s.id != panes[src].get_area() ),
                           get_location_key( *iter ),
                           prefix + " " + s.name + " " + ( s.veh != nullptr ? s.veh->name : "" ) );
        }
    }
    // Selected keyed to uimenu.entries, which starts at 0.
    menu.selected = uistate.adv_inv_last_popup_dest - AIM_SOUTHWEST;
    menu.show(); // generate and show window.
    while( menu.ret == UIMENU_INVALID && menu.keypress != 'q' && menu.keypress != KEY_ESCAPE ) {
        // Render a fancy ascii grid at the left of the menu.
        menu_square( &menu );
        menu.query( false ); // query, but don't loop
    }
    redraw = true; // the menu has messed the screen up.
    if( menu.ret >= AIM_SOUTHWEST && menu.ret <= AIM_NORTHEAST ) {
        assert( squares[menu.ret].canputitems() );
        def = static_cast<aim_location>( menu.ret );
        // we have to set the destination pane so that move actions will target it
        // we can use restore_area later to undo this
        panes[dest].set_area( squares[def], true );
        uistate.adv_inv_last_popup_dest = menu.ret;
        return true;
    }
    return false;
}

int advanced_inventory::remove_item( advanced_inv_listitem &sitem, int count )
{
    // quick bail for no count
    if(count <= 0) {
        return 0;
    }

    assert( sitem.area != AIM_ALL );        // should be a specific location instead
    assert( sitem.area != AIM_INVENTORY );  // does not work for inventory
    assert( !sitem.items.empty() );
    bool rc = true;

    while(count > 0) {
        auto &s = squares[sitem.area];
        if( s.id == AIM_CONTAINER ) {
            const auto cont = s.get_container( panes[src].in_vehicle() );
            assert( cont != nullptr );
            assert( !cont->contents.empty() );
            assert( &cont->contents.front() == sitem.items.front() );
            cont->contents.erase( cont->contents.begin() );
        } else if( sitem.area == AIM_WORN ) {
            rc &= g->u.takeoff( sitem.items.front() );
        } else if( sitem.from_vehicle ) {
            rc &= s.veh->remove_item( s.vstor, sitem.items.front() );
        } else {
            g->m.i_rem( s.pos, sitem.items.front() );
        }
        if(rc == false) {
            break;
        }
        sitem.items.erase(sitem.items.begin());
        --count;
    }
    return count;
}

int advanced_inventory::add_item( aim_location destarea, item &new_item, int count )
{
    // quick bail for no count
    if(count <= 0) {
        return 0;
    }

    assert( destarea != AIM_ALL ); // should be a specific location instead
    bool rc = true;

    while(count > 0) {
        if( destarea == AIM_INVENTORY ) {
            g->u.i_add( new_item );
            g->u.moves -= 100;
        } else if( destarea == AIM_WORN ) {
            rc = g->u.wear_item(new_item);
        } else {
            advanced_inv_area &p = squares[destarea];
            if( panes[dest].in_vehicle() ) {
                rc &= p.veh->add_item( p.vstor, new_item );
            } else {
                rc &= !g->m.add_item_or_charges( p.pos, new_item, 0 ).is_null();
            }
        }
        // show a message to why we can't add the item
        if(rc == false) {
            const char *msg = nullptr;
            switch(destarea) {
                case AIM_WORN:
                    msg = _("You can't wear any more of that!");
                    break;
                case AIM_INVENTORY:
                    msg = _("You don't have enough room for that!");
                    break;
                default:
                    msg = _("Destination area is full.  Remove some items first");
                    break;
            }
            assert(msg != nullptr);
            popup(msg);
            break;
        }
        --count;
    }
    return count;
}

bool advanced_inventory::move_content( item &src_container, item &dest_container )
{
    if( !src_container.is_container() ) {
        popup( _( "Source must be container." ) );
        return false;
    }
    if( src_container.is_container_empty() ) {
        popup( _( "Source container is empty." ) );
        return false;
    }

    item &src = src_container.contents[0];

    if( !src.made_of( LIQUID ) ) {
        popup( _( "You can unload only liquids into target container." ) );
        return false;
    }

    if ( !src_container.is_sealable_container() ) {
        long max_charges = dest_container.get_remaining_capacity_for_liquid( src );
        if ( src.charges > max_charges ) {
            popup( _( "You can't partially unload liquids from unsealable container." ) );
            return false;
        }
    }

    std::string err;
    // @todo Allow buckets here, but require them to be on the ground or wielded
    if( !dest_container.fill_with( src, err, false ) ) {
        popup( err.c_str() );
        return false;
    }

    uistate.adv_inv_container_content_type = dest_container.contents[0].typeId();
    if( src.charges <= 0 ) {
        src_container.contents.clear();
    }

    return true;
}

int advanced_inv_area::free_volume( bool in_vehicle ) const
{
    assert( id != AIM_ALL ); // should be a specific location instead
    if( id == AIM_INVENTORY || id == AIM_WORN ) {
        return ( g->u.volume_capacity() - g->u.volume_carried() );
    }
    return (in_vehicle) ? veh->free_volume( vstor ) : g->m.free_volume( pos );
}

bool advanced_inventory::query_charges( aim_location destarea, const advanced_inv_listitem &sitem,
                                        const std::string &action, long &amount )
{
    assert( destarea != AIM_ALL ); // should be a specific location instead
    assert( !sitem.items.empty() ); // valid item is obviously required
    const item &it = *sitem.items.front();
    advanced_inv_area &p = squares[destarea];
    const bool by_charges = it.count_by_charges();
    const int unitvolume = it.precise_unit_volume();
    const int free_volume = 1000 * p.free_volume( panes[dest].in_vehicle() );
    // default to move all, unless if being equipped
    const long input_amount = by_charges ? it.charges :
            (action == "MOVE_SINGLE_ITEM") ? 1 : sitem.stacks;
    assert( input_amount > 0 ); // there has to be something to begin with
    amount = input_amount;

    // Includes moving from/to inventory and around on the map.
    if( it.made_of( LIQUID ) ) {
        popup( _( "You can't pick up a liquid." ) );
        redraw = true;
        return false;
    }
    // Check volume, this should work the same for inventory, map and vehicles, but not for worn
    if( unitvolume > 0 && ( unitvolume * amount ) > free_volume && squares[destarea].id != AIM_WORN ) {
        const long volmax = free_volume / unitvolume;
        if( volmax <= 0 ) {
            popup( _( "Destination area is full.  Remove some items first." ) );
            redraw = true;
            return false;
        }
        amount = std::min( volmax, amount );
    }
    // Map and vehicles have a maximal item count, check that. Inventory does not have this.
    if( destarea != AIM_INVENTORY &&
            destarea != AIM_WORN &&
            destarea != AIM_CONTAINER ) {
        const long cntmax = p.max_size - p.get_item_count();
        if( cntmax <= 0 ) {
            // TODO: items by charges might still be able to be add to an existing stack!
            popup( _( "Destination area has too many items.  Remove some first." ) );
            redraw = true;
            return false;
        }
        // Items by charge count as a single item, regardless of the charges. As long as the
        // destination can hold another item, one can move all charges.
        if( !by_charges ) {
            amount = std::min( cntmax, amount );
        }
    }
    // Inventory has a weight capacity, map and vehicle don't have that
    if( destarea == AIM_INVENTORY  || destarea == AIM_WORN ) {
        const long unitweight = it.weight() * 1000 / ( by_charges ? it.charges : 1 );
        const long max_weight = ( g->u.weight_capacity() * 4 - g->u.weight_carried() ) * 1000;
        if( unitweight > 0 && unitweight * amount > max_weight ) {
            const long weightmax = max_weight / unitweight;
            if( weightmax <= 0 ) {
                popup( _( "This is too heavy!." ) );
                redraw = true;
                return false;
            }
            amount = std::min( weightmax, amount );
        }
    }
    // handle how many of armour type we can equip (max of 2 per type)
    if(destarea == AIM_WORN) {
        const auto &id = sitem.items.front()->typeId();
        // how many slots are available for the item?
        const long slots_available = MAX_WORN_PER_TYPE - g->u.amount_worn(id);
        // base the amount to equip on amount of slots available
        amount = std::min(slots_available, input_amount);
    }
    // Now we have the final amount. Query if requested or limited room left.
    if( action == "MOVE_VARIABLE_ITEM" || amount < input_amount ) {
        const int count = (by_charges) ? it.charges : sitem.stacks;
        const char *msg = nullptr;
        std::string popupmsg;
        if(amount >= input_amount) {
            msg = _("How many do you want to move? [Have %d] (0 to cancel)");
            popupmsg = string_format(msg, count);
        } else {
            msg = _("Destination can only hold %d! Move how many? [Have %d] (0 to cancel)");
            popupmsg = string_format(msg, amount, count);
        }
        // At this point amount contains the maximal amount that the destination can hold.
        const long possible_max = std::min( input_amount, amount );
        if(amount <= 0) {
           popup(_("The destination is already full!"));
        } else {
            amount = std::atoi(string_input_popup(popupmsg, 20, "", "", "", -1, true).c_str());
        }
        if( amount <= 0 ) {
            redraw = true;
            return false;
        }
        if( amount > possible_max ) {
            amount = possible_max;
        }
    }
    return true;
}

bool advanced_inv_area::is_same( const advanced_inv_area &other ) const
{
    // All locations (sans the below) are compared by the coordinates,
    // e.g. dragged vehicle (to the south) and AIM_SOUTH are the same.
    if( id != AIM_INVENTORY && other.id != AIM_INVENTORY &&
        id != AIM_WORN      && other.id != AIM_WORN      &&
        id != AIM_CONTAINER && other.id != AIM_CONTAINER ) {
        //     have a vehicle?...     ...do the cargo index and pos match?...    ...at least pos?
        return (veh == other.veh) ? (pos == other.pos && vstor == other.vstor) : pos == other.pos;
    }
    //      ...is the id?
    return id == other.id;
}

bool advanced_inv_area::canputitems( const advanced_inv_listitem *advitem )
{
    bool canputitems = false;
    bool from_vehicle = false;
    item *it = nullptr;
    switch( id ) {
        case AIM_CONTAINER:
            if( advitem != nullptr && advitem->is_item_entry() ) {
                it = advitem->items.front();
                from_vehicle = advitem->from_vehicle;
            }
            if(get_container(from_vehicle) != nullptr) {
                it = get_container(from_vehicle);
            }
            if( it != nullptr ) {
                canputitems = it->is_watertight_container();
            }
            break;
        default:
            canputitems = canputitemsloc;
            break;
    }
    return canputitems;
}

item *advanced_inv_area::get_container( bool in_vehicle )
{
    item *container = nullptr;

    if( uistate.adv_inv_container_location != -1 ) {
        // try to find valid container in the area
        if( uistate.adv_inv_container_location == AIM_INVENTORY ) {
            const invslice &stacks = g->u.inv.slice();

            // check index first
            if( stacks.size() > ( size_t )uistate.adv_inv_container_index ) {
                auto &it = stacks[uistate.adv_inv_container_index]->front();
                if( is_container_valid( &it ) ) {
                    container = &it;
                }
            }

            // try entire area
            if( container == nullptr ) {
                for( size_t x = 0; x < stacks.size(); ++x ) {
                    auto &it = stacks[x]->front();
                    if( is_container_valid( &it ) ) {
                        container = &it;
                        uistate.adv_inv_container_index = x;
                        break;
                    }
                }
            }
        } else if( uistate.adv_inv_container_location == AIM_WORN ) {
            auto& worn = g->u.worn;
            size_t idx = ( size_t )uistate.adv_inv_container_index;
            if( worn.size() > idx ) {
                auto iter = worn.begin();
                std::advance( iter, idx );
                if( is_container_valid( &*iter ) ) {
                    container = &*iter;
                }
            }

            // no need to reinvent the wheel
            if( container == nullptr ) {
                auto iter = worn.begin();
                for( size_t i = 0; i < worn.size(); ++i, ++iter ) {
                    if( is_container_valid( &*iter ) ) {
                        container = &*iter;
                        uistate.adv_inv_container_index = i;
                        break;
                    }
                }
            }
        } else {
            map &m = g->m;
            bool is_in_vehicle = veh &&
                ( uistate.adv_inv_container_in_vehicle ||
                  (can_store_in_vehicle() && in_vehicle) );

            const itemstack &stacks = (is_in_vehicle) ?
                i_stacked( veh->get_items( vstor ) ) :
                i_stacked( m.i_at( pos ) );

            // check index first
            if( stacks.size() > ( size_t )uistate.adv_inv_container_index ) {
                auto it = stacks[uistate.adv_inv_container_index].front();
                if( is_container_valid( it ) ) {
                    container = it;
                }
            }

            // try entire area
            if( container == nullptr ) {
                for( size_t x = 0; x < stacks.size(); ++x ) {
                    auto it = stacks[x].front();
                    if( is_container_valid( it ) ) {
                        container = it;
                        uistate.adv_inv_container_index = x;
                        break;
                    }
                }
            }
        }

        // no valid container in the area, resetting container
        if( container == nullptr ) {
            set_container( nullptr );
            desc[0] = _( "Invalid container" );
        }
    }

    return container;
}

void advanced_inv_area::set_container( const advanced_inv_listitem *advitem )
{
    if( advitem != nullptr ) {
        item *it( advitem->items.front() );
        uistate.adv_inv_container_location = advitem->area;
        uistate.adv_inv_container_in_vehicle = advitem->from_vehicle;
        uistate.adv_inv_container_index = advitem->idx;
        uistate.adv_inv_container_type = it->typeId();
        uistate.adv_inv_container_content_type = ( !it->is_container_empty() ) ?
            it->contents[0].typeId() : "null";
        set_container_position();
    } else {
        uistate.adv_inv_container_location = -1;
        uistate.adv_inv_container_index = 0;
        uistate.adv_inv_container_in_vehicle = false;
        uistate.adv_inv_container_type = "null";
        uistate.adv_inv_container_content_type = "null";
    }
}

bool advanced_inv_area::is_container_valid( const item *it ) const
{
    if( it != nullptr ) {
        if( it->typeId() == uistate.adv_inv_container_type ) {
            if( it->is_container_empty() ) {
                if( uistate.adv_inv_container_content_type == "null" ) {
                    return true;
                }
            } else {
                if( it->contents[0].typeId() == uistate.adv_inv_container_content_type ) {
                    return true;
                }
            }
        }
    }

    return false;
}

void advanced_inv_area::set_container_position()
{
    // update the offset of the container based on location
    switch( uistate.adv_inv_container_location ) {
        case AIM_DRAGGED:
            off = g->u.grab_point;
            break;
        case AIM_SOUTHWEST:
            off = tripoint( -1, 1, 0 );
            break;
        case AIM_SOUTH:
            off = tripoint( 0, 1, 0 );
            break;
        case AIM_SOUTHEAST:
            off = tripoint( 1, 1, 0 );
            break;
        case AIM_WEST:
            off = tripoint( -1, 0, 0 );
            break;
        case AIM_EAST:
            off = tripoint( 1, 0, 0 );
            break;
        case AIM_NORTHWEST:
            off = tripoint( -1, -1, 0 );
            break;
        case AIM_NORTH:
            off = tripoint( 0, -1, 0 );
            break;
        case AIM_NORTHEAST:
            off = tripoint( 1, -1, 0 );
            break;
        default:
            off = tripoint( 0, 0, 0 );
            break;
    }
    // update the absolute position
    pos = g->u.pos() + off;
    // update vehicle information
    vstor = -1;
    veh = g->m.veh_at( pos, vstor );
    if( veh != nullptr ) {
        vstor = veh->part_with_feature( vstor, "CARGO", false );
    }
    if( vstor < 0 ) {
        veh = nullptr;
    }
}


void advanced_inv()
{
    advanced_inventory advinv;
    advinv.display();
}

void advanced_inventory::refresh_minimap()
{
    // don't update ui if processing demands
    if(is_processing()) {
        return;
    }
    // redraw border around minimap
    draw_border( mm_border );
    // minor addition to border for AIM_ALL, sorta hacky
    if( panes[src].get_area() == AIM_ALL || panes[dest].get_area() == AIM_ALL ) {
        mvwprintz( mm_border, 0, 1, c_ltgray,
                   utf8_truncate( _( "All" ), minimap_width ).c_str() );
    }
    // refresh border, then minimap
    wrefresh( mm_border );
    wrefresh( minimap );
}

void advanced_inventory::draw_minimap()
{
    // if player is in one of the below, invert the player cell
    static const std::array<aim_location, 3> great_music = {
        {AIM_CENTER, AIM_INVENTORY, AIM_WORN}
    };
    static const std::array<side, NUM_PANES> sides = {{left, right}};
    // get the center of the window
    tripoint pc = {getmaxx(minimap) / 2, getmaxy(minimap) / 2, 0};
    // draw the 3x3 tiles centered around player
    g->m.draw( minimap, g->u.pos() );
    for(auto s : sides) {
        char sym = get_minimap_sym(s);
        if(sym == '\0') { continue; }
        auto sq = squares[panes[s].get_area()];
        auto pt = pc + sq.off;
        // invert the color if pointing to the player's position
        auto cl = (sq.id == AIM_INVENTORY || sq.id == AIM_WORN) ?
            invert_color(c_ltcyan) : c_ltcyan | A_BLINK;
        mvwputch(minimap, pt.y, pt.x, static_cast<nc_color>(cl), sym);
    }
    // the below "routine," if you will, determines whether to invert the
    // player's cell if it is in one of the tiles in `great_music' above.

    /* I now present to you, a story of killer moves and even chiller grooves */
    bool is_funky, supah_funky, da_funkiest; // it must be talkin' about this fly guy
    da_funkiest = supah_funky = is_funky = false; // time to krunk the funky dunk!
    auto play_a_tune_that = [this, &is_funky] // there we go, now _that's_ funky!
        (const aim_location &groovy) { // as groovy as this tye-dye?
            // for maximum groovage, and radical coolage!
            return groovy == this->panes[(is_funky = !is_funky)].get_area();
        };
    for(auto /* jefferson */ &airplane : great_music) {
        supah_funky = play_a_tune_that(/* on that */ airplane);
        // listen to the funk in the krunkosphere...
        da_funkiest = play_a_tune_that(/* under that */ airplane);
        // ... and groove to those tunes on the krunkwalk!
    }
    if(!(supah_funky && da_funkiest)) { // and remember the funkiest of them all!
        bool player_is_funky = supah_funky || da_funkiest; // thanks to all the players (and bug-hunters)!
        g->u.draw(minimap, g->u.pos(), player_is_funky); // and thanks for reading fellow coder! :-)
        // hope you enjoyed the far out experience, man!    -davek
    }
}

char advanced_inventory::get_minimap_sym(side p) const
{
    static const std::array<char, NUM_PANES> c_side = {{'L', 'R'}};
    static const std::array<char, NUM_PANES> d_side = {{'^', 'v'}};
    static const std::array<char, NUM_AIM_LOCATIONS> g_nome = {{
        '@', '#', '#', '#', '#', '@', '#',
        '#', '#', '#', 'D', '^', 'C', '@'
    }};
    char ch = g_nome[panes[p].get_area()];
    switch(ch) {
        case '@': // '^' or 'v'
            ch = d_side[panes[-p+1].get_area() == AIM_CENTER];
            break;
        case '#': // 'L' or 'R'
            ch = (panes[p].in_vehicle()) ? 'V' : c_side[p];
            break;
        case '^': // do not show anything
            ch ^= ch;
            break;
    }
    return ch;
}

aim_location advanced_inv_area::offset_to_location() const
{
    static aim_location loc_array[3][3] = {
        {AIM_NORTHWEST,     AIM_NORTH,      AIM_NORTHEAST},
        {AIM_WEST,          AIM_CENTER,     AIM_EAST},
        {AIM_SOUTHWEST,     AIM_SOUTH,      AIM_SOUTHEAST}
    };
    return loc_array[off.y + 1][off.x + 1];
}

void advanced_inventory::swap_panes()
{
    // Switch left and right pane.
    std::swap( panes[left], panes[right] );
    // Window pointer must be unchanged!
    std::swap( panes[left].window, panes[right].window );
    // No recalculation needed, data has not changed
    redraw = true;
}

void advanced_inventory::do_return_entry()
{
    // only save pane settings
    save_settings( true );
    g->u.assign_activity( ACT_ADV_INVENTORY, -1 );
    g->u.activity.auto_resume = true;
    uistate.adv_inv_exit_code = exit_re_entry;
}

bool advanced_inventory::is_processing() const
{
    return ( uistate.adv_inv_re_enter_move_all != ENTRY_START );
}

aim_location advanced_inventory::screen_relative_location( aim_location area )
{

    if( !( tile_iso && use_tiles ) ) {
        return area;
    }
    switch( area ) {

        case AIM_SOUTHWEST:
            return AIM_WEST;

        case AIM_SOUTH:
            return AIM_SOUTHWEST;

        case AIM_SOUTHEAST:
            return AIM_SOUTH;

        case AIM_WEST:
            return AIM_NORTHWEST;

        case AIM_EAST:
            return AIM_SOUTHEAST;

        case AIM_NORTHWEST:
            return AIM_NORTH;

        case AIM_NORTH:
            return AIM_NORTHEAST;

        case AIM_NORTHEAST:
            return AIM_EAST;

        default :
            return area;
    }
}
