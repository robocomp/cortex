#include <dsr/gui/dsr_gui.h>
#include <cppitertools/range.hpp>
#include <QApplication>
#include <dsr/gui/viewers/graph_viewer/graph_node.h>
#include <dsr/gui/viewers/graph_viewer/graph_edge.h>
#include <dsr/gui/viewers/graph_viewer/graph_viewer.h>
#include <QMessageBox>
#include <graphviz/cgraph.h>
#include <graphviz/gvc.h>
#include <graphviz/types.h>
#include <qglobal.h>
#include <cmath>
#include <deque>
#include <string>

using namespace DSR ;

GraphViewer::GraphViewer(std::shared_ptr<DSR::DSRGraph> G_, QWidget *parent) :  AbstractGraphicViewer(parent)
{
    qRegisterMetaType<std::int32_t>("std::int32_t");
    qRegisterMetaType<std::uint32_t>("std::uint32_t");
    qRegisterMetaType<std::uint64_t>("std::uint64_t");
    qRegisterMetaType<uint64_t>("uint64_t");
    qRegisterMetaType<std::string>("std::string");
    G = std::move(G_);
	own = std::shared_ptr<GraphViewer>(this);

    graphviz_context = gvContext();
    graphviz_graph =  agopen((char*)"G", Agdirected, nullptr);

    contextMenu = new QMenu(this);
    showMenu = contextMenu->addMenu(tr("&Show:"));
    // The way back after a manual zoom/pan: refit everything and re-arm automatic refitting.
    contextMenu->addAction(tr("&Fit graph to view"), this, [this]() { fit_graph_to_view(); });

    createGraph();

	update_scene_rect();

	this->fitInView(scene.itemsBoundingRect(), Qt::KeepAspectRatio );

	central_point = new QGraphicsEllipseItem(0,0,0,0);
	scene.addItem(central_point);

	connect(G.get(), &DSR::DSRGraph::update_node_signal, this, &GraphViewer::add_or_assign_node_SLOT, Qt::QueuedConnection);
	connect(G.get(), &DSR::DSRGraph::update_edge_signal, this, &GraphViewer::add_or_assign_edge_SLOT, Qt::QueuedConnection);
	connect(G.get(), &DSR::DSRGraph::del_edge_signal, this, &GraphViewer::del_edge_SLOT, Qt::QueuedConnection);
	connect(G.get(), &DSR::DSRGraph::del_node_signal, this, &GraphViewer::del_node_SLOT, Qt::QueuedConnection);

	// Coalesced refit: one fitInView per burst of updates instead of one per update.
	refit_timer_.setSingleShot(true);
	connect(&refit_timer_, &QTimer::timeout, this, [this]()
	{
		update_scene_rect();
		this->fitInView(scene.itemsBoundingRect(), Qt::KeepAspectRatio);
	});
}

// The scrollable area. QGraphicsView clamps the scrollbars to sceneRect, and AbstractGraphicViewer
// pans by driving those scrollbars -- so sceneRect IS the pan limit. Setting it to
// itemsBoundingRect() (what this class used to do everywhere) makes the drag stop dead as soon as
// the graph's own bounding box reaches the viewport edge: the scene slides a little and then
// refuses, which is not a pan.
//
// Pad by a full viewport on every side, floored at the graph's own size, so there is always
// somewhere left to scroll and a node can be dragged right off-screen if that is what the user
// wants. The padding is derived from the viewport size and the ZOOM only -- never from the current
// scroll position -- because feeding the visible rect back in would let sceneRect and the scrollbar
// range chase each other. fitInView() keeps using the TIGHT rect, so framing still frames the graph
// and not the padding.
void GraphViewer::update_scene_rect()
{
	const QRectF items = scene.itemsBoundingRect();
	if (items.isNull())
		return;
	const qreal sx = std::abs(transform().m11()) > 1e-9 ? std::abs(transform().m11()) : 1.0;
	const qreal sy = std::abs(transform().m22()) > 1e-9 ? std::abs(transform().m22()) : 1.0;
	const qreal pad_x = std::max(items.width(),  viewport()->width()  / sx);
	const qreal pad_y = std::max(items.height(), viewport()->height() / sy);
	scene.setSceneRect(items.adjusted(-pad_x, -pad_y, pad_x, pad_y));
}

