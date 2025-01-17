#include "visitable.h"

#include "debug.h"
#include "item.h"
#include "inventory.h"
#include "character.h"
#include "map_selector.h"
#include "vehicle_selector.h"
#include "map.h"
#include "submap.h"
#include "vehicle.h"
#include "veh_type.h"
#include "game.h"
#include "itype.h"
#include "player.h"

template <typename T>
item *visitable<T>::find_parent( const item &it )
{
    item *res = nullptr;
    if( visit_items( [&]( item * node, item * parent ) {
    if( node == &it ) {
            res = parent;
            return VisitResponse::ABORT;
        }
        return VisitResponse::NEXT;
    } ) != VisitResponse::ABORT ) {
        debugmsg( "Tried to find item parent using an object that doesn't contain it" );
    }
    return res;
}

template <typename T>
const item *visitable<T>::find_parent( const item &it ) const
{
    return const_cast<visitable<T> *>( this )->find_parent( it );
}

template <typename T>
std::vector<item *> visitable<T>::parents( const item &it )
{
    std::vector<item *> res;
    for( item *obj = find_parent( it ); obj; obj = find_parent( *obj ) ) {
        res.push_back( obj );
    }
    return res;
}

template <typename T>
std::vector<const item *> visitable<T>::parents( const item &it ) const
{
    std::vector<const item *> res;
    for( const item *obj = find_parent( it ); obj; obj = find_parent( *obj ) ) {
        res.push_back( obj );
    }
    return res;
}

template <typename T>
bool visitable<T>::has_item( const item &it ) const
{
    return visit_items( [&it]( const item * node ) {
        return node == &it ? VisitResponse::ABORT : VisitResponse::NEXT;
    } ) == VisitResponse::ABORT;
}

template <typename T>
bool visitable<T>::has_item_with( const std::function<bool( const item & )> &filter ) const
{
    return visit_items( [&filter]( const item * node ) {
        return filter( *node ) ? VisitResponse::ABORT : VisitResponse::NEXT;
    } ) == VisitResponse::ABORT;
}

template <typename T>
static int has_quality_internal( const T& self, const std::string &qual, int level, int limit )
{
    int qty = 0;

    self.visit_items( [&qual, level, &limit, &qty]( const item *e ) {
        if( e->get_quality( qual ) >= level ) {
            qty += e->count_by_charges() ? e->charges : 1;
            if( qty >= limit ) {
                return VisitResponse::ABORT; // found sufficient items
            }
        }
        return VisitResponse::NEXT;
    } );
    return std::min( qty, limit );
}

static int has_quality_from_vpart( const vehicle& veh, int part, const std::string& qual, int level, int limit )
{
    int qty = 0;

    auto pos = veh.parts[ part ].mount;
    for( const auto &n : veh.parts_at_relative( pos.x, pos.y ) ) {

        // only unbroken parts can provide tool qualities
        if( veh.parts[ n ].hp > 0 ) {
            auto tq = veh.part_info( n ).qualities;
            auto iter = tq.find( qual );

            // does the part provide this quality?
            if( iter != tq.end() && iter->second >= level ) {
                if( ++qty >= limit ) {
                    break;
                }
            }
        }
    }
    return std::min( qty, limit );
}

template <typename T>
bool visitable<T>::has_quality( const std::string &qual, int level, int qty ) const
{
    return has_quality_internal( *this, qual, level, qty ) == qty;
}

template <>
bool visitable<vehicle_selector>::has_quality( const std::string &qual, int level, int qty ) const
{
    for( const auto& cursor : static_cast<const vehicle_selector &>( *this ) ) {
        qty -= has_quality_from_vpart( cursor.veh, cursor.part, qual, level, qty );
        if( qty <= 0 ) {
            return true;
        }
    }
    return has_quality_internal( *this, qual, level, qty ) == qty;
}

template <>
bool visitable<vehicle_cursor>::has_quality( const std::string &qual, int level, int qty ) const
{
    auto self = static_cast<const vehicle_cursor *>( this );

    qty -= has_quality_from_vpart( self->veh, self->part, qual, level, qty );
    return qty <= 0 ? true : has_quality_internal( *this, qual, level, qty ) == qty;
}

template <typename T>
static int max_quality_internal( const T& self, const std::string &qual )
{
    int res = INT_MIN;
    self.visit_items( [&res,&qual]( const item *e ) {
        res = std::max( res, e->get_quality( qual ) );
        return VisitResponse::NEXT;
    } );
    return res;
}

