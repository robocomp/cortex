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

GraphViewer::GraphViewer(const std::shared_ptr<SpecificWorker>& worker_) :  gcrdt(worker_->getGCRDT()) , worker(worker_)
{
    qRegisterMetaType<std::int32_t>("std::int32_t");
    qRegisterMetaType<std::string>("std::string");

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

	this->setParent(worker->scrollArea);
	worker->scrollArea->setWidget(this);
	worker->scrollArea->setMinimumSize(600,600);
	auto ind_2 = worker->splitter_1->indexOf(worker->scrollArea);
	auto ind_1 = worker->splitter_1->indexOf(worker->splitter_1);
	worker->splitter_1->setStretchFactor(ind_1,1);	
	worker->splitter_1->setStretchFactor(ind_2,9);
	QSettings settings("RoboComp", "DSR");
    settings.beginGroup("MainWindow");
    	resize(settings.value("size", QSize(400, 400)).toSize());
    	move(settings.value("pos", QPoint(200, 200)).toPoint());
    settings.endGroup();
	settings.beginGroup("QGraphicsView");
		setTransform(settings.value("matrix", QTransform()).value<QTransform>());
	settings.endGroup();

	this->createGraph();

	connect(worker->actionSave, &QAction::triggered, this, &GraphViewer::saveGraphSLOT);
	connect(worker->actionStart_Stop, &QAction::triggered, this, &GraphViewer::toggleSimulationSLOT);
    connect(gcrdt.get(), &CRDT::CRDTGraph::update_node_signal, this, &GraphViewer::addOrAssignNodeSLOT);
	connect(gcrdt.get(), &CRDT::CRDTGraph::del_edge_signal, this, &GraphViewer::delEdgeSLOT);
	connect(gcrdt.get(), &CRDT::CRDTGraph::del_node_signal, this, &GraphViewer::delNodeSLOT);

}

GraphViewer::~GraphViewer()
{
	QSettings settings("RoboComp", "DSR");
    settings.beginGroup("MainWindow");
		settings.setValue("size", size());
		settings.setValue("pos", pos());
    settings.endGroup();
}

