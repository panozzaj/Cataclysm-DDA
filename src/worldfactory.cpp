#include "worldfactory.h"
#include "filesystem.h"
#include "char_validity_check.h"
#include "mod_manager.h"
#include "path_info.h"
#include "debug.h"
#include "mapsharing.h"
#include "gamemode.h"
#include "translations.h"
#include "input.h"
#include "cursesdef.h"
#include "catacharset.h"
#include "calendar.h"
#include "name.h"
#include "json.h"

#include <fstream>

#define SAVE_MASTER "master.gsav"
#define SAVE_EXTENSION ".sav"

// single instance of world generator
worldfactory *world_generator;

std::string get_next_valid_worldname()
{
    std::string worldname = Name::get(nameIsWorldName);

    return worldname;
}

WORLD::WORLD()
{
    world_name = get_next_valid_worldname();
    std::stringstream path;
    path << FILENAMES["savedir"] << world_name;
    world_path = path.str();
    WORLD_OPTIONS.clear();

    for( auto &elem : OPTIONS ) {
        if( elem.second.getPage() == "world_default" ) {
            WORLD_OPTIONS[elem.first] = elem.second;
        }
    }

    world_saves.clear();
    active_mod_order = world_generator->get_mod_manager()->get_default_mods();
}

bool WORLD::save_exists( const std::string &name ) const
{
    return std::find( world_saves.begin(), world_saves.end(), name ) != world_saves.end();
}

void WORLD::add_save( const std::string &name )
{
    if ( !save_exists( name ) ) {
        world_saves.push_back( name );
    }
}

worldfactory::worldfactory()
: active_world( nullptr )
, all_worlds()
, all_worldnames()
, mman( nullptr )
, mman_ui( nullptr )
{
    mman.reset( new mod_manager );
    mman->refresh_mod_list();
    mman_ui.reset( new mod_ui( mman.get() ) );

    // prepare tab display order
    tabs.push_back(&worldfactory::show_worldgen_tab_modselection);
    tabs.push_back(&worldfactory::show_worldgen_tab_options);
    tabs.push_back(&worldfactory::show_worldgen_tab_confirm);

    tab_strings.push_back(_("Mods to use"));
    tab_strings.push_back(_("World Gen Options"));
    tab_strings.push_back(_("CONFIRMATION"));
}

worldfactory::~worldfactory()
{
    for( auto &wp : all_worlds ) {
        delete wp.second;
    }
}

WORLDPTR worldfactory::make_new_world( bool show_prompt )
{
    // World to return after generating
    WORLDPTR retworld = new WORLD();
    if( show_prompt ) {
        // Window variables
        const int iOffsetX = (TERMX > FULL_SCREEN_WIDTH) ? (TERMX - FULL_SCREEN_WIDTH) / 2 : 0;
        const int iOffsetY = (TERMY > FULL_SCREEN_HEIGHT) ? (TERMY - FULL_SCREEN_HEIGHT) / 2 : 0;
        // set up window
        WINDOW *wf_win = newwin(FULL_SCREEN_HEIGHT, FULL_SCREEN_WIDTH, iOffsetY, iOffsetX);
        WINDOW_PTR wf_winptr( wf_win );

        int curtab = 0;
        int lasttab; // give placement memory to menus, sorta.
        const int numtabs = tabs.size();
        while (curtab >= 0 && curtab < numtabs) {
            lasttab = curtab;
            draw_worldgen_tabs(wf_win, curtab);
            curtab += (world_generator->*tabs[curtab])(wf_win, retworld);

            if (curtab < 0) {
                if (!query_yn(_("Do you want to abort World Generation?"))) {
                    curtab = lasttab;
                }
            }
        }
        if (curtab < 0) {
            delete retworld;
            return NULL;
        }
    } else { // 'Play NOW'
#ifndef LUA
        // Silently remove all Lua mods setted by default.
        std::vector<std::string>::iterator mod_it;
        for (mod_it = retworld->active_mod_order.begin(); mod_it != retworld->active_mod_order.end();) {
            MOD_INFORMATION *minfo = mman->mod_map[*mod_it];
            if ( minfo->need_lua ) {
                mod_it = retworld->active_mod_order.erase(mod_it);
            } else {
                mod_it++;
            }
        }
#endif
    }

    // add world to world list
    all_worlds[retworld->world_name] = retworld;
    all_worldnames.push_back(retworld->world_name);

    std::stringstream path;
    path << FILENAMES["savedir"] << retworld->world_name;
    retworld->world_path = path.str();
    //debugmsg("worldpath: %s", path.str().c_str());

    if (!save_world(retworld)) {
        std::string worldname = retworld->world_name;
        std::vector<std::string>::iterator it = std::find(all_worldnames.begin(), all_worldnames.end(),
                                                worldname);
        all_worldnames.erase(it);
        if (all_worlds[worldname] != retworld) {
            delete retworld;
        }
        delete all_worlds[worldname];
        all_worlds.erase(worldname);
        return NULL;
    }
    return retworld;
}

WORLDPTR worldfactory::make_new_world(special_game_id special_type)
{
    std::string worldname;
    switch(special_type) {
    case SGAME_TUTORIAL:
        worldname = "TUTORIAL";
        break;
    case SGAME_DEFENSE:
        worldname = "DEFENSE";
        break;
    default:
        return NULL;
    }

    // Look through all worlds and see if a world named worldname already exists. If so, then just return it instead of
    // making a new world.
    if (all_worlds.find(worldname) != all_worlds.end()) {
        return all_worlds[worldname];
    }

    WORLDPTR special_world = new WORLD();
    special_world->world_name = worldname;

    special_world->WORLD_OPTIONS["DELETE_WORLD"].setValue("yes");

    // add world to world list!
    all_worlds[worldname] = special_world;
    all_worldnames.push_back(worldname);

    std::stringstream path;
    path << FILENAMES["savedir"] << worldname;
    special_world->world_path = path.str();

    if (!save_world(special_world)) {
        std::vector<std::string>::iterator it = std::find(all_worldnames.begin(), all_worldnames.end(),
                                                worldname);
        all_worldnames.erase(it);
        delete all_worlds[worldname];
        delete special_world;
        all_worlds.erase(worldname);
        return NULL;
    }

    return special_world;
}

WORLDPTR worldfactory::convert_to_world(std::string origin_path)
{
    // prompt for worldname? Nah, just make a worldname... the user can fix it later if they really don't want this as a name...
    std::string worldname = get_next_valid_worldname();

    // check and loop on validity

    // create world informations
    WORLDPTR newworld = new WORLD();
    newworld->world_name = worldname;

    std::stringstream path;
    path << FILENAMES["savedir"] << worldname;
    newworld->world_path = path.str();

    // save world as conversion world
    if (save_world(newworld, true)) {
        // move files from origin_path into new world path
        for( auto &origin_file : get_files_from_path(".", origin_path, false) ) {
            std::string filename = origin_file.substr( origin_file.find_last_of( "/\\" ) );

            rename( origin_file.c_str(), std::string( newworld->world_path + filename ).c_str() );
        }

        DebugLog( D_INFO, DC_ALL ) << "worldfactory::convert_to_world -- World Converted Successfully!";
        return newworld;
    } else {
        // something horribly wrong happened
        DebugLog( D_ERROR, DC_ALL ) << "worldfactory::convert_to_world -- World Conversion Failed!";
        return NULL;
    }
}