static int max_quality_from_vpart( const vehicle& veh, int part, const std::string& qual )
{
    int res = INT_MIN;

    auto pos = veh.parts[ part ].mount;
    for( const auto &n : veh.parts_at_relative( pos.x, pos.y ) ) {

        // only unbroken parts can provide tool qualities
        if( veh.parts[ n ].hp > 0 ) {
            auto tq = veh.part_info( n ).qualities;
            auto iter = tq.find( qual );

            // does the part provide this quality?
            if( iter != tq.end() ) {
                res = std::max( res, iter->second );
            }
        }
    }
    return res;
}

template <typename T>
int visitable<T>::max_quality( const std::string &qual ) const
{
    return max_quality_internal( *this, qual );
}

template<>
int visitable<Character>::max_quality( const std::string &qual ) const
{
    int res = INT_MIN;

    auto self = static_cast<const Character *>( this );

    if( self->has_bionic( "bio_tools" ) ) {
        res = std::max( res, item( "toolset" ).get_quality( qual ) );
    }

    if( qual == "BUTCHER" ) {
        if( self->has_bionic( "bio_razor" ) || self->has_trait( "CLAWS_ST" ) ) {
            res = std::max( res, 8 );
        } else if( self->has_trait( "TALONS" ) || self->has_trait( "MANDIBLES" ) ||
                   self->has_trait( "CLAWS" ) || self->has_trait( "CLAWS_RETRACT" ) ||
                   self->has_trait( "CLAWS_RAT" ) ) {
            res = std::max( res, 4 );
        }
    }

    return std::max( res, max_quality_internal( *this, qual ) );
}

template <>
int visitable<vehicle_cursor>::max_quality( const std::string &qual ) const
{
    auto self = static_cast<const vehicle_cursor *>( this );
    return std::max( max_quality_from_vpart( self->veh, self->part, qual ),
                     max_quality_internal( *this, qual ) );
}

template <>
int visitable<vehicle_selector>::max_quality( const std::string &qual ) const
{
    int res = INT_MIN;
    for( const auto &e : static_cast<const vehicle_selector &>( *this ) ) {
        res = std::max( res, e.max_quality( qual ) );
    }
    return res;
}

template <typename T>
std::vector<item *> visitable<T>::items_with( const std::function<bool( const item & )> &filter )
{
    std::vector<item *> res;
    visit_items( [&res,&filter]( item * node, item * ) {
        if( filter( *node ) ) {
            res.push_back( node );
        }
        return VisitResponse::NEXT;
    } );
    return res;
}

template <typename T>
std::vector<const item *> visitable<T>::items_with( const std::function<bool( const item & )> &filter ) const
{
    std::vector<const item *> res;
    visit_items( [&res,&filter]( const item * node, const item * ) {
        if( filter( *node ) ) {
            res.push_back( node );
        }
        return VisitResponse::NEXT;
    } );
    return res;
}

template <typename T>
VisitResponse visitable<T>::visit_items(
    const std::function<VisitResponse( const item *, const item * )> &func ) const
{
    return const_cast<visitable<T> *>( this )->visit_items(
               static_cast<const std::function<VisitResponse( item *, item * )>&>( func ) );
}

template <typename T>
VisitResponse visitable<T>::visit_items( const std::function<VisitResponse( const item * )> &func ) const
{
    return const_cast<visitable<T> *>( this )->visit_items(
               static_cast<const std::function<VisitResponse( item * )>&>( func ) );
}

template <typename T>
VisitResponse visitable<T>::visit_items( const std::function<VisitResponse( item * )> &func )
{
    return visit_items( [&func]( item * it, item * ) {
        return func( it );
    } );
}

// Specialize visitable<T>::visit_items() for each class that will implement the visitable interface

static VisitResponse visit_internal( const std::function<VisitResponse( item *, item * )> &func,
                                     item *node, item *parent = nullptr )
{
    switch( func( node, parent ) ) {
        case VisitResponse::ABORT:
            return VisitResponse::ABORT;

        case VisitResponse::NEXT:
            if( node->is_gun() || node->is_magazine() ) {
                // content of guns and magazines are accessible only via their specific accessors
                return VisitResponse::NEXT;
            }

            for( auto &e : node->contents ) {
                if( visit_internal( func, &e, node ) == VisitResponse::ABORT ) {
                    return VisitResponse::ABORT;
                }
            }
        /* intentional fallthrough */

        case VisitResponse::SKIP:
            return VisitResponse::NEXT;
    }

    /* never reached but suppresses GCC warning */
    return VisitResponse::ABORT;
}

