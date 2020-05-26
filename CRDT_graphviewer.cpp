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

#include "CRDT_graphviewer.h"
#include <cppitertools/range.hpp>
#include <qmat/QMatAll>
#include <QDesktopWidget>
#include <QGLViewer/qglviewer.h>
#include <QApplication>
#include <QTableWidget>
#include "CRDT_graphnode.h"
#include "CRDT_graphedge.h"
#include "specificworker.h"

using namespace DSR;

GraphViewer::GraphViewer(std::shared_ptr<CRDT::CRDTGraph> G_, std::list<View> options) 
{
	G = G_;
    qRegisterMetaType<std::int32_t>("std::int32_t");
    qRegisterMetaType<std::string>("std::string");
	setupUi(this);
 	QRect availableGeometry(QApplication::desktop()->availableGeometry());
 	this->move((availableGeometry.width() - width()) / 2, (availableGeometry.height() - height()) / 2);
	//auto ind_1 = splitter_1->indexOf(splitter_1);
	auto ind = splitter->indexOf(tabWidget);
	//splitter_1->setStretchFactor(ind_1,1);	
	splitter->setStretchFactor(ind,9);
	
	// QSettings settings("RoboComp", "DSR");
    // settings.beginGroup("MainWindow");
    // 	graphicsView->resize(settings.value("size", QSize(400, 400)).toSize());
    // 	graphicsView->move(settings.value("pos", QPoint(200, 200)).toPoint());
    // settings.endGroup();
	// settings.beginGroup("QGraphicsView");
	// 	graphicsView->setTransform(settings.value("matrix", QTransform()).value<QTransform>());
	// settings.endGroup();
	
	tabWidget->setCurrentIndex(0);
	for(auto option: options)
	{
		if(option == View::Scene)
		 	dsr_to_graph_viewer = std::make_unique<DSR::DSRtoGraphViewer>(G, graphicsView);
		// //dsr_to_tree_viewer = std::make_unique<DSR::DSRtoTreeViewer>(G, treeWidget);
		if(option == View::OSG)
		 	dsr_to_osg_viewer = std::make_unique<DSR::DSRtoOSGViewer>(G, 1, 1, tab_2);
		// if(option == View::Graph)
		// 	dsr_to_graphicscene_viewer = std::make_unique<DSR::DSRtoGraphicsceneViewer>(G, 1, 1, graphicsView_2D);
	}
}

GraphViewer::~GraphViewer()
{
	QSettings settings("RoboComp", "DSR");
    settings.beginGroup("MainWindow");
		settings.setValue("size", size());
		settings.setValue("pos", pos());
    settings.endGroup();
}

////////////////////////////////////////
/// UI slots
////////////////////////////////////////
void GraphViewer::saveGraphSLOT()
{ 
	emit saveGraphSIGNAL(); 
}

void GraphViewer::toggleSimulationSLOT()
{
	this->do_simulate = !do_simulate;
	if(do_simulate)
	   timerId = startTimer(1000 / 25);
}

///////////////////////////////////////

void GraphViewer::itemMoved()
{
	//std::cout << "timerId " << timerId << std::endl;
	//if(do_simulate and timerId == 0)
    //if (timerId == 0)
    //   timerId = startTimer(1000 / 25);
}

void GraphViewer::timerEvent(QTimerEvent *event)
{
    // Q_UNUSED(event)

	// for( auto &[k,node] : gmap)
	// {
	// 	(void)k;
	//     node->calculateForces();
	// }
	// bool itemsMoved = false;
	
	// for( auto &[k,node] : gmap)
	// {
	// 	(void)k;
    //     if (node->advancePosition())
    //         itemsMoved = true;
    // }
	// if (!itemsMoved) 
	// {
    //     killTimer(timerId);
    //     timerId = 0;
    // }
}

/////////////////////////
///// Qt Events
/////////////////////////

void GraphViewer::keyPressEvent(QKeyEvent* event) 
{
	if (event->key() == Qt::Key_Escape)
		emit closeWindowSIGNAL();
}

