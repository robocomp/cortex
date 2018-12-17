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

#ifndef GRAPHEDGE_H
#define GRAPHEDGE_H

#include <QGraphicsItem>
#include <QContextMenuEvent>
#include <QTableWidget>

class GraphNode;


class GraphEdge : public QGraphicsItem
{
	public:
    GraphEdge(GraphNode *sourceNode, GraphNode *destNode, const QString &edge_name);
    GraphNode *sourceNode() const;
    GraphNode *destNode() const;
    void adjust();
    int type() const override { return Type; }

	protected:
    QPainterPath shape() const override;
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
		void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;  
    void keyPressEvent(QKeyEvent *event) override;
	
	private:
		GraphNode *source, *dest;
    qreal arrowSize;
    QPointF sourcePoint;
    QPointF destPoint;
		QString tag;
		QGraphicsTextItem *rt_values = nullptr;
    QTableWidget *label = nullptr;
};

#endif // GRAPHEDGE_H
