#include <glib.h>

#include "item.h"
#include "attr.h"
#include "navit.h"
#include "xmlconfig.h" // for NAVIT_OBJECT
#include "layout.h"
#include "map.h"
#include "transform.h"
#include "backend.h"

#include "qml_map.h"
#include "qml_poi.h"
#include "qml_search.h"

#include "mapset.h"

#include <QQmlContext>

#include "search.h"

extern "C" {
#include "proxy.h"
}


Backend::Backend(QObject * parent):QObject(parent)
{
}

/**
 * @brief Set some variables and display the main menu
 * @param struct point *p the point coordinate where we clicked on the screen
 * @returns nothing
 */
void Backend::showMenu(struct point *p)
{
        struct coord co;

        transform_reverse(navit_get_trans(nav), p, &co);
        dbg(lvl_debug, "Point 0x%x 0x%x\n", co.x, co.y);
        dbg(lvl_debug, "Screen coord : %d %d\n", p->x, p->y);
        transform_to_geo(transform_get_projection(navit_get_trans(nav)), &co, &(this->g));
        dbg(lvl_debug, "%f %f\n", this->g.lat, this->g.lng);
        dbg(lvl_debug, "%p %p\n", nav, &c);
        this->c.pro = transform_get_projection(navit_get_trans(nav));
        this->c.x = co.x;
        this->c.y = co.y;
        dbg(lvl_debug, "c : %x %x\n", this->c.x, this->c.y);

        // As a test, set the Demo vehicle position to wherever we just clicked
        navit_set_position(this->nav, &c);
        emit displayMenu();
}

/**
 * @brief update the private m_maps list. Expected to be called from QML
 * @param none
 * @returns nothing
 */ 
void Backend::get_maps()
{
        struct attr attr, on, off, description, type, data, active;
        char * label;
        bool is_active;
        struct attr_iter * iter;
        _maps.clear();

        iter = navit_attr_iter_new();
        on.type = off.type = attr_active;
        on.u.num = 1;
        off.u.num = 0;
        while (navit_get_attr(this->nav, attr_map, &attr, iter)) {
                if (map_get_attr(attr.u.map, attr_description, &description, NULL)) {
                        label = g_strdup(description.u.str);
                } else {
                        if (!map_get_attr(attr.u.map, attr_type, &type, NULL))
                                type.u.str = "";
                        if (!map_get_attr(attr.u.map, attr_data, &data, NULL))
                                data.u.str = "";
                        label = g_strdup_printf("%s:%s", type.u.str, data.u.str);
                }
                is_active = false;
                if (map_get_attr(attr.u.map, attr_active, &active, NULL)) {
                        if (active.u.num == 1) {
                                is_active = true;
                        }
                }
                _maps.append(new MapObject(label, is_active));
        }
        emit mapsChanged();
}

/**
 * @brief set a pointer to the struct navit * for local use
 * @param none
 * @returns nothing
 */ 
void Backend::set_navit(struct navit *nav)
{
        this->nav = nav;
}

/**
 * @brief set a pointer to the QQmlApplicationEngine * for local use
 * @param none
 * @returns nothing
 */ 
void Backend::set_engine(QQmlApplicationEngine * engine)
{
        this->engine = engine;
}

/**
 * @brief apply search filters on one specific item
 * @param struct item * the item to filter
 * @returns 0 if the item should be discarded, 1 otherwise
 */ 
int Backend::filter_pois(struct item *item)
{
        enum item_type *types;
        enum item_type type=item->type;
        if (type >= type_line)
                return 0;
        return 1;
}

/**
 * @brief update the private m_pois list. Expected to be called from QML
 * @param none
 * @returns nothing
 */ 
void Backend::get_pois()
{
        struct map_selection * sel, * selm;
        struct coord c, center;
        struct mapset_handle * h;
        struct map * m;
        struct map_rect * mr;
        struct item * item;
        enum projection pro = this->c.pro;
        int idist, dist;
        _pois.clear();
        dist = 10000;
        sel = map_selection_rect_new(&(this->c), dist * transform_scale(abs(this->c.y) + dist * 1.5), 18);
        center.x = this->c.x;
        center.y = this->c.y;

        dbg(lvl_debug, "center is at %x, %x\n", center.x, center.y);

        h = mapset_open(navit_get_mapset(this->nav));
        while ((m = mapset_next(h, 1))) {
                selm = map_selection_dup_pro(sel, pro, map_projection(m));
                mr = map_rect_new(m, selm);
                dbg(lvl_debug, "mr=%p\n", mr);
                if (mr) {
                        while ((item = map_rect_get_item(mr))) {
                                if ( filter_pois(item) &&
                                                item_coord_get_pro(item, &c, 1, pro) &&
                                                coord_rect_contains(&sel->u.c_rect, &c)  &&
                                                (idist=transform_distance(pro, &center, &c)) < dist) {

                                        struct attr attr;
                                        char * label;
                                        char * icon = get_icon(this->nav, item);
                                        struct pcoord item_coord;
                                        item_coord.pro = transform_get_projection(navit_get_trans(nav));
                                        item_coord.x = c.x;
                                        item_coord.y = c.y;

                                        idist = transform_distance(pro, &center, &c);
                                        if (item_attr_get(item, attr_label, &attr)) {
                                                label = map_convert_string(item->map, attr.u.str);
                                                if (icon) {
                                                        _pois.append(new PoiObject(label, item_to_name(item->type), idist, icon, item_coord));
                                                }
                                        }
                                }
                        }
                        map_rect_destroy(mr);
                }
                map_selection_destroy(selm);
        }
        map_selection_destroy(sel);
        mapset_close(h);
        emit poisChanged();
}