void GraphViewer::schedule_refit()
{
	// Once the user has zoomed or panned, the view is THEIRS: never refit behind their back. A DSR
	// graph is under constant attribute churn (agent heartbeats, robot pose, FSM state), so this is
	// called several times a second for ever — each refit snapped the viewport back to the whole
	// scene, which made zooming impossible. fit_graph_to_view() is the explicit way back.
	if (user_framed_)
		return;
	if (not refit_timer_.isActive())
		refit_timer_.start(150);   // ≤ ~6-7 refits/s regardless of graph update rate
}

// Zooming or panning by hand hands the viewport to the user. Both defer to the base class for the
// actual transform; all we add is the latch.
void GraphViewer::wheelEvent(QWheelEvent *event)
{
	user_framed_ = true;
	AbstractGraphicViewer::wheelEvent(event);
}

void GraphViewer::showEvent(QShowEvent *event)
{
	// THIS is the path that made the earlier gates look ineffective: the base class refits on every
	// show, and a docked/tabbed graph view is shown far more often than "once at startup" suggests —
	// enough that a zoom was undone immediately. Skip straight to QGraphicsView (the base-of-base) so
	// the widget is shown without any refit.
	if (user_framed_)
	{
		QGraphicsView::showEvent(event);
		return;
	}
	AbstractGraphicViewer::showEvent(event);
}

void GraphViewer::mouseMoveEvent(QMouseEvent *event)
{
	if (_pan)                 // a drag in progress, not just the pointer crossing the view
		user_framed_ = true;
	AbstractGraphicViewer::mouseMoveEvent(event);
}

void GraphViewer::fit_graph_to_view()
{
	user_framed_ = false;
	update_scene_rect();
	this->fitInView(scene.itemsBoundingRect(), Qt::KeepAspectRatio);
}


GraphViewer::~GraphViewer()
{
    qDebug() << __FUNCTION__ << "Destroy";
    qDebug()  << "GraphViewer: " << G.use_count();
    //G.reset();
    gmap.clear();
	gmap_edges.clear();
    type_id_map.clear();
	QList<QGraphicsItem*> allGraphicsItems = scene.items();
	for(int i = 0; i < allGraphicsItems.size(); i++)
	{
		QGraphicsItem *graphicItem = allGraphicsItems[i];
		if(graphicItem->scene() == &scene){
			scene.removeItem(graphicItem);
		}

	}
	scene.clear();
    agclose(graphviz_graph);
    gvFreeContext(graphviz_context);
}

void GraphViewer::createGraph()
{
	gmap.clear();
	gmap_edges.clear();
    type_id_map.clear();
	// `collapsed_parents` deliberately survives a reload so the user's folded/unfolded choice is
	// not lost; the index it refers to is rebuilt below from the incoming RT edges.
	collapsible_children.clear();
	this->scene.clear();
	qDebug() << __FUNCTION__ << "Reading graph in Graph Viewer";
    try
    {
        std::set<std::string> type_list;
        auto map = G->getCopy();
		for(const auto &[k, node] : map) {
            add_or_assign_node_SLOT(k, node.type());
            //context menu
            if (type_list.find(node.type()) == type_list.end()) {
                QAction *action = new QAction(QString::fromStdString(node.type()));
                action->setCheckable(true);
                action->setChecked(true);
                showMenu->addAction(action);
                std::string type = node.type();
                connect(action, &QAction::toggled, this, [this, type](bool visible){
                    std::cout<<"hide/show";
                    for(auto id : type_id_map[type])
                        hide_show_node_SLOT(id, visible);
                });
                type_list.insert(node.type());
                type_id_map.insert(std::pair<std::string, std::set<std::uint64_t>>(node.type(), {node.id()}));
            } else
                type_id_map[node.type()].insert(node.id());
        }
		for(auto &[id, node] : map)
           	for(const auto &[k, edges] : node.fano())
			   add_or_assign_edge_SLOT(edges.from(), edges.to(), edges.type());
		// Re-fold whatever the user had folded before the reload. Done once the whole scene exists
		// so the subtree walk below sees every RT edge.
		for(const auto &[parent_id, children] : collapsible_children)
			apply_collapse_state(parent_id);
    }
	catch(const std::exception &e) { std::cout << e.what() << " Error accessing "<< __FUNCTION__<<":"<<__LINE__<< std::endl;}
}


