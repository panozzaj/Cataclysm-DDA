#include "mutation.h"

#include "catacharset.h"
#include "debug.h"
#include "game.h"
#include "input.h"
#include "output.h"
#include "player.h"
#include "translations.h"

#include <algorithm> //std::min
#include <sstream>

// '!' and '=' are uses as default bindings in the menu
const invlet_wrapper
mutation_chars( "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ\"#&()*+./:;@[\\]^_{|}" );

void draw_exam_window( WINDOW *win, const int border_y )
{
    int width = getmaxx( win );
    mvwputch( win, border_y, 0, BORDER_COLOR, LINE_XXXO );
    whline( win, LINE_OXOX, width - 2 );
    mvwputch( win, border_y, width - 1, BORDER_COLOR, LINE_XOXX );
}

void show_mutations_titlebar( WINDOW *window, std::string &menu_mode )
{
    werase( window );

    std::string caption = _( "MUTATIONS -" );
    int cap_offset = utf8_width( caption ) + 1;
    mvwprintz( window, 0,  0, c_blue, "%s", caption.c_str() );

    std::string desc;
    int desc_length = getmaxx( window ) - cap_offset;

    if( menu_mode == "reassigning" ) {
        desc = _( "Reassigning.\nSelect a mutation to reassign or press SPACE to cancel." );
    } else if( menu_mode == "activating" ) {
        desc = _( "<color_green>Activating</color>  <color_yellow>!</color> to examine, <color_yellow>=</color> to reassign." );
    } else if( menu_mode == "examining" ) {
        desc = _( "<color_ltblue>Examining</color>  <color_yellow>!</color> to activate, <color_yellow>=</color> to reassign." );
    }
    fold_and_print( window, 0, cap_offset, desc_length, c_white, desc );
    fold_and_print( window, 1, 0, desc_length, c_white,
                    _( "Might need to use ? to assign the keys." ) );

    wrefresh( window );
}

