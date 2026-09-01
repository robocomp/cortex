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
			// Nodes whose graph-declared `collapsed` default has already been honoured. The attribute
			// seeds the fold ONCE; after that the badge belongs to the user, so an agent rewriting the
			// node cannot keep re-folding a subtree the user has just opened.
			std::set<std::uint64_t> collapse_default_applied;
			// Reads `collapsed_att` off the node and, the first time only, folds its RT subtree.
			void seed_collapse_default(std::uint64_t id, const Node &n);
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
			// Sets the scrollable area: the items' bounding box PADDED. Panning is implemented by
			// moving the scrollbars, and scrollbars cannot go outside sceneRect -- so pinning
			// sceneRect to itemsBoundingRect() made a drag dead-stop at the edge of the graph
			// instead of panning. The padding is what turns that back into a real pan.
			void update_scene_rect();
			// True once the user has zoomed or panned this view by hand. While set, the automatic
			// refits stop: a DSR graph is under CONSTANT attribute churn (heartbeats, poses, agent
			// state), so schedule_refit() fires every ~150 ms for ever, and each fitInView threw the
			// user straight back out of the zoom they had just made — the zoom was unusable. The
			// layout itself still runs; only the viewport is left alone. Cleared by fit_graph_to_view().
			bool user_framed_ = false;
            void showContextMenu(QMouseEvent *event);
            
            // Graphviz layout
            GVC_t* graphviz_context;
            Agraph_t* graphviz_graph;

    	protected:

            void createGraph();
			virtual void timerEvent(QTimerEvent *event);
			virtual void mousePressEvent(QMouseEvent *event);
			// Both mark the view as user-framed, then defer to the base class for the actual
			// zoom/pan. mouseMoveEvent only counts while a pan is in progress (_pan), so merely
			// moving the pointer across the view does not disable auto-fit.
			virtual void wheelEvent(QWheelEvent *event) override;
			virtual void mouseMoveEvent(QMouseEvent *event) override;
			// The base class refits on every show. A dock being raised or re-shown must not undo a
			// hand-framed view either, so this is gated the same way as the other refit paths.
			virtual void showEvent(QShowEvent *event) override;

		public:
			// Refit the whole graph into the viewport and re-enable automatic refitting. This is the
			// way back after a manual zoom/pan — offered in the right-click menu as "Fit graph to view".
			void fit_graph_to_view();


    };
};
#endif