///////////////////////////////////////

void GraphViewer::toggle_animation(bool animate)
{
	qDebug() << "timerId " << timerId ;
	if(animate)
	{
		if(timerId == 0)
	   		timerId = startTimer(1000 / 25);
	}
	else
	{
		killTimer(timerId);
		timerId = 0;
	}
}

void GraphViewer::timerEvent(QTimerEvent *event)
{
	// Q_UNUSED(event)

	for( auto &[_,node] : gmap)
	{
		node->calculateForces();
	}
	bool itemsMoved = false;

	for( auto &[_,node] : gmap)
	{
		itemsMoved = node->advancePosition() or itemsMoved;
	}
	if (!itemsMoved)
	{
		killTimer(timerId);
		timerId = 0;
	}
}
//////////////////////////////////////////////////////////////////////////////////////
///// SLOTS
//////////////////////////////////////////////////////////////////////////////////////
void GraphViewer::add_or_assign_node_SLOT(uint64_t id, const std::string &type)
{
    static std::random_device rd;
    static std::mt19937 mt(rd());
    static std::uniform_real_distribution<double> unif_dist(-300, 300);
    //std::cout << "[SLOT] Insert node:  "<<id<< std::endl;

    GraphNode *gnode;
	std::optional<Node> n = G->get_node(id);
    if (n.has_value())
    {
        auto &name = n->name();
        if (gmap.count(id) == 0)    // if node does not exist, create it
        {
            qDebug()<<__FUNCTION__<<"##### New node";
            gnode = this->new_visual_node(id, type, name, false);
            gmap.insert(std::pair(id, gnode));

			gnode->setType(type);   // seeds the per-type default colour (node_colors.h)
            auto id_str = std::to_string(id);
            Agnode_t* gvnode = agnode(graphviz_graph, id_str.data(), 1);
            agset(gvnode, (char*)"width", (char*)"1.5");
            agset(gvnode, (char*)"height", (char*)"1.5");
            agset(gvnode, (char*)"shape", (char*)"box");
            agset(gvnode, (char*)"fixedsize", (char*)"true");
        }
        else
		{
			qDebug()<<__FUNCTION__<<"##### Updated node";
            gnode = gmap.at(id);
		}
		// Agent nodes report live health by rewriting their `color` attribute (see
		// rc::AgentStatePublisher), so theirs must be re-read on EVERY update, not just at creation
		// — an attribute-only change is what repaints them.
		//
		// Deliberately scoped to type "agent". Honouring `color` for every node type looks more
		// principled, but the graphs in this project carry stale/unparseable colour attributes that
		// had been dead data for years (setType ran last and always overrode them), so switching
		// them on turned root/mind/Shadow/body black. Node colouring for everything else stays where
		// it was: node_colors.h, keyed by type.
		if (type == "agent")
			if (const auto color = G->get_attrib_by_name<color_att>(n.value()); color.has_value())
				gnode->set_color(color.value().get());
		gnode->change_detected();
        float posx, posy;
        if(auto px = G->get_attrib_by_name<pos_x_att>(n.value()); px.has_value())
            posx = px.value();
        else
            posx = unif_dist(mt);
        if(auto py = G->get_attrib_by_name<pos_y_att>(n.value()); py.has_value())
            posy = py.value();
        else
            posy = unif_dist(mt);
        // Avoid to move if it's in the same position or if the node is grabbed
        if ((posx != gnode->x() or posy != gnode->y()) and gnode != scene.mouseGrabberItem()) {
			qDebug()<<__FUNCTION__<<"##### posx "<<posx<<" != gnode->x() "<<gnode->x()<<" or posy "<<posy<<" != gnode->y() "<<gnode->y();
			gnode->setPos(posx, posy);
		}
        //emit G->update_node_attr_signal(id, {});
        for(const auto &[k, edges] : (*n).fano())
        {
            auto key = std::make_tuple(edges.from(), edges.to(), edges.type());
		    if (gmap_edges.contains(key) && gmap_edges[key] == nullptr) 
                add_or_assign_edge_SLOT(edges.from(), edges.to(), edges.type());
        }
    }

	schedule_refit();
}

