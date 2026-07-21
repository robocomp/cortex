/*
 * Copyright 2018 <copyright holder> <email>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <dsr/gui/viewers/graph_viewer/graph_node.h>
#include <QFont>
#include <QPushButton>
#include <algorithm>
#include <QWidgetAction>
#include <functional>
//#include <dsr/gui/viewers/graph_viewer/node_colors.h>
#include <dsr/gui/viewers/graph_viewer/graph_colors.h>
#include <dsr/gui/viewers/graph_viewer/graph_node_imu_widget.h>
#include <dsr/gui/viewers/graph_viewer/graph_node_laser_widget.h>
#include <dsr/gui/viewers/graph_viewer/graph_node_widget.h>
#include <dsr/gui/viewers/graph_viewer/graph_node_rgbd_widget.h>
#include <dsr/gui/viewers/graph_viewer/graph_node_person_widget.h>
#include <dsr/gui/viewers/graph_viewer/graph_node_mind_widget.h>


// "+"/"-" collapse handle, a child item of the node it belongs to.
//
// It must consume its own mouse events: a QGraphicsItem that is neither movable nor selectable
// IGNORES presses, and the scene then hands them to the item underneath — the node — whose
// mouseReleaseEvent opens the context menu (see GraphNode::mouseReleaseEvent). Accepting the
// press here is what keeps clicking the badge from also popping that menu.
class CollapseBadge : public QGraphicsSimpleTextItem
{
    public:
        CollapseBadge(GraphNode *parent, std::function<void()> on_click)
            : QGraphicsSimpleTextItem(parent), on_click_(std::move(on_click))
        {
            QFont f = font(); f.setPointSize(9); f.setBold(true); setFont(f);
            setZValue(1);
            setAcceptedMouseButtons(Qt::LeftButton);
            setCursor(Qt::PointingHandCursor);
        }
    protected:
        void mousePressEvent(QGraphicsSceneMouseEvent *event) override
        { event->accept(); }
        void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override
        {
            event->accept();
            if(on_click_) on_click_();
        }
    private:
        std::function<void()> on_click_;
};

GraphNode::GraphNode(const std::shared_ptr<DSR::GraphViewer>&
        graph_viewer_):QGraphicsEllipseItem(0,0,DEFAULT_DIAMETER,DEFAULT_DIAMETER), graph_viewer(graph_viewer_)
{
    auto flags = ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges | ItemUsesExtendedStyleOption | ItemIsFocusable;
    setFlags(flags);
    setCacheMode(DeviceCoordinateCache);
    setAcceptHoverEvents(true);
    setZValue(-1);
    node_brush.setStyle(Qt::SolidPattern);

    // context menu — order: View table, View data, then the destructive "Delete node" LAST, in red.
    contextMenu = new QMenu();
    QAction *table_action = new QAction("View table");
    contextMenu->addAction(table_action);
    connect(table_action, &QAction::triggered, this, [this](){ this->show_node_widget("table");});
    // "View data" is offered for EVERY node. Added ONCE here (not in setType, which is called more
    // than once per node → would stack duplicate entries). request_view_data() reads the node's
    // `type` at click time, so the action needs no type info now.
    QAction *view_data_action = new QAction("View data");
    contextMenu->addAction(view_data_action);
    connect(view_data_action, &QAction::triggered, this, [this](){ this->request_view_data();});
    // Destructive action last + red. QAction has no text-colour API, so wrap a flat red push button
    // in a QWidgetAction; it still triggers and dismisses the menu like a normal item.
    contextMenu->addSeparator();
    auto *delete_btn = new QPushButton("Delete node");
    delete_btn->setFlat(true);
    delete_btn->setCursor(Qt::PointingHandCursor);
    delete_btn->setStyleSheet(
        "QPushButton { color:#d33; text-align:left; padding:4px 24px; border:none; background:transparent; }"
        "QPushButton:hover { background:palette(highlight); color:white; }");
    auto *delete_action = new QWidgetAction(contextMenu);
    delete_action->setDefaultWidget(delete_btn);
    contextMenu->addAction(delete_action);
    connect(delete_btn, &QPushButton::clicked, this, [this](){ contextMenu->close(); this->delete_node(); });

    animation = new QPropertyAnimation(this, "node_color", this);
	animation->setDuration(animation_time);
	animation->setStartValue(plain_color);
	animation->setEndValue(dark_color);
	animation->setLoopCount(ANIMATION_REPEAT);
    QObject::connect(graph_viewer_->getGraph().get(), &DSR::DSRGraph::update_node_attr_signal, this, &GraphNode::update_node_attr_slot, Qt::QueuedConnection);
}

void GraphNode::setTag(const std::string &tag_)
{
    // Idempotent: the label is refreshed whenever a node's live state changes (agent health, see
    // AgentStatePublisher), so this is called repeatedly on the same node. It used to `new` a fresh
    // text item every call, leaking the previous one AND stacking overlapping text on the item.
    const QString c = QString::fromStdString(tag_);
    if (tag == nullptr)
    {
        tag = new QGraphicsSimpleTextItem(c, this);
        QFont f = tag->font(); f.setPointSize(7); tag->setFont(f);
        tag->setX(DEFAULT_DIAMETER);
        tag->setY(-10);
    }
    else if (tag->text() != c)
        tag->setText(c);
}

void GraphNode::setType(const std::string &type_)
{
    // May be called more than once per node — keep it idempotent (no menu building here; the
    // "View data" action is added once in the constructor).
    type = type_;
    auto color = GraphColors<DSR::Node>()[type];
    set_color(color);
}

void GraphNode::set_collapse_indicator(bool has_collapsible_children, bool collapsed)
{
    // Nothing to show and nothing built yet → don't create the item at all (most nodes).
    if(collapse_badge == nullptr)
    {
        if(not has_collapsible_children)
            return;
        collapse_badge = new CollapseBadge(this, [this](){ emit toggle_children_signal(id_in_graph); });
        // Left of the circle, opposite the name tag (which sits at +DEFAULT_DIAMETER, see setTag).
        collapse_badge->setX(-DEFAULT_RADIUS - 12);
        collapse_badge->setY(-10);
    }
    const QString c = collapsed ? "+" : "-";
    if(collapse_badge->text() != c)
        collapse_badge->setText(c);
    collapse_badge->setVisible(has_collapsible_children);
}

bool GraphNode::has_inline_data() const
{
    const auto graph = graph_viewer->getGraph();
    std::optional<Node> n = graph->get_node(id_in_graph);
    if(not n.has_value())
        return false;
    // Probe the representative payload attribute each built-in widget renders from. The stream is
    // "inline" only if that attribute is present AND non-empty: media-plane nodes keep the attribute
    // DECLARED (from the graph bootstrap) but never fill it, so presence alone is not enough — a
    // present-but-empty buffer means the data now flows on the media plane and must be forwarded.
    // get_attrib_by_name returns optional<reference_wrapper<const vector>> → unwrap with .get().
    const auto non_empty = [](const auto& opt){ return opt.has_value() and not opt->get().empty(); };
    // The mind node is rendered by a local widget that reads the whole mind subtree straight from the
    // graph — treat it as "inline" so "View data" opens GraphNodeMindWidget here instead of forwarding.
    if(type == "mind")
        return true;
    if(type == "rgbd")
        return non_empty(graph->get_attrib_by_name<cam_rgb_att>(n.value()));
    if(type == "laser")
        return non_empty(graph->get_attrib_by_name<laser_X_att>(n.value()));
    if(type == "imu")
        return non_empty(graph->get_attrib_by_name<imu_accelerometer_att>(n.value()));
    if(type == "person")
        return non_empty(graph->get_attrib_by_name<laser_angles_att>(n.value()));
    return false;   // unknown types never had a built-in widget → always forward
}

void GraphNode::request_view_data()
{
    // Legacy path: raw stream still inlined in the graph → open the matching built-in
    // widget. Media-plane path: no inline payload → hand the request to the agent,
    // which owns the DDS subscriber and its own type-specific viewer.
    if(has_inline_data())
        show_node_widget(type);
    else
        emit view_data_signal(id_in_graph, type);
}


void GraphNode::addEdge(GraphEdge *edge)
{
    qDebug()<<"====================================";
    int same_count = 0;
    int bend_factor = 0;
    // look for edges with the same dest
    qDebug()<<__FUNCTION__ <<"Checking edges for node: "<<this->id_in_graph;
    for (auto old_edge: edgeList)
    {
        if(old_edge == edge)
            throw std::runtime_error("Trying to add an already existing edge " + std::to_string(edge->sourceNode()->id_in_graph)+"--"+std::to_string(edge->destNode()->id_in_graph));
//        qDebug()<<__FUNCTION__ <<"\tExisting EDGE: "<<edge->sourceNode()->id_in_graph<<edge->destNode()->id_in_graph<<"OTHER: "<<old_edge->sourceNode()->id_in_graph<<old_edge->destNode()->id_in_graph;
        qDebug()<<"\t"<<__FUNCTION__ <<"Existing EDGE: "<<old_edge->sourceNode()->id_in_graph<<"--"<<old_edge->destNode()->id_in_graph;
        qDebug()<<"\t"<<__FUNCTION__ <<"     New EDGE: "<<edge->sourceNode()->id_in_graph<<"--"<<edge->destNode()->id_in_graph;
        if((edge->sourceNode()->id_in_graph==old_edge->sourceNode()->id_in_graph or edge->sourceNode()->id_in_graph==old_edge->destNode()->id_in_graph)
        and (edge->destNode()->id_in_graph==old_edge->sourceNode()->id_in_graph or edge->destNode()->id_in_graph==old_edge->destNode()->id_in_graph))
        {
            same_count++;
            qDebug()<<"\t\t"<<__FUNCTION__ <<"SAME EDGE"<<same_count;
        }
    }

    bend_factor = (pow(-1,same_count)*(-1 + pow(-1,same_count) - 2*same_count))/4;
    qDebug()<<__FUNCTION__ <<__LINE__<<"ID: "<<id_in_graph<<"SAME: "<<same_count<<"FACTOR: "<<bend_factor;
    edge->set_bend_factor(bend_factor);
    edgeList << edge;
    
    edge->adjust();
    qDebug()<<"====================================";

}

void GraphNode::deleteEdge(GraphEdge *edge)
{
    edgeList.removeAll(edge);
}

QList<GraphEdge *> GraphNode::edges() const
{
    return edgeList;
}

void GraphNode::calculateForces()
{
    if (!scene() || scene()->mouseGrabberItem() == this) 
	{
        newPos = pos();
        return;
    }

    // Sum up all forces pushing this item away
    qreal xvel = 0;
    qreal yvel = 0;
    //foreach (QGraphicsItem *item, scene()->items()) 
    for( auto &[k,node] : graph_viewer->getGMap())
	{
        //GraphNode *node = qgraphicsitem_cast<GraphNode *>(item);
        //if (!node)
        //    continue;
        (void)k;

        QPointF vec = mapToItem(node, 0, 0);
        qreal dx = vec.x();
        qreal dy = vec.y();
        double l = 2.0 * (dx * dx + dy * dy);
        if (l > 0) 
		{
            xvel += (dx * force_velocity_factor) / l;
            yvel += (dy * force_velocity_factor) / l;
        }
    }

    // Now subtract all forces pulling items together
    double weight = (edgeList.size() + 1) * EDGE_PULL_FACTOR;
    foreach (GraphEdge *edge, edgeList) 
	{
        QPointF vec;
       
        if (edge->sourceNode() == this)
            vec = mapToItem(edge->destNode(), 0, 0);
        else
            vec = mapToItem(edge->sourceNode(), 0, 0);
        xvel -= vec.x() / weight;
        yvel -= vec.y() / weight;
    }

    // Subtract force from central pos pulling item to the center of the image
    QPointF to_central_point = mapFromItem(graph_viewer->getCentralPoint(), 0, 0);
    xvel += to_central_point.x() / (weight/2) ;
    yvel += to_central_point.y() / (weight/2) ;

    // sludge
    if (qAbs(xvel) < 0.1 && qAbs(yvel) < 0.1)
        xvel = yvel = 0;

    QRectF sceneRect = scene()->sceneRect();
    newPos = pos() + QPointF(xvel, yvel);
    newPos.setX(qMin(qMax(newPos.x(), sceneRect.left() + SCENE_MARGIN), sceneRect.right() - SCENE_MARGIN));
    newPos.setY(qMin(qMax(newPos.y(), sceneRect.top() + SCENE_MARGIN), sceneRect.bottom() - SCENE_MARGIN));
}

bool GraphNode::advancePosition()
{
    if (newPos == pos())
        return false;

    setPos(newPos);
    return true;
}

QRectF GraphNode::boundingRect() const
{
    qreal adjust = 2;
    return QRectF(
            -DEFAULT_RADIUS - adjust,
            -DEFAULT_RADIUS - adjust,
            DEFAULT_DIAMETER + 3 + adjust,
            DEFAULT_DIAMETER + 3 + adjust
            );
}

QPainterPath GraphNode::shape() const
{
    QPainterPath path;
    path.addEllipse(-DEFAULT_RADIUS, -DEFAULT_RADIUS, DEFAULT_DIAMETER, DEFAULT_DIAMETER);
    return path;
}

void GraphNode::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *)
{
    painter->setPen(Qt::NoPen);
    painter->setBrush(SUNKEN_COLOR);
    QRadialGradient gradient(-3, -3, 10);
    if (option->state & QStyle::State_Sunken)
    {
        gradient.setColorAt(0, QColor(Qt::darkGray).lighter());
        gradient.setColorAt(1, QColor(Qt::darkGray));
    } else
		{
        gradient.setColorAt(0, node_brush.color());
        gradient.setColorAt(1, QColor(node_brush.color().darker()));
    }
    painter->setBrush(gradient);
    if(isSelected())
        painter->setPen(QPen(Qt::green, 0, Qt::DashLine));
    else
        painter->setPen(QPen(Qt::black, 0));
    painter->drawEllipse(-DEFAULT_RADIUS, -DEFAULT_RADIUS, DEFAULT_DIAMETER, DEFAULT_DIAMETER);
}

QVariant GraphNode::itemChange(GraphicsItemChange change, const QVariant &value)
{
    switch (change) 
	{
        case ItemPositionHasChanged:
        {
            foreach (GraphEdge *edge, edgeList)
                 edge->adjust(this, value.toPointF());
            break;
        }
        default:
            break;
    }
    return QGraphicsItem::itemChange(change, value);
}

void GraphNode::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    // Remember where a left press started so mouseReleaseEvent can tell a plain click (→ menu)
    // from a click-and-drag (→ move the node).
    if(event->button() == Qt::LeftButton)
        press_screen_pos_ = event->screenPos();
    QGraphicsEllipseItem::mousePressEvent(event);
}

void GraphNode::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    QGraphicsEllipseItem::mouseDoubleClickEvent(event);
}

void GraphNode::show_node_widget(const std::string &show_type)
{
    //static std::unique_ptr<QWidget> do_stuff;
    const auto graph = graph_viewer->getGraph();
    if(show_type=="laser")
        node_widget = std::make_unique<GraphNodeLaserWidget>(graph, id_in_graph);
    else if(show_type=="rgbd")
        node_widget = std::make_unique<GraphNodeRGBDWidget>(graph, id_in_graph);
    else if(show_type=="person")
        node_widget = std::make_unique<GraphNodePersonWidget>(graph, id_in_graph);
    else if(show_type=="imu")
        node_widget = std::make_unique<GraphNodeIMUWidget>(graph, id_in_graph);
    else if(show_type=="mind")
        node_widget = std::make_unique<GraphNodeMindWidget>(graph, id_in_graph);
    else
        node_widget = std::make_unique<GraphNodeWidget>(graph, id_in_graph);

}

void GraphNode::change_detected()
{
    animation->start();
}

void GraphNode::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if(event->button() == Qt::LeftButton)
    {
        // Distinguish a plain left-click from a left-drag by how far the pointer travelled since
        // the press. A small movement is a click → open the node menu (moved here from right-click);
        // a larger movement is a drag → persist the node's new position.
        static constexpr int CLICK_DRAG_THRESHOLD = 4;   // screen pixels
        const int moved = (event->screenPos() - press_screen_pos_).manhattanLength();
        if(moved <= CLICK_DRAG_THRESHOLD)
        {
            contextMenu->exec(event->screenPos());
        }
        else
        {
            auto g = graph_viewer->getGraph();
            std::optional<Node> n = g->get_node(id_in_graph);
            if (n.has_value()) {
                g->add_or_modify_attrib_local<pos_x_att>(n.value(), (float) this->pos().x());
                g->add_or_modify_attrib_local<pos_y_att>(n.value(),  (float) this->pos().y());
                g->update_node(n.value());
            }
        }
    }
    QGraphicsItem::mouseReleaseEvent(event);
}

QColor GraphNode::_node_color()
{
    return this->brush().color();
}
void GraphNode::set_node_color(const QColor& c)
{
    node_brush.setColor(c);
    this->setBrush( node_brush );
}

void GraphNode::set_color(const std::string &plain)
{
    const QString c = QString::fromStdString(plain);
    const QColor parsed(c);
    // QColor yields BLACK for any name it cannot parse (and for an empty string). Node colours come
    // from a graph attribute now, i.e. from data we do not control, so an unparseable value must
    // leave the node alone rather than repaint the whole graph black.
    if (not parsed.isValid())
    {
        qWarning() << "GraphNode::set_color: ignoring unparseable colour" << c
                   << "on node" << id_in_graph;
        return;
    }
    plain_color = c;
    // Derive the pulse-target from the colour itself. This used to be the string "dark" + name,
    // which is NOT a colour for most names — darkSteelBlue, darkMediumPurple and darkcoral are all
    // invalid, so it silently produced black. darker() always yields a valid colour.
    dark_color = parsed.darker().name();
    set_node_color(parsed);
    // Pulse around THIS node's colour. The start value used to be hardcoded green, so a red or
    // orange node (an agent in Emergency/Waiting) flashed green on every change_detected() — i.e.
    // exactly the wrong signal at exactly the moment you are watching it. Both ends are QColor for
    // symmetry (QPropertyAnimation does coerce a QString via the property type, so the previous
    // QColor/QString mix was not itself a bug — measured, not assumed).
    animation->setStartValue(parsed.lighter());
    animation->setEndValue(parsed);
}

/////////////////////////////////////////////////////////////////////////////////////////7
////
/////////////////////////////////////////////////////////////////////////////////////////
void GraphNode::delete_node()
{
    std::cout << "Delete node" << id_in_graph <<  std::endl;
    //show confirmation dialog
    QMessageBox msgBox;
    msgBox.setText("Are you sure you want to delete node?");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    int reply = msgBox.exec();
    if (reply == QMessageBox::Yes) {
        emit del_node_signal(id_in_graph);
    }
}

void GraphNode::update_node_attr_slot(std::uint64_t node_id, const std::vector<std::string> &type_)
{
    if (node_id != this->id_in_graph)
        return;
    // Repaint when the node's `color` attribute changed. Agent nodes report live health by rewriting
    // `color` without being recreated, and an attribute-only write does not go through
    // GraphViewer::add_or_assign_node_SLOT — so without this the new colour was never shown.
    // Queued onto the GUI thread by the connection in the constructor.
    //
    // Scoped to type "agent" for the same reason as the viewer slot: other node types carry stale
    // colour attributes that were dead data until this feature switched them on.
    if (type != "agent")
        return;
    if (std::ranges::find(type_, "color") == type_.end())
        return;
    const auto n = graph_viewer->getGraph()->get_node(node_id);
    if (not n.has_value())
        return;
    if (const auto color = graph_viewer->getGraph()->get_attrib_by_name<color_att>(n.value());
        color.has_value())
        set_color(color.value().get());
}
