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

GraphViewer::GraphViewer(std::shared_ptr<CRDT::CRDTGraph> G_) 
{
	G = G_;
    qRegisterMetaType<std::int32_t>("std::int32_t");
    qRegisterMetaType<std::string>("std::string");
	setupUi(this);
	scene.setItemIndexMethod(QGraphicsScene::NoIndex);
	scene.setSceneRect(-200, -200, 400, 400);
	this->graphicsView->setScene(&scene);
	this->graphicsView->setCacheMode(QGraphicsView::CacheBackground);
	this->graphicsView->setViewport(new QGLWidget(QGLFormat(QGL::SampleBuffers)));
	this->graphicsView->setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
	this->graphicsView->setRenderHint(QPainter::Antialiasing);
	this->graphicsView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
	this->setContextMenuPolicy(Qt::ActionsContextMenu);
	this->graphicsView->scale(qreal(0.8), qreal(0.8));
	this->graphicsView->setMinimumSize(200, 200);
	this->graphicsView->fitInView(scene.sceneRect(), Qt::KeepAspectRatio );
	this->graphicsView->adjustSize();
 	QRect availableGeometry(QApplication::desktop()->availableGeometry());
 	this->move((availableGeometry.width() - width()) / 2, (availableGeometry.height() - height()) / 2);
	setMouseTracking(true);
    graphicsView->viewport()->setMouseTracking(true);
	// central_point = new QGraphicsEllipseItem();
	// central_point->setPos(scene.sceneRect().center());
	// scrollArea->setMinimumSize(600,600);
	auto ind_2 = splitter_1->indexOf(tabWidget);
	auto ind_1 = splitter_1->indexOf(splitter_1);
	splitter_1->setStretchFactor(ind_1,1);	
	splitter_2->setStretchFactor(ind_2,9);
	// QSettings settings("RoboComp", "DSR");
    // settings.beginGroup("MainWindow");
    // 	graphicsView->resize(settings.value("size", QSize(400, 400)).toSize());
    // 	graphicsView->move(settings.value("pos", QPoint(200, 200)).toPoint());
    // settings.endGroup();
	// settings.beginGroup("QGraphicsView");
	// 	graphicsView->setTransform(settings.value("matrix", QTransform()).value<QTransform>());
	// settings.endGroup();
	
	this->createGraph();
	tabWidget->setCurrentIndex(0);

    connect(G.get(), &CRDT::CRDTGraph::update_node_signal, this, &GraphViewer::addOrAssignNodeSLOT);
	connect(G.get(), &CRDT::CRDTGraph::update_edge_signal, this, &GraphViewer::addEdgeSLOT);
	connect(G.get(), &CRDT::CRDTGraph::del_edge_signal, this, &GraphViewer::delEdgeSLOT);
	connect(G.get(), &CRDT::CRDTGraph::del_node_signal, this, &GraphViewer::delNodeSLOT);
	// &CRDT::CRDTGraph::update_attrs_signal is used in GraphNode's DoTableStuff auxiliary class

	for (auto &[k, v] : gmap)
	{
		std::cout << "Nodo " << k << " " << v->getType() << '\n';
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

/// Cambiar las llamada para usar la API nueva

void GraphViewer::createGraph()
{
// 	std::cout << __FILE__ << __FUNCTION__ << "-- Entering GraphViewer::createGraph" << std::endl;
	try {
	    auto map = G->getCopy();
		for(const auto &[k, node] : map)
			try
			{
			   addOrAssignNodeSLOT(k,  node.type());
			}
			catch(const std::exception &e) { std::cout << e.what() <<  " Error accessing " << k <<__FUNCTION__<< std::endl;}
		// add edges after all nodes have been created
		for(auto node : map) // Aworset
           	for(const auto &[k, edges] : node.second.fano())
				    try
					{
					    addEdgeSLOT(edges.from(), edges.to(), edges.type());
				    } 
					catch(const std::exception &e) { std::cout << e.what() <<" Error accessing " << node.first <<", "<<__FUNCTION__<<":"<<__LINE__<< std::endl;}
	}
	catch(const std::exception &e) { std::cout << e.what() << " Error accessing "<< __FUNCTION__<<":"<<__LINE__<< std::endl;}
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
	//qDebug() << __FUNCTION__ << "node id " << id<<", type "<<QString::fromUtf8(type.c_str());
	GraphNode *gnode;														// CAMBIAR a sharer_ptr

    //std::string name = G->get_name_from_id(id);
	std::optional<Node> n = G->get_node(id);
    if (n.has_value()) {
        if (gmap.count(id) == 0)    // if node does not exist, create it
        {
            gnode = new GraphNode(std::shared_ptr<GraphViewer>(this));  //REEMPLAZAR THIS
            gnode->id_in_graph = id;
            //gnode->name_in_graph = name;
            gnode->setType(type);
            scene.addItem(gnode);
            gmap.insert(std::pair(id, gnode));
            // left table filling only if it is new
            tableWidgetNodes->setColumnCount(1);
            tableWidgetNodes->setHorizontalHeaderLabels(QStringList{"type"});
            tableWidgetNodes->verticalHeader()->setVisible(false);
            tableWidgetNodes->setShowGrid(false);
            nodes_types_list << QString::fromStdString(type);
            nodes_types_list.removeDuplicates();
            int i = 0;

            tableWidgetNodes->clearContents();
            tableWidgetNodes->setRowCount(nodes_types_list.size());
            for (auto &s : nodes_types_list) 
			{
                tableWidgetNodes->setItem(i, 0, new QTableWidgetItem(s));
                tableWidgetNodes->item(i, 0)->setIcon(QPixmap::fromImage(QImage("../../graph-related-classes/greenBall.png")));
                i++;
            }
            tableWidgetNodes->horizontalHeader()->setStretchLastSection(true);
            tableWidgetNodes->resizeRowsToContents();
            tableWidgetNodes->resizeColumnsToContents();
            tableWidgetNodes->show();

            // connect QTableWidget itemClicked to hide/show nodes of selected type and nodes fanning into it
            disconnect(tableWidgetNodes, &QTableWidget::itemClicked, nullptr, nullptr);
            connect(tableWidgetNodes, &QTableWidget::itemClicked, this, [this](const auto &item) 
			{
                static bool visible = true;
                std::cout << __FILE__ << " " << __FUNCTION__ << "hide or show all nodes of type " << item->text().toStdString() << std::endl;
                for (auto &[k, v] : gmap)
                    if (item->text().toStdString() == v->getType()) {
                        v->setVisible(!v->isVisible());
                        for (const auto &gedge: gmap.at(k)->edgeList)
                            gedge->setVisible(!gedge->isVisible());
                    }
                visible = !visible;
                if (visible)
                    tableWidgetNodes->item(item->row(), 0)->setIcon(
                            QPixmap::fromImage(QImage("../../graph-related-classes/greenBall.png")));
                else
                    tableWidgetNodes->item(item->row(), 0)->setIcon(
                            QPixmap::fromImage(QImage("../../graph-related-classes/redBall.png")));
            }, Qt::UniqueConnection);

            try 
			{
                auto qname = G->get_attrib_by_name<std::string>(n.value(), "name");
                if (qname.has_value()) {
                    qDebug() << QString::fromStdString(qname.value());
                    gnode->setTag(qname.value());
                }
            }
            catch (const std::exception &e) { std::cout << e.what() << " Exception name" << std::endl; }

            try
			{
                auto color = G->get_attrib_by_name<std::string>(n.value(), "color");
                if (color.has_value())
                    gnode->setColor(color.value());
            }
            catch (const std::exception &e) { std::cout << e.what() << " Exception in color " << std::endl; }
        } else
            gnode = gmap.at(id);

        float posx = 10;
        float posy = 10;
        try 
		{
            posx = G->get_attrib_by_name<float>(n.value(), "pos_x").value_or(10);
            posy = G->get_attrib_by_name<float>(n.value(), "pos_y").value_or(10);
        }
        catch (const std::exception &e) {
            auto rd = QVec::uniformVector(2, -200, 200);
            posx = rd.x();
            posy = rd.y();
        }
        if (posx != gnode->x() or posy != gnode->y())
            gnode->setPos(posx, posy);

        emit G->update_attrs_signal(id, n.value().attrs());
    }
}

void GraphViewer::addEdgeSLOT(std::int32_t from, std::int32_t to, const std::string &edge_tag)
{
	try {
 		qDebug() << __FUNCTION__ << "edge id " << QString::fromStdString(edge_tag) << from << to;
		std::tuple<std::int32_t, std::int32_t, std::string> key = std::make_tuple(from, to, edge_tag);

		if(gmap_edges.count(key) == 0) 
		{ 		
			// check if edge already exists
			auto node_origen = gmap.at(from);
			auto node_dest = gmap.at(to);
			auto item = new GraphEdge(node_origen, node_dest, edge_tag.c_str());
			scene.addItem(item);
			gmap_edges.insert(std::make_pair(key, item));
			// side table filling
			tableWidgetEdges->setColumnCount(1);
			tableWidgetEdges->setHorizontalHeaderLabels(QStringList{"label"});
			tableWidgetNodes->verticalHeader()->setVisible(false);
			tableWidgetNodes->setShowGrid(false);
			edges_types_list << QString::fromStdString(edge_tag);
			edges_types_list.removeDuplicates();
			int i = 0;
			tableWidgetEdges->clearContents();
			tableWidgetEdges->setRowCount(edges_types_list.size());
			for (auto &s : edges_types_list) {
				tableWidgetEdges->setItem(i, 0, new QTableWidgetItem(s));
				tableWidgetEdges->item(i, 0)->setIcon(
						QPixmap::fromImage(QImage("../../graph-related-classes/greenBall.png")));
				i++;
			}
			tableWidgetEdges->horizontalHeader()->setStretchLastSection(true);
			tableWidgetEdges->resizeRowsToContents();
			tableWidgetEdges->resizeColumnsToContents();
			tableWidgetEdges->show();
		}
	}
	catch(const std::exception &e) {
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
	const QGraphicsView::ViewportAnchor anchor = graphicsView->transformationAnchor();
	graphicsView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
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
	graphicsView->scale(factor, factor);
	graphicsView->setTransformationAnchor(anchor);
	
	QSettings settings("RoboComp", "DSR");
	settings.beginGroup("QGraphicsView");
		settings.setValue("matrix", graphicsView->transform());
	settings.endGroup();
}

void GraphViewer::keyPressEvent(QKeyEvent* event) 
{
	if (event->key() == Qt::Key_Escape)
		emit closeWindowSIGNAL();
}