void GraphViewer::createGraph()
{
// 	std::cout << __FILE__ << __FUNCTION__ << "-- Entering GraphViewer::createGraph" << std::endl;
	try {
		for(auto node : gcrdt->get().getMap()) // Aworset
		{
			try
			{
				addOrAssignNodeSLOT(node.first,  gcrdt->get_node_type(node.second.dots().ds.rbegin()->second));
			}
			catch(const std::exception &e) { std::cout << e.what() <<  " Error accessing " << node.first <<__FUNCTION__<< std::endl;}
		}
		// add edges after all nodes have been created
		for(auto node : gcrdt->get().getMap()) // Aworset
		{
//			std::cout << "Edges from "<<node.second.readAsList().back().id<<std::endl;
			std::list<Node> ns;
			try
			{
				ns = node.second.readAsList();
			}
			catch(const std::exception &e) { std::cout << e.what() <<" Error accessing edge" << node.second.readAsList().back() <<", "<<__FUNCTION__<<":"<<__LINE__<< std::endl;}
			for( auto &edge :ns.back().fano())
			{
				try{
					addEdgeSLOT(edge.second.from(), edge.second.to(), edge.second.label());
				} catch(const std::exception &e) { std::cout << e.what() <<" Error accessing " << node.first <<", "<<__FUNCTION__<<":"<<__LINE__<< std::endl;}
			}
		}
	}catch(const std::exception &e) { std::cout << e.what() << " Error accessing "<< __FUNCTION__<<":"<<__LINE__<< std::endl;}

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

////////////////////////////////////////
/// update slots
////////////////////////////////////////

void GraphViewer::addOrAssignNodeSLOT(int id, const std::string &type)
{	
	qDebug() << __FUNCTION__ << "node id " << id<<", type "<<QString::fromUtf8(type.c_str());
	GraphNode *gnode;														// CAMBIAR a sharer_ptr

    std::string name = gcrdt->get_name_from_id(id);
	Node n = gcrdt->get_node(name);

    if( gmap.count(id) == 0)	// if node does not exist, create it
	{
		gnode = new GraphNode(std::shared_ptr<GraphViewer>(this));  //REEMPLAZAR THIS 
		gnode->id_in_graph = id;
		gnode->name_in_graph = name;
		gnode->setType( type );
		scene.addItem(gnode);
		gmap.insert(std::pair(id, gnode));

		// left table filling only if it is new
		worker->tableWidgetNodes->setColumnCount(1);
		worker->tableWidgetNodes->setHorizontalHeaderLabels(QStringList{"type"}); 
		worker->tableWidgetNodes->verticalHeader()->setVisible(false);
		worker->tableWidgetNodes->setShowGrid(false);
		nodes_types_list << QString::fromStdString(type);
		nodes_types_list.removeDuplicates();
		int i = 0;

		worker->tableWidgetNodes->clearContents();
		worker->tableWidgetNodes->setRowCount(nodes_types_list.size());
		for( auto &s : nodes_types_list)
		{
			worker->tableWidgetNodes->setItem(i,0, new QTableWidgetItem(s));
			worker->tableWidgetNodes->item(i,0)->setIcon(QPixmap::fromImage(QImage("../../graph-related-classes/greenBall.png")));
			i++;
		}
		worker->tableWidgetNodes->horizontalHeader()->setStretchLastSection(true);
		worker->tableWidgetNodes->resizeRowsToContents();
		worker->tableWidgetNodes->resizeColumnsToContents();
		worker->tableWidgetNodes->show();

		// connect QTableWidget itemClicked to hide/show nodes of selected type and nodes fanning into it
		disconnect(worker->tableWidgetNodes, &QTableWidget::itemClicked, nullptr, nullptr);

		connect(worker->tableWidgetNodes, &QTableWidget::itemClicked, this, [this](const auto &item){ 
							static bool visible = true;
							std::cout << __FILE__ << " " << __FUNCTION__ << "hide or show all nodes of type " << item->text().toStdString() << std::endl;
							for( auto &[k, v] : gmap) 
								if( item->text().toStdString() == v->getType()) 
								{
									v->setVisible(!v->isVisible());
									for(const auto &gedge: gmap.at(k)->edgeList)
										gedge->setVisible(!gedge->isVisible());
								}
							visible = !visible;
							if(visible)
								worker->tableWidgetNodes->item(item->row(),0)->setIcon(QPixmap::fromImage(QImage("../../graph-related-classes/greenBall.png")));
							else 
								worker->tableWidgetNodes->item(item->row(),0)->setIcon(QPixmap::fromImage(QImage("../../graph-related-classes/redBall.png")));
						} , Qt::UniqueConnection);


		try
		{
			auto qname = gcrdt->get_node_attrib_by_name(n, "name").value();
			gnode->setTag(qname);
		}
		catch(const std::exception &e){ std::cout << e.what() << " Exception name" << std::endl;}

		try
		{
			auto color = gcrdt->get_node_attrib_by_name(n, "color").value();
			gnode->setColor(color);
		}
		catch(const std::exception &e){ std::cout << e.what() << " Exception in color " << std::endl;}
	}
	else
		gnode = gmap.at(id);

    float posx = 10; float posy = 10;
	try
	{
        posx = std::stof((std::string)gcrdt->get_node_attrib_by_name(n, "pos_x").value());
        posy = std::stof((std::string)gcrdt->get_node_attrib_by_name(n, "pos_y").value());
	}
	catch(const std::exception &e){ }
	if(posx != gnode->x() or posy != gnode->y())
		gnode->setPos(posx, posy);

    emit gcrdt->update_attrs_signal(id, n.attrs() );

    //auto e = gcrdt->getEdges(id);
    //	if (!e.empty())
    //	{
    //		for (auto &[k,v] : e)
    //			addEdgeSLOT(id, k, v.label);
    //	}

}

void GraphViewer::addEdgeSLOT(std::int32_t from, std::int32_t to, const std::string &edge_tag)
{
	try {
 		qDebug() << "edge id " << QString::fromStdString(edge_tag) << from << to;
		std::tuple<std::int32_t, std::int32_t, std::string> key = std::make_tuple(from, to, edge_tag);

		if(gmap_edges.count(key) == 0) { 		// check if edge already exists
			auto node_origen = gmap.at(from);
			auto node_dest = gmap.at(to);
			auto item = new GraphEdge(node_origen, node_dest, edge_tag.c_str());
			scene.addItem(item);
			gmap_edges.insert(std::make_pair(key, item));
			// side table filling
			worker->tableWidgetEdges->setColumnCount(1);
			worker->tableWidgetEdges->setHorizontalHeaderLabels(QStringList{"label"});
			worker->tableWidgetNodes->verticalHeader()->setVisible(false);
			worker->tableWidgetNodes->setShowGrid(false);
			edges_types_list << QString::fromStdString(edge_tag);
			edges_types_list.removeDuplicates();
			int i = 0;
			worker->tableWidgetEdges->clearContents();
			worker->tableWidgetEdges->setRowCount(edges_types_list.size());
			for (auto &s : edges_types_list) {
				worker->tableWidgetEdges->setItem(i, 0, new QTableWidgetItem(s));
				worker->tableWidgetEdges->item(i, 0)->setIcon(
						QPixmap::fromImage(QImage("../../graph-related-classes/greenBall.png")));
				i++;
			}
			worker->tableWidgetEdges->horizontalHeader()->setStretchLastSection(true);
			worker->tableWidgetEdges->resizeRowsToContents();
			worker->tableWidgetEdges->resizeColumnsToContents();
			worker->tableWidgetEdges->show();
		}
	}catch(const std::exception &e) {
		std::cout << e.what() <<" Error  "<<__FUNCTION__<<":"<<__LINE__<<" "<<e.what()<< std::endl;}
}

void GraphViewer::delEdgeSLOT(const std::int32_t from, const std::int32_t to, const std::string &edge_tag)
{
    std::cout<<__FUNCTION__<<":"<<__LINE__<< std::endl;
	try {
		std::tuple<std::int32_t, std::int32_t, std::string> key = std::make_tuple(from, to, edge_tag);
		while (gmap_edges.count(key) > 0) {
            scene.removeItem(gmap_edges.at(key));
		    gmap_edges.erase(key);
		}
	} catch(const std::exception &e) { std::cout << e.what() <<" Error  "<<__FUNCTION__<<":"<<__LINE__<< std::endl;}

}

void GraphViewer::delNodeSLOT(int id)
{
    std::cout<<__FUNCTION__<<":"<<__LINE__<< std::endl;
    try {
        while (gmap.count(id) > 0) {
            scene.removeItem(gmap.at(id));
            gmap.erase(id);
        }
    } catch(const std::exception &e) { std::cout << e.what() <<" Error  "<<__FUNCTION__<<":"<<__LINE__<< std::endl;}

}

/*
 void GraphViewer::NodeAttrsChangedSLOT(const std::int32_t &id, const DSR::Attribs &attribs)
 {
	try 
	{
		std::cout << __FUNCTION__ << id << std::endl;
		float posx = std::get<float>(attribs.at("pos_x"));
		float posy = std::get<float>(attribs.at("pos_y"));
		auto &gnode = gmap.at(id);
		if(posx != gnode->x() or posy != gnode->y())
			gnode->setPos(posx, posy);
	}
	catch(const std::exception &e){ std::cout << "Exception: " << e.what() << " pos_x and pos_y attribs not found in node "  << id << std::endl;};
 }
*/
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
    Q_UNUSED(event)

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
///// Qt Events
/////////////////////////

void GraphViewer::wheelEvent(QWheelEvent *event)
{
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
	
	QSettings settings("RoboComp", "DSR");
	settings.beginGroup("QGraphicsView");
		settings.setValue("matrix", this->transform());
	settings.endGroup();
}

void GraphViewer::keyPressEvent(QKeyEvent* event) 
{
	if (event->key() == Qt::Key_Escape)
		emit closeWindowSIGNAL();
}

