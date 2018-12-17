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

#include <QGraphicsItem>
#include <QTableWidget>

class GraphEdge;
class GraphViewer;
class QGraphicsSceneMouseEvent;

class GraphNode : public QGraphicsItem
{
	public:
    GraphNode(GraphViewer *graph_viewer);

    enum { Type = UserType + 1 };
    int type() const override { return Type; }
		void addEdge(GraphEdge *edge);
    QList<GraphEdge *> edges() const;
    void calculateForces();
    bool advancePosition();
		void setTag(const QString &tag_);
    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
		void setColor(const QString &plain);

	protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;  
    // void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    // void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
	
	private:
    GraphViewer *graph;
    QPointF newPos;
		QList<GraphEdge *> edgeList;
		QGraphicsSimpleTextItem *tag;
		QString dark_color = "darkyello", plain_color = "yellow";
    QTableWidget *label;
};

#endif // GRAPHNODE_H