void worldfactory::set_active_world(WORLDPTR world)
{
    world_generator->active_world = world;
    if (world) {
        ACTIVE_WORLD_OPTIONS = world->WORLD_OPTIONS;
        calendar::set_season_length( ACTIVE_WORLD_OPTIONS["SEASON_LENGTH"] );
    } else {
        ACTIVE_WORLD_OPTIONS.clear();
    }
}

bool worldfactory::save_world(WORLDPTR world, bool is_conversion)
{
    // if world is NULL then change it to the active_world
    if (!world) {
        world = active_world;
    }
    // if the active_world is NULL then return w/o saving
    if (!world) {
        return false;
    }

    std::ofstream fout;
    const auto savefile = world->world_path + "/" + FILENAMES["worldoptions"];

    if (!assure_dir_exist(world->world_path)) {
        DebugLog( D_ERROR, DC_ALL ) << "Unable to create or open world[" << world->world_name <<
                                    "] directory for saving";
        return false;
    }

    if (!is_conversion) {
        fout.exceptions(std::ios::badbit | std::ios::failbit);

        fout.open(savefile.c_str());

        if (!fout.is_open()) {
            popup( _( "Could not open the world file %s, check file permissions." ), savefile.c_str() );
            return false;
        }

        JsonOut jout( fout, true );

        jout.start_array();

        for( auto &elem : world->WORLD_OPTIONS ) {
            if( elem.second.getDefaultText() != "" ) {
                jout.start_object();

                jout.member( "info", elem.second.getTooltip() );
                jout.member( "default", elem.second.getDefaultText( false ) );
                jout.member( "name", elem.first );
                jout.member( "value", elem.second.getValue() );

                jout.end_object();
            }
        }

        jout.end_array();

        fout.close();
    }

    mman->save_mods_list(world);
    return true;
}

std::map<std::string, WORLDPTR> worldfactory::get_all_worlds()
{
    std::map<std::string, WORLDPTR> retworlds;

    std::vector<std::string> qualifiers;
    qualifiers.push_back(FILENAMES["worldoptions"]);
    qualifiers.push_back(FILENAMES["legacy_worldoptions"]);
    qualifiers.push_back(SAVE_MASTER);

    if (!all_worlds.empty()) {
        for( auto &elem : all_worlds ) {
            delete elem.second;
        }
        all_worlds.clear();
        all_worldnames.clear();
    }
    // get the master files. These determine the validity of a world
    // worlds exist by having an option file
    // create worlds
    for( const auto &world_dir : get_directories_with(qualifiers, FILENAMES["savedir"], true) ) {
        // get the save files
        auto world_sav_files = get_files_from_path( SAVE_EXTENSION, world_dir, false );
        // split the save file names between the directory and the extension
        for( auto &world_sav_file : world_sav_files ) {
            size_t save_index = world_sav_file.find( SAVE_EXTENSION );
            world_sav_file = world_sav_file.substr( world_dir.size() + 1,
                                                    save_index - ( world_dir.size() + 1 ) );
        }
        // the directory name is the name of the world
        std::string worldname;
        unsigned name_index = world_dir.find_last_of( "/\\" );
        worldname = world_dir.substr( name_index + 1 );

        // create and store the world
        retworlds[worldname] = new WORLD();
        // give the world a name
        retworlds[worldname]->world_name = worldname;
        all_worldnames.push_back(worldname);
        // add sav files
        for( auto &world_sav_file : world_sav_files ) {
            retworlds[worldname]->world_saves.push_back( world_sav_file );
        }
        // set world path
        retworlds[worldname]->world_path = world_dir;
        mman->load_mods_list(retworlds[worldname]);

        // load options into the world
        if ( !load_world_options(retworlds[worldname]) ) {
            for( auto &elem : OPTIONS ) {
                if( elem.second.getPage() == "world_default" ) {
                    retworlds[worldname]->WORLD_OPTIONS[elem.first] = elem.second;
                }
            }
            retworlds[worldname]->WORLD_OPTIONS["DELETE_WORLD"].setValue("yes");
            save_world(retworlds[worldname]);
        }
    }

    // check to see if there exists a worldname "save" which denotes that a world exists in the save
    // directory and not in a sub-world directory
    if (retworlds.find("save") != retworlds.end()) {
        WORLDPTR converted_world = convert_to_world(retworlds["save"]->world_path);
        if (converted_world) {
            converted_world->world_saves = retworlds["save"]->world_saves;
            converted_world->WORLD_OPTIONS = retworlds["save"]->WORLD_OPTIONS;

            std::vector<std::string>::iterator oldindex = std::find(all_worldnames.begin(),
                    all_worldnames.end(), "save");

            delete retworlds["save"];
            retworlds.erase("save");
            all_worldnames.erase(oldindex);

            retworlds[converted_world->world_name] = converted_world;
            all_worldnames.push_back(converted_world->world_name);
        }
    }
    all_worlds = retworlds;
    return retworlds;
}

