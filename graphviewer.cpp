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

#include "graphviewer.h"
#include <cppitertools/range.hpp>
#include <qmat/QMatAll>
#include <QDesktopWidget>
#include <QGLViewer/qglviewer.h>
#include <QApplication>

#include "graphnode.h"
#include "graphedge.h"

GraphViewer::GraphViewer()
{
	scene.setItemIndexMethod(QGraphicsScene::NoIndex);
	scene.setSceneRect(-200, -200, 400, 400);
	this->setScene(&scene);
	this->setCacheMode(CacheBackground);
	this->setViewport(new QGLWidget(QGLFormat(QGL::SampleBuffers)));
	this->setViewportUpdateMode(BoundingRectViewportUpdate);
	this->setRenderHint(QPainter::Antialiasing);
	this->setTransformationAnchor(AnchorUnderMouse);
	//this->setContextMenuPolicy(Qt::ActionsContextMenu);
	this->scale(qreal(0.8), qreal(0.8));
	this->setMinimumSize(400, 400);
	this->fitInView(scene.sceneRect(), Qt::KeepAspectRatio );
	this->adjustSize();
 	QRect availableGeometry(QApplication::desktop()->availableGeometry());
 	this->move((availableGeometry.width() - width()) / 2, (availableGeometry.height() - height()) / 2);
}

void GraphViewer::setGraph(std::shared_ptr<DSR::Graph> graph_, QScrollArea *scrollArea)
{
	std::cout << __FUNCTION__ << "-- Entering setGraph" << std::endl;
	this->graph = graph_;
	this->setParent(scrollArea);
	scrollArea->setWidget(this);
	scrollArea->setMinimumSize(600,600);
	
	for(const auto &par : *graph)
	{
			const auto &node_id = par.first;
			// get attrs from graph
			auto &node_draw_attrs = graph->getNodeDrawAttrs(node_id);
			float node_posx = graph->attr<float>(node_draw_attrs.at("posx"));
			float node_posy = graph->attr<float>(node_draw_attrs.at("posy"));
	
			std::string color_name = graph->attr<std::string>(node_draw_attrs.at("color"));
			QString qname = QString::fromStdString(graph->attr<std::string>(node_draw_attrs.at("name")));
			
			//create graphic nodes 
			auto gnode = new GraphNode(this);
			gnode->setColor(color_name.c_str());
			scene.addItem(gnode);
			gnode->setPos(node_posx, node_posy);
			gnode->setTag(qname);
			
			//add to graph
			node_draw_attrs["gnode"] = gnode;
	}		

	// add edges after all nodes have been created
	for(const auto &par : *graph)
	{
			const auto &node_id = par.first;
			const auto &node_draw_attrs = graph->getNodeDrawAttrs(node_id);
			auto node_origen = graph->attr<GraphNode*>(node_draw_attrs.at("gnode")); 
			auto &node_fanout = graph->fanout(node_id);
			for( auto &[node_adj, edge_atts] : node_fanout)
			{
				auto node_dest_draw_attrs = graph->getNodeDrawAttrs(node_adj);
				auto node_dest = graph->attr<GraphNode*>(node_dest_draw_attrs.at("gnode")); 
				auto edge_tag = graph->attr<std::string>(edge_atts.draw_attrs.at("name"));
				scene.addItem(new GraphEdge(node_origen, node_dest, edge_tag.c_str()));
			}
	}
}

void GraphViewer::draw()
{
	//std::cout << this->width() << " " << this->height() << std::endl;
	//this->fitInView(scene.sceneRect(), Qt::KeepAspectRatio );
	show();
}

void GraphViewer::itemMoved()
{
	//std::cout << "timerId " << timerId << std::endl;
    // if (timerId == -1)
    //    timerId = startTimer(1000 / 25);
}

void GraphViewer::timerEvent(QTimerEvent *event)
{
    //Q_UNUSED(event);

	//std::cout << "gola"  << std::endl;

    QList<GraphNode *> nodes;
    foreach (QGraphicsItem *item, scene.items()) 
		{
        if (GraphNode *node = qgraphicsitem_cast<GraphNode *>(item))
            nodes << node;
    }
    foreach (GraphNode *node, nodes)
        node->calculateForces();

    bool itemsMoved = false;
    foreach (GraphNode *node, nodes) 
		{
        if (node->advancePosition())
            itemsMoved = true;
    }

    if (!itemsMoved) 
	{
        killTimer(timerId);
        timerId = 0;
    }
}

/////////////////////////
/////////////////////////

void GraphViewer::wheelEvent(QWheelEvent *event)
{
		//zoom
// 	  double scaleFactor = pow((double)2, -event->delta() / 240.0);
// 	  qreal factor = this->transform().scale(scaleFactor, scaleFactor).mapRect(QRectF(0, 0, 1, 1)).width();
//     if (factor < 0.07 || factor > 100)
//         return;
//     this->scale(scaleFactor, scaleFactor);
		
	// zoom
	const ViewportAnchor anchor = this->transformationAnchor();
	this->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
	int angle = event->angleDelta().y();
	qreal factor;
	if (angle > 0) 
	{
		factor = 1.1;
		QRectF r = scene.sceneRect();
		this->scene.setSceneRect(r);
	}
	else
	{
		factor = 0.9;
		QRectF r = scene.sceneRect();
		this->scene.setSceneRect(r);
	}
	this->scale(factor, factor);
	this->setTransformationAnchor(anchor);
	
// 	qDebug() << "scene rect" << scene.sceneRect();
// 	qDebug() << "view rect" << rect();

}