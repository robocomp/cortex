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
#include <QTableWidget>
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
	setMouseTracking(true);
    viewport()->setMouseTracking(true);
	central_point = new QGraphicsEllipseItem();
	central_point->setPos(scene.sceneRect().center());
}

void GraphViewer::setWidget(SpecificWorker *worker_)
{
	worker = worker_;
	this->setParent(worker->scrollArea);
	worker->scrollArea->setWidget(this);
	worker->scrollArea->setMinimumSize(600,600);

	connect(worker->actionSave, &QAction::triggered, this, &GraphViewer::saveGraphSLOT);
	connect(worker->actionStart_Stop, &QAction::triggered, this, &GraphViewer::toggleSimulationSLOT);
}

////////////////////////////////////////
/// UI slots
////////////////////////////////////////


void GraphViewer::saveGraphSLOT()
{ 
	emit saveGraphSIGNAL(); 
};

void GraphViewer::toggleSimulationSLOT()
{
	this->do_simulate = !do_simulate;
	if(do_simulate)
	   timerId = startTimer(1000 / 25);
}

////////////////////////////////////////
/// update slots
////////////////////////////////////////

void GraphViewer::addNodeSLOT(std::int32_t id, const std::string &name, const std::string &type, float posx, float posy, const std::string &color)
{	
	auto gnode = new GraphNode(this);
	qDebug() << "node id " << id;
	gnode->id_in_graph = id;
	gnode->setColor(color.c_str());
	gnode->setPos(posx, posy);
	gnode->setTag(QString::fromStdString(name));
	scene.addItem(gnode);
	gmap.insert(std::pair(id, gnode));
	// table filling
	worker->tableWidgetNodes->setColumnCount(2);
	worker->tableWidgetNodes->setHorizontalHeaderLabels(QStringList{"#", "type"}); 
	nodes_types_list << QString::fromStdString(type);
	nodes_types_list.removeDuplicates();
	int i = 0;
	worker->tableWidgetNodes->clearContents();
	worker->tableWidgetNodes->setRowCount(nodes_types_list.size());
	for( auto &s : nodes_types_list)
	{
		worker->tableWidgetNodes->setItem(i,0, new QTableWidgetItem());
		worker->tableWidgetNodes->item(i,0)->setIcon(QPixmap::fromImage(QImage("../../graph-related-classes/greenBall.png")));
		worker->tableWidgetNodes->setItem(i, 1, new QTableWidgetItem(s));
    	i++;
	}
	worker->tableWidgetNodes->horizontalHeader()->setStretchLastSection(true);
    worker->tableWidgetNodes->resizeRowsToContents();
	worker->tableWidgetNodes->resizeColumnsToContents();
    worker->tableWidgetNodes->show();
}

void GraphViewer::addEdgeSLOT(std::int32_t from, std::int32_t to, const std::string &edge_tag)
{
	qDebug() << "edge id " << QString::fromStdString(edge_tag);
	auto node_origen = gmap.at(from);
	auto node_dest = gmap.at(to);
	scene.addItem(new GraphEdge(node_origen, node_dest, edge_tag.c_str()));
	//table filling
	worker->tableWidgetEdges->setColumnCount(2);
	worker->tableWidgetEdges->setHorizontalHeaderLabels(QStringList{"#", "label"}); 
	edges_types_list << QString::fromStdString(edge_tag);
	edges_types_list.removeDuplicates();
	int i = 0;
	worker->tableWidgetEdges->clearContents();
	worker->tableWidgetEdges->setRowCount(edges_types_list.size());
	for( auto &s : edges_types_list)
	{
		worker->tableWidgetEdges->setItem(i,0, new QTableWidgetItem());
		worker->tableWidgetEdges->item(i,0)->setIcon(QPixmap::fromImage(QImage("../../graph-related-classes/greenBall.png")));
		worker->tableWidgetEdges->setItem(i, 1, new QTableWidgetItem(s));
    	i++;
	}
	worker->tableWidgetEdges->horizontalHeader()->setStretchLastSection(true);
    worker->tableWidgetEdges->resizeRowsToContents();
	worker->tableWidgetEdges->resizeColumnsToContents();
    worker->tableWidgetEdges->show();
}

 void GraphViewer::NodeAttrsChangedSLOT(const IDType &node, const DSR::Attribs &attr)
 {
	 
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
	//if(do_simulate and timerId == 0)
    //if (timerId == 0)
    //   timerId = startTimer(1000 / 25);
}

void GraphViewer::timerEvent(QTimerEvent *event)
{
    Q_UNUSED(event);

	for( auto &[k,node] : gmap)
	{
		(void)k;
	    node->calculateForces();
	}
	bool itemsMoved = false;
	
	for( auto &[k,node] : gmap)
	{
		(void)k;
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
/////
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

void GraphViewer::keyPressEvent(QKeyEvent* event) 
{
	if (event->key() == Qt::Key_Escape)
    {
		emit closeWindowSIGNAL();
	}
}