WORLDPTR worldfactory::pick_world( bool show_prompt )
{
    std::map<std::string, WORLDPTR> worlds = get_all_worlds();
    std::vector<std::string> world_names = all_worldnames;

    // Filter out special worlds (TUTORIAL | DEFENSE) from world_names.
    for (std::vector<std::string>::iterator it = world_names.begin(); it != world_names.end();) {
        if (*it == "TUTORIAL" || *it == "DEFENSE") {
            it = world_names.erase(it);
        } else if (world_need_lua_build(*it)) {
            it = world_names.erase(it);
        } else {
            ++it;
        }
    }
    // If there is only one world to pick from, autoreturn it.
    if (world_names.size() == 1) {
        return worlds[world_names[0]];
    }
    // If there are no worlds to pick from, immediately try to make one.
    else if (world_names.empty()) {
        return make_new_world( show_prompt );
    }
    // If we're skipping prompts, just return the first one.
    else if( !show_prompt ) {
        return worlds[world_names[0]];
    }

    const int iTooltipHeight = 3;
    const int iContentHeight = FULL_SCREEN_HEIGHT - 3 - iTooltipHeight;
    const unsigned int num_pages = world_names.size() / iContentHeight + 1; // at least 1 page
    const int iOffsetX = (TERMX > FULL_SCREEN_WIDTH) ? (TERMX - FULL_SCREEN_WIDTH) / 2 : 0;
    const int iOffsetY = (TERMY > FULL_SCREEN_HEIGHT) ? (TERMY - FULL_SCREEN_HEIGHT) / 2 : 0;

    std::map<int, bool> mapLines;
    mapLines[3] = true;

    std::map<int, std::vector<std::string> > world_pages;
    unsigned int worldnum = 0;
    for (size_t i = 0; i < num_pages; ++i) {
        for (int j = 0; j < iContentHeight && worldnum < world_names.size(); ++j) {
            world_pages[i].push_back(world_names[worldnum++]);
        }
    }
    unsigned int sel = 0, selpage = 0;

    WINDOW *w_worlds_border = newwin(FULL_SCREEN_HEIGHT, FULL_SCREEN_WIDTH, iOffsetY, iOffsetX);
    WINDOW *w_worlds_tooltip = newwin(iTooltipHeight, FULL_SCREEN_WIDTH - 2, 1 + iOffsetY,
                                      1 + iOffsetX);
    WINDOW *w_worlds_header = newwin(1, FULL_SCREEN_WIDTH - 2, 1 + iTooltipHeight + iOffsetY,
                                     1 + iOffsetX);
    WINDOW *w_worlds        = newwin(iContentHeight, FULL_SCREEN_WIDTH - 2,
                                     iTooltipHeight + 2 + iOffsetY, 1 + iOffsetX);

    draw_border( w_worlds_border, BORDER_COLOR, _( " WORLD SELECTION " ) );
    mvwputch(w_worlds_border, 4, 0, BORDER_COLOR, LINE_XXXO); // |-
    mvwputch(w_worlds_border, 4, FULL_SCREEN_WIDTH - 1, BORDER_COLOR, LINE_XOXX); // -|

    for( auto &mapLine : mapLines ) {
        mvwputch( w_worlds_border, FULL_SCREEN_HEIGHT - 1, mapLine.first + 1, BORDER_COLOR,
                  LINE_XXOX ); // _|_
    }

    wrefresh(w_worlds_border);

    for (int i = 0; i < 78; i++) {
        if (mapLines[i]) {
            mvwputch(w_worlds_header, 0, i, BORDER_COLOR, LINE_OXXX);
        } else {
            mvwputch(w_worlds_header, 0, i, BORDER_COLOR, LINE_OXOX); // Draw header line
        }
    }

    wrefresh(w_worlds_header);

    input_context ctxt("PICK_WORLD_DIALOG");
    ctxt.register_updown();
    ctxt.register_action("HELP_KEYBINDINGS");
    ctxt.register_action("QUIT");
    ctxt.register_action("NEXT_TAB");
    ctxt.register_action("PREV_TAB");
    ctxt.register_action("CONFIRM");

    std::stringstream sTemp;

    while(true) {
        //Clear the lines
        for (int i = 0; i < iContentHeight; i++) {
            for (int j = 0; j < 79; j++) {
                if (mapLines[j]) {
                    mvwputch(w_worlds, i, j, BORDER_COLOR, LINE_XOXO);
                } else {
                    mvwputch(w_worlds, i, j, c_black, ' ');
                }

                if (i < iTooltipHeight) {
                    mvwputch(w_worlds_tooltip, i, j, c_black, ' ');
                }
            }
        }

        //Draw World Names
        for (size_t i = 0; i < world_pages[selpage].size(); ++i) {
            sTemp.str("");
            sTemp << i + 1;
            mvwprintz(w_worlds, i, 0, c_white, "%s", sTemp.str().c_str());
            mvwprintz(w_worlds, i, 4, c_white, "");

            std::string world_name = (world_pages[selpage])[i];
            size_t saves_num = world_generator->all_worlds[world_name]->world_saves.size();

            if (i == sel) {
                wprintz(w_worlds, c_yellow, ">> ");
            } else {
                wprintz(w_worlds, c_yellow, "   ");
            }

            if (world_need_lua_build(world_name)) {
                wprintz(w_worlds, c_dkgray, "%s (%i)", world_name.c_str(), saves_num);
            } else {
                wprintz(w_worlds, c_white, "%s (%i)", world_name.c_str(), saves_num);
            }
        }

        //Draw Tabs
        mvwprintz(w_worlds_header, 0, 7, c_white, "");

        for (size_t i = 0; i < num_pages; ++i) {
            nc_color tabcolor = (selpage == i) ? hilite(c_white) : c_white;
            if (!world_pages[i].empty()) { //skip empty pages
                wprintz(w_worlds_header, c_white, "[");
                wprintz(w_worlds_header, tabcolor, _("Page %d"), i + 1);
                wprintz(w_worlds_header, c_white, "]");
                wputch(w_worlds_header, BORDER_COLOR, LINE_OXOX);
            }
        }

        wrefresh(w_worlds_header);

        fold_and_print(w_worlds_tooltip, 0, 0, 78, c_white, _("Pick a world to enter game"));
        wrefresh(w_worlds_tooltip);

        wrefresh(w_worlds);

        const std::string action = ctxt.handle_input();

        if (action == "QUIT") {
            break;
        } else if (!world_pages[selpage].empty() && action == "DOWN") {
            sel++;
            if (sel >= world_pages[selpage].size()) {
                sel = 0;
            }
        } else if (!world_pages[selpage].empty() && action == "UP") {
            if (sel == 0) {
                sel = world_pages[selpage].size() - 1;
            } else {
                sel--;
            }
        } else if (action == "NEXT_TAB") {
            sel = 0;
            do { //skip empty pages
                selpage++;
                if (selpage >= world_pages.size()) {
                    selpage = 0;
                }
            } while(world_pages[selpage].empty());
        } else if (action == "PREV_TAB") {
            sel = 0;
            do { //skip empty pages
                if (selpage != 0) {
                    selpage--;
                } else {
                    selpage = world_pages.size() - 1;
                }
            } while(world_pages[selpage].empty());
        } else if (action == "CONFIRM") {
            if (world_need_lua_build(world_pages[selpage][sel])) {
                popup(_("Can't start in world [%s]. Some of mods require Lua support."),
                      world_pages[selpage][sel].c_str());
                continue;
            }
            // we are wanting to get out of this by confirmation, so ask if we want to load the level [y/n prompt] and if yes exit
            if (query_yn(_("Do you want to start the game in world [%s]?"),
                         world_pages[selpage][sel].c_str())) {
                werase(w_worlds);
                werase(w_worlds_border);
                werase(w_worlds_header);
                werase(w_worlds_tooltip);
                return all_worlds[world_pages[selpage][sel]];//sel + selpage * iContentHeight;
            }
        }
    }

    werase(w_worlds);
    werase(w_worlds_border);
    werase(w_worlds_header);
    werase(w_worlds_tooltip);

    return NULL;
}

void worldfactory::remove_world(std::string worldname)
{
    std::vector<std::string>::iterator it = std::find(all_worldnames.begin(), all_worldnames.end(),
                                            worldname);
    if (it != all_worldnames.end()) {
        all_worldnames.erase(it);

        delete all_worlds[worldname];
        all_worlds.erase(worldname);
    }
}

std::string worldfactory::pick_random_name()
{
    // TODO: add some random worldname parameters to name generator
    return get_next_valid_worldname();
}