GraphNode* GraphViewer::new_visual_node(uint64_t id, const std::string &type, const std::string &name, bool debug)
{
    GraphNode *gnode = new GraphNode(own);
    gnode->id_in_graph = id;
    gnode->setType(type);
    std::string tag = name;
    tag += debug?" [" + std::to_string(id) + "]":"";
    gnode->setTag(tag);
    scene.addItem(gnode);
    // connect delete signal
    QObject::connect(gnode, &GraphNode::del_node_signal, this, &GraphViewer::remove_node_SLOT, Qt::QueuedConnection);
    // re-emit "view data" requests (media-plane nodes) up to whoever holds the viewer (the agent)
    QObject::connect(gnode, &GraphNode::view_data_signal, this, &GraphViewer::view_data_signal, Qt::QueuedConnection);
    // "+"/"-" badge: the node only reports the click, the viewer owns the parent/child index
    QObject::connect(gnode, &GraphNode::toggle_children_signal, this, &GraphViewer::toggle_children_SLOT, Qt::QueuedConnection);
    return gnode;
}

void GraphViewer::add_or_assign_edge_SLOT(std::uint64_t from, std::uint64_t to, const std::string &edge_tag)
{
	try
    {
        //std::cout << "[SLOT] Insert edge:  "<<from << ", " << to << ", "<< edge_tag<< std::endl;
 		qDebug() << __FUNCTION__ << "edge id " << QString::fromStdString(edge_tag) << from << to;
		std::tuple<std::uint64_t, std::uint64_t, std::string> key = std::make_tuple(from, to, edge_tag);
        if ( G->get_edge(from, to, edge_tag).has_value() )
        {
            if (gmap_edges.count(key) == 0 || gmap_edges[key] == nullptr)
            {
                auto item = this->new_visual_edge(from, to, edge_tag);
                gmap_edges.insert(std::make_pair(key, item));

                auto from_str = std::to_string(from);
                auto to_str = std::to_string(to);
                Agnode_t *gvnode_f = agfindnode(graphviz_graph, from_str.data());
                Agnode_t *gvnode_t = agfindnode(graphviz_graph, to_str.data());

                Agedge_t* gvedge = agedge(graphviz_graph, gvnode_f, gvnode_t, nullptr, 1);
                agset(gvedge, (char *)"type", (char *)edge_tag.data());

                note_parent_edge(from, to, edge_tag, true);
                // A child inserted while its parent is folded must come up hidden, not visible
                apply_collapse_state(from);
            }
            if (gmap_edges[key]) gmap_edges[key]->change_detected();
        }
	}
	catch(const std::exception &e) {
		std::cout << e.what() <<" Error  "<<__FUNCTION__<<":"<<__LINE__<<" "<<e.what()<< std::endl;}

    schedule_refit();

}

GraphEdge* GraphViewer::new_visual_edge(GraphNode *sourceNode, GraphNode *destNode, const QString &edge_name)
{
    try {
        if (!sourceNode || !destNode) return nullptr;
        auto gedge = new GraphEdge(sourceNode, destNode, edge_name);
        return gedge;
    }
    catch(const std::runtime_error &e)
    {
        QMessageBox::warning(this,"Error creating edge", e.what());
        return nullptr;
    }
}