/**
 * @brief get the POIs as a QList
 * @param none
 * @returns the pois QList
 */ 
QQmlListProperty<QObject> Backend::getPois(){
        return QQmlListProperty<QObject>(this, _pois);
}

/**
 * @brief get the maps as a QList
 * @param none
 * @returns the maps QList
 */ 
QQmlListProperty<QObject> Backend::getMaps(){
        return QQmlListProperty<QObject>(this, _maps);
}

/**
 * @brief get the search results as a QList
 * @param none
 * @returns the search results QList
 */ 
QQmlListProperty<QObject> Backend::getSearchResults(){
        return QQmlListProperty<QObject>(this, _search_results);
}

/**
 * @brief get the active POI. Used when displaying the relevant menu
 * @param none
 * @returns the active POI
 */ 
PoiObject * Backend::activePoi() {
        dbg(lvl_debug, "name : %s\n", m_activePoi->name().toUtf8().data());
        dbg(lvl_debug, "type : %s\n", m_activePoi->type().toLatin1().data());
        return m_activePoi;
}


/**
 * @brief set the canvas size to use when drawing the map
 * @param int width
 * @param int height
 * @returns nothing
 */ 
void Backend::resize(int width, int height){
        navit_handle_resize(nav, width, height);
}

/**
 * @brief set the active POI. Used when clicking on a POI list to display one single POI
 * @param int index the index of the POI in the m_pois list
 * @returns nothing
 */ 
void Backend::setActivePoi(int index) {
        struct pcoord c;
        m_activePoi = (PoiObject *)_pois.at(index);
        c = m_activePoi->coords();
        resize(320, 240);
        navit_set_center(this->nav, &c, 1);
        emit activePoiChanged();
}

/**
 * @brief returns the icon (xpm) absolute path
 * @param none
 * @returns the icon (xpm) absolute path as a QString
 */ 
QString Backend::get_icon_path(){
        return QString(g_strjoin(NULL,"file://",getenv("NAVIT_SHAREDIR"),"/xpm/",NULL));
}

/**
 * @brief set the destination using the currently active POI's coordinates
 * @param none
 * @returns nothing
 */ 
void Backend::setActivePoiAsDestination(){
        struct pcoord c;
        c = m_activePoi->coords();
        dbg(lvl_debug, "Destination : %s c=%d:0x%x,0x%x\n",
                        m_activePoi->name().toUtf8().data(),
                        c.pro, c.x, c.y);
        navit_set_destination(this->nav, &c,  m_activePoi->name().toUtf8().data(), 1);
        emit hideMenu();
}

/**
 * @brief set the view to the town at the given index in the m_search_results list
 * @param int index the index of the result in the m_search_results list
 * @returns nothing
 */ 
void Backend::gotoTown(int index){
        SearchObject * r = (SearchObject *)_search_results.at(index);
        dbg(lvl_debug, "Going to %s [%i] %x %x\n", 
                        r->name().toUtf8().data(),
                        index, r->getCoords()->x, r->getCoords()->y);
        navit_set_center(this->nav, r->getCoords(), 1);
        emit hideMenu();
}

/**
 * @brief get the icon that matches the country currently used for searches
 * @param none
 * @returns an absolute path for the country icon
 */ 
QString Backend::get_country_icon(){
        if ( _country_iso2 == NULL ){
                _country_iso2 = "DE";
        }
        return QString(g_strjoin(NULL,"file://",getenv("NAVIT_SHAREDIR"),"/xpm/country_",_country_iso2,".svgz",NULL));
}


static struct search_param {
        struct navit *nav;
        struct mapset *ms;
        struct search_list *sl;
        struct attr attr;
        int partial;
        void *entry_country, *entry_postal, *entry_city, *entry_district;
        void *entry_street, *entry_number;
} search_param;


/**
 * @brief update the current search results according to new inputs. Currently only works to search for towns
 * @param QString text the text to search for
 * @returns nothing
 */ 
void Backend::updateSearch(QString text){
        struct search_param *search=&search_param;
        struct search_list_result *res;
        _search_results.clear();
        //  search->attr.type=attr_country_all;
        //  search->attr.type=attr_town_postal;
        //  search->attr.type=attr_town_name;
        //  search->attr.type=attr_street_name;

        search->attr.type=attr_town_name;
        search->nav=this->nav;
        search->ms=navit_get_mapset(this->nav);
        search->sl=search_list_new(search->ms);
        search->partial = 1;

        struct attr search_attr;

        if ( _country_iso2 == NULL ){
                _country_iso2 = "DE";
        }

        dbg(lvl_debug,"attempting to use country '%s'\n", _country_iso2);
        search_attr.type=attr_country_iso2;
        search_attr.u.str=_country_iso2;
        search_list_search(search->sl, &search_attr, 0);
        while((res=search_list_get_result(search->sl)));

        search->attr.u.str = text.toUtf8().data();
        dbg(lvl_debug, "searching for %s partial %d\n", search->attr.u.str, search->partial);

        search_list_search(search->sl, &search->attr, search->partial);
        int count = 0;
        while((res=search_list_get_result(search->sl))) {
                dbg(lvl_debug, "res %p\n", res);
                if (res->country) {
                        dbg(lvl_debug, "country : '%s'\n", res->country->name);
                }
                if (res->town) {
                        char * label;
                        label = g_strdup(res->town->common.town_name);
                        dbg(lvl_debug, "got result %s\n", label);
                        _search_results.append(
                                        new SearchObject(label, "icons/bigcity.png", res->c)
                                        );
                }
                if (res->street) {
                        dbg(lvl_debug, "street\n");
                }
                if (count ++ > 50) {
                        break;
                }
        }
        emit searchResultsChanged();
}