int worldfactory::show_worldgen_tab_options(WINDOW *win, WORLDPTR world)
{
    const int iTooltipHeight = 4;
    const int iContentHeight = FULL_SCREEN_HEIGHT - 5 - iTooltipHeight;

    const int iOffsetX = (TERMX > FULL_SCREEN_WIDTH) ? (TERMX - FULL_SCREEN_WIDTH) / 2 : 0;
    const int iOffsetY = (TERMY > FULL_SCREEN_HEIGHT) ? (TERMY - FULL_SCREEN_HEIGHT) / 2 : 0;

    WINDOW *w_options = newwin(iContentHeight, FULL_SCREEN_WIDTH - 2, iTooltipHeight + 4 + iOffsetY,
                               1 + iOffsetX);
    WINDOW_PTR w_optionsptr( w_options );

    WINDOW *w_options_tooltip = newwin(iTooltipHeight - 2, FULL_SCREEN_WIDTH - 2, 3 + iOffsetY,
                                       1 + iOffsetX);
    WINDOW_PTR w_options_tooltipptr( w_options_tooltip );

    WINDOW *w_options_header = newwin(1, FULL_SCREEN_WIDTH - 2, iTooltipHeight + 3 + iOffsetY,
                                      1 + iOffsetX);
    WINDOW_PTR w_options_headerptr( w_options_header );

    std::stringstream sTemp;

    std::map<int, bool> mapLines;
    mapLines[4] = true;
    mapLines[60] = true;

    for( auto &mapLine : mapLines ) {
        mvwputch( win, FULL_SCREEN_HEIGHT - 1, mapLine.first + 1, BORDER_COLOR, LINE_XXOX ); // _|_
    }

    for (int i = 0; i < 78; i++) {
        if (mapLines[i]) {
            mvwputch(w_options_header, 0, i, BORDER_COLOR, LINE_OXXX);
        } else {
            mvwputch(w_options_header, 0, i, BORDER_COLOR, LINE_OXOX); // Draw header line
        }
    }

    mvwputch(win, iTooltipHeight + 3,  0, BORDER_COLOR, LINE_XXXO); // |-
    mvwputch(win, iTooltipHeight + 3, 79, BORDER_COLOR, LINE_XOXX); // -|

    wrefresh(win);
    wrefresh(w_options_header);

    input_context ctxt("WORLDGEN_OPTION_DIALOG");
    ctxt.register_cardinal();
    ctxt.register_action("HELP_KEYBINDINGS");
    ctxt.register_action("QUIT");
    ctxt.register_action("NEXT_TAB");
    ctxt.register_action("PREV_TAB");
    int iStartPos = 0;
    int iCurrentLine = 0;

    do {
        for (int i = 0; i < iContentHeight; i++) {
            for (int j = 0; j < 79; j++) {
                if (mapLines[j]) {
                    mvwputch(w_options, i, j, BORDER_COLOR, LINE_XOXO);
                } else {
                    mvwputch(w_options, i, j, c_black, ' ');
                }

                if (i < iTooltipHeight) {
                    mvwputch(w_options_tooltip, i, j, c_black, ' ');
                }
            }
        }

        calcStartPos(iStartPos, iCurrentLine, iContentHeight, mPageItems[iWorldOptPage].size());

        //Draw options
        int iBlankOffset = 0;
        for (int i = iStartPos; i < iStartPos + ((iContentHeight > (int)mPageItems[iWorldOptPage].size()) ?
                (int)mPageItems[iWorldOptPage].size() : iContentHeight); i++) {
            nc_color cLineColor = c_ltgreen;

            if (world->WORLD_OPTIONS[mPageItems[iWorldOptPage][i]].getMenuText() == "") {
                iBlankOffset++;
                continue;
            }

            sTemp.str("");
            sTemp << i + 1 - iBlankOffset;
            mvwprintz(w_options, i - iStartPos, 1, c_white, sTemp.str().c_str());
            mvwprintz(w_options, i - iStartPos, 5, c_white, "");

            if (iCurrentLine == i) {
                wprintz(w_options, c_yellow, ">> ");
            } else {
                wprintz(w_options, c_yellow, "   ");
            }
            wprintz(w_options, c_white, "%s",
                    (world->WORLD_OPTIONS[mPageItems[iWorldOptPage][i]].getMenuText()).c_str());

            if (world->WORLD_OPTIONS[mPageItems[iWorldOptPage][i]].getValue() == "false") {
                cLineColor = c_ltred;
            }

            mvwprintz(w_options, i - iStartPos, 62, (iCurrentLine == i) ? hilite(cLineColor) :
                      cLineColor, "%s", (world->WORLD_OPTIONS[mPageItems[iWorldOptPage][i]].getValueName()).c_str());
        }

        draw_scrollbar(win, iCurrentLine, iContentHeight,
                       mPageItems[iWorldOptPage].size(), iTooltipHeight + 4, 0, BORDER_COLOR);
        wrefresh(win);

        fold_and_print(w_options_tooltip, 0, 0, 78, c_white, "%s #%s",
                       world->WORLD_OPTIONS[mPageItems[iWorldOptPage][iCurrentLine]].getTooltip().c_str(),
                       world->WORLD_OPTIONS[mPageItems[iWorldOptPage][iCurrentLine]].getDefaultText().c_str());

        wrefresh(w_options_tooltip);
        wrefresh(w_options);
        refresh();

        const std::string action = ctxt.handle_input();
        if (action == "DOWN") {
            do {
                iCurrentLine++;
                if (iCurrentLine >= (int)mPageItems[iWorldOptPage].size()) {
                    iCurrentLine = 0;
                }
            } while(world->WORLD_OPTIONS[mPageItems[iWorldOptPage][iCurrentLine]].getMenuText() == "");

        } else if (action == "UP") {
            do {
                iCurrentLine--;
                if (iCurrentLine < 0) {
                    iCurrentLine = mPageItems[iWorldOptPage].size() - 1;
                }
            } while(world->WORLD_OPTIONS[mPageItems[iWorldOptPage][iCurrentLine]].getMenuText() == "");

        } else if (!mPageItems[iWorldOptPage].empty() && action == "RIGHT") {
            world->WORLD_OPTIONS[mPageItems[iWorldOptPage][iCurrentLine]].setNext();

        } else if (!mPageItems[iWorldOptPage].empty() && action == "LEFT") {
            world->WORLD_OPTIONS[mPageItems[iWorldOptPage][iCurrentLine]].setPrev();

        } else if (action == "PREV_TAB") {
            return -1;

        } else if (action == "NEXT_TAB") {
            return 1;

        } else if (action == "QUIT") {
            return -999;
        }

    } while (true);

    return 0;
}