template <>
VisitResponse visitable<item>::visit_items(
    const std::function<VisitResponse( item *, item * )> &func )
{
    auto it = static_cast<item *>( this );
    return visit_internal( func, it );
}

template <>
VisitResponse visitable<inventory>::visit_items(
    const std::function<VisitResponse( item *, item * )> &func )
{
    auto inv = static_cast<inventory *>( this );
    for( auto &stack : inv->items ) {
        for( auto &it : stack ) {
            if( visit_internal( func, &it ) == VisitResponse::ABORT ) {
                return VisitResponse::ABORT;
            }
        }
    }
    return VisitResponse::NEXT;
}

template <>
VisitResponse visitable<Character>::visit_items(
    const std::function<VisitResponse( item *, item * )> &func )
{
    auto ch = static_cast<Character *>( this );

    if( !ch->weapon.is_null() &&
        visit_internal( func, &ch->weapon ) == VisitResponse::ABORT ) {
        return VisitResponse::ABORT;
    }

    for( auto &e : ch->worn ) {
        if( visit_internal( func, &e ) == VisitResponse::ABORT ) {
            return VisitResponse::ABORT;
        }
    }

    return ch->inv.visit_items( func );
}

template <>
VisitResponse visitable<map_cursor>::visit_items(
    const std::function<VisitResponse( item *, item * )> &func )
{
    auto cur = static_cast<map_cursor *>( this );

    for( auto &e : g->m.i_at( *cur ) ) {
        if( visit_internal( func, &e ) == VisitResponse::ABORT ) {
            return VisitResponse::ABORT;
        }
    }
    return VisitResponse::NEXT;
}

template <>
VisitResponse visitable<map_selector>::visit_items(
    const std::function<VisitResponse( item *, item * )> &func )
{
    for( auto &cursor : static_cast<map_selector &>( *this ) ) {
        if( cursor.visit_items( func ) == VisitResponse::ABORT ) {
            return VisitResponse::ABORT;
        }
    }
    return VisitResponse::NEXT;
}

template <>
VisitResponse visitable<vehicle_cursor>::visit_items(
    const std::function<VisitResponse( item *, item * )> &func )
{
    auto self = static_cast<vehicle_cursor *>( this );

    int idx = self->veh.part_with_feature( self->part, "CARGO" );
    if( idx >= 0 ) {
        for( auto &e : self->veh.get_items( idx ) ) {
            if( visit_internal( func, &e ) == VisitResponse::ABORT ) {
                return VisitResponse::ABORT;
            }
        }
    }
    return VisitResponse::NEXT;
}

template <>
VisitResponse visitable<vehicle_selector>::visit_items(
    const std::function<VisitResponse( item *, item * )> &func )
{
    for( auto &cursor : static_cast<vehicle_selector &>( *this ) ) {
        if( cursor.visit_items( func ) == VisitResponse::ABORT ) {
            return VisitResponse::ABORT;
        }
    }
    return VisitResponse::NEXT;
}

// Specialize visitable<T>::remove_items_with() for each class that will implement the visitable interface

template <typename T>
item visitable<T>::remove_item( item& it ) {
    auto obj = remove_items_with( [&it]( const item& e ) { return &e == &it; }, 1 );
    if( !obj.empty() ) {
        return obj.front();

    } else {
        debugmsg( "Tried removing item from object which did not contain it" );
        return item();
    }
}

template <typename OutputIterator>
static void remove_internal( const std::function<bool( item & )> &filter, item &node, int &count,
                             OutputIterator out )
{
    for( auto it = node.contents.begin(); it != node.contents.end(); ) {
        if( filter( *it ) ) {
            out = std::move( *it );
            it = node.contents.erase( it );
            if( --count == 0 ) {
                return;
            }
        } else {
            remove_internal( filter, *it, count, out );
            ++it;
        }
    }
}

template <>
std::list<item> visitable<item>::remove_items_with( const std::function<bool( const item &e )>
        &filter, int count )
{
    auto it = static_cast<item *>( this );
    std::list<item> res;

    if( count <= 0 ) {
        return res; // nothing to do
    }

    remove_internal( filter, *it, count, std::back_inserter( res ) );
    return res;
}


