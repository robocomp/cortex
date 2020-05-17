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

#ifndef DSR_TO_GRAPH_VIEWER_H
#define DSR_TO_GRAPH_VIEWER_H

#include <chrono>
#include <QWidget>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMouseEvent>
#include <QGLWidget>
#include "CRDT.h"

class GraphNode;
class GraphEdge;

namespace DSR
{
    class DSRtoGraphViewer : public QGraphicsView
    {
        public:
            DSRtoGraphViewer(std::shared_ptr<CRDT::CRDTGraph> G_, QGraphicsView *parent=0);
            std::shared_ptr<CRDT::CRDTGraph> getGraph()  			  	{return G;};
			std::map<std::int32_t, GraphNode*> getGMap() const 			{return gmap;};
            QGraphicsEllipseItem* getCentralPoint() const 				{return central_point;};
            QGraphicsScene scene;

        public slots:   // From G
            //void add_or_assign_node_slot(const std::int32_t id, const std::string &type){};
            //void add_or_assign_edge_slot(const std::int32_t from, const std::int32_t to, const std::string& type){};
            void addOrAssignNodeSLOT(const std::int32_t id, const std::string &type);
			void saveGraphSLOT();		
			void toggleSimulationSLOT();
			void addEdgeSLOT(const std::int32_t from, const std::int32_t to, const std::string &ege_tag);
			void delEdgeSLOT(const std::int32_t from, const std::int32_t to,  const std::string &edge_tag);
			void delNodeSLOT(int id);
            
        protected:  
            virtual void wheelEvent(QWheelEvent* event);
            void resizeEvent(QResizeEvent *e) override;

        private:
            std::shared_ptr<CRDT::CRDTGraph> G;
            std::map<std::int32_t, GraphNode*> gmap;
			std::map<std::tuple<std::int32_t, std::int32_t, std::string>, GraphEdge*> gmap_edges;
			QGraphicsEllipseItem *central_point;
            
            void createGraph();
    };
};
#endif