void worldfactory::draw_mod_list( WINDOW *w, int &start, int &cursor, const std::vector<std::string> &mods, bool is_active_list, const std::string &text_if_empty, WINDOW *w_shift )
{
    werase( w );
    werase(w_shift);

    const int iMaxRows = getmaxy( w );
    int iModNum = mods.size();
    int iActive = cursor;

    if( mods.empty() ) {
        center_print( w, 0, c_red, "%s", text_if_empty.c_str() );
    } else {
        int iCatSortNum = 0;
        std::string sLastCategoryName = "";
        std::map<int, std::string> mSortCategory;
        mSortCategory[0] = sLastCategoryName;

        for( size_t i = 0; i < mods.size(); ++i ) {
            if ( sLastCategoryName != mman->mod_map[mods[i]]->category.second ) {
                sLastCategoryName = mman->mod_map[mods[i]]->category.second;
                mSortCategory[ i + iCatSortNum++ ] = sLastCategoryName;
                iModNum++;
            }
        }

        const size_t wwidth = getmaxx( w ) - 1 - 3; // border (1) + ">> " (3)

        int iNum = 0;
        int index = 0;
        bool bKeepIter = false;
        int iCatBeforeCursor = 0;

        for( int i = 0; i <= iActive; i++ ) {
            if( mSortCategory[i] != "" ) {
                iActive++;
                iCatBeforeCursor++;
            }
        }

        calcStartPos( start, iActive, iMaxRows, iModNum );

        for( int i = 0; i < start; i++ ) {
            if( mSortCategory[i] != "" ) {
                iNum++;
            }
        }

        for( auto iter = mods.begin(); iter != mods.end(); ++index ) {
            if( iNum >= start && iNum < start + ((iMaxRows > iModNum) ? iModNum : iMaxRows) ) {
                if( mSortCategory[iNum] != "" ) {
                    bKeepIter = true;
                    trim_and_print( w, iNum - start, 1, wwidth, c_magenta, "%s", mSortCategory[iNum].c_str() );

                } else {
                    if( iNum == iActive ) {
                        cursor = iActive - iCatBeforeCursor;
                        //mvwprintw( w, iNum - start + iCatSortOffset, 1, "   " );
                        if( is_active_list ) {
                            mvwprintz( w, iNum - start, 1, c_yellow, ">> " );
                        } else {
                            mvwprintz( w, iNum - start, 1, c_blue, ">> " );
                        }
                    }

                    auto mod = mman->mod_map[*iter];
#ifndef LUA
                    if( mod->need_lua ) {
                        trim_and_print( w, iNum - start, 4, wwidth, c_dkgray, "%s", mod->name.c_str() );
                    } else {
                        trim_and_print( w, iNum - start, 4, wwidth, c_white, "%s", mod->name.c_str() );
                    }
#else
                    trim_and_print( w, iNum - start, 4, wwidth, c_white, "%s", mod->name.c_str() );
#endif

                    if( w_shift ) {
                        // get shift information for the active item
                        std::string shift_display = "";
                        const size_t iPos = std::distance( mods.begin(), iter );

                        if( mman_ui->can_shift_up(iPos, mods) ) {
                            shift_display += "<color_blue>+</color> ";
                        } else {
                            shift_display += "<color_dkgray>+</color> ";
                        }

                        if( mman_ui->can_shift_down(iPos, mods) ) {
                            shift_display += "<color_blue>-</color>";
                        } else {
                            shift_display += "<color_dkgray>-</color>";
                        }

                        trim_and_print( w_shift, 2 + iNum - start, 1, 3, c_white, shift_display.c_str() );
                    }
                }
            }

            if( bKeepIter ) {
                bKeepIter = false;
            } else {
                ++iter;
            }

            iNum++;
        }
    }

    draw_scrollbar( w, iActive, iMaxRows, iModNum, 0);

    wrefresh( w );
    wrefresh(w_shift);
}