template <>
std::list<item> visitable<inventory>::remove_items_with( const
        std::function<bool( const item &e )> &filter, int count )
{
    auto inv = static_cast<inventory *>( this );
    std::list<item> res;

    if( count <= 0 ) {
        return res; // nothing to do
    }

    for( auto stack = inv->items.begin(); stack != inv->items.end(); ) {
        // all items in a stack are identical so we only need to call the predicate once
        if( filter( stack->front() ) ) {

            if( count >= int( stack->size() ) ) {
                // remove the entire stack
                count -= stack->size();
                res.splice( res.end(), *stack );
                stack = inv->items.erase( stack );
                if( count == 0 ) {
                    return res;
                }

            } else {
                // remove only some of the stack
                char invlet = stack->front().invlet;
                auto fin = stack->begin();
                std::advance( fin, count );
                res.splice( res.end(), *stack, stack->begin(), fin );
                stack->front().invlet = invlet; // preserve invlet for remaining stacked items
                return res;
            }

        } else {
            // recurse through the contents of each stacked item separately
            for( auto &e : *stack ) {
                remove_internal( filter, e, count, std::back_inserter( res ) );
                if( count == 0 ) {
                    return res;
                }
            }

            ++stack;
        }
    }
    return res;
}

template <>
std::list<item> visitable<Character>::remove_items_with( const
        std::function<bool( const item &e )> &filter, int count )
{
    auto ch = static_cast<Character *>( this );
    std::list<item> res;

    if( count <= 0 ) {
        return res; // nothing to do
    }

    // first try and remove items from the inventory
    res = ch->inv.remove_items_with( filter, count );
    count -= res.size();
    if( count == 0 ) {
        return res;
    }

    // then try any worn items
    for( auto iter = ch->worn.begin(); iter != ch->worn.end(); ) {
        if( filter( *iter ) ) {
            res.splice( res.end(), ch->worn, iter++ );
            if( --count == 0 ) {
                return res;
            }
        } else {
            remove_internal( filter, *iter, count, std::back_inserter( res ) );
            if( count == 0 ) {
                return res;
            }
            ++iter;
        }
    }

    // finally try the currently wielded item (if any)
    if( filter( ch->weapon ) ) {
        res.push_back( ch->remove_weapon() );
        count--;
    } else {
        remove_internal( filter, ch->weapon, count, std::back_inserter( res ) );
    }

    return res;
}

template <>
std::list<item> visitable<map_cursor>::remove_items_with( const
        std::function<bool( const item &e )> &filter, int count )
{
    auto cur = static_cast<map_cursor *>( this );
    std::list<item> res;

    if( count <= 0 ) {
        return res; // nothing to do
    }

    if( !g->m.inbounds( *cur ) ) {
        debugmsg( "cannot remove items from map: cursor out-of-bounds" );
        return res;
    }

    // fetch the appropriate item stack
    int x, y;
    submap *sub = g->m.get_submap_at( *cur, x, y );

    for( auto iter = sub->itm[ x ][ y ].begin(); iter != sub->itm[ x ][ y ].end(); ) {
        if( filter( *iter ) ) {
            // check for presence in the active items cache
            if( sub->active_items.has( iter, point( x, y ) ) ) {
                sub->active_items.remove( iter, point( x, y ) );
            }

            // if necessary remove item from the luminosity map
            sub->update_lum_rem( *iter, x, y );

            // finally remove the item
            res.splice( res.end(), sub->itm[ x ][ y ], iter++ );

            if( --count == 0 ) {
                return res;
            }
        } else {
            remove_internal( filter, *iter, count, std::back_inserter( res ) );
            if( count == 0 ) {
                return res;
            }
            ++iter;
        }
    }
    return res;
}

template <>
std::list<item> visitable<map_selector>::remove_items_with( const
        std::function<bool( const item &e )> &filter, int count )
{
    std::list<item> res;

    for( auto &cursor : static_cast<map_selector &>( *this ) ) {
        std::list<item> out = cursor.remove_items_with( filter, count );
        count -= out.size();
        res.splice( res.end(), out );
    }

    return res;
}

