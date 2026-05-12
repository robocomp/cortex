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

#include <dsr/gui/viewers/graph_viewer/graph_edge.h>
#include <dsr/gui/viewers/graph_viewer/graph_node.h>
#include <qmath.h>
#include <QPainter>
#include <QDebug>
#include <dsr/gui/dsr_gui.h>
#include <QGraphicsSceneMouseEvent>
#include <cppitertools/range.hpp>
#include <dsr/gui/viewers/graph_viewer/graph_colors.h>
#include <dsr/gui/viewers/graph_viewer/graph_edge_widget.h>
#include <dsr/gui/viewers/graph_viewer/graph_edge_rt_widget.h>

GraphEdge::GraphEdge(GraphNode* sourceNode, GraphNode* destNode, const QString& edge_name)
        :QGraphicsLineItem(), arrowSize(10)
{
//    this->graphic_debug = true;
    source = dest = nullptr;
    setZValue(-1);
    // non-movable but selectable
    auto flags = ItemIsSelectable | ItemSendsGeometryChanges | ItemUsesExtendedStyleOption;
    setFlags(flags);
    tag = new QGraphicsTextItem(edge_name, this);
    tag->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
    tag->installEventFilter(this);
    color = QString::fromStdString(GraphColors<DSR::Edge>()[edge_name.toStdString()]);
    animation = new QPropertyAnimation(this, "edge_pen");
    animation->setDuration(200);
    animation->setStartValue(4);
    animation->setEndValue(2);
    animation->setLoopCount(3);
//    bend_factor = 0;
    line_width = 2;

    // accept hovers
    setAcceptHoverEvents(true);
    source = sourceNode;
    dest = destNode;

    if (source->id_in_graph == destNode->id_in_graph) {
        source->addEdge(this);
    } else {
        source->addEdge(this);
        dest->addEdge(this);
    }

    adjust();
    QObject::connect(source->getGraphViewer()->getGraph().get(), &DSR::DSRGraph::update_edge_attr_signal, this,
            &GraphEdge::update_edge_attr_slot, Qt::QueuedConnection);
}


GraphNode* GraphEdge::sourceNode() const
{
    return source;
}

GraphNode* GraphEdge::destNode() const
{
    return dest;
}

void GraphEdge::adjust(GraphNode* node, QPointF pos)
{
    if (!source || !dest)
        return;

    QLineF the_line(mapFromItem(source, 0, 0), mapFromItem(dest, 0, 0));
    qreal length = the_line.length();

    prepareGeometryChange();
    tag->setPos(line().center());
    if (length > qreal(GraphNode::DEFAULT_DIAMETER)) {
        QPointF edgeOffset((the_line.dx() * GraphNode::DEFAULT_RADIUS) / length, (the_line.dy() * GraphNode::DEFAULT_RADIUS) / length);
        QPointF sourcePoint = the_line.p1() + edgeOffset;
        QPointF destPoint = the_line.p2() - edgeOffset;
        setLine(QLineF(sourcePoint, destPoint));
    } else
        setLine(the_line);
}

QRectF GraphEdge::boundingRect() const
{
    if (!source || !dest)
        return QRectF();

    qreal penWidth = 1;
    qreal extra = (penWidth + arrowSize) / 2.0;

    if(source!=dest)
        return QRectF(line().p1(), QSizeF(line().p2().x() - line().p1().x(),
            line().p2().y() - line().p1().y()))//.united(source->boundingRect()).united(dest->boundingRect())
            .normalized()
            .adjusted(-extra, -extra, extra, extra);
    else
    {
        return QRectF(this->line().p1().x()-GraphNode::DEFAULT_RADIUS*2, this->line().p1().y(), GraphNode::DEFAULT_RADIUS*2, GraphNode::DEFAULT_RADIUS*2);
    }
}

void GraphEdge::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*)
{
    painter->save();
    if (!source || !dest)
        return;

    draw_arrows(painter);
    draw_arc(painter);
    painter->restore();
    if(source==dest)
        this->setZValue(-10);
}

void GraphEdge::draw_line(QPainter* painter) const
{
    QPen myPen = this->pen();
    myPen.setColor(this->color);
    painter->setPen(myPen);
    painter->setBrush(this->color);
    painter->drawLine(this->line());
}

void GraphEdge::draw_arrows(QPainter* painter) const
{// Draw the arrows
    double angle = atan2(-this->line().dy(), this->line().dx());

    QPointF destArrowP1 = this->line().p2() + QPointF(sin(angle - M_PI / 3) * this->ARROW_SIZE,
            cos(angle - M_PI / 3) * this->ARROW_SIZE);
    QPointF destArrowP2 = this->line().p2() + QPointF(sin(angle - M_PI + M_PI / 3) * this->ARROW_SIZE,
            cos(angle - M_PI + M_PI / 3) * this->ARROW_SIZE);

    painter->setBrush(this->color);
    painter->setPen(this->color);
    painter->drawPolygon(QPolygonF() << this->line().p2() << destArrowP1 << destArrowP2);
}