int worldfactory::show_worldgen_tab_modselection(WINDOW *win, WORLDPTR world)
{
    // Use active_mod_order of the world,
    // saves us from writing 'world->active_mod_order' all the time.
    std::vector<std::string> &active_mod_order = world->active_mod_order;
    {
        std::vector<std::string> tmp_mod_order;
        // clear active_mod_order and re-add all the mods, his ensures
        // that changes (like changing dependencies) get updated
        tmp_mod_order.swap(active_mod_order);
        for( auto &elem : tmp_mod_order ) {
            mman_ui->try_add( elem, active_mod_order );
        }
    }

    input_context ctxt("MODMANAGER_DIALOG");
    ctxt.register_updown();
    ctxt.register_action("LEFT", _("Switch to other list"));
    ctxt.register_action("RIGHT", _("Switch to other list"));
    ctxt.register_action("HELP_KEYBINDINGS");
    ctxt.register_action("QUIT");
    ctxt.register_action("NEXT_CATEGORY_TAB");
    ctxt.register_action("PREV_CATEGORY_TAB");
    ctxt.register_action("NEXT_TAB");
    ctxt.register_action("PREV_TAB");
    ctxt.register_action("CONFIRM", _("Activate / deactive mod"));
    ctxt.register_action("ADD_MOD");
    ctxt.register_action("REMOVE_MOD");
    ctxt.register_action("SAVE_DEFAULT_MODS");

    const int iOffsetX = (TERMX > FULL_SCREEN_WIDTH) ? (TERMX - FULL_SCREEN_WIDTH) / 2 : 0;
    const int iOffsetY = (TERMY > FULL_SCREEN_HEIGHT) ? (TERMY - FULL_SCREEN_HEIGHT) / 2 : 0;

    // lots of small windows so that each section can be drawn to independently of the others as necessary
    WINDOW *w_header1, *w_header2, *w_shift, *w_list, *w_active, *w_description;
    w_header1 = newwin(1, FULL_SCREEN_WIDTH / 2 - 5, 3 + iOffsetY, 1 + iOffsetX);
    w_header2 = newwin(1, FULL_SCREEN_WIDTH / 2 - 4, 3 + iOffsetY,
                       FULL_SCREEN_WIDTH / 2 + 3 + iOffsetX);
    w_shift   = newwin(13, 5, 3 + iOffsetY, FULL_SCREEN_WIDTH / 2 - 3 + iOffsetX);
    w_list    = newwin(11, FULL_SCREEN_WIDTH / 2 - 4, 5 + iOffsetY, iOffsetX);
    w_active  = newwin(11, FULL_SCREEN_WIDTH / 2 - 4, 5 + iOffsetY,
                       FULL_SCREEN_WIDTH / 2 + 2 + iOffsetX);
    w_description = newwin(4, FULL_SCREEN_WIDTH - 2, 19 + iOffsetY, 1 + iOffsetX);

    draw_modselection_borders(win, &ctxt);
    std::vector<std::string> headers;
    headers.push_back(_("Mod List"));
    headers.push_back(_("Mod Load Order"));
    std::vector<WINDOW *> header_windows;
    header_windows.push_back(w_header1);
    header_windows.push_back(w_header2);

    int tab_output = 0;
    int last_active_header = 0;
    size_t active_header = 0;
    size_t useable_mod_count = mman_ui->usable_mods.size();
    int startsel[2] = {0, 0};
    int cursel[2] = {0, 0};
    int iCurrentTab = 0;
    std::vector<std::string> current_tab_mods;

    bool redraw_headers = true;
    bool redraw_description = true;
    bool redraw_list = true;
    bool redraw_active = true;
    bool selection_changed = false;
    bool recalc_tabs = true;

    while (tab_output == 0) {
        if (redraw_headers) {
            for (size_t i = 0; i < headers.size(); ++i) {
                werase(header_windows[i]);
                const int header_x = (getmaxx(header_windows[i]) - headers[i].size()) / 2;
                mvwprintz(header_windows[i], 0, header_x , c_cyan, "%s", headers[i].c_str());

                if (active_header == i) {
                    mvwputch(header_windows[i], 0, header_x - 3, c_red, '<');
                    mvwputch(header_windows[i], 0, header_x + headers[i].size() + 2, c_red, '>');
                }
                wrefresh(header_windows[i]);
            }
            redraw_list = true;
            redraw_active = true;
            redraw_headers = false;
        }

        if( recalc_tabs ) {
            current_tab_mods.clear();

            for( const auto &item : mman_ui->usable_mods ) {
                const auto &iter = get_mod_list_cat_tab().find(get_mod_list_categories()[mman->mod_map[item]->category.first].first);

                std::string sCatTab = "tab_default";
                if( iter != get_mod_list_cat_tab().end() ) {
                    sCatTab = iter->second;
                }

                if( sCatTab == get_mod_list_tabs()[iCurrentTab].first ) {
                    current_tab_mods.push_back(item);
                }

                useable_mod_count = current_tab_mods.size();
            }

            recalc_tabs = false;
        }

        if( selection_changed ) {
            if( active_header == 0 ) {
                redraw_list = true;
            }
            if( active_header == 1 ) {
                redraw_active = true;
            }
            selection_changed = false;
            redraw_description = true;
        }

        if( redraw_description ) {
            werase( w_description );

            MOD_INFORMATION *selmod = NULL;
            if( current_tab_mods.empty() ) {
                // Do nothing, leave selmod == NULL
            } else if( active_header == 0 ) {
                selmod = mman->mod_map[current_tab_mods[cursel[0]]];
            } else if( !active_mod_order.empty() ) {
                selmod = mman->mod_map[active_mod_order[cursel[1]]];
            }

            if( selmod != NULL ) {
                fold_and_print(w_description, 0, 1, getmaxx(w_description) - 1,
                               c_white, mman_ui->get_information(selmod));
            }

            //redraw tabs
            mvwprintz(win, 4, 2, c_white, "");
            for( size_t i = 0; i < get_mod_list_tabs().size(); i++ ) {
                wprintz(win, c_white, "[");
                wprintz(win, (iCurrentTab == (int)i) ? hilite(c_ltgreen) : c_ltgreen, (get_mod_list_tabs()[i].second).c_str());
                wprintz(win, c_white, "]");
                wputch(win, BORDER_COLOR, LINE_OXOX);
            }

            redraw_description = false;
            wrefresh(w_description);
            wrefresh(win);
        }

        if( redraw_list ) {
            draw_mod_list( w_list, startsel[0], cursel[0], current_tab_mods, active_header == 0, _("--NO AVAILABLE MODS--"), nullptr );
        }
        if( redraw_active ) {
            draw_mod_list( w_active, startsel[1], cursel[1], active_mod_order, active_header == 1, _("--NO ACTIVE MODS--"), w_shift );
        }
        refresh();

        last_active_header = active_header;
        const int next_header = (active_header == 1) ? 0 : 1;
        const int prev_header = (active_header == 0) ? 1 : 0;

        int selection = (active_header == 0) ? cursel[0] : cursel[1];
        int last_selection = selection;
        unsigned int next_selection = selection + 1;
        int prev_selection = selection - 1;
        if (active_header == 0) {
            next_selection = (next_selection >= useable_mod_count) ? 0 : next_selection;
            prev_selection = (prev_selection < 0) ? useable_mod_count - 1 : prev_selection;
        } else {
            next_selection = (next_selection >= active_mod_order.size()) ? 0 : next_selection;
            prev_selection = (prev_selection < 0) ? active_mod_order.size() - 1 : prev_selection;
        }

        const std::string action = ctxt.handle_input();

        if( action == "DOWN" ) {
            selection = next_selection;
        } else if( action == "UP" ) {
            selection = prev_selection;
        } else if( action == "RIGHT" ) {
            active_header = next_header;
        } else if( action == "LEFT" ) {
            active_header = prev_header;
        } else if( action == "CONFIRM" ) {
            if( active_header == 0 && !current_tab_mods.empty() ) {
#ifndef LUA
                if( mman->mod_map[current_tab_mods[cursel[0]]]->need_lua ) {
                    popup(_("Can't add mod. This mod requires Lua support."));
                    redraw_active = true;
                    draw_modselection_borders(win, &ctxt);
                    redraw_description = true;
                    continue;
                }
#endif
                // try-add
                mman_ui->try_add(current_tab_mods[cursel[0]], active_mod_order);
                redraw_active = true;
            } else if( active_header == 1 && !active_mod_order.empty() ) {
                // try-rem
                mman_ui->try_rem(cursel[1], active_mod_order);
                redraw_active = true;
                if( active_mod_order.empty() ) {
                    // switch back to other list, we can't change
                    // anything in the empty active mods list.
                    active_header = 0;
                }
            }
        } else if( action == "ADD_MOD" ) {
            if( active_header == 1 && active_mod_order.size() > 1 ) {
                mman_ui->try_shift('+', cursel[1], active_mod_order);
                redraw_active = true;
            }
        } else if( action == "REMOVE_MOD" ) {
            if( active_header == 1 && active_mod_order.size() > 1 ) {
                mman_ui->try_shift('-', cursel[1], active_mod_order);
                redraw_active = true;
            }
        } else if( action == "NEXT_CATEGORY_TAB" ) {
            if(  active_header == 0  ) {
                if(  ++iCurrentTab >= (int)get_mod_list_tabs().size()  ) {
                    iCurrentTab = 0;
                }

                startsel[0] = 0;
                cursel[0] = 0;

                recalc_tabs = true;
                redraw_description = true;
            }

        } else if( action == "PREV_CATEGORY_TAB" ) {
            if(  active_header == 0  ) {
                if(  --iCurrentTab < 0  ) {
                    iCurrentTab = get_mod_list_tabs().size()-1;
                }

                startsel[0] = 0;
                cursel[0] = 0;

                recalc_tabs = true;
                redraw_description = true;
            }
        } else if( action == "NEXT_TAB" ) {
            tab_output = 1;
        } else if( action == "PREV_TAB" ) {
            tab_output = -1;
        } else if( action == "SAVE_DEFAULT_MODS" ) {
            if(mman->set_default_mods(active_mod_order) ) {
                popup(_("Saved list of active mods as default"));
                draw_modselection_borders(win, &ctxt);
                redraw_description = true;
                redraw_headers = true;
            }
        } else if( action == "HELP_KEYBINDINGS" ) {
            // Redraw all the things!
            redraw_headers = true;
            redraw_description = true;
            redraw_list = true;
            redraw_active = true;
            draw_worldgen_tabs( win, 0 );
            draw_modselection_borders( win, &ctxt );
            redraw_description = true;
        } else if( action == "QUIT" ) {
            tab_output = -999;
        }
        // RESOLVE INPUTS
        if( last_active_header != (int)active_header ) {
            redraw_headers = true;
            redraw_description = true;
        }
        if( last_selection != selection ) {
            if( active_header == 0 ) {
                redraw_list = true;
                cursel[0] = selection;
            } else {
                redraw_active = true;
                cursel[1] = selection;
            }
            redraw_description = true;
        }
        if( active_mod_order.empty() ) {
            redraw_active = true;
            cursel[1] = 0;
        }

        if( active_header == 1 ) {
            if( active_mod_order.empty() ) {
                cursel[1] = 0;
            } else {
                if( cursel[1] < 0 ) {
                    cursel[1] = 0;
                } else if( cursel[1] >= (int)active_mod_order.size() ) {
                    cursel[1] = active_mod_order.size() - 1;
                }
            }
        }
        // end RESOLVE INPUTS
    }
    werase(w_header1);
    werase(w_header2);
    werase(w_shift);
    werase(w_list);
    werase(w_active);
    werase(w_description);

    delwin(w_header1);
    delwin(w_header2);
    delwin(w_shift);
    delwin(w_list);
    delwin(w_active);
    delwin(w_description);
    return tab_output;
}