void player::power_mutations()
{
    if( !is_player() ) {
        // TODO: Implement NPCs activating muts
        return;
    }

    std::vector <std::string> passive;
    std::vector <std::string> active;
    for( auto &mut : my_mutations ) {
        if( !mutation_branch::get( mut.first ).activated ) {
            passive.push_back( mut.first );
        } else {
            active.push_back( mut.first );
        }
        // New mutations are initialized with no key at all, so we have to do this here.
        if( mut.second.key == ' ' ) {
            for( const auto &letter : mutation_chars ) {
                if( trait_by_invlet( letter ).empty() ) {
                    mut.second.key = letter;
                    break;
                }
            }
        }
    }

    // maximal number of rows in both columns
    const int mutations_count = std::max( passive.size(), active.size() );

    int TITLE_HEIGHT = 2;
    int DESCRIPTION_HEIGHT = 5;

    // Main window
    /** Total required height is:
    * top frame line:                                         + 1
    * height of title window:                                 + TITLE_HEIGHT
    * line after the title:                                   + 1
    * line with active/passive mutation captions:               + 1
    * height of the biggest list of active/passive mutations:   + mutations_count
    * line before mutation description:                         + 1
    * height of description window:                           + DESCRIPTION_HEIGHT
    * bottom frame line:                                      + 1
    * TOTAL: TITLE_HEIGHT + mutations_count + DESCRIPTION_HEIGHT + 5
    */
    int HEIGHT = std::min( TERMY, std::max( FULL_SCREEN_HEIGHT,
                                            TITLE_HEIGHT + mutations_count + DESCRIPTION_HEIGHT + 5 ) );
    int WIDTH = FULL_SCREEN_WIDTH + ( TERMX - FULL_SCREEN_WIDTH ) / 2;
    int START_X = ( TERMX - WIDTH ) / 2;
    int START_Y = ( TERMY - HEIGHT ) / 2;
    WINDOW *wBio = newwin( HEIGHT, WIDTH, START_Y, START_X );

    // Description window @ the bottom of the bio window
    int DESCRIPTION_START_Y = START_Y + HEIGHT - DESCRIPTION_HEIGHT - 1;
    int DESCRIPTION_LINE_Y = DESCRIPTION_START_Y - START_Y - 1;
    WINDOW *w_description = newwin( DESCRIPTION_HEIGHT, WIDTH - 2,
                                    DESCRIPTION_START_Y, START_X + 1 );

    // Title window
    int TITLE_START_Y = START_Y + 1;
    int HEADER_LINE_Y = TITLE_HEIGHT + 1; // + lines with text in titlebar, local
    WINDOW *w_title = newwin( TITLE_HEIGHT, WIDTH - 2, TITLE_START_Y, START_X + 1 );

    int scroll_position = 0;
    int second_column = 32 + ( TERMX - FULL_SCREEN_WIDTH ) /
                        4; // X-coordinate of the list of active mutations

    input_context ctxt( "MUTATIONS" );
    ctxt.register_updown();
    ctxt.register_action( "ANY_INPUT" );
    ctxt.register_action( "TOGGLE_EXAMINE" );
    ctxt.register_action( "REASSIGN" );
    ctxt.register_action( "HELP_KEYBINDINGS" );
    bool redraw = true;
    std::string menu_mode = "activating";

    while( true ) {
        // offset for display: mutation with index i is drawn at y=list_start_y+i
        // drawing the mutation starts with mutation[scroll_position]
        const int list_start_y = HEADER_LINE_Y + 2 - scroll_position;
        int max_scroll_position = HEADER_LINE_Y + 2 + mutations_count -
                                  ( ( menu_mode == "examining" ) ? DESCRIPTION_LINE_Y : ( HEIGHT - 1 ) );
        if( redraw ) {
            redraw = false;

            werase( wBio );
            draw_border( wBio );
            // Draw line under title
            mvwhline( wBio, HEADER_LINE_Y, 1, LINE_OXOX, WIDTH - 2 );
            // Draw symbols to connect additional lines to border
            mvwputch( wBio, HEADER_LINE_Y, 0, BORDER_COLOR, LINE_XXXO ); // |-
            mvwputch( wBio, HEADER_LINE_Y, WIDTH - 1, BORDER_COLOR, LINE_XOXX ); // -|

            // Captions
            mvwprintz( wBio, HEADER_LINE_Y + 1, 2, c_ltblue, _( "Passive:" ) );
            mvwprintz( wBio, HEADER_LINE_Y + 1, second_column, c_ltblue, _( "Active:" ) );

            if( menu_mode == "examining" ) {
                draw_exam_window( wBio, DESCRIPTION_LINE_Y );
            }
            nc_color type;
            if( passive.empty() ) {
                mvwprintz( wBio, list_start_y, 2, c_ltgray, _( "None" ) );
            } else {
                for( size_t i = scroll_position; i < passive.size(); i++ ) {
                    const auto &md = mutation_branch::get( passive[i] );
                    const auto &td = my_mutations[passive[i]];
                    if( list_start_y + static_cast<int>( i ) ==
                        ( menu_mode == "examining" ? DESCRIPTION_LINE_Y : HEIGHT - 1 ) ) {
                        break;
                    }
                    type = c_cyan;
                    mvwprintz( wBio, list_start_y + i, 2, type, "%c %s", td.key, md.name.c_str() );
                }
            }

            if( active.empty() ) {
                mvwprintz( wBio, list_start_y, second_column, c_ltgray, _( "None" ) );
            } else {
                for( size_t i = scroll_position; i < active.size(); i++ ) {
                    const auto &md = mutation_branch::get( active[i] );
                    const auto &td = my_mutations[active[i]];
                    if( list_start_y + static_cast<int>( i ) ==
                        ( menu_mode == "examining" ? DESCRIPTION_LINE_Y : HEIGHT - 1 ) ) {
                        break;
                    }
                    if( !td.powered ) {
                        type = c_red;
                    } else if( td.powered ) {
                        type = c_ltgreen;
                    } else {
                        type = c_ltred;
                    }
                    // TODO: track resource(s) used and specify
                    mvwputch( wBio, list_start_y + i, second_column, type, td.key );
                    std::stringstream mut_desc;
                    mut_desc << md.name;
                    if( md.cost > 0 && md.cooldown > 0 ) {
                        mut_desc << string_format( _( " - %d RU / %d turns" ),
                                                   md.cost, md.cooldown );
                    } else if( md.cost > 0 ) {
                        mut_desc << string_format( _( " - %d RU" ), md.cost );
                    } else if( md.cooldown > 0 ) {
                        mut_desc << string_format( _( " - %d turns" ), md.cooldown );
                    }
                    if( td.powered ) {
                        mut_desc << _( " - Active" );
                    }
                    mvwprintz( wBio, list_start_y + i, second_column + 2, type,
                               mut_desc.str().c_str() );
                }
            }

            // Scrollbar
            if( scroll_position > 0 ) {
                mvwputch( wBio, HEADER_LINE_Y + 2, 0, c_ltgreen, '^' );
            }
            if( scroll_position < max_scroll_position && max_scroll_position > 0 ) {
                mvwputch( wBio, ( menu_mode == "examining" ? DESCRIPTION_LINE_Y : HEIGHT - 1 ) - 1,
                          0, c_ltgreen, 'v' );
            }
        }
        wrefresh( wBio );
        show_mutations_titlebar( w_title, menu_mode );
        const std::string action = ctxt.handle_input();
        const long ch = ctxt.get_raw_input().get_first_input();
        if( menu_mode == "reassigning" ) {
            menu_mode = "activating";
            const auto mut_id = trait_by_invlet( ch );
            if( mut_id.empty() ) {
                // Selected an non-existing mutation (or escape, or ...)
                continue;
            }
            redraw = true;
            const long newch = popup_getkey( _( "%s; enter new letter." ),
                                             mutation_branch::get_name( mut_id ).c_str() );
            wrefresh( wBio );
            if( newch == ch || newch == ' ' || newch == KEY_ESCAPE ) {
                continue;
            }
            if( !mutation_chars.valid( newch ) ) {
                popup( _( "Invalid mutation letter. Only those characters are valid:\n\n%s" ),
                       mutation_chars.get_allowed_chars().c_str() );
                continue;
            }
            const auto other_mut_id = trait_by_invlet( newch );
            if( !other_mut_id.empty() ) {
                std::swap( my_mutations[mut_id].key, my_mutations[other_mut_id].key );
            } else {
                my_mutations[mut_id].key = newch;
            }
            // TODO: show a message like when reassigning a key to an item?
        } else if( action == "DOWN" ) {
            if( scroll_position < max_scroll_position ) {
                scroll_position++;
                redraw = true;
            }
        } else if( action == "UP" ) {
            if( scroll_position > 0 ) {
                scroll_position--;
                redraw = true;
            }
        } else if( action == "REASSIGN" ) {
            menu_mode = "reassigning";
        } else if( action == "TOGGLE_EXAMINE" ) { // switches between activation and examination
            menu_mode = menu_mode == "activating" ? "examining" : "activating";
            werase( w_description );
            redraw = true;
        } else if( action == "HELP_KEYBINDINGS" ) {
            redraw = true;
        } else {
            const auto mut_id = trait_by_invlet( ch );
            if( mut_id.empty() ) {
                // entered a key that is not mapped to any mutation,
                // -> leave screen
                break;
            }
            const auto &mut_data = mutation_branch::get( mut_id );
            if( menu_mode == "activating" ) {
                if( mut_data.activated ) {
                    if( my_mutations[mut_id].powered ) {
                        add_msg_if_player( m_neutral, _( "You stop using your %s." ), mut_data.name.c_str() );

                        deactivate_mutation( mut_id );
                        delwin( w_title );
                        delwin( w_description );
                        delwin( wBio );
                        // Action done, leave screen
                        break;
                    } else if( ( !mut_data.hunger || get_hunger() <= 400 ) &&
                               ( !mut_data.thirst || get_thirst() <= 400 ) &&
                               ( !mut_data.fatigue || get_fatigue() <= 400 ) ) {

                        // this will clear the mutations menu for targeting purposes
                        werase( wBio );
                        wrefresh( wBio );
                        delwin( w_title );
                        delwin( w_description );
                        delwin( wBio );
                        g->draw();
                        add_msg_if_player( m_neutral, _( "You activate your %s." ), mut_data.name.c_str() );
                        activate_mutation( mut_id );
                        // Action done, leave screen
                        break;
                    } else {
                        popup( _( "You don't have enough in you to activate your %s!" ), mut_data.name.c_str() );
                        redraw = true;
                        continue;
                    }
                } else {
                    popup( _( "\
You cannot activate %s!  To read a description of \
%s, press '!', then '%c'." ), mut_data.name.c_str(), mut_data.name.c_str(),
                           my_mutations[mut_id].key );
                    redraw = true;
                }
            }
            if( menu_mode == "examining" ) { // Describing mutations, not activating them!
                draw_exam_window( wBio, DESCRIPTION_LINE_Y );
                // Clear the lines first
                werase( w_description );
                fold_and_print( w_description, 0, 0, WIDTH - 2, c_ltblue, mut_data.description );
                wrefresh( w_description );
            }
        }
    }
    //if we activated a mutation, already killed the windows
    if( !( menu_mode == "activating" ) ) {
        werase( wBio );
        wrefresh( wBio );
        delwin( w_title );
        delwin( w_description );
        delwin( wBio );
    }
}
