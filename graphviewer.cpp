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
#include "specificworker.h"

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

	central_point = new QGraphicsEllipseItem();
	central_point->setPos(scene.sceneRect().center());

}

//void GraphViewer::setWidget(QScrollArea *scrollArea, QListView *list_view_)
void GraphViewer::setWidget(SpecificWorker *worker_)
{
	worker = worker_;
	this->setParent(worker->scrollArea);
	worker->scrollArea->setWidget(this);
	worker->scrollArea->setMinimumSize(600,600);

	//connect(worker->actionSave, SIGNAL(activated()), this, SLOT(saveGraphSLOT()));
	connect(worker->actionSave, &QAction::triggered, this, &GraphViewer::saveGraphSLOT);
}


////////////////////////////////////////
/// update slots
////////////////////////////////////////

void GraphViewer::addNodeSLOT(std::int32_t id, const std::string &name, const std::string &type, float posx, float posy, const std::string &color)
{	
	auto gnode = new GraphNode(this);
	gnode->setColor(color.c_str());
	scene.addItem(gnode);
	gnode->setPos(posx, posy);
	gnode->setTag(QString::fromStdString(name));
	gmap.insert(std::pair(id, gnode));
	nodes_types_list << QString::fromStdString(type);
	nodes_types_list.removeDuplicates();
	types_nodes_model.setStringList(nodes_types_list);
	worker->listViewNodes->setModel(&types_nodes_model);
}

void GraphViewer::addEdgeSLOT(std::int32_t from, std::int32_t to, const std::string &edge_tag)
{
	auto node_origen = gmap.at(from);
	auto node_dest = gmap.at(to);
	scene.addItem(new GraphEdge(node_origen, node_dest, edge_tag.c_str()));
	edges_types_list << QString::fromStdString(edge_tag);
	edges_types_list.removeDuplicates();
	types_edges_model.setStringList(edges_types_list);
	worker->listViewEdges->setModel(&types_edges_model);
}


///////////////////////////////////////

void GraphViewer::draw()
{
	//std::cout << this->width() << " " << this->height() << std::endl;
	//this->fitInView(scene.sceneRect(), Qt::KeepAspectRatio );
	show();
}

void GraphViewer::itemMoved()
{
	//std::cout << "timerId " << timerId << std::endl;
     if (timerId == -1)
        timerId = startTimer(1000 / 25);
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