int worldfactory::show_worldgen_tab_confirm(WINDOW *win, WORLDPTR world)
{
    const int iTooltipHeight = 1;
    const int iContentHeight = FULL_SCREEN_HEIGHT - 3 - iTooltipHeight;

    const int iOffsetX = (TERMX > FULL_SCREEN_WIDTH) ? (TERMX - FULL_SCREEN_WIDTH) / 2 : 0;
    const int iOffsetY = (TERMY > FULL_SCREEN_HEIGHT) ? (TERMY - FULL_SCREEN_HEIGHT) / 2 : 0;

    const char* line_of_32_underscores = "________________________________";

    WINDOW *w_confirmation = newwin(iContentHeight, FULL_SCREEN_WIDTH - 2,
                                    iTooltipHeight + 2 + iOffsetY, 1 + iOffsetX);
    WINDOW_PTR w_confirmationptr( w_confirmation );

    unsigned namebar_y = 1;
    unsigned namebar_x = 3 + utf8_width(_("World Name:"));

    int line = 1;
    bool noname = false;
    input_context ctxt("WORLDGEN_CONFIRM_DIALOG");
    ctxt.register_action("HELP_KEYBINDINGS");
    ctxt.register_action("QUIT");
    ctxt.register_action("ANY_INPUT");
    ctxt.register_action("NEXT_TAB");
    ctxt.register_action("PREV_TAB");
    ctxt.register_action("PICK_RANDOM_WORLDNAME");

    std::string worldname = world->world_name;
    do {
        mvwprintz(w_confirmation, namebar_y, 2, c_white, _("World Name:"));
        mvwprintz(w_confirmation, namebar_y, namebar_x, c_ltgray, line_of_32_underscores);
        fold_and_print(w_confirmation, 3, 2, 76, c_ltgray,
                       _("Press <color_yellow>%s</color> to pick a random name for your world."), ctxt.get_desc("PICK_RANDOM_WORLDNAME").c_str());
        fold_and_print(w_confirmation, FULL_SCREEN_HEIGHT / 2 - 2, 2, 76, c_ltgray, _("\
Press <color_yellow>%s</color> when you are satisfied with the world as it is and are ready \
to continue, or <color_yellow>%s</color> to go back and review your world."), ctxt.get_desc("NEXT_TAB").c_str(), ctxt.get_desc("PREV_TAB").c_str());
        if (!noname) {
            mvwprintz(w_confirmation, namebar_y, namebar_x, c_ltgray, "%s", worldname.c_str());
            if (line == 1) {
                wprintz(w_confirmation, h_ltgray, "_");
            }
        }
        if (noname) {
            mvwprintz(w_confirmation, namebar_y, namebar_x, c_ltgray, line_of_32_underscores);
            noname = false;
        }

        wrefresh(win);
        wrefresh(w_confirmation);
        refresh();

        const std::string action = ctxt.handle_input();
        if (action == "NEXT_TAB") {
#ifndef LUA
            MOD_INFORMATION *temp = NULL;
            for (std::string &mod : world->active_mod_order) {
                temp = mman->mod_map[mod];
                if ( temp->need_lua ) {
                    popup(_("Mod '%s' requires Lua support."), temp->name.c_str());
                    return -2; // Move back to modselect tab.
                }
            }
#endif
            if (worldname.empty()) {
                mvwprintz(w_confirmation, namebar_y, namebar_x, h_ltgray, _("_______NO NAME ENTERED!!!!______"));
                noname = true;
                wrefresh(w_confirmation);
                if (!query_yn(_("Are you SURE you're finished? World name will be randomly generated."))) {
                    continue;
                } else {
                    world->world_name = pick_random_name();
                    if (!valid_worldname(world->world_name)) {
                        continue;
                    }
                    return 1;
                }
            } else if (query_yn(_("Are you SURE you're finished?")) && valid_worldname(worldname)) {
                world->world_name = worldname;
                return 1;
            } else {
                continue;
            }
        } else if (action == "PREV_TAB") {
            world->world_name = worldname;
            return -1;
        } else if (action == "PICK_RANDOM_WORLDNAME") {
            mvwprintz(w_confirmation, namebar_y, namebar_x, c_ltgray, line_of_32_underscores);
            world->world_name = worldname = pick_random_name();
        } else if (action == "QUIT") {
            // Cache the current name just in case they say No to the exit query.
            world->world_name = worldname;
            return -999;
        } else if (action == "ANY_INPUT") {
            const input_event ev = ctxt.get_raw_input();
            const long ch = ev.get_first_input();
            switch (line) {
            case 1: {
                utf8_wrapper wrap(worldname);
                utf8_wrapper newtext( ev.text );
                if( ch == KEY_BACKSPACE ) {
                    if (!wrap.empty()) {
                        wrap.erase(wrap.length() - 1, 1);
                        worldname = wrap.str();
                    }
                } else if(ch == KEY_F(2)) {
                    std::string tmp = get_input_string_from_file();
                    int tmplen = utf8_width( tmp );
                    if(tmplen > 0 && tmplen + utf8_width(worldname) < 30) {
                        worldname.append(tmp);
                    }
                } else if( !newtext.empty() && is_char_allowed( newtext.at( 0 ) ) ) {
                    // No empty string, no slash, no backslash, no control sequence
                    wrap.append( newtext );
                    worldname = wrap.str();
                }
                mvwprintz(w_confirmation, namebar_y, namebar_x, c_ltgray, line_of_32_underscores);
                mvwprintz(w_confirmation, namebar_y, namebar_x, c_ltgray, "%s", worldname.c_str());
                wprintz(w_confirmation, h_ltgray, "_");
            }
            break;
            }
        }
    } while (true);

    return 0;
}