void GraphEdge::draw_arc(QPainter* painter) const
{
    if(this->source != this->dest) {
        auto m_controlPos = (this->line().p1() + this->line().p2()) / 2;
        QPointF t1 = m_controlPos;
        float posFactor = qAbs(m_bendFactor);

        bool bendDirection = true;
        if (m_bendFactor < 0)
            bendDirection = !bendDirection;

        QLineF f1(t1, this->line().p2());
        f1.setAngle(bendDirection ? f1.angle() + 90 : f1.angle() - 90);
        f1.setLength(f1.length() * 0.2 * posFactor);

        m_controlPos = f1.p2();
        auto m_controlPoint = m_controlPos - (t1 - m_controlPos) * 0.33;

        auto path = QPainterPath();
        path.moveTo(this->line().p1());
        path.cubicTo(m_controlPoint, m_controlPoint, this->line().p2());
        auto r = tag->boundingRect();
        int w = r.width();
        int h = r.height();
        tag->setDefaultTextColor(this->color);
        tag->setPos(m_controlPoint.x() - w / 2, m_controlPoint.y() - h / 2);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(this->color);
        painter->drawPath(path);
    }
    else
    {
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(this->line().p1().x()-GraphNode::DEFAULT_RADIUS*2, this->line().p1().y(), GraphNode::DEFAULT_RADIUS*2, GraphNode::DEFAULT_RADIUS*2);
        qDebug()<<"//////////////////////";
    }
}

void GraphEdge::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button()==Qt::RightButton) {
        mouse_double_clicked();
    }
    QGraphicsLineItem::mouseDoubleClickEvent(event);
}

// For Tag Double click
bool GraphEdge::eventFilter(QObject* object, QEvent* event)
{
    if(object == this->tag)
    {
        if(event->type() == QEvent::GraphicsSceneMouseDoubleClick){
            auto mouseEvent = static_cast<QGraphicsSceneMouseEvent*>(event);
            if (mouseEvent->button()==Qt::RightButton) {
                mouse_double_clicked();
            }
            return true;
        }
    }
    return false;
}

void GraphEdge::mouse_double_clicked()
{
    qDebug() << __FILE__ << " " << __FUNCTION__ << "Edge from " << this->source->id_in_graph << " to " << this->dest->id_in_graph
             << " tag: " << this->tag->toPlainText();
    static std::unique_ptr<QWidget> do_stuff;
    const auto graph = this->source->getGraphViewer()->getGraph();

    if (this->tag->toPlainText()=="RT" or this->tag->toPlainText()=="VRT" or this->tag->toPlainText()=="looking-at") {
        do_stuff = std::make_unique<GraphEdgeRTWidget>(graph, this->source->id_in_graph, this->dest->id_in_graph,
                this->tag->toPlainText().toStdString());
    }
    else {
        do_stuff = std::make_unique<GraphEdgeWidget>(graph, this->source->id_in_graph, this->dest->id_in_graph,
                this->tag->toPlainText().toStdString());
    }
    this->animation->start();
    this->update();
}

void GraphEdge::keyPressEvent(QKeyEvent* event)
{
    if (event->key()==Qt::Key_Escape) {
        if (label!=nullptr) {
            label->close();
            delete label;
            label = nullptr;
        }
    }
}

void GraphEdge::change_detected()
{
    animation->start();
}

int GraphEdge::_edge_pen()
{
    return this->line_width;
}

void GraphEdge::set_edge_pen(const int p)
{
    this->line_width = p;
    this->setPen(QPen(Qt::black, line_width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
}

void GraphEdge::update_edge_attr_slot(std::uint64_t from, std::uint64_t to, const std::string& type, const std::vector<std::string>& att_name)
{
    if ((from!=this->source->id_in_graph) or (to!=this->dest->id_in_graph))
        return;
    if (std::find(att_name.begin(), att_name.end(), "color")!=att_name.end()) {
        std::optional<Edge> edge = source->getGraphViewer()->getGraph()->get_edge(from, to, tag->toPlainText().toStdString());
        if (edge.has_value()) {
            auto& attrs = edge.value().attrs();
            auto value = attrs.find("color");
            if (value!=attrs.end()) {
                this->color = QColor(QString::fromStdString(value->second.str()));
            }
        }
    }
}
void GraphEdge::set_bend_factor(int bf)
{
    qDebug()<<__FUNCTION__ <<bf;
    m_bendFactor = bf;
}
