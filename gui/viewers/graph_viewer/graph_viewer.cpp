#include <dsr/gui/dsr_gui.h>
#include <cppitertools/range.hpp>
#include <QApplication>
#include <dsr/gui/viewers/graph_viewer/graph_node.h>
#include <dsr/gui/viewers/graph_viewer/graph_edge.h>
#include <dsr/gui/viewers/graph_viewer/graph_viewer.h>
#include <QMessageBox>

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

    contextMenu = new QMenu(this);
    showMenu = contextMenu->addMenu(tr("&Show:"));

    createGraph();

	this->scene.setSceneRect(scene.itemsBoundingRect());

	this->fitInView(scene.itemsBoundingRect(), Qt::KeepAspectRatio );

	central_point = new QGraphicsEllipseItem(0,0,0,0);
	scene.addItem(central_point);

	connect(G.get(), &DSR::DSRGraph::update_node_signal, this, &GraphViewer::add_or_assign_node_SLOT, Qt::QueuedConnection);
	connect(G.get(), &DSR::DSRGraph::update_edge_signal, this, &GraphViewer::add_or_assign_edge_SLOT, Qt::QueuedConnection);
	connect(G.get(), &DSR::DSRGraph::del_edge_signal, this, &GraphViewer::del_edge_SLOT, Qt::QueuedConnection);
	connect(G.get(), &DSR::DSRGraph::del_node_signal, this, &GraphViewer::del_node_SLOT, Qt::QueuedConnection);
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
}

void GraphViewer::createGraph()
{
	gmap.clear();
	gmap_edges.clear();
    type_id_map.clear();
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

            std::string color("coral");
            color = G->get_attrib_by_name<color_att>(n.value()).value_or(color);
			gnode->set_color(color);
			gnode->setType(type);
        }
        else
		{
			qDebug()<<__FUNCTION__<<"##### Updated node";
            gnode = gmap.at(id);
		}
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
            }
            if (gmap_edges[key]) gmap_edges[key]->change_detected();
        }
	}
	catch(const std::exception &e) {
		std::cout << e.what() <<" Error  "<<__FUNCTION__<<":"<<__LINE__<<" "<<e.what()<< std::endl;}

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
    qDebug()<<__FUNCTION__<<":"<<__LINE__;
	try {
        //std::cout << "[SLOT] Delete edge:  "<<from << ", " << to << ", "<< edge_tag<< std::endl;
		std::tuple<std::uint64_t, std::uint64_t, std::string> key = std::make_tuple(from, to, edge_tag);
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
		}
	} catch(const std::exception &e) { std::cout << e.what() <<" Error  "<<__FUNCTION__<<":"<<__LINE__<< std::endl;}

}

// remove node from scene
void GraphViewer::del_node_SLOT(uint64_t id)
{
    qDebug()<<__FUNCTION__<<":"<<__LINE__;
    try {
        //std::cout << "[SLOT] Delete node:  "<<id<< std::endl;
        while (gmap.count(id) > 0) {
            auto item = gmap.at(id);
            scene.removeItem(item);
            delete item;
            gmap.erase(id);
        }
    } catch(const std::exception &e) { std::cout << e.what() <<" Error  "<<__FUNCTION__<<":"<<__LINE__<< std::endl;}

}

void GraphViewer::hide_show_node_SLOT(uint64_t id, bool visible)
{
	auto item = gmap[id];
	item->setVisible(visible);
	for (const auto &gedge: item->edgeList)
	{
		if((visible and gedge->destNode()->isVisible() and gedge->sourceNode()->isVisible()) or !visible)
		{
			gedge->setVisible(visible);
		}
	}

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
    contextMenu->exec(event->globalPos());
}

// remove node from DSR
void GraphViewer::remove_node_SLOT(uint64_t node_id)
{
    std::cout << "Remove node in graph_viewer class"<<node_id <<std::endl;
    G->delete_node(node_id);
}