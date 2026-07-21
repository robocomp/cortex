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

#include <dsr/api/dsr_api.h>
#include <dsr/gui/viewers/_abstract_graphic_view.h>

#include <chrono>
#include <QWidget>

#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QResizeEvent>
#include <QMenu>
#include <QTimer>

#include <graphviz/gvc.h>
#include <graphviz/cgraph.h>

class GraphNode;
class GraphEdge;

namespace DSR
{
    class GraphViewer : public AbstractGraphicViewer
    {
        Q_OBJECT
        public:
            GraphViewer(std::shared_ptr<DSR::DSRGraph> G_, QWidget *parent=0);
			~GraphViewer();
            std::shared_ptr<DSR::DSRGraph> getGraph()  			  	{return G;};
			std::map<std::uint64_t , GraphNode*> getGMap() const 			{return gmap;};
            QGraphicsEllipseItem* getCentralPoint() const 				{return central_point;};


        public slots:
    		// From G
            void add_or_assign_node_SLOT(std::uint64_t id, const std::string &type);
            void add_or_assign_edge_SLOT(std::uint64_t from, std::uint64_t to, const std::string& type);
			void del_edge_SLOT(std::uint64_t from, std::uint64_t to,  const std::string &edge_tag);
			void del_node_SLOT(uint64_t id);  // remove node from visual graph
			void hide_show_node_SLOT(uint64_t id, bool visible);
			// Collapse/expand the RT subtree hanging from a node (the "+"/"-" badge)
			void toggle_children_SLOT(uint64_t parent_id);
			// Others
			void toggle_animation(bool state);
            void compute_layout(const char * alg = "dot");
			void reload(QWidget * widget);
            void remove_node_SLOT(uint64_t id);  // remove node from DSR

        signals:
            // Re-emitted from a node's GraphNode::view_data_signal. The agent connects
            // here (Qt::QueuedConnection) to open a media-plane (DDS) viewer for the node.
            void view_data_signal(uint64_t id, const std::string &type);

        protected:
            std::shared_ptr<DSR::DSRGraph> G;
            GraphNode* new_visual_node(uint64_t id, const std::string &type, const std::string &name, bool debug = false);
            GraphEdge* new_visual_edge(GraphNode *sourceNode, GraphNode *destNode, const QString &edge_name);
            GraphEdge* new_visual_edge(std::uint64_t from, std::uint64_t to, const std::string &edge_tag);
        private:
			std::shared_ptr<GraphViewer> own;

            std::map<std::uint64_t, GraphNode*> gmap;
			std::map<std::tuple<std::uint64_t, std::uint64_t, std::string>, GraphEdge*> gmap_edges;
			QGraphicsEllipseItem *central_point;
			QMenu *contextMenu, *showMenu;
			std::map<std::string,std::set<std::uint64_t>> type_id_map;

			// ---- collapsible children (any RT subtree folded away from its parent) ----
			// Edge type that defines parenthood in DSR (see dsr_rt_api.cpp, which also maintains
			// parent_att/level_att). Any node with at least one RT child gets a "+"/"-" badge.
			static constexpr const char *PARENT_EDGE_TYPE = "RT";
			std::map<std::uint64_t, std::set<std::uint64_t>> collapsible_children;  // parent -> RT children
			std::set<std::uint64_t> collapsed_parents;   // parents currently folded (purely visual)
			// Keeps `collapsible_children` in sync with the RT edges arriving/leaving from G
			void note_parent_edge(std::uint64_t from, std::uint64_t to, const std::string &edge_tag, bool added);
			// All descendants of `root` reachable through RT edges, root NOT included. With
			// stop_at_collapsed the walk does not descend past a node the user folded on its own.
			std::vector<std::uint64_t> subtree_ids(std::uint64_t root, bool stop_at_collapsed) const;
			void refresh_collapse_badge(std::uint64_t parent_id);
			// Re-applies the stored collapsed/expanded state (used when new nodes or edges arrive
			// into an already collapsed parent, and after a full createGraph())
			void apply_collapse_state(std::uint64_t parent_id);
			int timerId = 0;
			// Coalesces the (expensive, full-scene) setSceneRect/fitInView refit so a burst of
			// high-frequency node/attribute updates triggers it at most a few times per second
			// instead of once per update — avoids the GUI-thread repaint storm under fast graphs.
			QTimer refit_timer_;
			void schedule_refit();
            void showContextMenu(QMouseEvent *event);
            
            // Graphviz layout
            GVC_t* graphviz_context;
            Agraph_t* graphviz_graph;

    	protected:

            void createGraph();
			virtual void timerEvent(QTimerEvent *event);
			virtual void mousePressEvent(QMouseEvent *event);


    };
};
#endif