GraphEdge* GraphViewer::new_visual_edge(std::uint64_t from, std::uint64_t to, const std::string &edge_tag)
{

    auto sourceNode = gmap.at(from);
    auto destNode = gmap.at(to);


    auto gedge = new GraphEdge(sourceNode, destNode, edge_tag.c_str());
    if (gedge) scene.addItem(gedge);
    return gedge;
}

void GraphViewer::del_edge_SLOT(std::uint64_t from, std::uint64_t to, const std::string &edge_tag)
{
    qDebug()<<__FUNCTION__<<"from:"<<from<<"to:"<<to<<"type:"<<QString::fromStdString(edge_tag);
	try {
        //std::cout << "[SLOT] Delete edge:  "<<from << ", " << to << ", "<< edge_tag<< std::endl;
		std::tuple<std::uint64_t, std::uint64_t, std::string> key = std::make_tuple(from, to, edge_tag);
		// Before the endpoints go away: the parent may have just lost its last collapsible child
		note_parent_edge(from, to, edge_tag, false);
		while (gmap_edges.count(key) > 0) {
            GraphEdge *edge = gmap_edges.extract(key).mapped();
            if (gmap.find(from) != gmap.end())
                gmap.at(from)->deleteEdge(edge);
            if (gmap.find(to) != gmap.end())
                gmap.at(to)->deleteEdge(edge);
            if (edge) {
                scene.removeItem(edge);
                delete edge;
            }

            auto from_str = std::to_string(from);
            auto to_str = std::to_string(to);
            Agnode_t *gvnode_f = agfindnode(graphviz_graph, from_str.data());
            Agnode_t *gvnode_t = agfindnode(graphviz_graph, to_str.data());

            if (Agedge_t *gvedge = agfindedge(graphviz_graph, gvnode_f, gvnode_t)) {
                agdeledge(graphviz_graph, gvedge);
            }
		}
	} catch(const std::exception &e) { std::cout << e.what() <<" Error  "<<__FUNCTION__<<":"<<__LINE__<< std::endl;}

    schedule_refit();

}

// remove node from scene
void GraphViewer::del_node_SLOT(uint64_t id)
{
    qDebug()<<__FUNCTION__<<"node id:"<<id;
    try {
        //std::cout << "[SLOT] Delete node:  "<<id<< std::endl;
        // Remove every edge touching this node BEFORE deleting the node. GraphEdge keeps raw
        // source/dest pointers (graph_edge.cpp) that paint()/adjust()/calculateForces()
        // dereference, so deleting a node while its edges remain in the scene leaves dangling
        // edges → use-after-free on the next paint/force-layout (the free(stack-addr) crash in
        // QWidgetRepaintManager::paintAndFlush). We must not rely on del_edge_signal arriving
        // before del_node_signal, nor on DSR cascading the edge deletes — clean them here.
        std::vector<std::tuple<std::uint64_t, std::uint64_t, std::string>> connected_edges;
        for (const auto &[key, edge] : gmap_edges)
        {
            const auto &[from, to, tag] = key;
            if (from == id or to == id)
                connected_edges.push_back(key);
        }
        for (const auto &[from, to, tag] : connected_edges)
            del_edge_SLOT(from, to, tag);

        while (gmap.count(id) > 0) {
            auto item = gmap.at(id);
            scene.removeItem(item);
            for (auto &[type, ids] : type_id_map)
                ids.erase(id);
            delete item;
            gmap.erase(id);
            // the node itself may have been a folding parent
            collapsible_children.erase(id);
            collapsed_parents.erase(id);
            auto id_str = std::to_string(id);
            if (Agnode_t *gvnode = agfindnode(graphviz_graph, id_str.data())) {
                agdelnode(graphviz_graph, gvnode);
            }
        }
    } catch(const std::exception &e) { std::cout << e.what() <<" Error  "<<__FUNCTION__<<":"<<__LINE__<< std::endl;}

    schedule_refit();

}