void worldfactory::draw_modselection_borders(WINDOW *win, input_context *ctxtp)
{
    // make appropriate lines: X & Y coordinate of starting point, length, horizontal/vertical type
    int xs[] = {1, 1, (FULL_SCREEN_WIDTH / 2) + 2, (FULL_SCREEN_WIDTH / 2) - 4,
                (FULL_SCREEN_WIDTH / 2) + 2
               };
    int ys[] = {FULL_SCREEN_HEIGHT - 8, 4, 4, 3, 3};
    int ls[] = {FULL_SCREEN_WIDTH - 2, (FULL_SCREEN_WIDTH / 2) - 4, (FULL_SCREEN_WIDTH / 2) - 3,
                FULL_SCREEN_HEIGHT - 11, 1
               };
    bool hv[] = {true, true, true, false, false}; // horizontal line = true, vertical line = false

    for (int i = 0; i < 5; ++i) {
        int x = xs[i];
        int y = ys[i];
        int l = ls[i];
        if (hv[i]) {
            for (int j = 0; j < l; ++j) {
                mvwputch(win, y, x + j, BORDER_COLOR, LINE_OXOX); // _
            }
        } else {
            for (int j = 0; j < l; ++j) {
                mvwputch(win, y + j, x, BORDER_COLOR, LINE_XOXO); // |
            }
        }
    }

    // Add in connective characters
    mvwputch(win, 4, 0, BORDER_COLOR, LINE_XXXO);
    mvwputch(win, FULL_SCREEN_HEIGHT - 8, 0, BORDER_COLOR, LINE_XXXO);
    mvwputch(win, 4, FULL_SCREEN_WIDTH / 2 + 2, BORDER_COLOR, LINE_XXXO);

    mvwputch(win, 4, FULL_SCREEN_WIDTH - 1, BORDER_COLOR, LINE_XOXX);
    mvwputch(win, FULL_SCREEN_HEIGHT - 8, FULL_SCREEN_WIDTH - 1, BORDER_COLOR, LINE_XOXX);
    mvwputch(win, 4, FULL_SCREEN_WIDTH / 2 - 4, BORDER_COLOR, LINE_XOXX);

    mvwputch(win, 2, FULL_SCREEN_WIDTH / 2 - 4, BORDER_COLOR, LINE_OXXX); // -.-
    mvwputch(win, 2, FULL_SCREEN_WIDTH / 2 + 2, BORDER_COLOR, LINE_OXXX); // -.-

    mvwputch(win, FULL_SCREEN_HEIGHT - 8, FULL_SCREEN_WIDTH / 2 - 4, BORDER_COLOR, LINE_XXOX); // _|_
    mvwputch(win, FULL_SCREEN_HEIGHT - 8, FULL_SCREEN_WIDTH / 2 + 2, BORDER_COLOR, LINE_XXOX); // _|_

    // Add tips & hints
    fold_and_print(win, FULL_SCREEN_HEIGHT - 7, 2, getmaxx(win) - 4, c_green,
                   _("Press %s to save the list of active mods as default. Press %s for help."),
                   ctxtp->get_desc("SAVE_DEFAULT_MODS").c_str(),
                   ctxtp->get_desc("HELP_KEYBINDINGS").c_str()
                  );
    wrefresh(win);
    refresh();
}

void worldfactory::draw_worldgen_tabs(WINDOW *w, unsigned int current)
{
    werase(w);

    for (int i = 1; i < FULL_SCREEN_WIDTH - 1; i++) {
        mvwputch(w, 2, i, BORDER_COLOR, LINE_OXOX);
        mvwputch(w, FULL_SCREEN_HEIGHT - 1, i, BORDER_COLOR, LINE_OXOX);

        if (i > 2 && i < FULL_SCREEN_HEIGHT - 1) {
            mvwputch(w, i, 0, BORDER_COLOR, LINE_XOXO);
            mvwputch(w, i, FULL_SCREEN_WIDTH - 1, BORDER_COLOR, LINE_XOXO);
        }
    }

    int x = 2;
    for (size_t i = 0; i < tab_strings.size(); ++i) {
        draw_tab(w, x, tab_strings[i], (i == current) ? true : false);
        x += utf8_width( tab_strings[i] ) + 7;
    }

    mvwputch(w, 2, 0, BORDER_COLOR, LINE_OXXO); // |^
    mvwputch(w, 2, FULL_SCREEN_WIDTH - 1, BORDER_COLOR, LINE_OOXX); // ^|

    mvwputch(w, 4, 0, BORDER_COLOR, LINE_XOXO); // |
    mvwputch(w, 4, FULL_SCREEN_WIDTH - 1, BORDER_COLOR, LINE_XOXO); // |

    mvwputch(w, FULL_SCREEN_HEIGHT - 1, 0, BORDER_COLOR, LINE_XXOO); // |_
    mvwputch(w, FULL_SCREEN_HEIGHT - 1, FULL_SCREEN_WIDTH - 1, BORDER_COLOR, LINE_XOOX); // _|
}


bool worldfactory::world_need_lua_build(std::string world_name)
{
#ifndef LUA
    WORLDPTR world = all_worlds[world_name];

    if( world == nullptr ) {
        return false;
    }
    for (std::string &mod : world->active_mod_order) {
        if( mman->has_mod( mod ) && mman->mod_map[mod]->need_lua ) {
            return true;
        }
    }
#endif
    // Prevent unused var error when LUA and RELEASE enabled.
    world_name.size();
    return false;
}

bool worldfactory::valid_worldname(std::string name, bool automated)
{
    std::string msg;

    if (name == "save" || name == "TUTORIAL" || name == "DEFENSE") {
        msg = string_format(_("%s is a reserved name!"), name.c_str());
    } else if (std::find(all_worldnames.begin(), all_worldnames.end(), name) == all_worldnames.end()) {
        return true;
    } else {
        msg = string_format(_("A world named %s already exists!"), name.c_str());
    }
    if (!automated) {
        popup(msg, PF_GET_KEY);
    }
    return false;
}

void worldfactory::get_default_world_options(WORLDPTR &world)
{
    std::unordered_map<std::string, options_manager::cOpt> retoptions;
    for( auto &elem : OPTIONS ) {
        if( elem.second.getPage() == "world_default" ) {
            world->WORLD_OPTIONS[elem.first] = elem.second;
        }
    }
}

bool worldfactory::load_world_options(WORLDPTR &world)
{
    get_default_world_options(world);
    std::ifstream fin;

    auto path = world->world_path + "/" + FILENAMES["worldoptions"];

    fin.open(path.c_str(), std::ifstream::in | std::ifstream::binary);

    if (!fin.is_open()) {
        fin.close();

        path = world->world_path + "/" + FILENAMES["legacy_worldoptions"];
        fin.open(path.c_str());

        if (!fin.is_open()) {
            fin.close();

            DebugLog( D_ERROR, DC_ALL ) << "Couldn't read world options file";
            return false;

        } else {
            //load legacy txt
            std::string sLine;

            while (!fin.eof()) {
                getline(fin, sLine);

                if (sLine != "" && sLine[0] != '#' && std::count(sLine.begin(), sLine.end(), ' ') == 1) {
                    int ipos = sLine.find(' ');
                    // make sure that the option being loaded is part of the world_default page in OPTIONS
                    if(OPTIONS[sLine.substr(0, ipos)].getPage() == "world_default") {
                        world->WORLD_OPTIONS[sLine.substr(0, ipos)].setValue(sLine.substr(ipos + 1, sLine.length()));
                    }
                }
            }
            fin.close();

            if ( save_world( world ) ) {
                remove_file( path );
            }

            return true;
        }
    }

    //load json
    JsonIn jsin(fin);

    jsin.start_array();
    while (!jsin.end_array()) {
        JsonObject jo = jsin.get_object();

        const std::string name = jo.get_string("name");
        const std::string value = jo.get_string("value");

        if(OPTIONS[name].getPage() == "world_default") {
            world->WORLD_OPTIONS[ name ].setValue( value );
        }
    }

    // for legacy saves, try to simulate old city_size based density
    if( world->WORLD_OPTIONS.count( "CITY_SPACING" ) == 0 ) {
        world->WORLD_OPTIONS["CITY_SPACING"].setValue( 5 - world->WORLD_OPTIONS["CITY_SIZE"] / 3 );
    }

    return true;
}

mod_manager *worldfactory::get_mod_manager()
{
    return mman.get();
}
