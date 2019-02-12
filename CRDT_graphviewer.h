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

#ifndef GRAPHVIEWER_H
#define GRAPHVIEWER_H

#include <memory>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QListView>
#include "CRDT.h"

class SpecificWorker;
class GraphNode;
class GraphEdge;

namespace DSR
{
	//////////////////////////////////////////////////////////////////////////////////////////////77
	/// Drawing controller to display the graph in real-time using DataStorm updates
	//////////////////////////////////////////////////////////////////////////////////////////////77
	
	class GraphViewer : public QGraphicsView
	{
		Q_OBJECT
		public:
			GraphViewer(std::shared_ptr<SpecificWorker> worker_);
			~GraphViewer();
			void itemMoved();
			void createGraph();
			std::shared_ptr<CRDT::CRDTGraph> getGraph()  			  		{ return gcrdt;};
			std::map<std::int32_t, GraphNode*> getGMap() const 			{return gmap;};
			QGraphicsEllipseItem* getCentralPoint() const 				{return central_point;};
		
		protected:
			void wheelEvent(QWheelEvent *event) override;
			void keyPressEvent(QKeyEvent *event) override;
			void timerEvent(QTimerEvent *event) override;
			
		private:
	        std::shared_ptr<CRDT::CRDTGraph> gcrdt;
			QGraphicsScene scene;
			int timerId = 0;
			QStringList nodes_types_list, edges_types_list;
			bool do_simulate = false;
			std::shared_ptr<SpecificWorker> worker;

            std::map<std::int32_t, GraphNode*> gmap;
			std::map<std::tuple<std::int32_t, std::int32_t, std::string>, GraphEdge*> gmap_edges;
			QGraphicsEllipseItem *central_point;

		public slots:
			void addNodeSLOT(const std::int32_t id, const std::string &type);
			void addEdgeSLOT(const std::int32_t from, const std::int32_t to, const std::string &ege_tag);
			void saveGraphSLOT();		
			void toggleSimulationSLOT();
			void NodeAttrsChangedSLOT(const IDType &id, const DSR::Attribs &attribs);

		signals:
			void saveGraphSIGNAL();
			void closeWindowSIGNAL();

	};
}
#endif // GRAPHVIEWER_H