void GraphViewer::hide_show_node_SLOT(uint64_t id, bool visible)
{
	auto it = gmap.find(id);
	if (it == gmap.end() || it->second == nullptr) {
		qDebug() << __FUNCTION__ << "skipping missing node" << id;
		return;
	}
	auto item = it->second;
	item->setVisible(visible);
	for (const auto &gedge: item->edgeList)
	{
		if((visible and gedge->destNode()->isVisible() and gedge->sourceNode()->isVisible()) or !visible)
		{
			gedge->setVisible(visible);
		}
	}

}

//////////////////////////////////////////////////////////////////////////////////////
///// Collapsible children (the RT subtree of a node folded away from it)
/////
///// Purely visual, like the per-type "Show:" menu — nothing is written back to G.
///// Note that graphviz still lays out hidden nodes (compute_layout works on the whole
///// graph), so folding does not compact the layout, it only removes clutter.
//////////////////////////////////////////////////////////////////////////////////////

void GraphViewer::note_parent_edge(std::uint64_t from, std::uint64_t to, const std::string &edge_tag, bool added)
{
	if (edge_tag != PARENT_EDGE_TYPE)
		return;
	// The child must still be in gmap when an edge is removed — del_edge_SLOT calls us before
	// erasing anything, and del_node_SLOT deletes the edges before the node (see its comment).
	const auto child = gmap.find(to);
	if (child == gmap.end() or child->second == nullptr)
		return;
	if (from == to)   // self RT edge: not a parent/child relation, and folding it would hide the node itself
		return;

	if (added)
		collapsible_children[from].insert(to);
	else
	{
		if (const auto it = collapsible_children.find(from); it != collapsible_children.end())
		{
			it->second.erase(to);
			if (it->second.empty())
			{
				collapsible_children.erase(it);
				collapsed_parents.erase(from);   // nothing left to unfold
			}
		}
	}
	refresh_collapse_badge(from);
}

std::vector<std::uint64_t> GraphViewer::subtree_ids(std::uint64_t root, bool stop_at_collapsed) const
{
	// BFS down the RT edges, `root` excluded. `visited` also guards against cycles, which a
	// hand-edited or half-updated graph can transiently contain.
	//
	// stop_at_collapsed is what makes nested folds survive: when UNfolding, a descendant that the
	// user had folded on its own must stay folded, so we don't walk past it.
	std::vector<std::uint64_t> result;
	std::set<std::uint64_t> visited{root};
	std::deque<std::uint64_t> pending{root};
	while (not pending.empty())
	{
		const auto current = pending.front();
		pending.pop_front();
		for (const auto &[key, edge] : gmap_edges)
		{
			const auto &[from, to, tag] = key;
			if (from != current or tag != PARENT_EDGE_TYPE)
				continue;
			if (visited.insert(to).second)
			{
				result.push_back(to);
				if (not (stop_at_collapsed and collapsed_parents.count(to) > 0))
					pending.push_back(to);
			}
		}
	}
	return result;
}

void GraphViewer::refresh_collapse_badge(std::uint64_t parent_id)
{
	const auto it = gmap.find(parent_id);
	if (it == gmap.end() or it->second == nullptr)
		return;
	it->second->set_collapse_indicator(collapsible_children.count(parent_id) > 0,
	                                   collapsed_parents.count(parent_id) > 0);
}

void GraphViewer::apply_collapse_state(std::uint64_t parent_id)
{
	// Only the hiding half is re-applied: unfolding is an explicit user action. Blindly showing
	// here would fight the per-type "Show:" menu, which hides nodes through the same slot.
	if (collapsed_parents.count(parent_id) == 0)
		return;
	if (const auto it = collapsible_children.find(parent_id); it != collapsible_children.end())
		for (const auto child : it->second)
		{
			hide_show_node_SLOT(child, false);
			for (const auto id : subtree_ids(child, false))
				hide_show_node_SLOT(id, false);
		}
	refresh_collapse_badge(parent_id);
}

