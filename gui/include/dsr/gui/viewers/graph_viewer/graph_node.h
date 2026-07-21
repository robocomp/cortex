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

#ifndef GRAPHNODE_H
#define GRAPHNODE_H

static const int animation_time = 200;
static const double force_velocity_factor = 150.0;
#include <QGraphicsEllipseItem>
#include <QTableWidget>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QStyleOption>
#include <QDebug>
#include <QDialog>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <utility>
#include <cppitertools/zip.hpp>

#include <dsr/api/dsr_api.h>
#include <dsr/gui/dsr_gui.h>
#include <dsr/gui/viewers/graph_viewer/graph_edge.h>


using namespace DSR;

class GraphEdge;
class QGraphicsSceneMouseEvent;
// Small "+"/"-" child item drawn beside a node that has collapsible children. Defined in
// graph_node.cpp — it only needs to handle its own clicks.
class CollapseBadge;


class GraphNode : public QObject, public QGraphicsEllipseItem
{
Q_OBJECT
    Q_PROPERTY(QColor node_color READ _node_color WRITE set_node_color)
private:
    QPointF newPos;
    QGraphicsSimpleTextItem *tag = nullptr;   // created on first setTag(), reused after (see setTag)
    std::string type;
    std::shared_ptr<DSR::GraphViewer> graph_viewer;
    QBrush node_brush;
    QString dark_color = "darkyellow", plain_color = "yellow";
    QPropertyAnimation* animation;
    QMenu *contextMenu = nullptr;
    std::unique_ptr<QWidget> node_widget;
    std::set<std::string> cached_edge_types;
    QPoint press_screen_pos_;   // where the last left press started (click-vs-drag discrimination)
    CollapseBadge *collapse_badge = nullptr;   // created on first set_collapse_indicator()

public:
    static constexpr int DEFAULT_DIAMETER = 20;
    static constexpr int DEFAULT_RADIUS = DEFAULT_DIAMETER/2;
    static constexpr int ANIMATION_REPEAT = 3;
    static constexpr int EDGE_PULL_FACTOR = 10;
    static constexpr int SCENE_MARGIN = 10;
    static constexpr Qt::GlobalColor SUNKEN_COLOR = Qt::darkGray;
    static constexpr int LUMINOSITY_FACTOR = 200;

    std::uint64_t id_in_graph;
    QList<GraphEdge *> edgeList;

    explicit GraphNode(const std::shared_ptr<DSR::GraphViewer>& graph_viewer_);

    void addEdge(GraphEdge *edge);
    void deleteEdge(GraphEdge *edge);

    QList<GraphEdge *> edges() const;
    void calculateForces();
    bool advancePosition();
    void setTag(const std::string &tag_);
    std::string getTag() const { return tag->text().toStdString();};
    std::string getColor() const { return plain_color.toStdString(); };
    void setType(const std::string &type_);
    std::string getType() const { return type;};
    // Shows/hides the "+"/"-" collapse handle beside the node. The node holds no policy: what
    // "collapsed" hides is decided by GraphViewer, which owns the parent/child index.
    void set_collapse_indicator(bool has_collapsible_children, bool collapsed);
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    std::shared_ptr<DSR::GraphViewer> getGraphViewer() const { return graph_viewer;};
    void set_node_color(const QColor& c);
    void set_color(const std::string &plain);
    QColor _node_color();
    void change_detected();

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    //void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override { qDebug() << "move " << event->pos();};

public slots:
    //void NodeAttrsChangedSLOT(const DSR::IDType &node, const DSR::Attribs&);
    void show_node_widget(const std::string &show_type= "table");
    void update_node_attr_slot(std::uint64_t node_id, const std::vector<std::string> &type_);
    void delete_node();
    // Decides at click time whether the node still carries its raw stream inline
    // in the graph (open the built-in widget) or the data lives on the media plane
    // (forward view_data_signal to the agent).
    void request_view_data();
signals:
    void del_node_signal(uint64_t id);
    // Emitted when the user asks to "View data" on a node whose raw stream is NOT
    // stored inline in the graph. The agent, which owns the media-plane (DDS)
    // subscriber, connects to this (QueuedConnection) and opens its own viewer.
    void view_data_signal(uint64_t id, const std::string &type);
    // The user clicked the "+"/"-" badge. GraphViewer decides which children to hide/show.
    void toggle_children_signal(uint64_t id);

private:
    // True if the node still holds its type's raw payload as a graph attribute.
    bool has_inline_data() const;
};
//Q_DECLARE_METATYPE(DSR::Node);
#endif // GRAPHNODE_H