template <>
std::list<item> visitable<vehicle_cursor>::remove_items_with( const
        std::function<bool( const item &e )> &filter, int count )
{
    auto cur = static_cast<vehicle_cursor *>( this );
    std::list<item> res;

    if( count <= 0 ) {
        return res; // nothing to do
    }

    int idx = cur->veh.part_with_feature( cur->part, "CARGO" );
    if( idx < 0 ) {
        return res;
    }

    vehicle_part& part = cur->veh.parts[ idx ];
    for( auto iter = part.items.begin(); iter != part.items.end(); ) {
        if( filter( *iter ) ) {
            // check for presence in the active items cache
            if( cur->veh.active_items.has( iter, part.mount ) ) {
                cur->veh.active_items.remove( iter, part.mount );
            }
            res.splice( res.end(), part.items, iter++ );
            if( --count == 0 ) {
                return res;
            }
        } else {
            remove_internal( filter, *iter, count, std::back_inserter( res ) );
            if( count == 0 ) {
                return res;
            }
            ++iter;
        }
    }

    if( !res.empty() ) {
        // if we removed any items then invalidate the cached mass
        cur->veh.invalidate_mass();
    }

    return res;
}

template <>
std::list<item> visitable<vehicle_selector>::remove_items_with( const
        std::function<bool( const item &e )> &filter, int count )
{
    std::list<item> res;

    for( auto &cursor : static_cast<vehicle_selector &>( *this ) ) {
        std::list<item> out = cursor.remove_items_with( filter, count );
        count -= out.size();
        res.splice( res.end(), out );
    }

    return res;
}

template <typename T>
static long charges_of_internal( const T& self, const itype_id& id, int limit )
{
    long qty = 0;

    self.visit_items( [&]( const item *e ) {
        if( e->is_tool() ) {
            // for tools we also need to check if this item is a subtype of the required id
            if( e->typeId() == id || ( e->is_tool() && e->type->tool->subtype == id ) ) {
                qty += e->ammo_remaining(); // includes charges from any contained magazine
            }
            return qty < limit ? VisitResponse::SKIP : VisitResponse::ABORT;

        } else if( e->count_by_charges() ) {
            if( e->typeId() == id ) {
                qty += e->charges;
            }
            // items counted by charges are not themselves expected to be containers
            return qty < limit ? VisitResponse::SKIP : VisitResponse::ABORT;
        }

        // recurse through any nested containers
        return qty < limit ? VisitResponse::NEXT : VisitResponse::ABORT;
    } );

    return std::min( qty, long( limit ) );
}

template <typename T>
long visitable<T>::charges_of( const std::string &what, int limit ) const
{
    return charges_of_internal( *this, what, limit );
}

template <>
long visitable<Character>::charges_of( const std::string &what, int limit ) const
{
    auto self = static_cast<const Character *>( this );
    auto p = dynamic_cast<const player *>( self );

    if( what == "toolset") {
        if( p && p->has_active_bionic( "bio_tools" ) ) {
            return std::min( p->power_level, limit );
        } else {
            return 0;
        }
    }

    if( what == "UPS" ) {
        long qty = 0;
        qty += charges_of( "UPS_off" );
        qty += charges_of( "adv_UPS_off" ) / 0.6;
        if ( p && p->has_active_bionic( "bio_ups" ) ) {
            qty += p->power_level * 10;
        }
        return std::min( qty, long( limit ) );
    }

    return charges_of_internal( *this, what, limit );
}

template <typename T>
static int amount_of_internal( const T& self, const itype_id& id, bool pseudo, int limit )
{
    int qty = 0;
    self.visit_items( [&qty, &id, &pseudo, &limit] ( const item *e ) {
        qty += ( e->typeId() == id && e->contents.empty() && ( pseudo || !e->has_flag( "PSEUDO" ) ) );
        return qty != limit ? VisitResponse::NEXT : VisitResponse::ABORT;
    } );
    return qty;
}

template <typename T>
int visitable<T>::amount_of( const std::string& what, bool pseudo, int limit ) const
{
    return amount_of_internal( *this, what, pseudo, limit );
}

template <>
int visitable<Character>::amount_of( const std::string& what, bool pseudo, int limit ) const
{
    auto self = static_cast<const Character *>( this );

    if( what == "toolset" && pseudo && self->has_active_bionic( "bio_tools" ) ) {
        return 1;
    }

    if( what == "apparatus" && pseudo ) {
        int qty = 0;
        visit_items( [&qty, &limit] ( const item *e ) {
            qty += e->get_quality( "SMOKE_PIPE" ) >= 1;
            return qty < limit ? VisitResponse::SKIP : VisitResponse::ABORT;
        } );
        return std::min( qty, limit );
    }

    return amount_of_internal( *this, what, pseudo, limit );
}

// explicit template initialization for all classes implementing the visitable interface
template class visitable<item>;
template class visitable<inventory>;
template class visitable<Character>;
template class visitable<map_selector>;
template class visitable<map_cursor>;
template class visitable<vehicle_selector>;
template class visitable<vehicle_cursor>;