void GraphViewer::toggle_children_SLOT(std::uint64_t parent_id)
{
	const auto it = collapsible_children.find(parent_id);
	if (it == collapsible_children.end())
		return;
	const bool collapse = collapsed_parents.count(parent_id) == 0;
	if (collapse)
		collapsed_parents.insert(parent_id);
	else
		collapsed_parents.erase(parent_id);

	// Each child goes away with whatever hangs below it, so no orphan nodes float in the scene.
	// hide_show_node_SLOT already takes care of the edges (it shows one only when both endpoints
	// are visible), and doing it node by node makes the order irrelevant.
	for (const auto child : it->second)
	{
		hide_show_node_SLOT(child, not collapse);
		// Folding takes the whole subtree down; unfolding stops at descendants the user folded
		// themselves, so their own state is not silently undone.
		for (const auto id : subtree_ids(child, not collapse))
			hide_show_node_SLOT(id, not collapse);
	}
	refresh_collapse_badge(parent_id);
	schedule_refit();
}

void GraphViewer::mousePressEvent(QMouseEvent *event)
{
	auto item = this->scene.itemAt(mapToScene(event->pos()), QTransform());
	if(item) {
		QGraphicsView::mousePressEvent(event);
	}
	else if (event->button() == Qt::RightButton) {
        showContextMenu(event);
    }
	else {
        AbstractGraphicViewer::mousePressEvent(event);
    }
}

void GraphViewer::reload(QWidget * widget)
{
	if(qobject_cast<GraphViewer*>(widget) != nullptr)
	{
		createGraph();
	}
}

void GraphViewer::showContextMenu(QMouseEvent *event)
{
    contextMenu->exec();
}

// remove node from DSR
void GraphViewer::remove_node_SLOT(uint64_t node_id)
{
    std::cout << "Remove node in graph_viewer class"<<node_id <<std::endl;
    G->delete_node(node_id);
}

void GraphViewer::compute_layout(const char * alg) {
    
    gvLayout(graphviz_context, graphviz_graph, alg);

    qreal root_x = 0.0, root_y = 0.0; 
    for (Agnode_t* n = agfstnode(graphviz_graph); n; n = agnxtnode(graphviz_graph, n)) {
        auto name = agnameof(n);
        uint64_t id = std::stoull(name);
        double x = ND_coord(n).x;
        double y = ND_coord(n).y;
        auto *gnode = gmap.at(id);
        if (gnode) gnode->setPos(x, y);
        if (std::string_view(name) == std::string_view("root")) {
            root_x = x;
            root_y = y;
        }
        // We don't need to process the edges to render them
        //Uncomment this is the layout propagation is desired
        //qDebug() << __FILE__ <<":"<<__FUNCTION__<< " node id in graphnode: " << id ;
        std::optional<Node> g_node = G->get_node(id);
        if (g_node.has_value()) {
            G->add_or_modify_attrib_local<pos_x_att>(*g_node, (float) x);
            G->add_or_modify_attrib_local<pos_y_att>(*g_node,  (float) y);
            G->update_node(*g_node);
        }
    }

    // The node POSITIONS above are the point of a relayout and always apply. Recentring and refitting
    // the VIEWPORT is a different thing, and agents re-run compute_layout on every structural change
    // (a peer joining, a concept node being born), so doing it unconditionally would yank a zoomed-in
    // user back out mid-inspection. Respect a hand-framed view here exactly as schedule_refit() does.
    if (not user_framed_)
    {
        centerOn(root_x, root_y);
        update_scene_rect();
        this->fitInView(scene.itemsBoundingRect(), Qt::KeepAspectRatio );
    }
    else
        update_scene_rect();   // keep the scrollable area honest

    gvFreeLayout(graphviz_context, graphviz_graph);
}
